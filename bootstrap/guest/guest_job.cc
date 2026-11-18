#include "guest_job.h"

#define GUEST_ENOENT (-2)
#define GUEST_ENOSPC (-28)

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
    cupid_stat_t pending;
    int status = io->sys->vfs_stat(io->pending_path, &pending);
    if (status != GUEST_ENOENT)
        return file_failure(status);
    cupid_guest_job_drain_cache(io);
    status = io->sys->vfs_write_all(io->pending_path, contents.data, contents.size);
    if (status >= 0 && (ctool_u32)status == contents.size)
        status = io->sys->vfs_rename(io->pending_path, path.data);
    else if (status >= 0)
        status = -1;
    if (status < 0) {
        io->sys->vfs_unlink(io->pending_path);
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
