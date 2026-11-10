#include "cupidbuild_disk_options.h"

#include <stdlib.h>
#include <string.h>

#include <path_encoding.h>

enum { TOKEN_VALUE, TOKEN_OPTION, TOKEN_UNKNOWN, TOKEN_AMBIGUOUS, TOKEN_END };

enum {
    OPTION_HELP,
    OPTION_SEED,
    OPTION_IMAGE,
    OPTION_BOOT,
    OPTION_KERNEL,
    OPTION_HDD,
    OPTION_FAT,
    OPTION_STAGE,
    OPTION_FORCE,
    OPTION_WADS
};

typedef struct {
    unsigned int kind;
    unsigned int option;
    const char *explicit_value;
    unsigned int short_help;
} option_token_t;

struct cupidbuild_disk_options {
    cupidbuild_disk_options_view_t view;
    cupidbuild_disk_argument_stage_t *stages;
    char *wad_destinations;
};

static const char *const option_names[] = {
    "--help",   "--seed-manifest", "--image", "--bootloader",   "--kernel",
    "--hdd-mb", "--fat-start-lba", "--stage", "--force-format", "--wads"};

static size_t scalar_width(const char *text) {
    unsigned int first = (unsigned char)text[0];
    if (first < 0x80u)
        return 1u;
    if (first < 0xe0u)
        return 2u;
    if (first < 0xf0u)
        return 3u;
    return 4u;
}

static int decimal_at(const char *text, unsigned int unicode_major) {
    cupidbuild_disk_integer_t result;
    if (!*text)
        return 0;
    return cupidbuild_disk_integer_parse((const unsigned char *)text, scalar_width(text),
                                         unicode_major, 0u,
                                         &result) == CUPIDBUILD_DISK_INTEGER_OK &&
           result.digits == 1u;
}

static int negative_value(const char *text, unsigned int unicode_major,
                          unsigned int grammar_profile) {
    const char *position = text + 1;
    size_t integer_digits = 0;
    size_t fraction_digits = 0;
    if (grammar_profile == 1u) {
        if (*position == '.')
            position++;
        return decimal_at(position, unicode_major);
    }
    while (decimal_at(position, unicode_major)) {
        integer_digits++;
        position += scalar_width(position);
    }
    if (*position == '.') {
        position++;
        while (decimal_at(position, unicode_major)) {
            fraction_digits++;
            position += scalar_width(position);
        }
        if (fraction_digits == 0)
            return 0;
    } else if (integer_digits == 0) {
        return 0;
    }
    return *position == '\0' || (*position == '\n' && position[1] == '\0');
}

static option_token_t classify(const char *text, unsigned int unicode_major,
                               unsigned int grammar_profile) {
    option_token_t token;
    const char *equals;
    size_t prefix;
    size_t index;
    unsigned int matches = 0;
    memset(&token, 0, sizeof(token));
    if (!*text || text[0] != '-' || !text[1])
        return token;
    if (strcmp(text, "--") == 0) {
        token.kind = TOKEN_END;
        return token;
    }
    if (text[1] == 'h') {
        token.kind = TOKEN_OPTION;
        token.option = OPTION_HELP;
        token.short_help = 1u;
        token.explicit_value = text[2] ? text + 2 : (const char *)0;
        return token;
    }
    equals = strchr(text, '=');
    prefix = equals ? (size_t)(equals - text) : strlen(text);
    for (index = 0; index < sizeof(option_names) / sizeof(option_names[0]); index++) {
        size_t option_size = strlen(option_names[index]);
        if (prefix == option_size && strncmp(text, option_names[index], prefix) == 0) {
            token.kind = TOKEN_OPTION;
            token.option = (unsigned int)index;
            token.explicit_value = equals ? equals + 1 : (const char *)0;
            return token;
        }
    }
    if (text[1] == '-') {
        for (index = 0; index < sizeof(option_names) / sizeof(option_names[0]); index++) {
            size_t option_size = strlen(option_names[index]);
            if (prefix <= option_size && strncmp(text, option_names[index], prefix) == 0) {
                matches++;
                token.option = (unsigned int)index;
            }
        }
    }
    if (matches) {
        token.kind = matches == 1u ? TOKEN_OPTION : TOKEN_AMBIGUOUS;
        token.explicit_value = equals ? equals + 1 : (const char *)0;
        return token;
    }
    if (negative_value(text, unicode_major, grammar_profile) || strchr(text, ' '))
        return token;
    token.kind = TOKEN_UNKNOWN;
    return token;
}

