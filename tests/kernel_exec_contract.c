#include "exec.h"
#include "vfs.h"
#include "process.h"
#include "memory.h"
#include "syscall.h"
#include "serial.h"
#include "string.h"

#define TEST_FILE_CAPACITY 0x5000u
#define TEST_HEAP_CAPACITY 0x300000u
#define TEST_PROGRAM_OFFSET 0x1000u
#define TEST_PROGRAM_BYTES 4u
#define TEST_PT_GNU_STACK 0x6474E551u
#define TEST_PT_INTERP 3u
#define TEST_PF_X 0x1u
#define TEST_PF_W 0x2u
#define TEST_PF_R 0x4u
#define TEST_OLD_EXTERNAL_BASE 0x00D00000u

_Static_assert(TEST_OLD_EXTERNAL_BASE >= STACK_BOTTOM &&
                   TEST_OLD_EXTERNAL_BASE < STACK_TOP,
               "former external base must stay inside the current stack");

static uint8_t test_file[TEST_FILE_CAPACITY];
static uint32_t test_reported_size;
static uint32_t test_physical_size;
static uint32_t test_position;
static bool test_file_open;

static uint8_t test_heap[TEST_HEAP_CAPACITY] __attribute__((aligned(16)));
static bool test_heap_used;
static uint32_t test_heap_allocations;
static uint32_t test_heap_frees;

static bool test_claim_allowed = true;
static bool test_create_allowed = true;
static uint32_t test_claim_calls;
static uint32_t test_discard_calls;
static uint32_t test_create_calls;
static uint32_t test_yield_calls;
static uint32_t test_fixed_copy_calls;
static process_image_t test_created_image;
static uint8_t test_external_arena[PAGE_SIZE];
static uint8_t test_old_external_arena[PAGE_SIZE];
static uint8_t test_cupidc_arena[PAGE_SIZE];
static uint8_t test_cupidasm_arena[PAGE_SIZE];

static bool strings_equal(const char *left, const char *right) {
    uint32_t i = 0u;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return false;
        }
        i++;
    }
    return left[i] == right[i];
}

static bool address_is_executable_arena(uint32_t address) {
    return address == EXTERNAL_EXEC_ARENA_START ||
           address == TEST_OLD_EXTERNAL_BASE ||
           address == CUPIDC_EXEC_ARENA_START ||
           address == CUPIDASM_EXEC_ARENA_START;
}

static uint8_t *arena_shadow(uint32_t address, size_t count) {
    uint32_t base;
    uint8_t *shadow;
    if (address == EXTERNAL_EXEC_ARENA_START) {
        base = EXTERNAL_EXEC_ARENA_START;
        shadow = test_external_arena;
    } else if (address == TEST_OLD_EXTERNAL_BASE) {
        base = TEST_OLD_EXTERNAL_BASE;
        shadow = test_old_external_arena;
    } else if (address == CUPIDC_EXEC_ARENA_START) {
        base = CUPIDC_EXEC_ARENA_START;
        shadow = test_cupidc_arena;
    } else if (address == CUPIDASM_EXEC_ARENA_START) {
        base = CUPIDASM_EXEC_ARENA_START;
        shadow = test_cupidasm_arena;
    } else {
        return NULL;
    }
    uint32_t offset = address - base;
    if (count > (size_t)(PAGE_SIZE - offset)) {
        return NULL;
    }
    return shadow + offset;
}

void *memset(void *destination, int value, size_t count) {
    uint8_t *bytes = (uint8_t *)destination;
    for (size_t i = 0u; i < count; i++) {
        bytes[i] = (uint8_t)value;
    }
    return destination;
}

void *memcpy(void *destination, const void *source, size_t count) {
    uint32_t destination_address = (uint32_t)destination;
    uint8_t *out = arena_shadow(destination_address, count);
    const uint8_t *in = (const uint8_t *)source;
    if (address_is_executable_arena(destination_address)) {
        test_fixed_copy_calls++;
    }
    if (!out) {
        out = (uint8_t *)destination;
    }
    for (size_t i = 0u; i < count; i++) {
        out[i] = in[i];
    }
    return destination;
}

