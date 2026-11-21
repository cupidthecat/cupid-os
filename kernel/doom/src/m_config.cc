//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 1993-2008 Raven Software
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//    Configuration file interface.
//

/**
 * @file m_config.cc
 * @brief Load, save and bind Doom configuration variables.
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "doomfeatures.h"
#include "doomkeys.h"
#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"

#include "z_zone.h"

#define CONFIG_VALUE_BUFFER_SIZE 512
#define CONFIG_LINE_BUFFER_SIZE 640

//
// DEFAULTS
//

// Location where all configuration data is stored -
// default.cfg, savegames, etc.

char *configdir;
static char *owned_configdir;

// Default filenames for configuration files.

static char *default_main_config;
static char *default_extra_config;

typedef enum {
    DEFAULT_INT,
    DEFAULT_INT_HEX,
    DEFAULT_STRING,
    DEFAULT_FLOAT,
    DEFAULT_KEY,
} default_type_t;

typedef struct {
    // Name of the variable
    char *name;

    // Pointer to the location in memory of the variable
    void *location;

    // Type of the variable
    default_type_t type;

    // If this is a key value, the original integer scancode we read from
    // the config file before translating it to the internal key value.
    // If zero, we didn't read this value from a config file.
    int untranslated;

    // The value we translated the scancode into when we read the
    // config file on startup.  If the variable value is different from
    // this, it has been changed and needs to be converted; otherwise,
    // use the 'untranslated' value.
    int original_translated;

    // If true, this config variable has been bound to a variable
    // and is being used.
    boolean bound;

    // Heap value loaded from the current config, if this entry owns one.
    char *owned_string;

    boolean initial_captured;
    int initial_int;
    float initial_float;
    char *initial_string;
} default_t;

typedef struct {
    default_t *defaults;
    int numdefaults;
    char *filename;
    char *owned_filename;
} default_collection_t;

static void ResetCollectionDefaults(default_collection_t *collection);

#define CONFIG_VARIABLE_GENERIC(name, type)                                                        \
    {#name, NULL, type, 0, 0, false, NULL, false, 0, 0.0f, NULL}

#define CONFIG_VARIABLE_KEY(name) CONFIG_VARIABLE_GENERIC(name, DEFAULT_KEY)
#define CONFIG_VARIABLE_INT(name) CONFIG_VARIABLE_GENERIC(name, DEFAULT_INT)
#define CONFIG_VARIABLE_INT_HEX(name) CONFIG_VARIABLE_GENERIC(name, DEFAULT_INT_HEX)
#define CONFIG_VARIABLE_FLOAT(name) CONFIG_VARIABLE_GENERIC(name, DEFAULT_FLOAT)
#define CONFIG_VARIABLE_STRING(name) CONFIG_VARIABLE_GENERIC(name, DEFAULT_STRING)

#include "m_config_defaults.h"

// Search a collection for a variable

static default_t *SearchCollection(default_collection_t *collection, char *name) {
    int i;

    for (i = 0; i < collection->numdefaults; ++i) {
        if (!strcmp(name, collection->defaults[i].name)) {
            return &collection->defaults[i];
        }
    }

    return NULL;
}

// Mapping from DOS keyboard scan code to internal key code (as defined
// in doomkey.h). I think I (fraggle) reused this from somewhere else
// but I can't find where. Anyway, notes:
//  * KEY_PAUSE is wrong - it's in the KEY_NUMLOCK spot. This shouldn't
//    matter in terms of Vanilla compatibility because neither of
//    those were valid for key bindings.
//  * There is no proper scan code for PrintScreen (on DOS machines it
//    sends an interrupt). So I added a fake scan code of 126 for it.
//    The presence of this is important so we can bind PrintScreen as
//    a screenshot key.
static const int scantokey[128] = {0,
                                   27,
                                   '1',
                                   '2',
                                   '3',
                                   '4',
                                   '5',
                                   '6',
                                   '7',
                                   '8',
                                   '9',
                                   '0',
                                   '-',
                                   '=',
                                   KEY_BACKSPACE,
                                   9,
                                   'q',
                                   'w',
                                   'e',
                                   'r',
                                   't',
                                   'y',
                                   'u',
                                   'i',
                                   'o',
                                   'p',
                                   '[',
                                   ']',
                                   13,
                                   KEY_RCTRL,
                                   'a',
                                   's',
                                   'd',
                                   'f',
                                   'g',
                                   'h',
                                   'j',
                                   'k',
                                   'l',
                                   ';',
                                   '\'',
                                   '`',
                                   KEY_RSHIFT,
                                   '\\',
                                   'z',
                                   'x',
                                   'c',
                                   'v',
                                   'b',
                                   'n',
                                   'm',
                                   ',',
                                   '.',
                                   '/',
                                   KEY_RSHIFT,
                                   KEYP_MULTIPLY,
                                   KEY_RALT,
                                   ' ',
                                   KEY_CAPSLOCK,
                                   KEY_F1,
                                   KEY_F2,
                                   KEY_F3,
                                   KEY_F4,
                                   KEY_F5,
                                   KEY_F6,
                                   KEY_F7,
                                   KEY_F8,
                                   KEY_F9,
                                   KEY_F10,
                                   /* KEY_NUMLOCK? */ KEY_PAUSE,
                                   KEY_SCRLCK,
                                   KEY_HOME,
                                   KEY_UPARROW,
                                   KEY_PGUP,
                                   KEY_MINUS,
                                   KEY_LEFTARROW,
                                   KEYP_5,
                                   KEY_RIGHTARROW,
                                   KEYP_PLUS,
                                   KEY_END,
                                   KEY_DOWNARROW,
                                   KEY_PGDN,
                                   KEY_INS,
                                   KEY_DEL,
                                   0,
                                   0,
                                   0,
                                   KEY_F11,
                                   KEY_F12,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   KEY_PRTSCR,
                                   0};