static cupidbuild_disk_argument_t argument(const char *data, size_t size) {
    cupidbuild_disk_argument_t view;
    view.data = data;
    view.size = size;
    return view;
}

static int stage_argument(const char *text, cupidbuild_disk_argument_stage_t *out) {
    const char *separator = strchr(text, ':');
    if (!separator)
        return 0;
    if (separator == text + 1 &&
        ((text[0] >= 'A' && text[0] <= 'Z') || (text[0] >= 'a' && text[0] <= 'z'))) {
        const char *second = strchr(separator + 1, ':');
        if (second)
            separator = second;
    }
    if (separator[1] != '/')
        return 0;
    out->source = argument(text, (size_t)(separator - text));
    out->destination = argument(separator + 1, strlen(separator + 1));
    return 1;
}

static int path_separator(char value, unsigned int windows) {
    return value == '/' || (windows && value == '\\');
}

static int ascii_equal(char left, char right) {
    if (left >= 'A' && left <= 'Z')
        left = (char)(left + ('a' - 'A'));
    if (right >= 'A' && right <= 'Z')
        right = (char)(right + ('a' - 'A'));
    return left == right;
}

static cupidbuild_disk_argument_t basename(const char *path, unsigned int windows) {
    size_t size = strlen(path);
    size_t position = 0;
    cupidbuild_disk_argument_t result = argument(path + size, 0);
    if (windows && size) {
        if (size >= 2 && path_separator(path[0], 1u) && path_separator(path[1], 1u)) {
            size_t start = 2;
            if (size >= 8 && path[2] == '?' && path_separator(path[3], 1u) &&
                ascii_equal(path[4], 'U') && ascii_equal(path[5], 'N') &&
                ascii_equal(path[6], 'C') && path_separator(path[7], 1u))
                start = 8;
            position = start;
            while (position < size && !path_separator(path[position], 1u))
                position++;
            if (position == size)
                return result;
            position++;
            while (position < size && !path_separator(path[position], 1u))
                position++;
            if (position == size)
                return result;
            position++;
        } else if (path_separator(path[0], 1u)) {
            position = 1;
        } else {
            size_t first = scalar_width(path);
            if (first < size && path[first] == ':') {
                position = first + 1;
                if (position < size && path_separator(path[position], 1u))
                    position++;
            }
        }
    }
    while (position < size) {
        size_t start;
        while (position < size && path_separator(path[position], windows))
            position++;
        start = position;
        while (position < size && !path_separator(path[position], windows))
            position++;
        if (position != start && !(position == start + 1 && path[start] == '.'))
            result = argument(path + start, position - start);
    }
    return result;
}

static int contains(cupidbuild_disk_argument_t name, const char *needle) {
    size_t width = strlen(needle);
    size_t index;
    if (width > name.size)
        return 0;
    for (index = 0; index <= name.size - width; index++) {
        size_t offset = 0;
        while (offset < width && ascii_equal(name.data[index + offset], needle[offset]))
            offset++;
        if (offset == width)
            return 1;
    }
    return 0;
}

static void wad_destination(const char *path, unsigned int windows, uint32_t index, char *out) {
    cupidbuild_disk_argument_t name = basename(path, windows);
    const char *alias = (const char *)0;
    if (contains(name, "freedoom1"))
        alias = "/wads/freedo~1.wad";
    else if (contains(name, "freedoom2"))
        alias = "/wads/freedo~2.wad";
    else if (contains(name, "doom2"))
        alias = "/wads/doom2.wad";
    else if (contains(name, "doom1") || (name.size == 8 && contains(name, "doom.wad")))
        alias = "/wads/doom.wad";
    if (alias) {
        strcpy(out, alias);
    } else {
        char reverse[10];
        size_t digits = 0;
        size_t position = 9;
        strcpy(out, "/wads/wad");
        do {
            reverse[digits++] = (char)('0' + index % 10u);
            index /= 10u;
        } while (index);
        while (digits)
            out[position++] = reverse[--digits];
        strcpy(out + position, ".wad");
    }
}

