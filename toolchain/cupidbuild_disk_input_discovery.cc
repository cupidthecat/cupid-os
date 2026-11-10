#include "cupidbuild_disk_input_discovery.h"
#include "cupidbuild_source_path.h"
#include <stdlib.h>
#include <string.h>

#define INPUT_PATH_BYTES 8192u
#define INPUT_STAGE_LIMIT 4096u
#define INPUT_ROOT_LIMIT 64u

typedef struct {
    cupidbuild_host_observer_t *observer;
    cupidbuild_source_file_t *file_anchor;
    cupidbuild_source_stage_t *stage_anchor;
    char *physical;
} input_root_t;

typedef struct input_string {
    struct input_string *next;
    char bytes[];
} input_string_t;

struct cupidbuild_disk_input_discovery {
    input_string_t *strings;
    char *working;
    char *image;
    cupidbuild_source_file_view_t required[3];
    cupidbuild_disk_input_stage_view_t *stages;
    input_root_t roots[INPUT_ROOT_LIMIT + 1u];
    size_t root_count;
    size_t stage_count;
    size_t quota;
    size_t external_limit;
    cupidbuild_disk_integer_t hdd_mb;
    cupidbuild_disk_integer_t fat_start_lba;
    unsigned int force_format;
    int failed;
};

static char *input_copy(const char *text) {
    size_t size;
    char *result;
    if (!text)
        return (char *)0;
    size = strlen(text);
    if (size >= INPUT_PATH_BYTES)
        return (char *)0;
    result = (char *)malloc(size + 1u);
    if (result)
        (void)memcpy(result, text, size + 1u);
    return result;
}

static char *input_argument(cupidbuild_disk_argument_t argument) {
    char *result;
    if (!argument.data || argument.size >= INPUT_PATH_BYTES ||
        memchr(argument.data, 0, argument.size))
        return (char *)0;
    result = (char *)malloc(argument.size + 1u);
    if (result) {
        (void)memcpy(result, argument.data, argument.size);
        result[argument.size] = 0;
    }
    return result;
}

static const char *input_owned_bytes(cupidbuild_disk_input_discovery_t *owner, const char *text,
                                     size_t size) {
    input_string_t *string;
    if (!text || size >= INPUT_PATH_BYTES || memchr(text, 0, size))
        return (const char *)0;
    string = (input_string_t *)malloc(sizeof(*string) + size + 1u);
    if (!string)
        return (const char *)0;
    (void)memcpy(string->bytes, text, size);
    string->bytes[size] = 0;
    string->next = owner->strings;
    owner->strings = string;
    return string->bytes;
}

static const char *input_owned_copy(cupidbuild_disk_input_discovery_t *owner, const char *text) {
    return text ? input_owned_bytes(owner, text, strlen(text)) : (const char *)0;
}

static int input_separator(char value) {
    return value == '/' || (cupidbuild_host_execution_format() == 2u && value == '\\');
}

static const char *input_relative(const char *root, const char *physical) {
    size_t length = strlen(root);
    if (strncmp(root, physical, length) != 0)
        return (const char *)0;
    if (length && input_separator(root[length - 1u]))
        return physical + length;
    if (!input_separator(physical[length]))
        return (const char *)0;
    return physical + length + 1u;
}

static int input_pair_same(const cupidbuild_source_file_view_t *left,
                           const cupidbuild_source_file_view_t *right) {
    int same = 0;
    return cupidbuild_host_observer_file_bindings_captured_same(
               left->observer, left->logical, right->observer, right->logical, &same) &&
           same;
}

static int input_temporary(cupidbuild_disk_input_discovery_t *owner, const char *request,
                           int optional, int selection, cupidbuild_source_file_t **file_out,
                           cupidbuild_source_stage_t **stage_out,
                           cupidbuild_source_stage_view_t *view) {
    *file_out = (cupidbuild_source_file_t *)0;
    *stage_out = (cupidbuild_source_stage_t *)0;
    (void)memset(view, 0, sizeof(*view));
    if (optional) {
        if (selection)
            return cupidbuild_source_stage_open_selection_view_quota(owner->working, request,
                                                                     owner->quota, stage_out, view);
        return cupidbuild_source_stage_open_quota(owner->working, request, owner->quota,
                                                  stage_out) &&
               cupidbuild_source_stage_view(*stage_out, view);
    }
    view->kind = CUPIDBUILD_ENTRY_FILE;
    if (selection)
        return cupidbuild_source_file_open_selection_view_quota(
            owner->working, request, owner->quota, file_out, &view->file);
    return cupidbuild_source_file_open_quota(owner->working, request, owner->quota, file_out) &&
           cupidbuild_source_file_view(*file_out, &view->file);
}

