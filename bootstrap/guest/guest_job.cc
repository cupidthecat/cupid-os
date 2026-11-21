/**
 * @file guest_job.cc
 * @brief Read worker requests and adapt Cupid OS files and memory to toolchain jobs.
 */

#include "guest_job.h"

#define GUEST_ENOENT (-2)
#define GUEST_ENOSPC (-28)
#define HOME_PREFIX "/home/"
#define TEMP_PREFIX "/tmp/"

char *cupid_guest_next_job_line(char **cursor, char *end) {
    char *line = *cursor;
    if (line >= end)
        return NULL;
    char *stop = line;
    while (stop < end && *stop != '\n') {
        if (*stop == '\r' || *stop == '\0')
            return NULL;
        stop++;
    }
    if (stop == line)
        return NULL;
    *stop = '\0';
    *cursor = stop < end ? stop + 1 : stop;
    return line;
}

int cupid_guest_read_root(cupid_syscall_table_t *sys, char **cursor, char *end,
                          const char *fallback, const char **root) {
    *root = fallback;
    if (end - *cursor < 5 || sys->strncmp(*cursor, "root ", 5u) != 0)
        return 1;
    char *line = cupid_guest_next_job_line(cursor, end);
    if (!line)
        return 0;
    ctool_path_t path = {ctool_string(line + 5)};
    if (path.text.size >= VFS_MAX_PATH || !ctool_path_is_canonical(&path))
        return 0;
    *root = path.text.size == 1u ? "" : path.text.data;
    return 1;
}

ctool_u32 cupid_guest_output_prefix(cupid_syscall_table_t *sys, ctool_string_t path) {
    if (path.size > sizeof(HOME_PREFIX) - 1u &&
        sys->strncmp(path.data, HOME_PREFIX, sizeof(HOME_PREFIX) - 1u) == 0)
        return sizeof(HOME_PREFIX) - 1u;
    if (path.size > sizeof(TEMP_PREFIX) - 1u &&
        sys->strncmp(path.data, TEMP_PREFIX, sizeof(TEMP_PREFIX) - 1u) == 0)
        return sizeof(TEMP_PREFIX) - 1u;
    return 0u;
}

int cupid_guest_read_path_lines(cupid_syscall_table_t *sys, char **cursor, char *end,
                               ctool_path_t *paths, ctool_u32 count) {
    if (count == 0u)
        return 0;
    for (ctool_u32 i = 0; i < count; i++) {
        char *line = cupid_guest_next_job_line(cursor, end);
        if (!line)
            return 0;
        ctool_path_t path = {ctool_string(line)};
        if (path.text.size >= VFS_MAX_PATH || !ctool_path_is_canonical(&path))
            return 0;
        paths[i] = path;
    }
    return cupid_guest_output_prefix(sys, paths[count - 1u].text) != 0u;
}

int cupid_guest_read_request(cupid_syscall_table_t *sys, const cupid_guest_request_t *job,
                             char *file, ctool_u32 capacity) {
    if (!job->text.data)
        return sys->vfs_read_text(job->path, file, capacity);
    if (job->text.size >= capacity)
        return GUEST_ENOSPC;
    sys->memcpy(file, job->text.data, job->text.size);
    file[job->text.size] = '\0';
    return (int)job->text.size;
}

int cupid_guest_read_paths(cupid_syscall_table_t *sys, const cupid_guest_request_t *job,
                          char *file, ctool_u32 capacity, ctool_path_t *paths, ctool_u32 count) {
    int size = cupid_guest_read_request(sys, job, file, capacity);
    if (size < 1 || (ctool_u32)size >= capacity)
        return 0;
    char *cursor = file;
    char *end = file + size;
    return cupid_guest_read_path_lines(sys, &cursor, end, paths, count) && cursor == end;
}

int cupid_guest_read_path_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job,
                              char *file, ctool_invocation_request_t *invocation) {
    ctool_path_t paths[2];
    if (!cupid_guest_read_paths(sys, job, file, CUPID_GUEST_PATH_JOB_BYTES, paths, 2u))
        return 0;
    invocation->input_path = paths[0].text;
    invocation->output_path = paths[1].text;
    return 1;
}