void cupidbuild_disk_options_close(cupidbuild_disk_options_t *owner) {
    if (!owner)
        return;
    free(owner->wad_destinations);
    free(owner->stages);
    free(owner);
}

cupidbuild_disk_options_status_t
cupidbuild_disk_options_view(const cupidbuild_disk_options_t *owner,
                             cupidbuild_disk_options_view_t *view_out) {
    if (!view_out)
        return CUPIDBUILD_DISK_OPTIONS_ARGUMENT;
    memset(view_out, 0, sizeof(*view_out));
    if (!owner)
        return CUPIDBUILD_DISK_OPTIONS_ARGUMENT;
    *view_out = owner->view;
    return CUPIDBUILD_DISK_OPTIONS_OK;
}

cupidbuild_disk_options_status_t
cupidbuild_disk_options_stage(const cupidbuild_disk_options_t *owner, size_t index,
                              cupidbuild_disk_argument_stage_t *stage_out) {
    if (!stage_out)
        return CUPIDBUILD_DISK_OPTIONS_ARGUMENT;
    memset(stage_out, 0, sizeof(*stage_out));
    if (!owner || index >= owner->view.stage_count)
        return CUPIDBUILD_DISK_OPTIONS_ARGUMENT;
    *stage_out = owner->stages[index];
    return CUPIDBUILD_DISK_OPTIONS_OK;
}