static int input_close_temporary(cupidbuild_source_file_t *file, cupidbuild_source_stage_t *stage) {
    int file_valid = cupidbuild_source_file_close(file);
    int stage_valid = cupidbuild_source_stage_close(stage);
    return file_valid && stage_valid;
}

typedef struct {
    cupidbuild_source_file_t *file;
    cupidbuild_source_stage_t *stage;
    cupidbuild_source_stage_view_t selected;
    cupidbuild_source_file_view_t *result;
    cupidbuild_host_observer_t **selection_observer;
    size_t *selection_remaining;
} input_pending_t;

static int input_adopt_selection(cupidbuild_disk_input_discovery_t *owner, const char *request,
                                 int optional, cupidbuild_source_file_view_t *result,
                                 cupidbuild_host_entry_kind_t *kind_out, input_pending_t *pending,
                                 const cupidbuild_source_stage_view_t *preselected) {
    cupidbuild_source_file_t *file = (cupidbuild_source_file_t *)0;
    cupidbuild_source_stage_t *stage = (cupidbuild_source_stage_t *)0;
    cupidbuild_source_stage_view_t temporary;
    const char *logical = (const char *)0;
    size_t root_index;
    int same, valid;
    if (preselected) {
        temporary = *preselected;
        valid = 1;
    } else
        valid = input_temporary(owner, request, optional, 1, &file, &stage, &temporary);
    if (valid && temporary.kind == CUPIDBUILD_ENTRY_NONE) {
        result->requested_absolute = input_owned_copy(owner, temporary.file.requested_absolute);
        valid = optional && result->requested_absolute != (const char *)0;
        if (kind_out)
            *kind_out = CUPIDBUILD_ENTRY_NONE;
    } else if (valid && temporary.kind == CUPIDBUILD_ENTRY_FILE) {
        for (root_index = 0u; root_index < owner->root_count; root_index++) {
            logical =
                input_relative(owner->roots[root_index].physical, temporary.file.physical_absolute);
            if (logical && logical[0])
                break;
            same = 0;
            if (!cupidbuild_host_observer_roots_captured_same(owner->roots[root_index].observer,
                                                              temporary.file.observer, &same)) {
                valid = 0;
                break;
            }
            if (same) {
                logical = temporary.file.logical;
                break;
            }
        }
        if (valid && root_index == owner->root_count) {
            if (owner->root_count - 1u >= owner->external_limit)
                valid = 0;
            else {
                cupidbuild_source_file_t *selected_file = file;
                cupidbuild_source_stage_t *selected_stage = stage;
                cupidbuild_source_stage_view_t selected = temporary;
                input_root_t *root = &owner->roots[root_index];
                file = (cupidbuild_source_file_t *)0;
                stage = (cupidbuild_source_stage_t *)0;
                valid = input_temporary(owner, request, optional, 0, &file, &stage, &temporary) &&
                        temporary.kind == CUPIDBUILD_ENTRY_FILE &&
                        strcmp(selected.file.physical_absolute, temporary.file.physical_absolute) ==
                            0 &&
                        input_pair_same(&selected.file, &temporary.file);
                if (!input_close_temporary(selected_file, selected_stage))
                    valid = 0;
                root->physical = valid ? input_copy(temporary.file.physical_root) : (char *)0;
                if (!root->physical)
                    valid = 0;
                else {
                    root->observer = temporary.file.observer;
                    root->file_anchor = file;
                    root->stage_anchor = stage;
                    file = (cupidbuild_source_file_t *)0;
                    stage = (cupidbuild_source_stage_t *)0;
                    owner->root_count++;
                    logical = temporary.file.logical;
                }
            }
        }
        if (valid) {
            result->observer = owner->roots[root_index].observer;
            result->physical_root = owner->roots[root_index].physical;
            result->logical = input_owned_copy(owner, logical);
            result->requested_absolute = input_owned_copy(owner, temporary.file.requested_absolute);
            result->physical_absolute = input_owned_copy(owner, temporary.file.physical_absolute);
            valid = result->logical && result->requested_absolute && result->physical_absolute;
            if (valid) {
                pending->file = file;
                pending->stage = stage;
                pending->selected = temporary;
                pending->result = result;
                file = (cupidbuild_source_file_t *)0;
                stage = (cupidbuild_source_stage_t *)0;
                if (kind_out)
                    *kind_out = CUPIDBUILD_ENTRY_FILE;
            }
        }
    } else
        valid = 0;
    if (!input_close_temporary(file, stage))
        valid = 0;
    return valid;
}