static void SaveDefaultCollection(default_collection_t *collection) {
#if defined(ORIGCODE) || defined(DOOM_PORT_CUPIDOS)
    default_t *defaults;
    int i, v;
    FILE *f;
    char *temporary_filename;
    boolean failed;

    temporary_filename = M_StringJoin(collection->filename, ".tmp.cfg", NULL);

    f = fopen(temporary_filename, "w");
    if (!f) {
        fprintf(stderr, "Could not open temporary config %s\n", temporary_filename);
        free(temporary_filename);
        return;
    }

    defaults = collection->defaults;
    failed = false;

    for (i = 0; i < collection->numdefaults; i++) {
        int chars_written;

        // Ignore unbound variables

        if (!defaults[i].bound) {
            continue;
        }

        // Print the name and line up all values at 30 characters

        chars_written = fprintf(f, "%s ", defaults[i].name);
        if (chars_written < 0) {
            failed = true;
            break;
        }

        for (; chars_written < 30; ++chars_written) {
            if (fprintf(f, " ") < 0) {
                failed = true;
                break;
            }
        }
        if (failed) {
            break;
        }

        // Print the value

        int value_status = -1;

        switch (defaults[i].type) {
            case DEFAULT_KEY:

                // use the untranslated version if we can, to reduce
                // the possibility of screwing up the user's config
                // file

                v = *(int *)defaults[i].location;

                if (v == KEY_RSHIFT) {
                    // Special case: for shift, force scan code for
                    // right shift, as this is what Vanilla uses.
                    // This overrides the change check below, to fix
                    // configuration files made by old versions that
                    // mistakenly used the scan code for left shift.

                    v = 54;
                } else if (defaults[i].untranslated && v == defaults[i].original_translated) {
                    // Has not been changed since the last time we
                    // read the config file.

                    v = defaults[i].untranslated;
                } else {
                    // search for a reverse mapping back to a scancode
                    // in the scantokey table

                    int s;

                    for (s = 0; s < 128; ++s) {
                        if (scantokey[s] == v) {
                            v = s;
                            break;
                        }
                    }
                }

                value_status = fprintf(f, "%i", v);
                break;

            case DEFAULT_INT:
                value_status = fprintf(f, "%i", *(int *)defaults[i].location);
                break;

            case DEFAULT_INT_HEX:
                value_status = fprintf(f, "0x%x", *(int *)defaults[i].location);
                break;

            case DEFAULT_FLOAT:
                value_status = fprintf(f, "%f", *(float *)defaults[i].location);
                break;

            case DEFAULT_STRING:
                value_status = fprintf(f, "\"%s\"", *(char **)(defaults[i].location));
                break;
        }

        if (value_status < 0 || fprintf(f, "\n") < 0) {
            failed = true;
            break;
        }
    }

    if (ferror(f)) {
        failed = true;
    }
    if (fclose(f) != 0) {
        failed = true;
    }

    if (!failed && rename(temporary_filename, collection->filename) == 0) {
        free(temporary_filename);
        return;
    }

    fprintf(stderr, "Could not commit config %s\n", collection->filename);
    remove(temporary_filename);
    free(temporary_filename);
#endif
}