ctool_u32 cupid_guest_read_object_list(cupid_syscall_table_t *sys, char *cursor, char *end,
                                        const char *reserved_leaf, ctool_invocation_request_t *items) {
    ctool_u32 count = 0u;
    while (cursor < end && count < CUPID_GUEST_OBJECT_LIST_LIMIT) {
        ctool_path_t paths[2];
        if (!cupid_guest_read_path_lines(sys, &cursor, end, paths, 2u) || paths[0].text.size <= 1u)
            return 0u;
        ctool_string_t output = paths[1].text;
        if (sys->strcmp(output.data + output.size - 2u, ".o") != 0 ||
            (reserved_leaf && sys->strcmp(
                output.data + cupid_guest_output_prefix(sys, output), reserved_leaf) == 0))
            return 0u;
        for (ctool_u32 i = 0; i < count; i++) {
            if (sys->strcmp(items[i].output_path.data, output.data) == 0)
                return 0u;
        }
        items[count++] = (ctool_invocation_request_t){paths[0].text, output};
    }
    return cursor == end ? count : 0u;
}

static ctool_status_t file_failure(int status) {
    if (status == GUEST_ENOENT)
        return CTOOL_ERR_NOT_FOUND;
    if (status == GUEST_ENOSPC)
        return CTOOL_ERR_LIMIT;
    return CTOOL_ERR_IO;
}

static ctool_status_t input_path(const cupid_guest_io_t *io, ctool_string_t logical,
                                 char path[VFS_MAX_PATH]) {
    ctool_path_t candidate = {logical};
    size_t prefix = io->input_prefix.size;
    if (!ctool_path_is_canonical(&candidate))
        return CTOOL_ERR_PATH;
    if (logical.size >= VFS_MAX_PATH - prefix)
        return CTOOL_ERR_LIMIT;
    if (prefix)
        io->sys->memcpy(path, io->input_prefix.data, prefix);
    io->sys->memcpy(path + prefix, logical.data, logical.size);
    path[prefix + logical.size] = '\0';
    return CTOOL_OK;
}

static ctool_status_t guest_size(void *context, ctool_string_t logical, ctool_u32 *size) {
    cupid_guest_io_t *io = context;
    char path[VFS_MAX_PATH];
    ctool_status_t status = input_path(io, logical, path);
    if (status != CTOOL_OK)
        return status;
    cupid_stat_t information;
    int result = io->sys->vfs_stat(path, &information);
    if (result < 0)
        return file_failure(result);
    if (information.type != VFS_TYPE_FILE)
        return CTOOL_ERR_INPUT;
    *size = information.size;
    return CTOOL_OK;
}

static ctool_status_t guest_read(void *context, ctool_string_t logical, ctool_u8 *bytes,
                                 ctool_u32 size) {
    cupid_guest_io_t *io = context;
    char path[VFS_MAX_PATH];
    ctool_status_t status = input_path(io, logical, path);
    if (status != CTOOL_OK)
        return status;
    int count = io->sys->vfs_read_all(path, bytes, size);
    if (count < 0)
        return file_failure(count);
    return (ctool_u32)count == size ? CTOOL_OK : CTOOL_ERR_IO;
}