static int input_select(cupidbuild_disk_input_discovery_t *owner, const char *request, int optional,
                        cupidbuild_source_file_view_t *result,
                        cupidbuild_host_entry_kind_t *kind_out, input_pending_t *pending) {
    return input_adopt_selection(owner, request, optional, result, kind_out, pending,
                                 (const cupidbuild_source_stage_view_t *)0);
}

typedef struct {
    char *requested;
    char *parent;
    char *logical;
    char *physical;
    size_t group;
    cupidbuild_host_entry_kind_t kind;
} input_stage_selection_t;

typedef struct {
    cupidbuild_host_observer_t *observer;
    const char *physical;
    size_t remaining;
} input_selection_group_t;

static int input_selection_less(const input_stage_selection_t *rows, size_t left, size_t right) {
    int order = strcmp(rows[left].parent, rows[right].parent);
    return order < 0 || (order == 0 && left < right);
}

static void input_selection_sift(const input_stage_selection_t *rows, size_t *order, size_t count,
                                 size_t root) {
    while (root < count / 2u) {
        size_t child = root * 2u + 1u, value;
        if (child + 1u < count && input_selection_less(rows, order[child], order[child + 1u]))
            child++;
        if (!input_selection_less(rows, order[root], order[child]))
            break;
        value = order[root];
        order[root] = order[child];
        order[child] = value;
        root = child;
    }
}

static void input_selection_sort(const input_stage_selection_t *rows, size_t *order, size_t count) {
    size_t index = count / 2u, value;
    while (index)
        input_selection_sift(rows, order, count, --index);
    while (count > 1u) {
        value = order[0];
        order[0] = order[count - 1u];
        order[count - 1u] = value;
        input_selection_sift(rows, order, --count, 0u);
    }
}

static int input_selection_close(input_stage_selection_t *rows, size_t count,
                                 input_selection_group_t *groups, size_t group_count) {
    size_t index;
    int valid = 1;
    for (index = 0u; index < group_count; index++) {
        if (!cupidbuild_host_observer_close(groups[index].observer))
            valid = 0;
    }
    if (rows) {
        for (index = 0u; index < count; index++) {
            free(rows[index].requested);
            free(rows[index].parent);
            free(rows[index].logical);
            free(rows[index].physical);
        }
    }
    free(rows);
    free(groups);
    return valid;
}