// Parses integer values in the configuration file

static boolean ParseIntParameter(char *strparm, int *result) {
    char *end;
    char *digits;
    long value;

    if (strparm == NULL || result == NULL) {
        return false;
    }

    digits = strparm;
    while (isspace((unsigned char)*digits)) {
        ++digits;
    }
    if (*digits == '+' || *digits == '-') {
        ++digits;
    }
    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        digits += 2;
        if (!isxdigit((unsigned char)*digits)) {
            return false;
        }
    } else if (!isdigit((unsigned char)*digits)) {
        return false;
    }

    errno = 0;
    value = strtol(strparm, &end, 0);
    while (isspace((unsigned char)*end)) {
        ++end;
    }
    if (*end != '\0' || errno == ERANGE || value < INT_MIN || value > INT_MAX) {
        return false;
    }

    *result = (int)value;
    return true;
}

static void SetVariable(default_t *def, char *value) {
    int intparm;

    // parameter found

    switch (def->type) {
        case DEFAULT_STRING: {
            char *replacement = strdup(value);
            if (replacement != NULL) {
                if (def->owned_string != NULL) {
                    free(def->owned_string);
                }
                *(char **)def->location = replacement;
                def->owned_string = replacement;
            }
            break;
        }

        case DEFAULT_INT:
        case DEFAULT_INT_HEX:
            if (ParseIntParameter(value, &intparm)) {
                *(int *)def->location = intparm;
            }
            break;

        case DEFAULT_KEY:

            // translate scancodes read from config
            // file (save the old value in untranslated)

            if (!ParseIntParameter(value, &intparm)) {
                break;
            }
            def->untranslated = intparm;
            if (intparm >= 0 && intparm < 128) {
                intparm = scantokey[intparm];
            } else {
                intparm = 0;
            }

            def->original_translated = intparm;
            *(int *)def->location = intparm;
            break;

        case DEFAULT_FLOAT:
            *(float *)def->location = (float)atof(value);
            break;
    }
}

static boolean ParseConfigLine(char *line, char *name, size_t name_size, char *value,
                               size_t value_size) {
    char *cursor;
    size_t length;

    if (line == NULL || name == NULL || value == NULL || name_size < 2 || value_size < 2) {
        return false;
    }

    cursor = line;
    while (*cursor != '\0' && *cursor != '\n' && isspace((unsigned char)*cursor)) {
        ++cursor;
    }

    length = 0;
    while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
        if (length + 1 >= name_size) {
            return false;
        }
        name[length++] = *cursor++;
    }
    if (length == 0) {
        return false;
    }
    name[length] = '\0';

    while (*cursor != '\0' && *cursor != '\n' && isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    if (*cursor == '\0' || *cursor == '\n') {
        return false;
    }

    length = 0;
    while (*cursor != '\0' && *cursor != '\n') {
        if (length + 1 >= value_size) {
            return false;
        }
        value[length++] = *cursor++;
    }
    value[length] = '\0';
    return length > 0;
}