static int prepare_arena(uint32_t address) {
    uint8_t *shadow = arena_shadow(address, PAGE_SIZE);
    if (!shadow) return 70;
    memset(shadow, 0xA5, PAGE_SIZE);
    test_fixed_copy_calls = 0u;
    return 0;
}

static void reset_observations(void) {
    test_position = 0u;
    test_file_open = false;
    test_heap_used = false;
    test_heap_allocations = 0u;
    test_heap_frees = 0u;
    test_claim_allowed = true;
    test_create_allowed = true;
    test_claim_calls = 0u;
    test_discard_calls = 0u;
    test_create_calls = 0u;
    test_yield_calls = 0u;
    test_fixed_copy_calls = 0u;
    memset(&test_created_image, 0, sizeof(test_created_image));
}

static void initialize_valid_elf(uint32_t address) {
    memset(test_file, 0, sizeof(test_file));
    reset_observations();

    elf32_ehdr_t *header = (elf32_ehdr_t *)test_file;
    header->e_ident[0] = ELF_MAGIC_0;
    header->e_ident[1] = ELF_MAGIC_1;
    header->e_ident[2] = ELF_MAGIC_2;
    header->e_ident[3] = ELF_MAGIC_3;
    header->e_ident[4] = ELF_CLASS_32;
    header->e_ident[5] = ELF_DATA_LSB;
    header->e_ident[6] = 1u;
    header->e_type = ELF_TYPE_EXEC;
    header->e_machine = ELF_MACHINE_386;
    header->e_version = 1u;
    header->e_entry = address;
    header->e_phoff = (uint32_t)sizeof(elf32_ehdr_t);
    header->e_ehsize = (uint16_t)sizeof(elf32_ehdr_t);
    header->e_phentsize = (uint16_t)sizeof(elf32_phdr_t);
    header->e_phnum = 2u;

    elf32_phdr_t *programs =
        (elf32_phdr_t *)(test_file + sizeof(elf32_ehdr_t));
    programs[0].p_type = ELF_PT_LOAD;
    programs[0].p_offset = TEST_PROGRAM_OFFSET;
    programs[0].p_vaddr = address;
    programs[0].p_paddr = address;
    programs[0].p_filesz = TEST_PROGRAM_BYTES;
    programs[0].p_memsz = 0x100u;
    programs[0].p_flags = TEST_PF_R | TEST_PF_X;
    programs[0].p_align = PAGE_SIZE;

    programs[1].p_type = TEST_PT_GNU_STACK;
    programs[1].p_flags = TEST_PF_R | TEST_PF_W;
    programs[1].p_align = 16u;

    test_file[TEST_PROGRAM_OFFSET] = 0x90u;
    test_file[TEST_PROGRAM_OFFSET + 1u] = 0x90u;
    test_file[TEST_PROGRAM_OFFSET + 2u] = 0xC3u;
    test_file[TEST_PROGRAM_OFFSET + 3u] = 0xCCu;
    test_reported_size = TEST_PROGRAM_OFFSET + TEST_PROGRAM_BYTES;
    test_physical_size = test_reported_size;
}

static elf32_ehdr_t *test_header(void) {
    return (elf32_ehdr_t *)test_file;
}

static elf32_phdr_t *test_programs(void) {
    return (elf32_phdr_t *)(test_file + sizeof(elf32_ehdr_t));
}

int vfs_stat(const char *path, vfs_stat_t *stat) {
    if (!path || !stat) {
        return VFS_EINVAL;
    }
    stat->size = test_reported_size;
    stat->type = VFS_TYPE_FILE;
    return VFS_OK;
}

int vfs_open(const char *path, uint32_t flags) {
    (void)flags;
    if (!path || test_file_open) {
        return VFS_EINVAL;
    }
    test_file_open = true;
    test_position = 0u;
    return 1;
}

int vfs_close(int fd) {
    if (fd != 1 || !test_file_open) {
        return VFS_EINVAL;
    }
    test_file_open = false;
    return VFS_OK;
}

int vfs_read(int fd, void *buffer, uint32_t count) {
    if (fd != 1 || !test_file_open || !buffer) {
        return VFS_EINVAL;
    }
    if (test_position >= test_physical_size) {
        return 0;
    }
    uint32_t available = test_physical_size - test_position;
    uint32_t read_count = count < available ? count : available;
    memcpy(buffer, test_file + test_position, read_count);
    test_position += read_count;
    return (int)read_count;
}