static int input_selection_prepare(cupidbuild_disk_input_discovery_t *owner,
                                   const cupidbuild_disk_options_view_t *options,
                                   input_stage_selection_t **rows_out,
                                   input_selection_group_t **groups_out, size_t *group_count_out) {
    input_stage_selection_t *rows;
    input_selection_group_t *groups;
    const char **names;
    cupidbuild_host_entry_kind_t *kinds;
    size_t *order;
    size_t index, present = 0u, cursor = 0u, group_count = 0u;
    int valid;
    *rows_out = (input_stage_selection_t *)0;
    *groups_out = (input_selection_group_t *)0;
    *group_count_out = 0u;
    if (!owner->stage_count)
        return 1;
    rows = (input_stage_selection_t *)calloc(owner->stage_count, sizeof(*rows));
    groups = (input_selection_group_t *)calloc(owner->stage_count, sizeof(*groups));
    names = (const char **)calloc(owner->stage_count, sizeof(*names));
    kinds = (cupidbuild_host_entry_kind_t *)calloc(owner->stage_count, sizeof(*kinds));
    order = (size_t *)calloc(owner->stage_count, sizeof(*order));
    valid = rows && groups && names && kinds && order;
    for (index = 0u; valid && index < owner->stage_count; index++) {
        char absolute[INPUT_PATH_BYTES];
        char *request = input_argument(options->stages[index].source);
        size_t length, split, parent_length, character;
        valid = request && cupidbuild_source_path_absolute(owner->working, request, absolute,
                                                           sizeof(absolute));
        free(request);
        if (!valid)
            break;
        rows[index].requested = input_copy(absolute);
        valid = rows[index].requested != (char *)0;
        if (!valid)
            break;
        if (cupidbuild_host_source_missing(absolute)) {
            valid = cupidbuild_host_source_missing(rows[index].requested);
            continue;
        }
        length = strlen(absolute);
        split = length;
        while (split && !input_separator(absolute[split - 1u]))
            split--;
        if (!split || split == length) {
            valid = 0;
            break;
        }
        rows[index].logical = input_copy(absolute + split);
        valid = rows[index].logical != (char *)0;
        if (!valid)
            break;
        for (character = 0u; rows[index].logical[character]; character++) {
            if (cupidbuild_host_execution_format() == 2u && rows[index].logical[character] == '\\')
                rows[index].logical[character] = '/';
        }
        parent_length = split - 1u;
        if (!parent_length)
            parent_length = 1u;
        if (cupidbuild_host_execution_format() == 2u && parent_length == 2u && absolute[1] == ':')
            parent_length = 3u;
        absolute[parent_length] = 0;
        rows[index].parent = input_copy(absolute);
        valid = rows[index].parent != (char *)0;
        if (valid)
            order[present++] = index;
    }
    if (valid)
        input_selection_sort(rows, order, present);
    while (valid && cursor < present) {
        size_t end = cursor + 1u, member, root_length;
        input_selection_group_t *group = &groups[group_count];
        while (end < present && strcmp(rows[order[cursor]].parent, rows[order[end]].parent) == 0)
            end++;
        valid = cupidbuild_host_observer_begin_source_selection_quota(
            rows[order[cursor]].parent, owner->quota, &group->observer, &group->physical);
        if (!valid)
            break;
        group_count++;
        group->remaining = end - cursor;
        for (member = cursor; member < end; member++)
            names[member - cursor] = rows[order[member]].logical;
        valid = cupidbuild_host_observer_source_selection_paths_kind(
            group->observer, names, end - cursor, (cupidbuild_host_path_observation_sink_t)0,
            (void *)0, kinds);
        root_length = strlen(group->physical);
        for (member = cursor; valid && member < end; member++) {
            input_stage_selection_t *row = &rows[order[member]];
            size_t leaf_length = strlen(row->logical);
            int separator = root_length && !input_separator(group->physical[root_length - 1u]);
            size_t joined = root_length + (separator ? 1u : 0u) + leaf_length;
            valid = kinds[member - cursor] == CUPIDBUILD_ENTRY_FILE && joined < INPUT_PATH_BYTES;
            if (!valid)
                break;
            row->physical = (char *)malloc(joined + 1u);
            valid = row->physical != (char *)0;
            if (!valid)
                break;
            (void)memcpy(row->physical, group->physical, root_length);
            if (separator)
                row->physical[root_length] = '/';
            (void)memcpy(row->physical + root_length + (separator ? 1u : 0u), row->logical,
                         leaf_length + 1u);
            row->group = group_count - 1u;
            row->kind = CUPIDBUILD_ENTRY_FILE;
        }
        cursor = end;
    }
    free(names);
    free(kinds);
    free(order);
    if (!valid) {
        (void)input_selection_close(rows, owner->stage_count, groups, group_count);
        return 0;
    }
    *rows_out = rows;
    *groups_out = groups;
    *group_count_out = group_count;
    return 1;
}

typedef struct {
    input_pending_t *pending;
    const size_t *slots;
    size_t count;
} input_capture_context_t;

