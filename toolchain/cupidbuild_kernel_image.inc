/**
 * @file cupidbuild_kernel_image.inc
 * @brief Generate the kernel symbol table and validate and flatten the kernel image.
 */

static int cupidbuild_ksyms_space(unsigned char character) {
    return character == (unsigned char)' ' || character == (unsigned char)'\t' ||
           character == (unsigned char)'\r' || character == (unsigned char)'\v' ||
           character == (unsigned char)'\f';
}

static cupidbuild_ksyms_row_kind_t cupidbuild_ksyms_address(const unsigned char *bytes, size_t size,
                                                            unsigned int *address_out) {
    unsigned int value = 0u;
    size_t index = 0u;
    if (size >= 2u && bytes[0] == (unsigned char)'0' &&
        (bytes[1] == (unsigned char)'x' || bytes[1] == (unsigned char)'X')) {
        index = 2u;
    }
    if (index == size) {
        return CUPIDBUILD_KSYMS_INVALID_ADDRESS;
    }
    while (index < size) {
        unsigned char character = bytes[index];
        unsigned int digit;
        if (character >= (unsigned char)'0' && character <= (unsigned char)'9') {
            digit = (unsigned int)(character - (unsigned char)'0');
        } else if (character >= (unsigned char)'a' && character <= (unsigned char)'f') {
            digit = 10u + (unsigned int)(character - (unsigned char)'a');
        } else if (character >= (unsigned char)'A' && character <= (unsigned char)'F') {
            digit = 10u + (unsigned int)(character - (unsigned char)'A');
        } else {
            return CUPIDBUILD_KSYMS_INVALID_ADDRESS;
        }
        if (value > (0xffffffffu - digit) / 16u) {
            return CUPIDBUILD_KSYMS_ADDRESS_OUTSIDE_I386;
        }
        value = value * 16u + digit;
        index++;
    }
    *address_out = value;
    return CUPIDBUILD_KSYMS_TEXT;
}

static cupidbuild_ksyms_row_kind_t
cupidbuild_ksyms_parse_row(const unsigned char *bytes, size_t start, size_t end, size_t order,
                           cupidbuild_ksyms_symbol_t *symbol_out) {
    size_t field_start[3];
    size_t field_size[3];
    size_t field_count = 0u;
    size_t index = start;
    unsigned int address = 0u;
    cupidbuild_ksyms_row_kind_t address_kind;
    while (index < end) {
        size_t token_start;
        while (index < end && cupidbuild_ksyms_space(bytes[index])) {
            index++;
        }
        if (index == end) {
            break;
        }
        if (field_count == 3u) {
            return CUPIDBUILD_KSYMS_MALFORMED;
        }
        token_start = index;
        while (index < end && !cupidbuild_ksyms_space(bytes[index])) {
            if (bytes[index] == 0u) {
                return CUPIDBUILD_KSYMS_MALFORMED;
            }
            index++;
        }
        field_start[field_count] = token_start;
        field_size[field_count] = index - token_start;
        field_count++;
    }
    if (field_count == 0u) {
        return CUPIDBUILD_KSYMS_EMPTY;
    }
    if (field_count == 2u) {
        if (field_size[0] == 1u && (bytes[field_start[0]] == (unsigned char)'U' ||
                                    bytes[field_start[0]] == (unsigned char)'u' ||
                                    bytes[field_start[0]] == (unsigned char)'v' ||
                                    bytes[field_start[0]] == (unsigned char)'w')) {
            return CUPIDBUILD_KSYMS_IGNORED;
        }
        return CUPIDBUILD_KSYMS_OMITTED_ADDRESS;
    }
    if (field_count != 3u || field_size[1] != 1u) {
        return CUPIDBUILD_KSYMS_MALFORMED;
    }
    address_kind = cupidbuild_ksyms_address(bytes + field_start[0], field_size[0], &address);
    if (address_kind != CUPIDBUILD_KSYMS_TEXT) {
        return address_kind;
    }
    if (!(bytes[field_start[1]] == (unsigned char)'t' ||
          bytes[field_start[1]] == (unsigned char)'T' ||
          bytes[field_start[1]] == (unsigned char)'w' ||
          bytes[field_start[1]] == (unsigned char)'W')) {
        return CUPIDBUILD_KSYMS_IGNORED;
    }
    if (field_size[2] >= 2u && bytes[field_start[2]] == (unsigned char)'.' &&
        bytes[field_start[2] + 1u] == (unsigned char)'L') {
        return CUPIDBUILD_KSYMS_IGNORED;
    }
    symbol_out->address = address;
    symbol_out->order = order;
    symbol_out->name_start = field_start[2];
    symbol_out->name_size = field_size[2];
    return CUPIDBUILD_KSYMS_TEXT;
}

static int cupidbuild_ksyms_symbol_less(const cupidbuild_ksyms_symbol_t *left,
                                        const cupidbuild_ksyms_symbol_t *right) {
    return left->address < right->address ||
           (left->address == right->address && left->order < right->order);
}