int vfs_seek(int fd, int32_t offset, int whence) {
    if (fd != 1 || !test_file_open || whence != SEEK_SET || offset < 0 ||
        (uint32_t)offset > test_reported_size) {
        return VFS_EINVAL;
    }
    test_position = (uint32_t)offset;
    return VFS_OK;
}

void *kmalloc_debug(size_t size, const char *file, uint32_t line) {
    (void)file;
    (void)line;
    if (test_heap_used || size > TEST_HEAP_CAPACITY) {
        return NULL;
    }
    test_heap_used = true;
    test_heap_allocations++;
    return test_heap;
}

void kfree(void *pointer) {
    if (pointer == test_heap && test_heap_used) {
        test_heap_used = false;
        test_heap_frees++;
    }
}

bool process_external_image_claim(process_image_t *image) {
    test_claim_calls++;
    if (!test_claim_allowed || !image || image->base != 0u ||
        image->size != 0u || image->ownership != PROCESS_IMAGE_NONE ||
        image->lease_generation != 0u) {
        return false;
    }
    image->base = EXTERNAL_EXEC_ARENA_START;
    image->size = EXTERNAL_EXEC_ARENA_END - EXTERNAL_EXEC_ARENA_START;
    image->ownership = PROCESS_IMAGE_EXTERNAL_LEASE;
    image->lease_generation = 7u;
    return true;
}

void process_image_discard(process_image_t *image) {
    test_discard_calls++;
    if (image) {
        memset(image, 0, sizeof(*image));
    }
}

uint32_t process_create_with_arg_image_ex(void (*entry_point)(void),
                                          const char *name,
                                          uint32_t stack_size,
                                          uint32_t arg,
                                          process_domain_t domain,
                                          process_image_t *image) {
    (void)entry_point;
    (void)name;
    (void)stack_size;
    (void)arg;
    if (domain != PROCESS_DOMAIN_EXTERNAL || !image) {
        return 0u;
    }
    test_create_calls++;
    test_created_image = *image;
    if (!test_create_allowed) {
        return 0u;
    }
    memset(image, 0, sizeof(*image));
    return 77u;
}

uint32_t process_create_ex(void (*entry_point)(void), const char *name,
                           uint32_t stack_size, process_domain_t domain) {
    (void)entry_point;
    (void)name;
    (void)stack_size;
    (void)domain;
    return 77u;
}

void process_yield(void) {
    test_yield_calls++;
}

cupid_syscall_table_t *syscall_get_table(void) {
    return (cupid_syscall_table_t *)0x00123000u;
}

void serial_printf(const char *format, ...) {
    (void)format;
}

static bool arena_unchanged(uint32_t address) {
    const uint8_t *bytes = arena_shadow(address, PAGE_SIZE);
    if (!bytes) return false;
    return bytes[0] == 0xA5u && bytes[1] == 0xA5u &&
           bytes[PAGE_SIZE - 1u] == 0xA5u;
}

static int expect_rejected(int expected) {
    int result = elf_exec("/program", "program");
    if (result != expected) return 1;
    if (test_fixed_copy_calls != 0u) return 2;
    if (test_claim_calls != 0u || test_create_calls != 0u ||
        test_discard_calls != 0u || test_yield_calls != 0u) return 3;
    if (!arena_unchanged(EXTERNAL_EXEC_ARENA_START)) return 4;
    return 0;
}

static int run_valid_external(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;

    int result = elf_exec("/program", "program");
    const uint8_t *loaded = test_external_arena;
    if (result != 77) return 1;
    if (test_claim_calls != 1u || test_create_calls != 1u ||
        test_discard_calls != 0u || test_yield_calls != 1u) return 2;
    if (test_fixed_copy_calls != 1u || test_heap_allocations != 1u ||
        test_heap_frees != 1u || test_heap_used) return 3;
    if (test_created_image.base != EXTERNAL_EXEC_ARENA_START ||
        test_created_image.size !=
            EXTERNAL_EXEC_ARENA_END - EXTERNAL_EXEC_ARENA_START ||
        test_created_image.ownership != PROCESS_IMAGE_EXTERNAL_LEASE ||
        test_created_image.lease_generation != 7u) return 4;
    if (loaded[0] != 0x90u || loaded[1] != 0x90u ||
        loaded[2] != 0xC3u || loaded[3] != 0xCCu || loaded[4] != 0u) return 5;
    return 0;
}