static int input_capture_selected(void *opaque, size_t index, const char *logical,
                                  cupidbuild_host_entry_kind_t kind) {
    input_capture_context_t *context = (input_capture_context_t *)opaque;
    input_pending_t *item;
    int valid;
    if (index >= context->count)
        return 0;
    item = &context->pending[context->slots[index]];
    valid = kind == CUPIDBUILD_ENTRY_FILE && item->result &&
            strcmp(logical, item->result->logical) == 0 &&
            input_pair_same(item->result, &item->selected.file);
    if (!input_close_temporary(item->file, item->stage))
        valid = 0;
    item->file = (cupidbuild_source_file_t *)0;
    item->stage = (cupidbuild_source_stage_t *)0;
    if (item->selection_remaining) {
        if (!*item->selection_remaining || !item->selection_observer || !*item->selection_observer)
            valid = 0;
        else {
            (*item->selection_remaining)--;
            if (!*item->selection_remaining) {
                if (!cupidbuild_host_observer_close(*item->selection_observer))
                    valid = 0;
                *item->selection_observer = (cupidbuild_host_observer_t *)0;
            }
        }
        item->selection_remaining = (size_t *)0;
        item->selection_observer = (cupidbuild_host_observer_t **)0;
    }
    return valid;
}

static int input_capture_pending(cupidbuild_disk_input_discovery_t *owner, input_pending_t *pending,
                                 size_t count) {
    const char **names = (const char **)calloc(count, sizeof(*names));
    cupidbuild_host_entry_kind_t *kinds =
        (cupidbuild_host_entry_kind_t *)calloc(count, sizeof(*kinds));
    size_t *slots = (size_t *)calloc(count, sizeof(*slots));
    input_capture_context_t context;
    size_t root, index;
    int valid = names && kinds && slots;
    context.pending = pending;
    context.slots = slots;
    for (root = 0u; valid && root < owner->root_count; root++) {
        context.count = 0u;
        for (index = 0u; index < count; index++) {
            if (pending[index].result &&
                pending[index].result->observer == owner->roots[root].observer) {
                names[context.count] = pending[index].result->logical;
                slots[context.count++] = index;
            }
        }
        valid =
            cupidbuild_host_observer_paths_kind(owner->roots[root].observer, names, context.count,
                                                input_capture_selected, &context, kinds);
    }
    free(names);
    free(kinds);
    free(slots);
    return valid;
}

static int input_close_pending(input_pending_t *pending, size_t count) {
    size_t index;
    int valid = 1;
    for (index = 0u; index < count; index++) {
        if (!input_close_temporary(pending[index].file, pending[index].stage))
            valid = 0;
    }
    return valid;
}

static int input_validate_requests(cupidbuild_disk_input_discovery_t *owner) {
    size_t capacity = owner->stage_count + 3u;
    cupidbuild_host_file_request_t *requests =
        (cupidbuild_host_file_request_t *)calloc(capacity, sizeof(*requests));
    size_t root, index, count;
    int valid = requests != (cupidbuild_host_file_request_t *)0;
    for (root = 0u; valid && root < owner->root_count; root++) {
        count = 0u;
        for (index = 0u; index < 3u; index++) {
            const cupidbuild_source_file_view_t *file = &owner->required[index];
            if (file->observer == owner->roots[root].observer) {
                requests[count].logical = file->logical;
                requests[count].requested_absolute = file->requested_absolute;
                requests[count].physical_absolute = file->physical_absolute;
                requests[count++].parent_identity_only = 0;
            }
        }
        for (index = 0u; index < owner->stage_count; index++) {
            const cupidbuild_disk_input_stage_view_t *stage = &owner->stages[index];
            if (stage->kind != CUPIDBUILD_ENTRY_NONE && stage->kind != CUPIDBUILD_ENTRY_FILE) {
                valid = 0;
                break;
            }
            if (stage->kind == CUPIDBUILD_ENTRY_FILE &&
                stage->file.observer == owner->roots[root].observer) {
                requests[count].logical = stage->file.logical;
                requests[count].requested_absolute = stage->file.requested_absolute;
                requests[count].physical_absolute = stage->file.physical_absolute;
                requests[count++].parent_identity_only = 1;
            }
        }
        if (valid)
            valid = cupidbuild_host_observer_require_file_request_bindings(
                owner->roots[root].observer, requests, count,
                (cupidbuild_host_file_request_sink_t)0, (void *)0);
    }
    free(requests);
    return valid;
}