static void LoadDefaultCollection(default_collection_t *collection) {
#if defined(ORIGCODE) || defined(DOOM_PORT_CUPIDOS)
    FILE *f;
    default_t *def;
    char defname[80];
    char strparm[CONFIG_VALUE_BUFFER_SIZE];
    char line[CONFIG_LINE_BUFFER_SIZE];

    // read the file in, overriding any set defaults
    f = fopen(collection->filename, "r");

    if (f == NULL) {
        // File not opened, but don't complain.
        // It's probably just the first time they ran the game.

        return;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        if (strchr(line, '\n') == NULL && !feof(f)) {
            int c;
            do {
                c = fgetc(f);
            } while (c >= 0 && c != '\n');

            continue;
        }

        if (!ParseConfigLine(line, defname, sizeof(defname), strparm, sizeof(strparm))) {
            continue;
        }

        // Find the setting in the list

        def = SearchCollection(collection, defname);

        if (def == NULL || !def->bound) {
            // Unknown variable?  Unbound variables are also treated
            // as unknown.

            continue;
        }

        // Strip off trailing non-printable characters (\r characters
        // from DOS text files)

        while (strlen(strparm) > 0 && !isprint(strparm[strlen(strparm) - 1])) {
            strparm[strlen(strparm) - 1] = '\0';
        }

        // Surrounded by quotes? If so, remove them.
        if (strlen(strparm) >= 2 && strparm[0] == '"' && strparm[strlen(strparm) - 1] == '"') {
            strparm[strlen(strparm) - 1] = '\0';
            memmove(strparm, strparm + 1, sizeof(strparm) - 1);
        }

        SetVariable(def, strparm);
    }

    if (ferror(f)) {
        fprintf(stderr, "Could not finish reading config %s\n", collection->filename);
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "Could not close config %s\n", collection->filename);
    }
#endif
}

