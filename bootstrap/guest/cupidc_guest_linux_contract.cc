/**
 * @file cupidc_guest_linux_contract.cc
 * @brief Supply Linux syscall fixtures for the guest toolchain workers.
 */

#include "ctool_host.h"
#include "guest_job.h"

#ifndef CUPID_TEST_GUEST_ENTRY
#define CUPID_TEST_GUEST_ENTRY cupidc_guest_run
#endif

void CUPID_TEST_GUEST_ENTRY(cupid_syscall_table_t *sys);
#ifdef CUPID_TEST_GUEST_MEDIA_ENTRY
void CUPID_TEST_GUEST_MEDIA_ENTRY(cupid_syscall_table_t *sys);
#endif
int cupid_linux_syscall1(int number, unsigned int argument);
int cupid_linux_syscall2(int number, unsigned int first, unsigned int second);
int cupid_linux_syscall3(int number, unsigned int first, unsigned int second, unsigned int third);

static ctool_host_adapter_t adapter;
static ctool_job_config_t config;
static unsigned live_allocations;
static int simulate_heap_pressure;
static int simulate_write_failure;
static const char *root;

static void *host_alloc(size_t size) {
    if (simulate_heap_pressure && live_allocations != 0u)
        return NULL;
    void *pointer = config.allocator.allocate(config.allocator.context, (ctool_u32)size);
    if (pointer)
        live_allocations++;
    return pointer;
}

static void host_release(void *pointer) {
    if (pointer) {
        live_allocations--;
        config.allocator.release(config.allocator.context, pointer, 0);
    }
}

static size_t host_length(const char *text) {
    return ctool_string(text).size;
}

static int host_equal_n(const char *a, const char *b, size_t size) {
    for (size_t i = 0; i < size; i++) {
        int difference = (unsigned char)a[i] - (unsigned char)b[i];
        if (difference != 0 || a[i] == '\0')
            return difference;
    }
    return 0;
}

static int host_equal(const char *a, const char *b) {
    return host_equal_n(a, b, host_length(a) + 1);
}

static void *host_copy(void *destination, const void *source, size_t bytes) {
    unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t i = 0; i < bytes; i++)
        out[i] = in[i];
    return destination;
}

static int host_result(ctool_status_t status) {
    return status == CTOOL_OK ? 0 : status == CTOOL_ERR_NOT_FOUND ? -2 : -5;
}

static int host_stat(const char *path, cupid_stat_t *info) {
    int result = host_result(config.files.file_size(config.files.context, ctool_string(path),
                                                     &info->size));
    info->type = VFS_TYPE_FILE;
    return result;
}

static int host_read_all(const char *path, void *bytes, uint32_t size) {
    cupid_stat_t info;
    int result = host_stat(path, &info);
    if (result < 0)
        return result;
    if (info.size > size)
        return -28;
    result = host_result(config.files.read_exact(config.files.context, ctool_string(path),
                                                 bytes, info.size));
    return result == 0 ? (int)info.size : result;
}

static int host_read_text(const char *path, char *text, uint32_t size) {
    int count = host_read_all(path, text, size - 1u);
    if (count >= 0)
        text[count] = '\0';
    return count;
}

static int absolute_path(const char *logical, char absolute[4096]) {
    size_t prefix = host_length(root);
    size_t suffix = host_length(logical) + 1;
    if (prefix + suffix > 4096)
        return 0;
    host_copy(absolute, root, prefix);
    host_copy(absolute + prefix, logical, suffix);
    return 1;
}

static int host_open(const char *path, uint32_t flags) {
    char absolute[4096];
    const unsigned int linux_create_write = 1u | 64u | 512u;
    if (flags != (O_WRONLY | O_CREAT | O_TRUNC) || !absolute_path(path, absolute))
        return -22;
    return cupid_linux_syscall3(5, (unsigned int)absolute, linux_create_write, 0600u);
}