static int input_validate_roots(cupidbuild_disk_input_discovery_t *owner) {
    size_t index;
    for (index = 0u; index < owner->root_count; index++) {
        if (!cupidbuild_host_observer_require_source_unchanged(owner->roots[index].observer))
            return 0;
    }
    return 1;
}

static int input_validate(cupidbuild_disk_input_discovery_t *owner) {
    size_t index;
    int valid;
    if (!owner || owner->failed)
        return 0;
    valid = input_validate_roots(owner);
    if (valid)
        valid = input_validate_requests(owner);
    for (index = 0u; valid && index < owner->stage_count; index++) {
        const cupidbuild_disk_input_stage_view_t *stage = &owner->stages[index];
        if (stage->kind == CUPIDBUILD_ENTRY_NONE)
            valid = cupidbuild_host_source_missing(stage->file.requested_absolute);
    }
    if (valid)
        valid = input_validate_roots(owner);
    if (!valid)
        owner->failed = 1;
    return valid;
}

int cupidbuild_disk_input_discovery_open_quota(const char *working_root,
                                               const cupidbuild_disk_options_view_t *options,
                                               size_t entry_quota, size_t external_root_limit,
                                               cupidbuild_disk_input_discovery_t **owner_out) {
    cupidbuild_disk_input_discovery_t *owner;
    char absolute[INPUT_PATH_BYTES];
    char *request;
    const char *physical;
    cupidbuild_disk_argument_t required[3];
    size_t index;
    input_pending_t *pending = (input_pending_t *)0;
    size_t pending_count = 0u;
    input_stage_selection_t *selection_rows = (input_stage_selection_t *)0;
    input_selection_group_t *selection_groups = (input_selection_group_t *)0;
    size_t selection_group_count = 0u;
    int valid;
    if (owner_out)
        *owner_out = (cupidbuild_disk_input_discovery_t *)0;
    if (!owner_out || !options || !entry_quota || entry_quota == (size_t)-1 ||
        external_root_limit > INPUT_ROOT_LIMIT || options->stage_count > INPUT_STAGE_LIMIT ||
        (options->stage_count && !options->stages) ||
        !cupidbuild_source_path_absolute(working_root, ".", absolute, sizeof(absolute)))
        return 0;
    owner = (cupidbuild_disk_input_discovery_t *)calloc(1u, sizeof(*owner));
    if (!owner)
        return 0;
    owner->quota = entry_quota;
    owner->external_limit = external_root_limit;
    owner->stage_count = options->stage_count;
    owner->hdd_mb = options->hdd_mb;
    owner->fat_start_lba = options->fat_start_lba;
    owner->force_format = options->force_format;
    owner->working = input_copy(absolute);
    valid =
        owner->working && cupidbuild_host_observer_resolve_source_root_quota(
                              owner->working, entry_quota, &owner->roots[0].observer, &physical);
    if (valid) {
        owner->root_count = 1u;
        owner->roots[0].physical = input_copy(physical);
        valid = owner->roots[0].physical != (char *)0;
    }
    request = input_argument(options->image);
    if (valid)
        valid = request && cupidbuild_source_path_absolute(owner->working, request, absolute,
                                                           sizeof(absolute));
    if (valid) {
        owner->image = input_copy(absolute);
        valid = owner->image != (char *)0;
    }
    free(request);
    if (valid) {
        valid = owner->stage_count <= (size_t)-1 / sizeof(*pending) - 3u;
        if (valid) {
            pending_count = owner->stage_count + 3u;
            pending = (input_pending_t *)calloc(pending_count, sizeof(*pending));
            valid = pending != (input_pending_t *)0;
        }
    }
    required[0] = options->seed_manifest;
    required[1] = options->bootloader;
    required[2] = options->kernel;
    for (index = 0u; valid && index < 3u; index++) {
        request = input_argument(required[index]);
        valid = request && input_select(owner, request, 0, &owner->required[index],
                                        (cupidbuild_host_entry_kind_t *)0, &pending[index]);
        free(request);
    }
    if (valid && owner->stage_count) {
        owner->stages = (cupidbuild_disk_input_stage_view_t *)calloc(owner->stage_count,
                                                                     sizeof(*owner->stages));
        valid = owner->stages != (cupidbuild_disk_input_stage_view_t *)0;
    }
    if (valid)
        valid = input_selection_prepare(owner, options, &selection_rows, &selection_groups,
                                        &selection_group_count);
    for (index = 0u; valid && index < owner->stage_count; index++) {
        cupidbuild_source_stage_view_t selected;
        input_stage_selection_t *row = &selection_rows[index];
        (void)memset(&selected, 0, sizeof(selected));
        selected.kind = row->kind;
        selected.file.requested_absolute = row->requested;
        if (row->kind == CUPIDBUILD_ENTRY_FILE) {
            selected.file.observer = selection_groups[row->group].observer;
            selected.file.physical_root = selection_groups[row->group].physical;
            selected.file.logical = row->logical;
            selected.file.physical_absolute = row->physical;
        }
        request = input_argument(options->stages[index].source);
        owner->stages[index].destination =
            input_owned_bytes(owner, options->stages[index].destination.data,
                              options->stages[index].destination.size);
        valid = request && owner->stages[index].destination &&
                input_adopt_selection(owner, request, 1, &owner->stages[index].file,
                                      &owner->stages[index].kind, &pending[index + 3u], &selected);
        if (valid && row->kind == CUPIDBUILD_ENTRY_FILE) {
            input_selection_group_t *group = &selection_groups[row->group];
            pending[index + 3u].selection_observer = &group->observer;
            pending[index + 3u].selection_remaining = &group->remaining;
        }
        free(request);
    }
    if (valid)
        valid = input_capture_pending(owner, pending, pending_count);
    if (pending && !input_close_pending(pending, pending_count))
        valid = 0;
    free(pending);
    if (!input_selection_close(selection_rows, owner->stage_count, selection_groups,
                               selection_group_count))
        valid = 0;
    if (valid)
        valid = input_validate(owner);
    if (!valid) {
        (void)cupidbuild_disk_input_discovery_close(owner);
        return 0;
    }
    *owner_out = owner;
    return 1;
}