static void cupidbuild_ksyms_symbol_swap(cupidbuild_ksyms_symbol_t *left,
                                         cupidbuild_ksyms_symbol_t *right) {
    cupidbuild_ksyms_symbol_t temporary = *left;
    *left = *right;
    *right = temporary;
}

static void cupidbuild_ksyms_symbol_sift_down(cupidbuild_ksyms_symbol_t *symbols, size_t root,
                                              size_t count) {
    for (;;) {
        size_t child;
        size_t selected;
        if (root >= count / 2u) {
            return;
        }
        child = root * 2u + 1u;
        selected = root;
        if (cupidbuild_ksyms_symbol_less(&symbols[selected], &symbols[child])) {
            selected = child;
        }
        if (child + 1u < count &&
            cupidbuild_ksyms_symbol_less(&symbols[selected], &symbols[child + 1u])) {
            selected = child + 1u;
        }
        if (selected == root) {
            return;
        }
        cupidbuild_ksyms_symbol_swap(&symbols[root], &symbols[selected]);
        root = selected;
    }
}

static void cupidbuild_ksyms_symbol_sort(cupidbuild_ksyms_symbol_t *symbols, size_t count) {
    size_t start = count / 2u;
    size_t end = count;
    while (start != 0u) {
        start--;
        cupidbuild_ksyms_symbol_sift_down(symbols, start, count);
    }
    while (end > 1u) {
        cupidbuild_ksyms_symbol_swap(&symbols[0], &symbols[end - 1u]);
        end--;
        cupidbuild_ksyms_symbol_sift_down(symbols, 0u, end);
    }
}

static void cupidbuild_ksyms_write_le32(unsigned char *bytes, size_t offset, unsigned int value) {
    bytes[offset] = (unsigned char)(value & 0xffu);
    bytes[offset + 1u] = (unsigned char)((value >> 8u) & 0xffu);
    bytes[offset + 2u] = (unsigned char)((value >> 16u) & 0xffu);
    bytes[offset + 3u] = (unsigned char)((value >> 24u) & 0xffu);
}

static int cupidbuild_ksyms_append(cupidbuild_ksyms_buffer_t *buffer, const void *bytes,
                                   size_t size) {
    size_t required;
    size_t capacity;
    unsigned char *grown;
    if (size > CUPIDBUILD_TOOL_BYTES || buffer->size > CUPIDBUILD_TOOL_BYTES - size) {
        return 0;
    }
    required = buffer->size + size;
    if (required > buffer->capacity) {
        capacity = buffer->capacity == 0u ? 4096u : buffer->capacity;
        while (capacity < required) {
            if (capacity > CUPIDBUILD_TOOL_BYTES / 2u) {
                capacity = CUPIDBUILD_TOOL_BYTES;
                break;
            }
            capacity *= 2u;
        }
        grown = (unsigned char *)realloc(buffer->bytes, capacity);
        if (grown == (unsigned char *)0) {
            return 0;
        }
        buffer->bytes = grown;
        buffer->capacity = capacity;
    }
    if (size != 0u) {
        (void)memcpy(buffer->bytes + buffer->size, bytes, size);
    }
    buffer->size = required;
    return 1;
}

static int cupidbuild_ksyms_append_literal(cupidbuild_ksyms_buffer_t *buffer, const char *text) {
    return cupidbuild_ksyms_append(buffer, text, strlen(text));
}

static int cupidbuild_ksyms_append_word(cupidbuild_ksyms_buffer_t *buffer, unsigned int value) {
    static const char digits[] = "0123456789abcdef";
    char text[12];
    size_t index;
    text[0] = '0';
    text[1] = 'x';
    for (index = 0u; index < 8u; index++) {
        size_t shift = (7u - index) * 4u;
        text[2u + index] = digits[(value >> shift) & 0x0fu];
    }
    text[10] = 'u';
    text[11] = ',';
    return cupidbuild_ksyms_append(buffer, text, sizeof(text));
}

static int cupidbuild_ksyms_append_decimal(cupidbuild_ksyms_buffer_t *buffer, unsigned int value) {
    char reverse[10];
    char text[10];
    size_t count = 0u;
    size_t index;
    do {
        reverse[count++] = (char)('0' + (char)(value % 10u));
        value /= 10u;
    } while (value != 0u);
    for (index = 0u; index < count; index++) {
        text[index] = reverse[count - index - 1u];
    }
    return cupidbuild_ksyms_append(buffer, text, count);
}

static const char *cupidbuild_ksyms_row_reason(cupidbuild_ksyms_row_kind_t kind) {
    if (kind == CUPIDBUILD_KSYMS_OMITTED_ADDRESS) {
        return "symbol reader omitted an address";
    }
    if (kind == CUPIDBUILD_KSYMS_INVALID_ADDRESS) {
        return "symbol reader emitted an invalid address";
    }
    if (kind == CUPIDBUILD_KSYMS_ADDRESS_OUTSIDE_I386) {
        return "symbol reader address is outside i386";
    }
    return "symbol reader emitted a malformed row";
}

