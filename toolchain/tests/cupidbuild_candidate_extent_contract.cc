#include "cupidbuild_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

static int decode(char *text) {
    size_t size = strlen(text);
    size_t index;
    if (size % 2u)
        return 0;
    for (index = 0u; index < size; index += 2u) {
        unsigned int high = (unsigned char)text[index];
        unsigned int low = (unsigned char)text[index + 1u];
        high = high >= '0' && high <= '9' ? high - '0' : high - 'a' + 10u;
        low = low >= '0' && low <= '9' ? low - '0' : low - 'a' + 10u;
        if (high > 15u || low > 15u || high * 16u + low == 0u)
            return 0;
        text[index / 2u] = (char)(high * 16u + low);
    }
    text[size / 2u] = 0;
    return 1;
}

static int extent(const char *text, size_t *result) {
    size_t value = 0u;
    if (*text == 0)
        return 0;
    while (*text != 0) {
        unsigned int digit = (unsigned char)*text++ - '0';
        if (digit > 9u || value > (2147483647u - digit) / 10u)
            return 0;
        value = value * 10u + digit;
    }
    *result = value;
    return 1;
}

static int write_pattern(const char *path, size_t size) {
    unsigned char block[65536];
    size_t index;
#if defined(_WIN32)
    int standard = strcmp(path, "-") == 0;
    HANDLE file = standard ? GetStdHandle(STD_OUTPUT_HANDLE)
                           : CreateFileA(path, GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || file == NULL)
        return 0;
#else
    FILE *file = strcmp(path, "-") == 0 ? stdout : fopen(path, "wb");
    if (file == NULL)
        return 0;
#endif
    for (index = 0u; index < sizeof(block); index++)
        block[index] = (unsigned char)(index * 29u + 17u);
    while (size != 0u) {
        size_t chunk = size < sizeof(block) ? size : sizeof(block);
        int written;
#if defined(_WIN32)
        DWORD count = 0u;
        written = WriteFile(file, block, (DWORD)chunk, &count, NULL) && count == chunk;
#else
        written = fwrite(block, 1u, chunk, file) == chunk;
#endif
        if (!written) {
#if defined(_WIN32)
            if (!standard)
                (void)CloseHandle(file);
#else
            (void)fclose(file);
#endif
            return 0;
        }
        size -= chunk;
    }
#if defined(_WIN32)
    return standard || CloseHandle(file);
#else
    return fclose(file) == 0;
#endif
}

static int check_pattern(const char *path, size_t size) {
    unsigned char block[65536];
#if defined(_WIN32)
    HANDLE file =
        CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    int ok = file != INVALID_HANDLE_VALUE && file != NULL;
#else
    FILE *file = fopen(path, "rb");
    int ok = file != NULL;
#endif
    while (ok && size != 0u) {
        size_t chunk = size < sizeof(block) ? size : sizeof(block);
        size_t index;
#if defined(_WIN32)
        DWORD count = 0u;
        ok = ReadFile(file, block, (DWORD)chunk, &count, NULL) && count == chunk;
#else
        ok = fread(block, 1u, chunk, file) == chunk;
#endif
        for (index = 0u; ok && index < chunk; index++)
            if (block[index] != (unsigned char)(index * 29u + 17u))
                ok = 0;
        size -= chunk;
    }
#if defined(_WIN32)
    if (ok) {
        DWORD count = 0u;
        ok = ReadFile(file, block, 1u, &count, NULL) && count == 0u;
    }
    return file != INVALID_HANDLE_VALUE && file != NULL && CloseHandle(file) && ok;
#else
    if (ok)
        ok = fread(block, 1u, 1u, file) == 0u && !ferror(file);
    return file != NULL && fclose(file) == 0 && ok;
#endif
}