static int run_valid_legacy_at(uint32_t address) {
    initialize_valid_elf(address);
    int status = prepare_arena(address);
    if (status != 0) return status;

    int result = elf_exec("/legacy", "legacy");
    if (result != 77 || test_claim_calls != 0u || test_create_calls != 1u ||
        test_fixed_copy_calls != 1u || test_yield_calls != 1u ||
        test_created_image.base != address ||
        test_created_image.size != PAGE_SIZE ||
        test_created_image.ownership != PROCESS_IMAGE_PERMANENT ||
        test_created_image.lease_generation != 0u) {
        return 1;
    }
    return 0;
}

static int run_valid_legacy(void) {
    int status = run_valid_legacy_at(CUPIDC_EXEC_ARENA_START);
    if (status != 0) return status;
    return run_valid_legacy_at(CUPIDASM_EXEC_ARENA_START);
}

static int run_busy_external(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    test_claim_allowed = false;

    int result = elf_exec("/busy", "busy");
    if (result != VFS_ENOSPC || test_claim_calls != 1u ||
        test_create_calls != 0u || test_fixed_copy_calls != 0u ||
        test_heap_allocations != 1u || test_heap_frees != 1u ||
        !arena_unchanged(EXTERNAL_EXEC_ARENA_START)) {
        return 1;
    }
    return 0;
}

static int run_truncated_table(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    test_reported_size = (uint32_t)sizeof(elf32_ehdr_t) +
                         2u * (uint32_t)sizeof(elf32_phdr_t);
    test_physical_size = (uint32_t)sizeof(elf32_ehdr_t) +
                         (uint32_t)sizeof(elf32_phdr_t);
    return expect_rejected(VFS_EIO);
}

static int run_truncated_segment(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    test_physical_size = test_reported_size - 2u;
    int result = expect_rejected(VFS_EIO);
    if (result != 0 || test_heap_allocations != 1u ||
        test_heap_frees != 1u || test_heap_used) {
        return 2;
    }
    return 0;
}

static int run_crossing_arena(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_END - PAGE_SIZE);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    test_programs()[0].p_memsz = PAGE_SIZE * 2u;
    return expect_rejected(VFS_EINVAL);
}

static int run_former_external_base(void) {
    initialize_valid_elf(TEST_OLD_EXTERNAL_BASE);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    status = prepare_arena(TEST_OLD_EXTERNAL_BASE);
    if (status != 0) return status;
    return expect_rejected(VFS_EINVAL);
}

static int run_overlapping_segments(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    elf32_phdr_t *programs = test_programs();
    programs[0].p_memsz = PAGE_SIZE * 2u;
    programs[1] = programs[0];
    programs[1].p_offset = TEST_PROGRAM_OFFSET * 2u;
    programs[1].p_vaddr = EXTERNAL_EXEC_ARENA_START + PAGE_SIZE;
    programs[1].p_paddr = programs[1].p_vaddr;
    test_file[TEST_PROGRAM_OFFSET * 2u] = 0xC3u;
    test_reported_size = TEST_PROGRAM_OFFSET * 2u + TEST_PROGRAM_BYTES;
    test_physical_size = test_reported_size;
    return expect_rejected(VFS_EINVAL);
}

static int run_bad_alignment(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    test_programs()[0].p_align = 3u;
    return expect_rejected(VFS_EINVAL);
}

static int run_bad_congruence(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    test_programs()[0].p_offset = TEST_PROGRAM_OFFSET + TEST_PROGRAM_BYTES;
    test_file[TEST_PROGRAM_OFFSET + TEST_PROGRAM_BYTES] = 0x90u;
    test_file[TEST_PROGRAM_OFFSET + TEST_PROGRAM_BYTES + 1u] = 0x90u;
    test_file[TEST_PROGRAM_OFFSET + TEST_PROGRAM_BYTES + 2u] = 0xC3u;
    test_file[TEST_PROGRAM_OFFSET + TEST_PROGRAM_BYTES + 3u] = 0xCCu;
    test_reported_size += TEST_PROGRAM_BYTES;
    test_physical_size = test_reported_size;
    return expect_rejected(VFS_EINVAL);
}