static ctool_status_t guest_write(void *context, ctool_string_t path, ctool_bytes_t contents) {
    cupid_guest_io_t *io = context;
    const char *candidate = io->pending_path;
    ctool_u32 prefix = cupid_guest_output_prefix(io->sys, path);
    if (!candidate || prefix == 0u)
        return CTOOL_ERR_PATH;

    char temporary[VFS_MAX_PATH];
    if (prefix == sizeof(TEMP_PREFIX) - 1u) {
        ctool_string_t pending = ctool_string(candidate);
        if (pending.size <= sizeof(HOME_PREFIX) - 1u || pending.size >= sizeof(temporary) ||
            io->sys->strncmp(candidate, HOME_PREFIX, sizeof(HOME_PREFIX) - 1u) != 0)
            return CTOOL_ERR_PATH;
        io->sys->memcpy(temporary, TEMP_PREFIX, sizeof(TEMP_PREFIX) - 1u);
        io->sys->memcpy(temporary + sizeof(TEMP_PREFIX) - 1u, candidate + sizeof(HOME_PREFIX) - 1u,
                        pending.size - (sizeof(HOME_PREFIX) - 1u) + 1u);
        candidate = temporary;
    }
    if (io->sys->strcmp(candidate, path.data) == 0)
        return CTOOL_ERR_INPUT;

    cupid_stat_t pending;
    int status = io->sys->vfs_stat(candidate, &pending);
    if (status != GUEST_ENOENT)
        return file_failure(status);
    cupid_guest_job_drain_cache(io);
    int fd = io->sys->vfs_open(candidate, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
        return file_failure(fd);

    ctool_u32 written = 0u;
    status = 0;
    while (written < contents.size) {
        ctool_u32 remaining = contents.size - written;
        int count = io->sys->vfs_write(fd, contents.data + written, remaining);
        if (count <= 0 || (ctool_u32)count > remaining) {
            status = count < 0 ? count : -1;
            break;
        }
        written += (ctool_u32)count;
    }
    /* Keep the candidate open so HomeFS syncs only the completed rename. */
    if (status == 0)
        status = io->sys->vfs_rename(candidate, path.data);
    io->sys->vfs_close(fd);
    if (status < 0) {
        io->sys->vfs_unlink(candidate);
        return file_failure(status);
    }
    return CTOOL_OK;
}

static ctool_status_t guest_diagnostic(void *context, ctool_bytes_t text) {
    cupid_guest_io_t *io = context;
    for (ctool_u32 i = 0; i < text.size; i++)
        io->sys->serial_write_char((char)text.data[i]);
    return CTOOL_OK;
}

#define GUEST_CACHE_LIMIT (8u * 1024u * 1024u)

/* Arena rewinds and temporary buffers request the same sizes repeatedly. */
static cupid_guest_cached_allocation_t *guest_cache_slot(cupid_guest_io_t *io, ctool_u32 size) {
    ctool_u32 hash = size ^ (size >> 4u) ^ (size >> 12u);
    return &io->cache[hash & (CUPID_GUEST_CACHE_SLOTS - 1u)];
}

void cupid_guest_job_drain_cache(cupid_guest_io_t *io) {
    for (ctool_u32 i = 0; i < CUPID_GUEST_CACHE_SLOTS; i++) {
        cupid_guest_cached_allocation_t *slot = &io->cache[i];
        if (slot->allocation) {
            io->sys->free(slot->allocation);
            slot->allocation = NULL;
            slot->size = 0u;
        }
    }
    io->cached_bytes = 0u;
}

static void *guest_allocate(void *context, ctool_u32 size) {
    if (size == 0u)
        return NULL;
    cupid_guest_io_t *io = context;
    cupid_guest_cached_allocation_t *slot = guest_cache_slot(io, size);
    if (slot->allocation && slot->size == size) {
        void *allocation = slot->allocation;
        slot->allocation = NULL;
        slot->size = 0u;
        io->cached_bytes -= size;
        return allocation;
    }

    void *allocation = io->sys->malloc(size);
    if (!allocation && io->cached_bytes != 0u) {
        cupid_guest_job_drain_cache(io);
        allocation = io->sys->malloc(size);
    }
    return allocation;
}

static void guest_release(void *context, void *allocation, ctool_u32 size) {
    cupid_guest_io_t *io = context;
    if (!allocation)
        return;
    if (size == 0u || size > GUEST_CACHE_LIMIT) {
        io->sys->free(allocation);
        return;
    }

    cupid_guest_cached_allocation_t *slot = guest_cache_slot(io, size);
    ctool_u32 retained = io->cached_bytes - slot->size;
    if (size > GUEST_CACHE_LIMIT - retained) {
        io->sys->free(allocation);
        return;
    }
    if (slot->allocation)
        io->sys->free(slot->allocation);
    slot->allocation = allocation;
    slot->size = size;
    io->cached_bytes = retained + size;
}

ctool_job_config_t cupid_guest_job_config(cupid_guest_io_t *io) {
    ctool_job_config_t config = {0};
    config.limits = ctool_default_limits();
    config.allocator = (ctool_allocator_t){io, guest_allocate, guest_release};
    config.files = (ctool_file_store_t){io, guest_size, guest_read, guest_write};
    config.diagnostics = (ctool_text_sink_t){io, guest_diagnostic};
    return config;
}