static int cupidbuild_render_ksyms_source(const unsigned char *contents, size_t contents_size,
                                          unsigned char **source_out, size_t *source_size_out,
                                          const char **reason_out, size_t *line_out) {
    cupidbuild_ksyms_symbol_t *symbols = (cupidbuild_ksyms_symbol_t *)0;
    unsigned char *blob = (unsigned char *)0;
    cupidbuild_ksyms_buffer_t output;
    size_t symbol_count = 0u;
    size_t unique_count = 0u;
    size_t offset = 0u;
    size_t line = 1u;
    size_t index;
    size_t string_offset;
    size_t blob_size;
    size_t padded_size;
    size_t string_cursor;
    int success = 0;
    (void)memset(&output, 0, sizeof(output));
    *source_out = (unsigned char *)0;
    *source_size_out = 0u;
    *reason_out = "kernel symbol source validation failed";
    *line_out = 0u;
    while (offset < contents_size) {
        size_t start = offset;
        cupidbuild_ksyms_symbol_t symbol;
        cupidbuild_ksyms_row_kind_t kind;
        while (offset < contents_size && contents[offset] != (unsigned char)'\n') {
            offset++;
        }
        kind = cupidbuild_ksyms_parse_row(contents, start, offset, symbol_count, &symbol);
        if (kind == CUPIDBUILD_KSYMS_TEXT) {
            symbol_count++;
        } else if (kind != CUPIDBUILD_KSYMS_EMPTY && kind != CUPIDBUILD_KSYMS_IGNORED) {
            *reason_out = cupidbuild_ksyms_row_reason(kind);
            *line_out = line;
            goto done;
        }
        if (offset < contents_size) {
            offset++;
            line++;
        }
    }
    if (symbol_count == 0u) {
        *reason_out = "symbol reader reported no kernel text symbols";
        goto done;
    }
    if (symbol_count > (size_t)-1 / sizeof(*symbols)) {
        *reason_out = "kernel symbol inventory exceeds the host size limit";
        goto done;
    }
    symbols = (cupidbuild_ksyms_symbol_t *)malloc(symbol_count * sizeof(*symbols));
    if (symbols == (cupidbuild_ksyms_symbol_t *)0) {
        *reason_out = "kernel symbol inventory cannot be allocated";
        goto done;
    }
    offset = 0u;
    index = 0u;
    while (offset < contents_size) {
        size_t start = offset;
        cupidbuild_ksyms_symbol_t symbol;
        cupidbuild_ksyms_row_kind_t kind;
        while (offset < contents_size && contents[offset] != (unsigned char)'\n') {
            offset++;
        }
        kind = cupidbuild_ksyms_parse_row(contents, start, offset, index, &symbol);
        if (kind == CUPIDBUILD_KSYMS_TEXT) {
            symbols[index++] = symbol;
        }
        if (offset < contents_size) {
            offset++;
        }
    }
    cupidbuild_ksyms_symbol_sort(symbols, symbol_count);
    for (index = 0u; index < symbol_count; index++) {
        if (unique_count == 0u || symbols[index].address != symbols[unique_count - 1u].address) {
            symbols[unique_count++] = symbols[index];
        }
    }
    if (unique_count > (0xffffffffu - 16u) / 8u) {
        *reason_out = "kernel symbol table size overflows i386";
        goto done;
    }
    string_offset = 16u + unique_count * 8u;
    blob_size = string_offset;
    for (index = 0u; index < unique_count; index++) {
        if (symbols[index].name_size >= 0xffffffffu ||
            blob_size > 0xffffffffu - symbols[index].name_size - 1u) {
            *reason_out = "kernel symbol strings overflow i386";
            goto done;
        }
        blob_size += symbols[index].name_size + 1u;
    }
    if (blob_size > 0xfffffffcu) {
        *reason_out = "word-packed kernel symbol source overflows i386";
        goto done;
    }
    padded_size = (blob_size + 3u) & ~(size_t)3u;
    if (padded_size > CUPIDBUILD_TOOL_BYTES) {
        *reason_out = "kernel symbol blob exceeds the validation limit";
        goto done;
    }
    blob = (unsigned char *)calloc(padded_size, 1u);
    if (blob == (unsigned char *)0) {
        *reason_out = "kernel symbol blob cannot be allocated";
        goto done;
    }
    cupidbuild_ksyms_write_le32(blob, 0u, 0x4d59534bu);
    cupidbuild_ksyms_write_le32(blob, 4u, (unsigned int)unique_count);
    cupidbuild_ksyms_write_le32(blob, 8u, (unsigned int)string_offset);
    cupidbuild_ksyms_write_le32(blob, 12u, (unsigned int)blob_size);
    string_cursor = string_offset;
    for (index = 0u; index < unique_count; index++) {
        cupidbuild_ksyms_write_le32(blob, 16u + index * 8u, symbols[index].address);
        cupidbuild_ksyms_write_le32(blob, 20u + index * 8u,
                                    (unsigned int)(string_cursor - string_offset));
        (void)memcpy(blob + string_cursor, contents + symbols[index].name_start,
                     symbols[index].name_size);
        string_cursor += symbols[index].name_size + 1u;
    }
    if (!cupidbuild_ksyms_append_literal(
            &output, "/* Auto-generated by tools/hostbuild.py -- do not edit. */\n"
                     "#include \"ksyms.h\"\n\n"
                     "/* i386 words preserve the blob bytes with fewer initializers. */\n"
                     "const unsigned int\n"
                     "__attribute__((section(\".ksyms\"), used, aligned(4)))\n"
                     "ksym_blob[] = {\n")) {
        *reason_out = "kernel symbol source exceeds the validation limit";
        goto done;
    }
    for (offset = 0u; offset < padded_size; offset += 4u) {
        unsigned int value = (unsigned int)blob[offset] | ((unsigned int)blob[offset + 1u] << 8u) |
                             ((unsigned int)blob[offset + 2u] << 16u) |
                             ((unsigned int)blob[offset + 3u] << 24u);
        size_t word_index = offset / 4u;
        if (!cupidbuild_ksyms_append_literal(&output, word_index % 8u == 0u ? "  " : " ") ||
            !cupidbuild_ksyms_append_word(&output, value) ||
            ((word_index % 8u == 7u || offset + 4u == padded_size) &&
             !cupidbuild_ksyms_append_literal(&output, "\n"))) {
            *reason_out = "kernel symbol source exceeds the validation limit";
            goto done;
        }
    }
    if (!cupidbuild_ksyms_append_literal(&output, "};\n\nconst unsigned int ksym_blob_size = ") ||
        !cupidbuild_ksyms_append_decimal(&output, (unsigned int)blob_size) ||
        !cupidbuild_ksyms_append_literal(&output, "u;\n")) {
        *reason_out = "kernel symbol source exceeds the validation limit";
        goto done;
    }
    *source_out = output.bytes;
    *source_size_out = output.size;
    output.bytes = (unsigned char *)0;
    success = 1;

done:
    free(output.bytes);
    free(blob);
    free(symbols);
    return success;
}