static int run_entry_in_bss(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    test_header()->e_entry = EXTERNAL_EXEC_ARENA_START + TEST_PROGRAM_BYTES;
    return expect_rejected(VFS_EINVAL);
}

static int run_entry_non_executable(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    test_programs()[0].p_flags = TEST_PF_R;
    return expect_rejected(VFS_EINVAL);
}

static int run_unsupported_type(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    test_programs()[1].p_type = TEST_PT_INTERP;
    return expect_rejected(VFS_EINVAL);
}

static int run_unknown_flags(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    test_programs()[0].p_flags |= 0x8u;
    return expect_rejected(VFS_EINVAL);
}

static int run_bad_ident_version(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    test_header()->e_ident[6] = 0u;
    return expect_rejected(VFS_EINVAL);
}

static int run_bad_header_version(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    test_header()->e_version = 0u;
    return expect_rejected(VFS_EINVAL);
}

static int run_process_create_failure(void) {
    initialize_valid_elf(EXTERNAL_EXEC_ARENA_START);
    int status = prepare_arena(EXTERNAL_EXEC_ARENA_START);
    if (status != 0) return status;
    test_create_allowed = false;

    int result = elf_exec("/create-failure", "create-failure");
    if (result != VFS_EIO || test_claim_calls != 1u ||
        test_create_calls != 1u || test_discard_calls != 1u ||
        test_yield_calls != 0u || test_fixed_copy_calls != 1u ||
        test_heap_allocations != 1u || test_heap_frees != 1u ||
        test_heap_used || test_created_image.ownership !=
                              PROCESS_IMAGE_EXTERNAL_LEASE ||
        test_external_arena[0] != 0x90u ||
        test_external_arena[2] != 0xC3u) {
        return 1;
    }
    return 0;
}

static int __attribute__((used)) contract_main(int argc, char **argv) {
    if (argc != 2) return 64;
    if (strings_equal(argv[1], "valid-external")) return run_valid_external();
    if (strings_equal(argv[1], "valid-legacy")) return run_valid_legacy();
    if (strings_equal(argv[1], "busy-external")) return run_busy_external();
    if (strings_equal(argv[1], "truncated-table")) return run_truncated_table();
    if (strings_equal(argv[1], "truncated-segment")) return run_truncated_segment();
    if (strings_equal(argv[1], "crossing-arena")) return run_crossing_arena();
    if (strings_equal(argv[1], "former-external-base")) {
        return run_former_external_base();
    }
    if (strings_equal(argv[1], "overlapping-segments")) {
        return run_overlapping_segments();
    }
    if (strings_equal(argv[1], "bad-alignment")) return run_bad_alignment();
    if (strings_equal(argv[1], "bad-congruence")) return run_bad_congruence();
    if (strings_equal(argv[1], "entry-in-bss")) return run_entry_in_bss();
    if (strings_equal(argv[1], "entry-non-executable")) {
        return run_entry_non_executable();
    }
    if (strings_equal(argv[1], "unsupported-type")) return run_unsupported_type();
    if (strings_equal(argv[1], "unknown-flags")) return run_unknown_flags();
    if (strings_equal(argv[1], "bad-ident-version")) {
        return run_bad_ident_version();
    }
    if (strings_equal(argv[1], "bad-header-version")) {
        return run_bad_header_version();
    }
    if (strings_equal(argv[1], "process-create-failure")) {
        return run_process_create_failure();
    }
    return 65;
}

#if defined(_WIN32)
int main(int argc, char **argv);

int main(int argc, char **argv) {
    return contract_main(argc, argv);
}
#else
void _start(void) __attribute__((naked, noreturn));

void _start(void) {
    __asm__("movl (%esp), %eax\n\t"
            "leal 4(%esp), %edx\n\t"
            "pushl %edx\n\t"
            "pushl %eax\n\t"
            "call contract_main\n\t"
            "movl %eax, %ebx\n\t"
            "movl $1, %eax\n\t"
            "int $0x80\n\t");
}
#endif
