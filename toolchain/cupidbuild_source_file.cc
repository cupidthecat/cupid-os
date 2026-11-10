#include "cupidbuild_source_file.h"
#include "cupidbuild_source_path.h"
#include <stdlib.h>
#include <string.h>

#define CUPIDBUILD_SOURCE_FILE_PATH_BYTES 8192u

struct cupidbuild_source_file {
    cupidbuild_host_observer_t *observer;
    const char *physical_root;
    char *requested;
    char *physical;
    char *logical;
    int optional;
};

static int source_file_separator(char value, unsigned int format) {
    return value == '/' || (format == 2u && value == '\\');
}

static size_t source_file_root_length(const char *path, unsigned int format) {
    size_t length = strlen(path), start = 2u, cursor;
    if (format == 1u)
        return path[1] == '/' ? 2u : 1u;
    if (length >= 3u && path[1] == ':' && source_file_separator(path[2], format))
        return 3u;
    if (length < 2u || !source_file_separator(path[0], format) ||
        !source_file_separator(path[1], format))
        return 0u;
    if (length >= 7u && path[2] == '?' && source_file_separator(path[3], format) &&
        path[5] == ':' && source_file_separator(path[6], format))
        return 7u;
    if (length >= 8u && path[2] == '?' && source_file_separator(path[3], format) &&
        (path[4] == 'U' || path[4] == 'u') && (path[5] == 'N' || path[5] == 'n') &&
        (path[6] == 'C' || path[6] == 'c') && source_file_separator(path[7], format))
        start = 8u;
    cursor = start;
    while (path[cursor] && !source_file_separator(path[cursor], format))
        cursor++;
    if (cursor == start || !path[cursor])
        return 0u;
    start = ++cursor;
    while (path[cursor] && !source_file_separator(path[cursor], format))
        cursor++;
    if (cursor == start)
        return 0u;
    return path[cursor] ? cursor + 1u : cursor;
}

static void source_file_fill_view(cupidbuild_source_file_t *owner,
                                  cupidbuild_source_file_view_t *view) {
    view->observer = owner->observer;
    view->physical_root = owner->physical_root;
    view->logical = owner->logical;
    view->requested_absolute = owner->requested;
    view->physical_absolute = owner->physical;
}

static int source_file_open(const char *working_root, const char *request, size_t entry_quota,
                            int optional, int selection, cupidbuild_source_file_t **owner_out,
                            cupidbuild_host_entry_kind_t *kind_out,
                            cupidbuild_source_file_view_t *view_out) {
    char absolute[CUPIDBUILD_SOURCE_FILE_PATH_BYTES];
    char parent[CUPIDBUILD_SOURCE_FILE_PATH_BYTES];
    cupidbuild_source_file_t *owner;
    cupidbuild_host_entry_kind_t kind = CUPIDBUILD_ENTRY_NONE;
    unsigned int format = cupidbuild_host_execution_format();
    size_t length, split, parent_length, root_length, leaf_length, joined_length, minimum, index;
    int separator, valid;
    if (view_out)
        (void)memset(view_out, 0, sizeof(*view_out));
    if (owner_out)
        *owner_out = (cupidbuild_source_file_t *)0;
    if (kind_out)
        *kind_out = CUPIDBUILD_ENTRY_NONE;
    if (!owner_out || (optional && !kind_out) || entry_quota == 0u || entry_quota == (size_t)-1 ||
        !cupidbuild_source_path_absolute(working_root, request, absolute, sizeof(absolute)))
        return 0;
    length = strlen(absolute);
    split = length;
    while (split && !source_file_separator(absolute[split - 1u], format))
        split--;
    if (!split || split == length)
        return 0;
    parent_length = split - 1u;
    if (!parent_length)
        parent_length = 1u;
    if (format == 2u && parent_length == 2u && absolute[1] == ':')
        parent_length = 3u;
    (void)memcpy(parent, absolute, parent_length);
    parent[parent_length] = 0;
    owner = (cupidbuild_source_file_t *)calloc(1u, sizeof(*owner));
    if (!owner)
        return 0;
    owner->optional = optional;
    owner->requested = (char *)malloc(length + 1u);
    if (!owner->requested) {
        (void)cupidbuild_source_file_close(owner);
        return 0;
    }
    (void)memcpy(owner->requested, absolute, length + 1u);
    minimum = source_file_root_length(absolute, format);
    for (;;) {
        valid = selection ? cupidbuild_host_observer_begin_source_selection_quota(
                                parent, entry_quota, &owner->observer, &owner->physical_root)
                          : cupidbuild_host_observer_resolve_source_root_quota(
                                parent, entry_quota, &owner->observer, &owner->physical_root);
        if (valid || !optional || !minimum || parent_length <= minimum)
            break;
        index = parent_length;
        while (index && !source_file_separator(absolute[index - 1u], format))
            index--;
        if (!index)
            break;
        parent_length = index - 1u;
        if (parent_length < minimum)
            parent_length = minimum;
        split = parent_length;
        while (source_file_separator(absolute[split], format))
            split++;
        parent[parent_length] = 0;
    }
    if (valid) {
        leaf_length = length - split;
        owner->logical = (char *)malloc(leaf_length + 1u);
        valid = owner->logical != (char *)0;
        if (valid) {
            (void)memcpy(owner->logical, absolute + split, leaf_length + 1u);
            if (format == 2u) {
                for (index = 0u; index < leaf_length; index++) {
                    if (owner->logical[index] == '\\')
                        owner->logical[index] = '/';
                }
            }
            valid = cupidbuild_host_observer_path_kind(owner->observer, owner->logical, &kind) &&
                    (kind == CUPIDBUILD_ENTRY_FILE || (optional && kind == CUPIDBUILD_ENTRY_NONE));
        }
    }
    if (valid) {
        root_length = strlen(owner->physical_root);
        leaf_length = strlen(owner->logical);
        separator =
            root_length && !source_file_separator(owner->physical_root[root_length - 1u], format);
        joined_length = root_length + (separator ? 1u : 0u) + leaf_length;
        valid = joined_length < CUPIDBUILD_SOURCE_FILE_PATH_BYTES;
        if (valid) {
            owner->physical = (char *)malloc(joined_length + 1u);
            valid = owner->physical != (char *)0;
            if (valid) {
                (void)memcpy(owner->physical, owner->physical_root, root_length);
                if (separator)
                    owner->physical[root_length++] = '/';
                (void)memcpy(owner->physical + root_length, owner->logical, leaf_length + 1u);
                valid = optional
                            ? cupidbuild_host_observer_require_source_unchanged(owner->observer)
                            : cupidbuild_host_observer_require_unchanged(owner->observer);
            }
        }
    }
    if (!valid) {
        (void)cupidbuild_source_file_close(owner);
        return 0;
    }
    if (view_out)
        source_file_fill_view(owner, view_out);
    *owner_out = owner;
    if (kind_out)
        *kind_out = kind;
    return 1;
}