static int host_write(int fd, const void *bytes, uint32_t size) {
    if (simulate_write_failure == 2)
        return -28;
    if (simulate_write_failure == 1) {
        simulate_write_failure = 2;
        size = 1u;
    }
    if (size > 32768u)
        size = 32768u;
    return cupid_linux_syscall3(4, (unsigned int)fd, (unsigned int)bytes, size);
}

static int host_close(int fd) {
    return cupid_linux_syscall1(6, (unsigned int)fd);
}

static int host_rename(const char *old_path, const char *new_path) {
    char old_absolute[4096];
    char new_absolute[4096];
    if (!absolute_path(old_path, old_absolute) || !absolute_path(new_path, new_absolute))
        return -22;
    return cupid_linux_syscall2(38, (unsigned int)old_absolute, (unsigned int)new_absolute);
}

static int host_unlink(const char *path) {
    char absolute[4096];
    if (!absolute_path(path, absolute))
        return -22;
    return cupid_linux_syscall1(10, (unsigned int)absolute);
}

static void host_serial(const char *text) {
    cupid_linux_syscall3(4, 1u, (unsigned int)text, (unsigned int)host_length(text));
}

static void host_serial_char(char c) {
    cupid_linux_syscall3(4, 1u, (unsigned int)&c, 1u);
}

static int check_allocation_cache(cupid_syscall_table_t *sys) {
    cupid_guest_io_t io = {.sys = sys};
    ctool_job_config_t guest_config = cupid_guest_job_config(&io);
    ctool_allocator_t allocator = guest_config.allocator;
    int accepted = 0;

    void *original = allocator.allocate(allocator.context, 128u);
    if (!original)
        return 0;
    allocator.release(allocator.context, original, 128u);
    if (live_allocations != 1u || allocator.allocate(allocator.context, 0u) != NULL)
        goto done;

    void *reused = allocator.allocate(allocator.context, 128u);
    if (reused)
        allocator.release(allocator.context, reused, 128u);
    if (reused != original || live_allocations != 1u)
        goto done;

    simulate_heap_pressure = 1;
    void *replacement = allocator.allocate(allocator.context, 197u);
    simulate_heap_pressure = 0;
    if (replacement)
        allocator.release(allocator.context, replacement, 197u);
    accepted = replacement != NULL && live_allocations == 1u;

done:
    cupid_guest_job_drain_cache(&io);
    return accepted && live_allocations == 0u;
}

int main(int argc, char **argv) {
    if ((argc != 2 && argc != 3) ||
        ctool_host_adapter_init(&adapter, argv[1]) != CTOOL_OK)
        return 2;
    void (*entry)(cupid_syscall_table_t *) = CUPID_TEST_GUEST_ENTRY;
    if (argc == 3) {
#ifdef CUPID_TEST_GUEST_MEDIA_ENTRY
        if (host_equal(argv[2], "--media") == 0)
            entry = CUPID_TEST_GUEST_MEDIA_ENTRY;
        else
#endif
        if (host_equal(argv[2], "--fail-write") == 0)
            simulate_write_failure = 1;
        else
            return 2;
    }
    root = argv[1];
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    cupid_syscall_table_t sys = {0};
    sys.version = CUPID_SYSCALL_VERSION;
    sys.table_size = sizeof(sys);
    sys.malloc = host_alloc;
    sys.free = host_release;
    sys.strlen = host_length;
    sys.strcmp = host_equal;
    sys.strncmp = host_equal_n;
    sys.memcpy = host_copy;
    sys.vfs_stat = host_stat;
    sys.vfs_read_all = host_read_all;
    sys.vfs_read_text = host_read_text;
    sys.vfs_open = host_open;
    sys.vfs_write = host_write;
    sys.vfs_close = host_close;
    sys.vfs_rename = host_rename;
    sys.vfs_unlink = host_unlink;
    sys.serial_write_string = host_serial;
    sys.serial_write_char = host_serial_char;
    if (!check_allocation_cache(&sys)) {
        host_serial("Guest allocation cache contract failed.\n");
        return 3;
    }
    entry(&sys);
    return live_allocations != 0;
}