cupidbuild_disk_options_status_t
cupidbuild_disk_options_open(size_t argc, const char *const *argv, unsigned int unicode_major,
                             uint32_t digit_limit, unsigned int grammar_profile,
                             unsigned int basename_profile, uint32_t stage_capacity,
                             cupidbuild_disk_options_t **owner_out) {
    cupidbuild_disk_options_t *owner = (cupidbuild_disk_options_t *)0;
    option_token_t *tokens = (option_token_t *)0;
    size_t index;
    size_t explicit_stages = 0;
    size_t wad_start = 0;
    size_t wad_count = 0;
    unsigned int after_end = 0;
    unsigned int seen = 0;
    unsigned int extras = 0;
    cupidbuild_disk_options_status_t status = CUPIDBUILD_DISK_OPTIONS_SYNTAX;
    if (!owner_out)
        return CUPIDBUILD_DISK_OPTIONS_ARGUMENT;
    *owner_out = (cupidbuild_disk_options_t *)0;
    if ((!argv && argc) || (unicode_major != 15u && unicode_major != 16u) || grammar_profile > 1u ||
        basename_profile > 1u || argc > SIZE_MAX / sizeof(*tokens) ||
        argc > SIZE_MAX / sizeof(*owner->stages))
        return CUPIDBUILD_DISK_OPTIONS_ARGUMENT;
    for (index = 0; index < argc; index++) {
        size_t units;
        if (!argv[index])
            return CUPIDBUILD_DISK_OPTIONS_ARGUMENT;
        if (!cupidbuild_path_to_utf16(argv[index], strlen(argv[index]), (unsigned short *)0, 0,
                                      &units))
            return CUPIDBUILD_DISK_OPTIONS_ENCODING;
    }
    tokens = (option_token_t *)calloc(argc ? argc : 1, sizeof(*tokens));
    owner = (cupidbuild_disk_options_t *)calloc(1, sizeof(*owner));
    if (!tokens || !owner) {
        status = CUPIDBUILD_DISK_OPTIONS_MEMORY;
        goto done;
    }
    for (index = 0; index < argc; index++) {
        if (!after_end)
            tokens[index] = classify(argv[index], unicode_major, grammar_profile);
        if (tokens[index].kind == TOKEN_END)
            after_end = 1u;
        if (grammar_profile == 0u && tokens[index].kind == TOKEN_AMBIGUOUS)
            goto done;
    }
    owner->stages =
        (cupidbuild_disk_argument_stage_t *)calloc(argc ? argc : 1, sizeof(*owner->stages));
    if (!owner->stages) {
        status = CUPIDBUILD_DISK_OPTIONS_MEMORY;
        goto done;
    }
    for (index = 0; index < argc; index++) {
        option_token_t token = tokens[index];
        const char *value = token.explicit_value;
        size_t value_index = index + 1;
        if (token.kind == TOKEN_AMBIGUOUS)
            goto done;
        if (token.kind != TOKEN_OPTION) {
            extras = 1u;
            continue;
        }
        if (token.option == OPTION_HELP) {
            if (token.short_help && value) {
                while (*value == 'h')
                    value++;
                if (*value == '=' || *value == '-')
                    goto done;
                if (*value != '\0' && token.explicit_value[0] == '=')
                    goto done;
            } else if (value) {
                goto done;
            }
            status = CUPIDBUILD_DISK_OPTIONS_HELP;
            goto done;
        }
        if (token.option == OPTION_FORCE) {
            if (value)
                goto done;
            owner->view.force_format = 1u;
            continue;
        }
        if (token.option == OPTION_WADS) {
            if (value) {
                wad_start = index;
                wad_count = 1;
            } else {
                wad_start = value_index;
                while (value_index < argc && tokens[value_index].kind == TOKEN_VALUE)
                    value_index++;
                wad_count = value_index - wad_start;
                index = value_index - 1;
            }
            continue;
        }
        if (!value) {
            if (value_index >= argc || tokens[value_index].kind != TOKEN_VALUE)
                goto done;
            value = argv[value_index];
            index = value_index;
        }
        if (token.option >= OPTION_SEED && token.option <= OPTION_KERNEL) {
            cupidbuild_disk_argument_t path = argument(value, strlen(value));
            if (token.option == OPTION_SEED)
                owner->view.seed_manifest = path;
            else if (token.option == OPTION_IMAGE)
                owner->view.image = path;
            else if (token.option == OPTION_BOOT)
                owner->view.bootloader = path;
            else
                owner->view.kernel = path;
            seen |= 1u << token.option;
        } else if (token.option == OPTION_HDD || token.option == OPTION_FAT) {
            cupidbuild_disk_integer_t parsed;
            if (cupidbuild_disk_integer_parse((const unsigned char *)value, strlen(value),
                                              unicode_major, digit_limit,
                                              &parsed) != CUPIDBUILD_DISK_INTEGER_OK)
                goto done;
            if (token.option == OPTION_HDD)
                owner->view.hdd_mb = parsed;
            else
                owner->view.fat_start_lba = parsed;
            seen |= 1u << token.option;
        } else if (token.option == OPTION_STAGE) {
            if (!stage_argument(value, &owner->stages[explicit_stages]))
                goto done;
            explicit_stages++;
        }
    }
    if (extras || seen != 126u)
        goto done;
    if (explicit_stages > stage_capacity || wad_count > stage_capacity - explicit_stages) {
        status = CUPIDBUILD_DISK_OPTIONS_CAPACITY;
        goto done;
    }
    if (wad_count > SIZE_MAX / 32u) {
        status = CUPIDBUILD_DISK_OPTIONS_ARGUMENT;
        goto done;
    }
    if (wad_count) {
        owner->wad_destinations = (char *)malloc(wad_count * 32u);
        if (!owner->wad_destinations) {
            status = CUPIDBUILD_DISK_OPTIONS_MEMORY;
            goto done;
        }
    }
    for (index = 0; index < wad_count; index++) {
        const char *path = tokens[wad_start].explicit_value && index == 0
                               ? tokens[wad_start].explicit_value
                               : argv[wad_start + index];
        char *destination = owner->wad_destinations + index * 32u;
        wad_destination(path, basename_profile, (uint32_t)index + 1u, destination);
        owner->stages[explicit_stages + index].source = argument(path, strlen(path));
        owner->stages[explicit_stages + index].destination =
            argument(destination, strlen(destination));
    }
    owner->view.stages = owner->stages;
    owner->view.stage_count = explicit_stages + wad_count;
    *owner_out = owner;
    owner = (cupidbuild_disk_options_t *)0;
    status = CUPIDBUILD_DISK_OPTIONS_OK;
done:
    free(tokens);
    cupidbuild_disk_options_close(owner);
    return status;
}