static int candidate_store_case(const char *root, const char *mode, size_t size,
                                unsigned long long capacity) {
    static unsigned char block[65536];
    static unsigned char observed[65536];
    cupidbuild_host_transaction_t *transaction = NULL;
    cupidbuild_host_snapshot_t snapshot;
    unsigned long long extent = 0u;
    size_t count = size < sizeof(block) ? size : sizeof(block);
    int changed = 0;
    int ok = 0;

    if (strcmp(mode, "store-invalid-capacity") == 0) {
        transaction = (cupidbuild_host_transaction_t *)1;
        ok = !cupidbuild_host_publication_transaction_open_bounded(
                 root, "nested/file.o", capacity, &transaction) && !transaction;
        if (transaction && transaction != (cupidbuild_host_transaction_t *)1)
            (void)cupidbuild_host_transaction_close(transaction);
        return ok;
    }
    if (!cupidbuild_host_publication_transaction_open_bounded(root, "nested/file.o", capacity,
                                                              &transaction) ||
        !cupidbuild_host_frozen_source(transaction) || *cupidbuild_host_frozen_source(transaction) ||
        !cupidbuild_host_candidate_store_open(transaction, size) ||
        !cupidbuild_host_candidate_store_extent(transaction, &extent) || extent != size)
        goto done;
    memset(block, 0x5a, count);
    if (!cupidbuild_host_candidate_store_write(transaction, 0u, block, count) ||
        !cupidbuild_host_candidate_store_read(transaction, 0u, observed, count) ||
        memcmp(block, observed, count))
        goto done;
    if (strcmp(mode, "store-invalid-range") == 0) {
        observed[0] = 0xa5u;
        ok = !cupidbuild_host_candidate_store_read(transaction, size, observed, 1u) &&
             observed[0] == 0xa5u &&
             !cupidbuild_host_candidate_store_finish(transaction, &snapshot) && !snapshot.present &&
             !cupidbuild_host_publish(transaction);
        goto done;
    }
    if (size > count &&
        (!cupidbuild_host_candidate_store_read(transaction, size - 1u, observed, 1u) ||
         observed[0] ||
         !cupidbuild_host_candidate_store_write(transaction, size - 1u, block, 1u)))
        goto done;
    if (!cupidbuild_host_candidate_store_finish(transaction, &snapshot) || snapshot.size != size ||
        (size && (!cupidbuild_host_candidate_store_read(transaction, size - 1u, observed, 1u) ||
                  observed[0] != 0x5au)))
        goto done;
    if (strcmp(mode, "store-late-write") == 0) {
        ok = !cupidbuild_host_candidate_store_write(transaction, 0u, block, 1u) &&
             !cupidbuild_host_publish(transaction);
    } else {
        ok = cupidbuild_host_publish_if_changed(transaction, &changed);
        if (ok)
            printf("changed %d\n", changed);
    }

done:
    if (!ok)
        fprintf(stderr, "%s\n", cupidbuild_host_error(transaction));
    return cupidbuild_host_transaction_close(transaction) && ok;
}