int cupidbuild_generate_ksyms(const cupidbuild_ksyms_request_t *request) {
    return cupidbuild_generate_ksyms_with_release(request, (const char *)0);
}

int cupidbuild_generate_ksyms_with_release(const cupidbuild_ksyms_request_t *request,
                                           const char *seed_release) {
    cupidbuild_host_transaction_t *transaction = (cupidbuild_host_transaction_t *)0;
    cupidbuild_seed_capture_t seed;
    cupidbuild_host_snapshot_t symbols_snapshot;
    cupidbuild_host_snapshot_t candidate_snapshot;
    unsigned char *symbols = (unsigned char *)0;
    unsigned char *candidate = (unsigned char *)0;
    unsigned char *expected = (unsigned char *)0;
    size_t expected_size = 0u;
    const char *validation_reason = (const char *)0;
    size_t validation_line = 0u;
    const char *disassembler_arguments[3];
    const char *object_arguments[5];
    int disassembler_status;
    int object_status;
    int result = 1;
    (void)memset(&seed, 0, sizeof(seed));
    if (request == (const cupidbuild_ksyms_request_t *)0 ||
        !cupidbuild_path_safe(request->repository_root, 0) ||
        !cupidbuild_path_safe(request->source, 1) || !cupidbuild_path_safe(request->output, 1) ||
        !cupidbuild_path_safe(request->seed_manifest, 0)) {
        (void)fprintf(stderr, "cupidbuild: invalid kernel symbol generation request\n");
        return 1;
    }
    if (!cupidbuild_host_transaction_open(request->repository_root, request->source,
                                          request->output, &transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (!cupidbuild_seed_freeze(transaction, request->repository_root, request->seed_manifest, 1, 1,
                                seed_release, &seed)) {
        goto done;
    }
    disassembler_arguments[0] = "-n";
    disassembler_arguments[1] = cupidbuild_host_frozen_source(transaction);
    disassembler_arguments[2] = (const char *)0;
    disassembler_status = cupidbuild_host_run_to_private_output(transaction, seed.frozen_tools[2],
                                                                disassembler_arguments, 60000u);
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (disassembler_status != 0) {
        (void)fprintf(stderr, "cupidbuild: checked CupidDis failed\n");
        goto done;
    }
    if (!cupidbuild_host_capture_private_output(transaction, &symbols_snapshot, &symbols)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (!cupidbuild_render_ksyms_source(symbols, symbols_snapshot.size, &expected, &expected_size,
                                        &validation_reason, &validation_line)) {
        if (validation_line != 0u) {
            (void)fprintf(stderr,
                          "cupidbuild: independent kernel symbol validation "
                          "failed at line %u: %s\n",
                          (unsigned int)validation_line, validation_reason);
        } else {
            (void)fprintf(stderr,
                          "cupidbuild: independent kernel symbol validation "
                          "failed: %s\n",
                          validation_reason);
        }
        goto done;
    }
    free(symbols);
    symbols = (unsigned char *)0;
    if (!cupidbuild_host_require_private_output(transaction, &symbols_snapshot)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    object_arguments[0] = "ksyms-source";
    object_arguments[1] = cupidbuild_host_private_output(transaction);
    object_arguments[2] = "-o";
    object_arguments[3] = cupidbuild_host_candidate(transaction);
    object_arguments[4] = (const char *)0;
    object_status =
        cupidbuild_host_run(transaction, seed.frozen_tools[4], object_arguments, 60000u);
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (!cupidbuild_host_require_private_output(transaction, &symbols_snapshot)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (object_status != 0) {
        (void)fprintf(stderr, "cupidbuild: checked CupidObj failed\n");
        goto done;
    }
    if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot, &candidate)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (candidate_snapshot.size != expected_size ||
        memcmp(candidate, expected, expected_size) != 0) {
        (void)fprintf(stderr, "cupidbuild: checked CupidObj kernel symbol source differs "
                              "from the independent renderer\n");
        goto done;
    }
    free(candidate);
    candidate = (unsigned char *)0;
    free(expected);
    expected = (unsigned char *)0;
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (!cupidbuild_host_require_private_output(transaction, &symbols_snapshot) ||
        !cupidbuild_host_require_candidate(transaction, &candidate_snapshot)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (!cupidbuild_host_require_publication_boundary(transaction) ||
        !cupidbuild_host_publish(transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    result = 0;

done:
    free(expected);
    free(candidate);
    free(symbols);
    cupidbuild_seed_capture_close(&seed);
    return cupidbuild_finish_publication(transaction, result, "kernel symbol object");
}

static int cupidbuild_code_path_equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        unsigned char left_byte = (unsigned char)*left;
        unsigned char right_byte = (unsigned char)*right;
        if (left_byte >= (unsigned char)'A' && left_byte <= (unsigned char)'Z') {
            left_byte = (unsigned char)(left_byte + ('a' - 'A'));
        }
        if (right_byte >= (unsigned char)'A' && right_byte <= (unsigned char)'Z') {
            right_byte = (unsigned char)(right_byte + ('a' - 'A'));
        }
        if (left_byte != right_byte) {
            return 0;
        }
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static int cupidbuild_code_manifest(unsigned char *bytes, size_t size, const char **paths,
                                    size_t *count_out, const char **reason_out, size_t *line_out) {
    size_t start = 0u;
    size_t count = 0u;
    size_t line = 1u;
    if (size == 0u) {
        *reason_out = "code input manifest may not be empty";
        return 0;
    }
    if (bytes[size - 1u] != (unsigned char)'\n') {
        *reason_out = "code input manifest must end with a newline";
        return 0;
    }
    while (start < size) {
        size_t end = start;
        size_t component = start;
        size_t index;
        while (end < size && bytes[end] != (unsigned char)'\n') {
            unsigned char byte = bytes[end];
            if (byte == (unsigned char)'\r') {
                *reason_out = "code input manifest must use LF newlines";
                *line_out = line;
                return 0;
            }
            if (byte == (unsigned char)'\\') {
                *reason_out = "code input path must use forward slashes";
                *line_out = line;
                return 0;
            }
            if (byte <= (unsigned char)' ' || byte == 127u) {
                *reason_out = "code input path may not contain whitespace or control bytes";
                *line_out = line;
                return 0;
            }
            end++;
        }
        if (end == start) {
            *reason_out = "code input manifest line is blank";
            *line_out = line;
            return 0;
        }
        if (bytes[start] == (unsigned char)'#') {
            *reason_out = "code input manifest line may not be a comment";
            *line_out = line;
            return 0;
        }
        if (bytes[start] == (unsigned char)'/' || bytes[end - 1u] == (unsigned char)'/') {
            *reason_out = "code input path is not canonical and relative";
            *line_out = line;
            return 0;
        }
        for (index = start; index <= end; index++) {
            if (index != end && bytes[index] != (unsigned char)'/') {
                continue;
            }
            if (index == component ||
                (index - component == 1u && bytes[component] == (unsigned char)'.') ||
                (index - component == 2u && bytes[component] == (unsigned char)'.' &&
                 bytes[component + 1u] == (unsigned char)'.')) {
                *reason_out = "code input path is not canonical and relative";
                *line_out = line;
                return 0;
            }
            component = index + 1u;
        }
        if (count == CUPIDBUILD_CODE_INPUTS) {
            *reason_out = "code input manifest exceeds the 500-input limit";
            *line_out = line;
            return 0;
        }
        bytes[end] = 0u;
        if (!cupidbuild_path_safe((const char *)(bytes + start), 1)) {
            *reason_out = "code input path is unsafe";
            *line_out = line;
            return 0;
        }
        for (index = 0u; index < count; index++) {
            if (cupidbuild_code_path_equal(paths[index], (const char *)(bytes + start))) {
                *reason_out = "code input is listed more than once";
                *line_out = line;
                return 0;
            }
        }
        paths[count++] = (const char *)(bytes + start);
        start = end + 1u;
        line++;
    }
    *count_out = count;
    return 1;
}

static int cupidbuild_flat_region_less(const cupidbuild_flat_region_t *left,
                                       const cupidbuild_flat_region_t *right) {
    return left->address < right->address ||
           (left->address == right->address && left->order < right->order);
}

static void cupidbuild_flat_region_swap(cupidbuild_flat_region_t *left,
                                        cupidbuild_flat_region_t *right) {
    cupidbuild_flat_region_t temporary = *left;
    *left = *right;
    *right = temporary;
}

static void cupidbuild_flat_region_sift_down(cupidbuild_flat_region_t *regions, size_t root,
                                             size_t count) {
    for (;;) {
        size_t child;
        size_t selected;
        if (root >= count / 2u) {
            return;
        }
        child = root * 2u + 1u;
        selected = root;
        if (cupidbuild_flat_region_less(&regions[selected], &regions[child])) {
            selected = child;
        }
        if (child + 1u < count &&
            cupidbuild_flat_region_less(&regions[selected], &regions[child + 1u])) {
            selected = child + 1u;
        }
        if (selected == root) {
            return;
        }
        cupidbuild_flat_region_swap(&regions[root], &regions[selected]);
        root = selected;
    }
}

static void cupidbuild_flat_region_sort(cupidbuild_flat_region_t *regions, size_t count) {
    size_t start = count / 2u;
    size_t end = count;
    while (start != 0u) {
        start--;
        cupidbuild_flat_region_sift_down(regions, start, count);
    }
    while (end > 1u) {
        cupidbuild_flat_region_swap(&regions[0], &regions[end - 1u]);
        end--;
        cupidbuild_flat_region_sift_down(regions, 0u, end);
    }
}

static int cupidbuild_render_flat_image(const unsigned char *bytes, size_t size,
                                        unsigned char **image_out, size_t *image_size_out,
                                        const char **reason_out) {
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_elf32_object_t object;
    cupidbuild_flat_region_t *regions = (cupidbuild_flat_region_t *)0;
    unsigned char *image = (unsigned char *)0;
    size_t load_count = 0u;
    size_t region_count = 0u;
    size_t position = 0u;
    size_t index;
    unsigned int base;
    unsigned int cursor;
    int success = 0;
    if (size > 4294967295u || ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
        *reason_out = "linked kernel cannot be inspected";
        return 0;
    }
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    if (ctool_job_open(&config, &job) != CTOOL_OK) {
        *reason_out = "linked kernel inspection job cannot be opened";
        return 0;
    }
    source.path.text = ctool_string("/kernel/kernel.elf");
    source.contents = ctool_bytes(bytes, (ctool_u32)size);
    if (ctool_elf32_read(job, &source, &object) != CTOOL_OK ||
        object.file_type != CTOOL_ELF32_ET_EXEC) {
        *reason_out = "linked kernel is not a static i386 executable";
        goto done;
    }
    for (index = 0u; index < object.program_header_count; index++) {
        if (object.program_headers[index].type == CTOOL_ELF32_PT_LOAD) {
            load_count++;
            if (object.program_headers[index].file_size != 0u) {
                region_count++;
            }
        }
    }
    if (load_count == 0u) {
        for (index = 0u; index < object.section_count; index++) {
            const ctool_elf32_section_t *section = &object.sections[index];
            if ((section->flags & CTOOL_ELF32_SHF_ALLOC) == 0u ||
                section->type == CTOOL_ELF32_SHT_NOBITS) {
                continue;
            }
            if (section->type != CTOOL_ELF32_SHT_PROGBITS) {
                *reason_out = "linked kernel has unsupported allocated content";
                goto done;
            }
            if (section->size != 0u) {
                region_count++;
            }
        }
    }
    if (region_count == 0u) {
        *reason_out = "linked kernel has no initialized load";
        goto done;
    }
    regions = (cupidbuild_flat_region_t *)calloc(region_count, sizeof(*regions));
    if (regions == (cupidbuild_flat_region_t *)0) {
        *reason_out = "flat-image region table cannot be allocated";
        goto done;
    }
    if (load_count != 0u) {
        for (index = 0u; index < object.program_header_count; index++) {
            const ctool_elf32_program_header_t *header = &object.program_headers[index];
            if (header->type == CTOOL_ELF32_PT_LOAD && header->file_size != 0u) {
                regions[position].address = header->physical_address;
                regions[position].order = header->file_index;
                regions[position].contents = header->contents;
                position++;
            }
        }
    } else {
        for (index = 0u; index < object.section_count; index++) {
            const ctool_elf32_section_t *section = &object.sections[index];
            if ((section->flags & CTOOL_ELF32_SHF_ALLOC) != 0u &&
                section->type == CTOOL_ELF32_SHT_PROGBITS && section->size != 0u) {
                regions[position].address = section->address;
                regions[position].order = section->file_index;
                regions[position].contents = section->contents;
                position++;
            }
        }
    }
    cupidbuild_flat_region_sort(regions, region_count);
    base = regions[0].address;
    cursor = base;
    for (index = 0u; index < region_count; index++) {
        const cupidbuild_flat_region_t *region = &regions[index];
        if (region->address < cursor || region->address > 0xffffffffu - region->contents.size) {
            *reason_out = "linked kernel initialized load ranges overlap or overflow";
            goto done;
        }
        cursor = region->address + region->contents.size;
    }
    if ((size_t)(cursor - base) > CUPIDBUILD_TOOL_BYTES) {
        *reason_out = "flat kernel exceeds the 64 MiB transaction limit";
        goto done;
    }
    image = (unsigned char *)calloc((size_t)(cursor - base), 1u);
    if (image == (unsigned char *)0) {
        *reason_out = "flat kernel buffer cannot be allocated";
        goto done;
    }
    for (index = 0u; index < region_count; index++) {
        (void)memcpy(image + (regions[index].address - base), regions[index].contents.data,
                     regions[index].contents.size);
    }
    *image_out = image;
    *image_size_out = (size_t)(cursor - base);
    image = (unsigned char *)0;
    success = 1;

done:
    free(image);
    free(regions);
    ctool_job_close(job);
    return success;
}

static void cupidbuild_report_checked_failure(cupidbuild_host_transaction_t *transaction,
                                              const char *operation) {
    const char *error = cupidbuild_host_error(transaction);
    if (error != (const char *)0 && error[0] != '\0') {
        (void)fprintf(stderr, "cupidbuild: %s: %s\n", operation, error);
    } else {
        (void)fprintf(stderr, "cupidbuild: %s\n", operation);
    }
}

int cupidbuild_flatten_kernel(const cupidbuild_kernel_request_t *request) {
    return cupidbuild_flatten_kernel_with_release(request, (const char *)0);
}

int cupidbuild_flatten_kernel_with_release(const cupidbuild_kernel_request_t *request,
                                           const char *seed_release) {
    cupidbuild_host_transaction_t *transaction = (cupidbuild_host_transaction_t *)0;
    cupidbuild_seed_capture_t seed;
    const char *logical_paths[CUPIDBUILD_CODE_INPUTS];
    const char *private_paths[CUPIDBUILD_CODE_INPUTS];
    const char *pass_one = (const char *)0;
    const char *linked = (const char *)0;
    const char *linked_frozen = (const char *)0;
    unsigned char *manifest = (unsigned char *)0;
    size_t manifest_size = 0u;
    size_t input_count = 0u;
    const char *manifest_reason = (const char *)0;
    size_t manifest_line = 0u;
    unsigned char *linked_bytes = (unsigned char *)0;
    size_t linked_size = 0u;
    unsigned char *expected = (unsigned char *)0;
    size_t expected_size = 0u;
    const char *flat_reason = (const char *)0;
    unsigned char *candidate = (unsigned char *)0;
    cupidbuild_host_snapshot_t candidate_snapshot;
    const char *disassembler_arguments[CUPIDBUILD_CODE_INPUTS + 2u];
    const char *linked_arguments[7];
    const char *object_arguments[5];
    size_t index;
    int status;
    int result = 1;
    (void)memset(&seed, 0, sizeof(seed));
    if (request == (const cupidbuild_kernel_request_t *)0 ||
        !cupidbuild_path_safe(request->repository_root, 0) ||
        !cupidbuild_path_safe(request->input_manifest, 1) ||
        !cupidbuild_path_safe(request->output, 1) ||
        !cupidbuild_path_safe(request->seed_manifest, 0)) {
        (void)fprintf(stderr, "cupidbuild: invalid kernel flatten request\n");
        return 1;
    }
    if (!cupidbuild_host_transaction_open(request->repository_root, request->input_manifest,
                                          request->output, &transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    manifest =
        cupidbuild_host_read_frozen_input(transaction, cupidbuild_host_frozen_source(transaction),
                                          CUPIDBUILD_MANIFEST_BYTES, &manifest_size);
    if (manifest == (unsigned char *)0 ||
        !cupidbuild_code_manifest(manifest, manifest_size, logical_paths, &input_count,
                                  &manifest_reason, &manifest_line)) {
        if (manifest_reason == (const char *)0) {
            manifest_reason = "code input manifest cannot be read";
        }
        if (manifest_line != 0u) {
            (void)fprintf(stderr, "cupidbuild: invalid code input manifest at line %u: %s\n",
                          (unsigned int)manifest_line, manifest_reason);
        } else {
            (void)fprintf(stderr, "cupidbuild: invalid code input manifest: %s\n", manifest_reason);
        }
        goto done;
    }
    if (!cupidbuild_host_reserve_inputs(
            transaction, input_count + 8u + (seed_release != (const char *)0 ? 1u : 0u))) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    for (index = 0u; index < input_count; index++) {
        char live_path[CUPIDBUILD_PATH_BYTES];
        char private_name[32];
        const char *frozen_path = (const char *)0;
        int written =
            snprintf(private_name, sizeof(private_name), "code-%03u.bin", (unsigned int)index);
        if (written <= 0 || (size_t)written >= sizeof(private_name) ||
            !cupidbuild_join(live_path, sizeof(live_path), request->repository_root,
                             logical_paths[index]) ||
            !cupidbuild_host_freeze_input(transaction, live_path, private_name, &frozen_path,
                                          (cupidbuild_host_snapshot_t *)0)) {
            (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
            goto done;
        }
        private_paths[index] = frozen_path;
#if defined(_WIN32)
        /* The retained private directory is the child's working directory. */
        {
            const char *cursor = frozen_path;
            while (*cursor != '\0') {
                if (*cursor == '/' || *cursor == '\\') {
                    private_paths[index] = cursor + 1;
                }
                cursor++;
            }
        }
#endif
        if (strcmp(logical_paths[index], "kernel/kernel.elf.pass1") == 0) {
            pass_one = private_paths[index];
        } else if (strcmp(logical_paths[index], "kernel/kernel.elf") == 0) {
            linked = private_paths[index];
            linked_frozen = frozen_path;
        }
    }
    if (pass_one == (const char *)0 || linked == (const char *)0) {
        (void)fprintf(stderr, "cupidbuild: code input manifest must include both linked kernels\n");
        goto done;
    }
    if (!cupidbuild_seed_freeze(transaction, request->repository_root, request->seed_manifest, 1, 1,
                                seed_release, &seed)) {
        goto done;
    }
    disassembler_arguments[0] = "--require-known";
    for (index = 0u; index < input_count; index++) {
        disassembler_arguments[index + 1u] = private_paths[index];
    }
    disassembler_arguments[input_count + 1u] = (const char *)0;
    status = cupidbuild_host_run_in_private(transaction, seed.frozen_tools[2],
                                            disassembler_arguments, 300000u);
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (status != 0) {
        cupidbuild_report_checked_failure(transaction, "checked CupidDis failed");
        goto done;
    }
    linked_arguments[0] = "--require-known";
    linked_arguments[1] = "--require-local-targets";
    linked_arguments[2] = "--require-code-anchors";
    linked_arguments[3] = pass_one;
    linked_arguments[4] = linked;
    linked_arguments[5] = (const char *)0;
    status = cupidbuild_host_run_in_private(transaction, seed.frozen_tools[2], linked_arguments,
                                            600000u);
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (status != 0) {
        cupidbuild_report_checked_failure(transaction, "checked CupidDis linked validation failed");
        goto done;
    }
    linked_bytes = cupidbuild_host_read_frozen_input(transaction, linked_frozen,
                                                     CUPIDBUILD_TOOL_BYTES, &linked_size);
    if (linked_bytes == (unsigned char *)0 ||
        !cupidbuild_render_flat_image(linked_bytes, linked_size, &expected, &expected_size,
                                      &flat_reason)) {
        (void)fprintf(stderr, "cupidbuild: independent flat kernel validation failed: %s\n",
                      flat_reason == (const char *)0 ? "unreadable linked kernel" : flat_reason);
        goto done;
    }
    free(linked_bytes);
    linked_bytes = (unsigned char *)0;
    object_arguments[0] = "flat";
    object_arguments[1] = linked;
    object_arguments[2] = "-o";
    object_arguments[3] = cupidbuild_host_candidate(transaction);
    object_arguments[4] = (const char *)0;
    status = cupidbuild_host_run_in_private(transaction, seed.frozen_tools[4], object_arguments,
                                            300000u);
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (status != 0) {
        cupidbuild_report_checked_failure(transaction, "checked CupidObj failed");
        goto done;
    }
    if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot, &candidate)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (candidate_snapshot.size != expected_size ||
        memcmp(candidate, expected, expected_size) != 0) {
        (void)fprintf(stderr, "cupidbuild: checked CupidObj flat kernel differs from the "
                              "independent renderer\n");
        goto done;
    }
    free(candidate);
    candidate = (unsigned char *)0;
    free(expected);
    expected = (unsigned char *)0;
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot) ||
        !cupidbuild_host_require_publication_boundary(transaction) ||
        !cupidbuild_host_publish(transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    result = 0;

done:
    free(candidate);
    free(expected);
    free(linked_bytes);
    free(manifest);
    cupidbuild_seed_capture_close(&seed);
    return cupidbuild_finish_publication(transaction, result, "flattened kernel");
}