#if defined(DOOM_PORT_CUPIDOS)
int M_ConfigFilesystemTest(void) {
    const char *filename = ".dglibc-config-roundtrip.cfg";
    const char *temporary_filename = ".dglibc-config-roundtrip.cfg.tmp.cfg";
    default_t defaults[6];
    default_collection_t collection;
    int integer_value = 123;
    int hex_value = 0x2a;
    float float_value = 3.25f;
    char *string_value = (char *)"Cupid config value";
    int key_value = KEY_RSHIFT;
    int unbound_value = 77;
    char boundary_value[510];
    char oversized_value[520];
    char oversized_line[280];
    FILE *f;
    int i;
    int result = 1;

    memset(defaults, 0, sizeof(defaults));
    defaults[0].name = (char *)"test_int";
    defaults[0].type = DEFAULT_INT;
    defaults[0].location = &integer_value;
    defaults[0].bound = true;
    defaults[0].initial_captured = true;
    defaults[0].initial_int = integer_value;
    defaults[1].name = (char *)"test_hex";
    defaults[1].type = DEFAULT_INT_HEX;
    defaults[1].location = &hex_value;
    defaults[1].bound = true;
    defaults[1].initial_captured = true;
    defaults[1].initial_int = hex_value;
    defaults[2].name = (char *)"test_float";
    defaults[2].type = DEFAULT_FLOAT;
    defaults[2].location = &float_value;
    defaults[2].bound = true;
    defaults[2].initial_captured = true;
    defaults[2].initial_float = float_value;
    defaults[3].name = (char *)"test_string";
    defaults[3].type = DEFAULT_STRING;
    defaults[3].location = &string_value;
    defaults[3].bound = true;
    defaults[3].initial_captured = true;
    defaults[3].initial_string = string_value;
    defaults[4].name = (char *)"test_key";
    defaults[4].type = DEFAULT_KEY;
    defaults[4].location = &key_value;
    defaults[4].bound = true;
    defaults[4].initial_captured = true;
    defaults[4].initial_int = key_value;
    defaults[5].name = (char *)"test_unbound";
    defaults[5].type = DEFAULT_INT;
    defaults[5].location = &unbound_value;

    collection.defaults = defaults;
    collection.numdefaults = 6;
    collection.filename = (char *)filename;
    collection.owned_filename = NULL;
    remove(filename);
    remove(temporary_filename);

    SaveDefaultCollection(&collection);
    integer_value = 0;
    hex_value = 0;
    float_value = 0.0f;
    string_value = (char *)"changed";
    key_value = 0;
    LoadDefaultCollection(&collection);
    if (integer_value != 123 || hex_value != 0x2a || float_value != 3.25f ||
        strcmp(string_value, "Cupid config value") || key_value != KEY_RSHIFT) {
        goto done;
    }

    for (i = 0; i + 1 < (int)sizeof(boundary_value); ++i) {
        boundary_value[i] = 'b';
    }
    boundary_value[sizeof(boundary_value) - 1] = '\0';
    string_value = boundary_value;
    SaveDefaultCollection(&collection);
    string_value = (char *)"changed at the boundary";
    LoadDefaultCollection(&collection);
    if (strcmp(string_value, boundary_value)) {
        goto done;
    }

    for (i = 0; i + 1 < (int)sizeof(oversized_value); ++i) {
        oversized_value[i] = 'x';
    }
    oversized_value[sizeof(oversized_value) - 1] = '\0';
    integer_value = 999;
    string_value = oversized_value;
    SaveDefaultCollection(&collection);
    integer_value = 0;
    string_value = (char *)"changed again";
    LoadDefaultCollection(&collection);
    if (integer_value != 123 || strcmp(string_value, boundary_value)) {
        goto done;
    }

    f = fopen(filename, "w");
    if (f == NULL || fputs("test_int 7\n", f) < 0 || fclose(f) != 0) {
        goto done;
    }
    ResetCollectionDefaults(&collection);
    LoadDefaultCollection(&collection);
    if (integer_value != 7 || hex_value != 0x2a) {
        goto done;
    }

    for (i = 0; i + 2 < (int)sizeof(oversized_line); ++i) {
        oversized_line[i] = 'z';
    }
    oversized_line[sizeof(oversized_line) - 2] = '\n';
    oversized_line[sizeof(oversized_line) - 1] = '\0';
    f = fopen(filename, "w");
    if (f == NULL || fputs("test_int 2147483648\n", f) < 0 || fputs("test_hex 0x2b\r\n", f) < 0 ||
        fputs("test_unbound 12\n", f) < 0 || fputs("unknown_setting 1\n", f) < 0 ||
        fputs(oversized_line, f) < 0 || fputs("test_float 1.500000\n", f) < 0 ||
        fputs("test_string \"Cupid CRLF\"\r\n", f) < 0 || fputs("test_key 54\n", f) < 0 ||
        fclose(f) != 0) {
        goto done;
    }
    ResetCollectionDefaults(&collection);
    LoadDefaultCollection(&collection);
    if (integer_value != 123 || hex_value != 0x2b || float_value != 1.5f ||
        strcmp(string_value, "Cupid CRLF") || key_value != KEY_RSHIFT || unbound_value != 77) {
        goto done;
    }

    f = fopen(filename, "w");
    if (f == NULL || fputs("test_int -2147483648\n", f) < 0 || fclose(f) != 0) {
        goto done;
    }
    ResetCollectionDefaults(&collection);
    LoadDefaultCollection(&collection);
    if (integer_value != (-2147483647 - 1)) {
        goto done;
    }

    f = fopen(filename, "w");
    if (f == NULL || fputs("test_int 2147483647\n", f) < 0 || fclose(f) != 0) {
        goto done;
    }
    ResetCollectionDefaults(&collection);
    LoadDefaultCollection(&collection);
    if (integer_value != 2147483647) {
        goto done;
    }

    remove(filename);
    ResetCollectionDefaults(&collection);
    integer_value = 88;
    LoadDefaultCollection(&collection);
    if (integer_value != 88) {
        goto done;
    }
    result = 0;

done:
    ResetCollectionDefaults(&collection);
    remove(filename);
    remove(temporary_filename);
    return result;
}
#endif