int cupidbuild_disk_input_discovery_view(cupidbuild_disk_input_discovery_t *owner,
                                         cupidbuild_disk_input_discovery_view_t *view_out) {
    if (view_out)
        (void)memset(view_out, 0, sizeof(*view_out));
    if (!owner || !view_out) {
        if (owner)
            owner->failed = 1;
        return 0;
    }
    if (!input_validate(owner))
        return 0;
    view_out->working_root_requested = owner->working;
    view_out->working_root_physical = owner->roots[0].physical;
    view_out->primary_observer = owner->roots[0].observer;
    view_out->image_requested_absolute = owner->image;
    view_out->seed_manifest = owner->required[0];
    view_out->bootloader = owner->required[1];
    view_out->kernel = owner->required[2];
    view_out->stages = owner->stages;
    view_out->stage_count = owner->stage_count;
    view_out->external_root_count = owner->root_count - 1u;
    view_out->hdd_mb = owner->hdd_mb;
    view_out->fat_start_lba = owner->fat_start_lba;
    view_out->force_format = owner->force_format;
    return 1;
}

int cupidbuild_disk_input_discovery_close(cupidbuild_disk_input_discovery_t *owner) {
    size_t index;
    int valid = 1;
    if (!owner)
        return 1;
    for (index = 0u; index < owner->root_count; index++) {
        input_root_t *root = &owner->roots[index];
        if (index == 0u) {
            if (!cupidbuild_host_observer_close(root->observer))
                valid = 0;
        } else if (!input_close_temporary(root->file_anchor, root->stage_anchor))
            valid = 0;
        free(root->physical);
    }
    while (owner->strings) {
        input_string_t *string = owner->strings;
        owner->strings = string->next;
        free(string);
    }
    free(owner->stages);
    free(owner->image);
    free(owner->working);
    free(owner);
    return valid;
}