static int frozen_input_case(const char *root, const char *mode, const char *size_text,
                             size_t size) {
    cupidbuild_host_transaction_t *transaction = NULL;
    cupidbuild_host_snapshot_t snapshot;
    const char *frozen = NULL;
    unsigned char tail[13];
    size_t offset;
    size_t index;
    int ok = 0;

    if (size < sizeof(tail) ||
        !cupidbuild_host_publication_transaction_open_bounded(root, "nested/file.o", size,
                                                              &transaction) ||
        !cupidbuild_host_reserve_inputs(transaction, 8u))
        goto done;
    if (strncmp(mode, "frozen-tool", 11u) == 0) {
        const char *tool = NULL;
        const char *arguments[4] = {"emit-file", NULL, size_text, NULL};
        char executable[8192];
        int reject = strcmp(mode, "frozen-tool-failure") == 0;
        int status;
        snprintf(executable, sizeof(executable), "%s/writer", root);
        if (!cupidbuild_host_freeze_input(transaction, executable, "writer.exe", &tool, NULL) ||
            !cupidbuild_host_make_input_executable(transaction, tool))
            goto done;
        arguments[1] = cupidbuild_host_private_output(transaction);
        if (reject)
            arguments[0] = "invalid-command";
        status = cupidbuild_host_run_in_private(transaction, tool, arguments, 10000u);
        if (reject) {
            ok = status == 90 &&
                 !cupidbuild_host_freeze_tool_output(transaction, &frozen, &snapshot) &&
                 !frozen && !snapshot.present && !cupidbuild_host_publish(transaction);
            goto done;
        }
        if (status != 0 || !cupidbuild_host_freeze_tool_output(transaction, &frozen, &snapshot))
            goto done;
        if (strcmp(mode, "frozen-tool-repeat") == 0) {
            ok = !cupidbuild_host_freeze_tool_output(transaction, &frozen, &snapshot) &&
                 !frozen && !snapshot.present && !cupidbuild_host_publish(transaction);
            goto done;
        }
        if (cupidbuild_host_capture_private_output(transaction, NULL, NULL) ||
            cupidbuild_host_write_private_output(transaction, tail, 0u))
            goto done;
    } else if (!cupidbuild_host_freeze_previous_output(transaction, "previous.img", &frozen,
                                                       &snapshot)) {
        goto done;
    }
    if (strcmp(mode, "frozen-grow") == 0 &&
        (!cupidbuild_host_reserve_inputs(transaction, 4112u) ||
         cupidbuild_host_reserve_inputs(transaction, 4113u)))
        goto done;
    offset = size - sizeof(tail);
    if (!snapshot.present || snapshot.size != size ||
        !cupidbuild_host_read_frozen_input_range(transaction, frozen, offset, tail, sizeof(tail)) ||
        !cupidbuild_host_read_frozen_input_range(transaction, frozen, size, NULL, 0u))
        goto done;
    for (index = 0u; index < sizeof(tail); index++) {
        if (tail[index] != (unsigned char)((offset + index) * 29u + 17u))
            goto done;
    }
    if (strcmp(mode, "frozen-invalid-range") == 0) {
        ok = !cupidbuild_host_read_frozen_input_range(transaction, frozen, offset + 1u, tail,
                                                      sizeof(tail)) &&
             tail[0] == (unsigned char)(offset * 29u + 17u) &&
             !cupidbuild_host_candidate_store_open(transaction, sizeof(tail)) &&
             !cupidbuild_host_publish(transaction);
        goto done;
    }
    ok = cupidbuild_host_candidate_store_open(transaction, sizeof(tail)) &&
         cupidbuild_host_candidate_store_write(transaction, 0u, tail, sizeof(tail)) &&
         cupidbuild_host_candidate_store_finish(transaction, &snapshot) &&
         cupidbuild_host_publish(transaction);

done:
    if (!ok)
        fprintf(stderr, "%s\n", cupidbuild_host_error(transaction));
    return cupidbuild_host_transaction_close(transaction) && ok;
}