// Set the default filenames to use for configuration files.

void M_SetConfigFilenames(char *main_config, char *extra_config) {
    default_main_config = main_config;
    default_extra_config = extra_config;
}

//
// M_SaveDefaults
//

void M_SaveDefaults(void) {
    SaveDefaultCollection(&doom_defaults);
    SaveDefaultCollection(&extra_defaults);
}

//
// Save defaults to alternate filenames
//

void M_SaveDefaultsAlternate(char *main, char *extra) {
    char *orig_main;
    char *orig_extra;

    // Temporarily change the filenames

    orig_main = doom_defaults.filename;
    orig_extra = extra_defaults.filename;

    doom_defaults.filename = main;
    extra_defaults.filename = extra;

    M_SaveDefaults();

    // Restore normal filenames

    doom_defaults.filename = orig_main;
    extra_defaults.filename = orig_extra;
}

static void SetCollectionFilename(default_collection_t *collection, char *filename, boolean owned) {
    if (collection->owned_filename != NULL) {
        free(collection->owned_filename);
    }
    collection->filename = filename;
    collection->owned_filename = owned ? filename : NULL;
}

static void ResetCollectionDefaults(default_collection_t *collection) {
    int i;

    for (i = 0; i < collection->numdefaults; ++i) {
        default_t *def = &collection->defaults[i];
        if (!def->bound || !def->initial_captured) {
            continue;
        }

        if (def->owned_string != NULL) {
            free(def->owned_string);
            def->owned_string = NULL;
        }
        switch (def->type) {
            case DEFAULT_STRING:
                *(char **)def->location = def->initial_string;
                break;
            case DEFAULT_FLOAT:
                *(float *)def->location = def->initial_float;
                break;
            default:
                *(int *)def->location = def->initial_int;
                break;
        }
        def->untranslated = 0;
        def->original_translated = 0;
    }
}

//
// M_LoadDefaults
//

void M_LoadDefaults(void) {
    int i;

    ResetCollectionDefaults(&doom_defaults);
    ResetCollectionDefaults(&extra_defaults);

    // check for a custom default file

    //!
    // @arg <file>
    // @vanilla
    //
    // Load main configuration from the specified file, instead of the
    // default.
    //

    i = M_CheckParmWithArgs("-config", 1);

    if (i) {
        SetCollectionFilename(&doom_defaults, myargv[i + 1], false);
        printf("	default file: %s\n", doom_defaults.filename);
    } else {
        SetCollectionFilename(&doom_defaults, M_StringJoin(configdir, default_main_config, NULL),
                              true);
    }

    printf("saving config in %s\n", doom_defaults.filename);

    //!
    // @arg <file>
    //
    // Load additional configuration from the specified file, instead of
    // the default.
    //

    i = M_CheckParmWithArgs("-extraconfig", 1);

    if (i) {
        SetCollectionFilename(&extra_defaults, myargv[i + 1], false);
        printf("        extra configuration file: %s\n", extra_defaults.filename);
    } else {
        SetCollectionFilename(&extra_defaults, M_StringJoin(configdir, default_extra_config, NULL),
                              true);
    }

    LoadDefaultCollection(&doom_defaults);
    LoadDefaultCollection(&extra_defaults);
}

// Get a configuration file variable by its name

static default_t *GetDefaultForName(char *name) {
    default_t *result;

    // Try the main list and the extras

    result = SearchCollection(&doom_defaults, name);

    if (result == NULL) {
        result = SearchCollection(&extra_defaults, name);
    }

    // Not found? Internal error.

    if (result == NULL) {
        I_Error("Unknown configuration variable: '%s'", name);
    }

    return result;
}

//
// Bind a variable to a given configuration file variable, by name.
//