int cupidbuild_source_file_open_quota(const char *working_root, const char *request,
                                      size_t entry_quota, cupidbuild_source_file_t **owner_out) {
    return source_file_open(working_root, request, entry_quota, 0, 0, owner_out,
                            (cupidbuild_host_entry_kind_t *)0, (cupidbuild_source_file_view_t *)0);
}

int cupidbuild_source_file_open_optional_quota(const char *working_root, const char *request,
                                               size_t entry_quota,
                                               cupidbuild_source_file_t **owner_out,
                                               cupidbuild_host_entry_kind_t *kind_out) {
    return source_file_open(working_root, request, entry_quota, 1, 0, owner_out, kind_out,
                            (cupidbuild_source_file_view_t *)0);
}

int cupidbuild_source_file_open_selection_quota(const char *working_root, const char *request,
                                                size_t entry_quota,
                                                cupidbuild_source_file_t **owner_out) {
    return source_file_open(working_root, request, entry_quota, 0, 1, owner_out,
                            (cupidbuild_host_entry_kind_t *)0, (cupidbuild_source_file_view_t *)0);
}

int cupidbuild_source_file_open_optional_selection_quota(const char *working_root,
                                                         const char *request, size_t entry_quota,
                                                         cupidbuild_source_file_t **owner_out,
                                                         cupidbuild_host_entry_kind_t *kind_out) {
    return source_file_open(working_root, request, entry_quota, 1, 1, owner_out, kind_out,
                            (cupidbuild_source_file_view_t *)0);
}

int cupidbuild_source_file_open_selection_view_quota(const char *working_root, const char *request,
                                                     size_t entry_quota,
                                                     cupidbuild_source_file_t **owner_out,
                                                     cupidbuild_source_file_view_t *view_out) {
    if (!view_out) {
        if (owner_out)
            *owner_out = (cupidbuild_source_file_t *)0;
        return 0;
    }
    return source_file_open(working_root, request, entry_quota, 0, 1, owner_out,
                            (cupidbuild_host_entry_kind_t *)0, view_out);
}

int cupidbuild_source_file_open_optional_selection_view_quota(
    const char *working_root, const char *request, size_t entry_quota,
    cupidbuild_source_file_t **owner_out, cupidbuild_host_entry_kind_t *kind_out,
    cupidbuild_source_file_view_t *view_out) {
    if (!view_out) {
        if (owner_out)
            *owner_out = (cupidbuild_source_file_t *)0;
        if (kind_out)
            *kind_out = CUPIDBUILD_ENTRY_NONE;
        return 0;
    }
    return source_file_open(working_root, request, entry_quota, 1, 1, owner_out, kind_out,
                            view_out);
}

int cupidbuild_source_file_view(cupidbuild_source_file_t *owner,
                                cupidbuild_source_file_view_t *view_out) {
    if (view_out)
        (void)memset(view_out, 0, sizeof(*view_out));
    if (!owner || !view_out || !owner->observer)
        return 0;
    if (!(owner->optional ? cupidbuild_host_observer_require_source_unchanged(owner->observer)
                          : cupidbuild_host_observer_require_unchanged(owner->observer)))
        return 0;
    source_file_fill_view(owner, view_out);
    return 1;
}

int cupidbuild_source_file_close(cupidbuild_source_file_t *owner) {
    int valid;
    if (!owner)
        return 1;
    valid = cupidbuild_host_observer_close(owner->observer);
    free(owner->requested);
    free(owner->physical);
    free(owner->logical);
    free(owner);
    return valid;
}