int main(int argc, char **argv) {
    cupidbuild_host_transaction_t *transaction = NULL;
    cupidbuild_host_snapshot_t snapshot;
    unsigned char *bytes = NULL;
    const char *path;
    const char *mode;
    const char *frozen = NULL;
    const char *arguments[4];
    char writer[8192];
    char checker_extent[32];
    size_t size;
    unsigned int index;
    int private_file;
    int payload;
    int captured = 0;
    int changed = -1;
    int ok;
    if (argc != 4 && argc != 5)
        return 90;
    if (!extent(argv[3], &size))
        return 91;
    if (strcmp(argv[1], "emit-file") == 0 || strcmp(argv[1], "emit-stdout") == 0)
        return write_pattern(argv[2], size) ? 0 : 93;
    if (strcmp(argv[1], "check-file") == 0)
        return check_pattern(argv[2], size) ? 0 : 94;
    if (argc != 5 || !decode(argv[1]))
        return 90;
    mode = argv[2];
    if (strncmp(mode, "frozen-", 7u) == 0)
        return frozen_input_case(argv[1], mode, argv[3], size) ? 0 : 3;
    if (strncmp(mode, "store-", 6u) == 0) {
        size_t capacity = 0u;
        unsigned long long selected;
        if (strcmp(argv[4], "high") == 0) {
            selected = 2147483648ULL;
        } else {
            if (!extent(argv[4], &capacity))
                return 91;
            selected = capacity;
        }
        return candidate_store_case(argv[1], mode, size, selected) ? 0 : 3;
    }
    private_file = strncmp(mode, "private", 7u) == 0;
    payload = strstr(mode, "bytes") != NULL;
    if (strcmp(mode, "invalid-capacity") == 0) {
        unsigned long long invalid = strcmp(argv[4], "zero") == 0      ? 0ull
                                     : strcmp(argv[4], "high") == 0    ? 4294967297ull
                                     : strcmp(argv[4], "maximum") == 0 ? 18446744073709551615ull
                                                                       : 2147483648ull;
        transaction = (cupidbuild_host_transaction_t *)1;
        ok = !cupidbuild_host_transaction_open_bounded(argv[1], "source.cc", "nested/file.o",
                                                       invalid, &transaction) &&
             transaction == NULL &&
             !cupidbuild_host_transaction_open_bounded(argv[1], "source.cc", "nested/file.o", 1ull,
                                                       NULL);
        printf("invalid %d\n", ok);
        return ok ? 0 : 3;
    }
    {
        size_t capacity;
        if (!extent(argv[4], &capacity))
            return 91;
        ok = strncmp(mode, "ordinary", 8u) == 0
                 ? cupidbuild_host_transaction_open(argv[1], "source.cc", "nested/file.o",
                                                    &transaction)
                 : cupidbuild_host_transaction_open_bounded(argv[1], "source.cc", "nested/file.o",
                                                            (unsigned long long)capacity,
                                                            &transaction);
    }
    if (!ok) {
        fprintf(stderr, "open: %s\n", cupidbuild_host_error(transaction));
        (void)cupidbuild_host_transaction_close(transaction);
        return 2;
    }
    path = private_file ? cupidbuild_host_private_output(transaction)
                        : cupidbuild_host_candidate(transaction);
    if (strcmp(mode, "candidate-limit") == 0 || strcmp(mode, "ordinary-limit") == 0) {
        ok = write_pattern(path, size);
    } else {
        snprintf(writer, sizeof(writer), "%s/writer", argv[1]);
        arguments[0] = private_file ? "emit-stdout" : "emit-file";
        arguments[1] = private_file ? "-" : path;
        arguments[2] = argv[3];
        arguments[3] = NULL;
        ok = cupidbuild_host_freeze_input(transaction, writer, "writer.exe", &frozen, NULL) &&
             cupidbuild_host_make_input_executable(transaction, frozen);
        if (ok) {
            int status = private_file ? cupidbuild_host_run_to_private_output(transaction, frozen,
                                                                              arguments, 30000u)
                                      : cupidbuild_host_run(transaction, frozen, arguments, 30000u);
            if (status != 0)
                fprintf(stderr, "writer exit %d\n", status);
            ok = status == 0;
        }
        if (ok && strstr(mode, "checker") != NULL) {
            captured = cupidbuild_host_capture_candidate(transaction, &snapshot, NULL);
            ok = captured && snapshot.size == size &&
                 cupidbuild_host_require_candidate(transaction, &snapshot);
            if (ok) {
                int status;
                int wrong_size = strstr(mode, "wrong-size") != NULL;
                arguments[0] = "check-file";
                if (wrong_size) {
                    if (size == 0u)
                        ok = 0;
                    else {
                        snprintf(checker_extent, sizeof(checker_extent), "%u",
                                 (unsigned int)(size - 1u));
                        arguments[2] = checker_extent;
                    }
                }
                status = ok ? cupidbuild_host_run(transaction, frozen, arguments, 30000u) : -1;
                printf("checker %d\n", status);
                if (wrong_size) {
                    int closed;
                    if (status != 94)
                        fprintf(stderr, "checker exit %d: %s\n", status,
                                cupidbuild_host_error(transaction));
                    closed = cupidbuild_host_transaction_close(transaction);
                    return status == 94 && closed ? 0 : 3;
                }
                if (status != 0)
                    fprintf(stderr, "checker exit %d\n", status);
                ok = status == 0;
            }
        }
    }
    captured =
        ok && (captured || (private_file ? cupidbuild_host_capture_private_output(
                                               transaction, &snapshot, payload ? &bytes : NULL)
                                         : cupidbuild_host_capture_candidate(
                                               transaction, &snapshot, payload ? &bytes : NULL)));
    if (strstr(mode, "limit") != NULL) {
        printf("limit %d\n", captured);
        ok = ok && !captured && bytes == NULL;
        if (ok && strcmp(mode, "candidate-bytes-limit") == 0) {
            ok = cupidbuild_host_capture_candidate(transaction, &snapshot, NULL) &&
                 snapshot.size == size && cupidbuild_host_require_candidate(transaction, &snapshot);
            printf("digest-after-limit %d\n", ok);
        }
    } else {
        ok = captured && snapshot.present && snapshot.size == size;
        if (ok && payload) {
            unsigned char digest[32];
            ok = bytes != NULL && bytes[size] == 0u;
            for (index = 0u; ok && index < size; index++)
                if (bytes[index] != (unsigned char)(index * 29u + 17u))
                    ok = 0;
            if (ok) {
                cupidbuild_host_sha256_bytes(bytes, size, digest);
                ok = memcmp(digest, snapshot.sha256, sizeof(digest)) == 0;
            }
        }
        free(bytes);
        bytes = NULL;
        if (ok) {
            printf("captured %u ", (unsigned int)snapshot.size);
            for (index = 0u; index < 32u; index++)
                printf("%02x", snapshot.sha256[index]);
            puts("");
        }
        if (ok && strstr(mode, "wrong") != NULL) {
            cupidbuild_host_snapshot_t wrong = snapshot;
            wrong.sha256[0] ^= 1u;
            ok = !(private_file ? cupidbuild_host_require_private_output(transaction, &wrong)
                                : cupidbuild_host_require_candidate(transaction, &wrong));
            printf("wrong %d\n", !ok);
        }
        if (ok)
            ok = private_file ? cupidbuild_host_require_private_output(transaction, &snapshot)
                              : cupidbuild_host_require_candidate(transaction, &snapshot);
        if (ok && strstr(mode, "pause") != NULL) {
            unsigned char resume;
            puts("ready");
            if (fflush(stdout) != 0 || fread(&resume, 1u, 1u, stdin) != 1u || resume != 'x')
                ok = 0;
            if (ok) {
                int boundary = cupidbuild_host_require_publication_boundary(transaction);
                int published = cupidbuild_host_publish_if_changed(transaction, &changed);
                printf("drift %d %d\n", boundary, published);
                ok = !boundary && !published;
            }
        } else if (ok && !private_file && strstr(mode, "wrong") == NULL) {
            ok = cupidbuild_host_publish_if_changed(transaction, &changed);
            printf("changed %d\n", changed);
        }
    }
    if (!ok)
        fprintf(stderr, "check: %s\n", cupidbuild_host_error(transaction));
    {
        int closed = cupidbuild_host_transaction_close(transaction);
        if (strstr(mode, "limit") != NULL)
            printf("closed %d\n", closed);
        else if (!closed)
            return 92;
    }
    return ok ? 0 : 3;
}