void M_BindVariable(char *name, void *location) {
    default_t *variable;

    variable = GetDefaultForName(name);

    variable->location = location;
    variable->bound = true;
    if (!variable->initial_captured) {
        switch (variable->type) {
            case DEFAULT_STRING:
                variable->initial_string = *(char **)location;
                break;
            case DEFAULT_FLOAT:
                variable->initial_float = *(float *)location;
                break;
            default:
                variable->initial_int = *(int *)location;
                break;
        }
        variable->initial_captured = true;
    }
}

// Set the value of a particular variable; an API function for other
// parts of the program to assign values to config variables by name.

boolean M_SetVariable(char *name, char *value) {
    default_t *variable;

    variable = GetDefaultForName(name);

    if (variable == NULL || !variable->bound) {
        return false;
    }

    SetVariable(variable, value);

    return true;
}

// Get the value of a variable.

int M_GetIntVariable(char *name) {
    default_t *variable;

    variable = GetDefaultForName(name);

    if (variable == NULL || !variable->bound ||
        (variable->type != DEFAULT_INT && variable->type != DEFAULT_INT_HEX)) {
        return 0;
    }

    return *((int *)variable->location);
}

const char *M_GetStrVariable(char *name) {
    default_t *variable;

    variable = GetDefaultForName(name);

    if (variable == NULL || !variable->bound || variable->type != DEFAULT_STRING) {
        return NULL;
    }

    return *((const char **)variable->location);
}

float M_GetFloatVariable(char *name) {
    default_t *variable;

    variable = GetDefaultForName(name);

    if (variable == NULL || !variable->bound || variable->type != DEFAULT_FLOAT) {
        return 0;
    }

    return *((float *)variable->location);
}

// Get the path to the default configuration dir to use, if NULL
// is passed to M_SetConfigDir.

static char *GetDefaultConfigDir(void) {
    char *result = (char *)malloc(2);
    if (result == NULL) {
        I_Error("Could not allocate the config directory path");
    }
    result[0] = '.';
    result[1] = '\0';

    return result;
}

//
// SetConfigDir:
//
// Sets the location of the configuration directory, where configuration
// files are stored - default.cfg, chocolate-doom.cfg, savegames, etc.
//

void M_SetConfigDir(char *dir) {
    char *next_configdir;

    // Use the directory that was passed, or find the default.

    if (dir != NULL) {
        next_configdir = dir;
    } else {
        next_configdir = GetDefaultConfigDir();
    }

    if (owned_configdir != NULL) {
        free(owned_configdir);
    }
    owned_configdir = dir == NULL ? next_configdir : NULL;
    configdir = next_configdir;

    if (strcmp(configdir, "") != 0) {
        printf("Using %s for configuration and saves\n", configdir);
    }

    // Make the directory if it doesn't already exist:

    M_MakeDirectory(configdir);
}

//
// Calculate the path to the directory to use to store save games.
// Creates the directory as necessary.
//

char *M_GetSaveGameDir(char *iwadname) {
    char *savegamedir;
#if ORIGCODE
    char *topdir;
#endif

    // If not "doing" a configuration directory (Windows), don't "do"
    // a savegame directory, either.

    if (!strcmp(configdir, "")) {
        savegamedir = strdup("");
    } else {
#if ORIGCODE
        // ~/.chocolate-doom/savegames

        topdir = M_StringJoin(configdir, "savegame", NULL);
        M_MakeDirectory(topdir);

        // eg. ~/.chocolate-doom/savegames/doom2.wad/

        savegamedir = M_StringJoin(topdir, DIR_SEPARATOR_S, iwadname, DIR_SEPARATOR_S, NULL);

        M_MakeDirectory(savegamedir);

        free(topdir);
#else
        savegamedir = M_StringJoin(configdir, DIR_SEPARATOR_S, ".savegame/", NULL);

        M_MakeDirectory(savegamedir);

        printf("Using %s for savegames\n", savegamedir);
#endif
    }

    return savegamedir;
}
