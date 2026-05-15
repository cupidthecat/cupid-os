/**
 * cupid.h - CupidOS User-Space API Header
 *
 * Include this header in ELF programs compiled for CupidOS.
 * It provides access to all kernel services through the syscall
 * table that the kernel passes to your _start() function.
 *
 * ── Quick Start ──────────────────────────────────────────────────
 *
 *   #include "cupid.h"
 *
 *   void _start(cupid_syscall_table_t *sys) {
 *       cupid_init(sys);
 *       print("Hello from ELF!\n");
 *       exit();
 *   }
 *
 * ── Compiling ────────────────────────────────────────────────────
 *
 *   gcc -m32 -fno-pie -nostdlib -static -ffreestanding -O2 \
 *       -I/path/to/cupid-os/user -c hello.c -o hello.o
 *   ld -m elf_i386 -Ttext=0x00400000 --oformat=elf32-i386 \
 *       -o hello hello.o
 *
 * ── Running in CupidOS ──────────────────────────────────────────
 *
 *   exec /home/hello
 *
 * ─────────────────────────────────────────────────────────────────
 */

#ifndef CUPID_H
#define CUPID_H

/* ── Base types (no libc available) ───────────────────────────────── */
#ifndef NULL
#define NULL ((void *)0)
#endif

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;
typedef unsigned long      size_t;

/* ── VFS constants ────────────────────────────────────────────────── */
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0100
#define O_TRUNC    0x0200
#define O_APPEND   0x0400

#define SEEK_SET   0
#define SEEK_CUR   1
#define SEEK_END   2

#define VFS_TYPE_FILE   0
#define VFS_TYPE_DIR    1
#define VFS_TYPE_DEV    2

#define VFS_MAX_NAME    64
#define VFS_MAX_PATH    128

/* ── Network constants ────────────────────────────────────────────── */
#define SOCK_UDP       1
#define SOCK_TCP       2
#define SOL_TLS        1
#define TLS_ENABLE     1

#define TCPS_CLOSED       0
#define TCPS_LISTEN       1
#define TCPS_SYN_SENT     2
#define TCPS_SYN_RCVD     3
#define TCPS_ESTABLISHED  4
#define TCPS_FIN_WAIT_1   5
#define TCPS_FIN_WAIT_2   6
#define TCPS_TIME_WAIT    7
#define TCPS_CLOSE_WAIT   8
#define TCPS_LAST_ACK     9

/* ── VFS structures (must match kernel definitions) ───────────────── */
typedef struct {
    char     name[VFS_MAX_NAME];
    uint32_t size;
    uint8_t  type;
} cupid_dirent_t;

typedef struct {
    uint32_t size;
    uint8_t  type;
} cupid_stat_t;

/* ── Syscall table (passed to _start) ─────────────────────────────── */
typedef struct cupid_syscall_table {
    uint32_t version;
    uint32_t table_size;

    void (*print)(const char *str);
    void (*putchar)(char c);
    void (*print_int)(uint32_t num);
    void (*print_hex)(uint32_t num);
    void (*clear_screen)(void);

    /* Memory management */
    void *(*malloc)(size_t size);
    void  (*free)(void *ptr);

    /* String operations */
    size_t (*strlen)(const char *s);
    int    (*strcmp)(const char *a, const char *b);
    int    (*strncmp)(const char *a, const char *b, size_t n);
    void  *(*memset)(void *ptr, int value, size_t num);
    void  *(*memcpy)(void *dest, const void *src, size_t n);

    /* VFS file operations */
    int (*vfs_open)(const char *path, uint32_t flags);
    int (*vfs_close)(int fd);
    int (*vfs_read)(int fd, void *buffer, uint32_t count);
    int (*vfs_write)(int fd, const void *buffer, uint32_t count);
    int (*vfs_seek)(int fd, int32_t offset, int whence);
    int (*vfs_stat)(const char *path, cupid_stat_t *st);
    int (*vfs_readdir)(int fd, cupid_dirent_t *dirent);
    int (*vfs_mkdir)(const char *path);
    int (*vfs_unlink)(const char *path);
    int (*vfs_rename)(const char *old_path, const char *new_path);
    int (*vfs_copy_file)(const char *src, const char *dest);
    int (*vfs_read_all)(const char *path, void *buffer, uint32_t max_size);
    int (*vfs_write_all)(const char *path, const void *buffer, uint32_t size);
    int (*vfs_read_text)(const char *path, char *buffer, uint32_t max_size);
    int (*vfs_write_text)(const char *path, const char *text);

    void     (*exit)(void);
    void     (*yield)(void);
    uint32_t (*getpid)(void);
    void     (*kill)(uint32_t pid);
    void     (*sleep_ms)(uint32_t ms);

    /* Shell integration */
    void (*shell_execute)(const char *line);
    const char *(*shell_get_cwd)(void);

    /* Time */
    uint32_t (*uptime_ms)(void);

    int (*exec)(const char *path, const char *name);
    void (*memstats)(void);

    uint32_t (*net_get_ip)(void);
    uint32_t (*net_get_gateway)(void);
    uint32_t (*net_get_dns)(void);
    uint32_t (*net_get_mask)(void);
    void     (*net_get_mac)(uint8_t *out);
    uint32_t (*net_link_up)(void);
    uint32_t (*net_rx_packets)(void);
    uint32_t (*net_tx_packets)(void);
    uint32_t (*net_rx_drops)(void);
    uint32_t (*net_tx_errors)(void);

    int  (*ip_parse)(const char *s, uint32_t *out);
    int  (*ipv4_send)(uint32_t dst_ip, uint8_t proto,
                      const uint8_t *payload, uint32_t plen);
    int  (*arp_resolve)(uint32_t ip, uint8_t mac_out[6]);
    void (*arp_dump)(void);
    int  (*arp_get_entries)(uint32_t *ips, uint8_t macs[][6], int max);
    int  (*icmp_send_echo)(uint32_t dst_ip, uint16_t id, uint16_t seq,
                           uint32_t paylen);
    int  (*icmp_wait_reply)(uint32_t expected_src, uint16_t id, uint16_t seq,
                            uint32_t timeout_ms);
    int  (*udp_send_raw)(uint32_t dst_ip, uint16_t src_port,
                         uint16_t dst_port, const uint8_t *data, uint32_t len);

    int      (*dns_resolve)(const char *name, uint32_t *ip_out);
    uint16_t (*htons)(uint16_t v);
    uint32_t (*htonl)(uint32_t v);
    uint16_t (*ntohs)(uint16_t v);
    uint32_t (*ntohl)(uint32_t v);

    int (*sock_socket)(int type);
    int (*sock_bind)(int fd, uint32_t ip, uint16_t port);
    int (*sock_listen)(int fd, int backlog);
    int (*sock_accept)(int fd, uint32_t *peer_ip, uint16_t *peer_port);
    int (*sock_connect)(int fd, uint32_t ip, uint16_t port);
    int (*sock_send)(int fd, const void *buf, uint32_t len);
    int (*sock_recv)(int fd, void *buf, uint32_t len);
    int (*sock_sendto)(int fd, const void *buf, uint32_t len,
                       uint32_t ip, uint16_t port);
    int (*sock_recvfrom)(int fd, void *buf, uint32_t len,
                         uint32_t *ip, uint16_t *port);
    int (*sock_close)(int fd);

    int (*blkdev_count)(void);
    int (*blkdev_read)(int idx, uint32_t lba, uint32_t count, void *buf);
    int (*blkdev_write)(int idx, uint32_t lba, uint32_t count,
                        const void *buf);
    int (*ata_read_sectors)(uint8_t drive, uint32_t lba, uint8_t count,
                            void *buf);
    int (*ata_write_sectors)(uint8_t drive, uint32_t lba, uint8_t count,
                             const void *buf);

    int  (*serial_read_char)(void);
    void (*serial_write_char)(char c);
    void (*serial_write_string)(const char *str);
    int  (*serial_has_rx)(void);

    void (*pc_speaker_on)(uint32_t freq);
    void (*pc_speaker_off)(void);
    void (*pit_set_frequency)(uint32_t channel, uint32_t freq);
    void (*timer_delay_us)(uint32_t us);

    int      (*pci_device_count)(void);
    uint32_t (*pci_get_vendor)(int idx);
    uint32_t (*pci_get_device_id)(int idx);
    uint32_t (*pci_get_class)(int idx);
    uint32_t (*pci_get_irq)(int idx);
    uint32_t (*pci_get_bar)(int idx, int bar);

    uint8_t  (*lapic_get_id)(void);
    void     (*lapic_eoi)(void);
    void     (*bkl_lock)(void);
    void     (*bkl_unlock)(void);

    void  (*paging_map_mmio)(uint32_t phys, uint32_t size);
    void *(*pmm_alloc_page)(void);
    void  (*pmm_free_page)(void *page);

    void    (*outb_io)(uint16_t port, uint8_t v);
    uint8_t (*inb_io)(uint16_t port);

    int (*sock_setsockopt)(int fd, int level, int optname,
                           const void *val, uint32_t vlen);
    int (*sock_avail)(int fd);
    int (*sock_state)(int fd);
} cupid_syscall_table_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Global syscall table pointer - set by cupid_init()
 * ══════════════════════════════════════════════════════════════════════ */
static cupid_syscall_table_t *__sys = NULL;

/**
 * cupid_init - Initialize the userspace API.
 * Call this at the start of _start() with the table pointer.
 */
static inline void cupid_init(cupid_syscall_table_t *sys) {
    __sys = sys;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Convenience wrappers - call these after cupid_init()
 * ══════════════════════════════════════════════════════════════════════ */

/* ── Console I/O ──────────────────────────────────────────────────── */
static inline void print(const char *s)     { __sys->print(s); }
static inline void putchar(char c)          { __sys->putchar(c); }
static inline void print_int(uint32_t n)    { __sys->print_int(n); }
static inline void print_hex(uint32_t n)    { __sys->print_hex(n); }
static inline void clear_screen(void)       { __sys->clear_screen(); }

/* ── Memory ───────────────────────────────────────────────────────── */
static inline void *malloc(size_t sz)       { return __sys->malloc(sz); }
static inline void  free(void *p)           { __sys->free(p); }

/* ── Strings ──────────────────────────────────────────────────────── */
static inline size_t strlen(const char *s)  { return __sys->strlen(s); }
static inline int strcmp(const char *a, const char *b)
    { return __sys->strcmp(a, b); }
static inline int strncmp(const char *a, const char *b, size_t n)
    { return __sys->strncmp(a, b, n); }
static inline void *memset(void *p, int v, size_t n)
    { return __sys->memset(p, v, n); }
static inline void *memcpy(void *d, const void *s, size_t n)
    { return __sys->memcpy(d, s, n); }

/* ── VFS ──────────────────────────────────────────────────────────── */
static inline int open(const char *path, uint32_t flags)
    { return __sys->vfs_open(path, flags); }
static inline int close(int fd)
    { return __sys->vfs_close(fd); }
static inline int read(int fd, void *buf, uint32_t count)
    { return __sys->vfs_read(fd, buf, count); }
static inline int write(int fd, const void *buf, uint32_t count)
    { return __sys->vfs_write(fd, buf, count); }
static inline int seek(int fd, int32_t off, int whence)
    { return __sys->vfs_seek(fd, off, whence); }
static inline int stat(const char *path, cupid_stat_t *st)
    { return __sys->vfs_stat(path, st); }
static inline int readdir(int fd, cupid_dirent_t *ent)
    { return __sys->vfs_readdir(fd, ent); }
static inline int mkdir(const char *path)
    { return __sys->vfs_mkdir(path); }
static inline int unlink(const char *path)
    { return __sys->vfs_unlink(path); }

/* ── Process ──────────────────────────────────────────────────────── */
static inline void exit(void)               { __sys->exit(); }
static inline void yield(void)              { __sys->yield(); }
static inline uint32_t getpid(void)         { return __sys->getpid(); }
static inline void kill(uint32_t pid)       { __sys->kill(pid); }
static inline void sleep_ms(uint32_t ms)    { __sys->sleep_ms(ms); }

/* ── Shell ────────────────────────────────────────────────────────── */
static inline void shell_execute(const char *line)
    { __sys->shell_execute(line); }
static inline const char *shell_get_cwd(void)
    { return __sys->shell_get_cwd(); }

/* ── Time ─────────────────────────────────────────────────────────── */
static inline uint32_t uptime_ms(void)      { return __sys->uptime_ms(); }

/* ── Program execution ────────────────────────────────────────────── */
static inline int exec_program(const char *path, const char *name)
    { return __sys->exec(path, name); }

/* ── Diagnostics ─────────────────────────────────────────────────── */
static inline void memstats(void)          { __sys->memstats(); }

/* ── Networking ──────────────────────────────────────────────────── */
static inline uint32_t net_get_ip(void)    { return __sys->net_get_ip(); }
static inline uint32_t net_get_gateway(void)
    { return __sys->net_get_gateway(); }
static inline uint32_t net_get_dns(void)   { return __sys->net_get_dns(); }
static inline uint32_t net_get_mask(void)  { return __sys->net_get_mask(); }
static inline void net_get_mac(uint8_t *out)
    { __sys->net_get_mac(out); }
static inline uint32_t net_link_up(void)   { return __sys->net_link_up(); }
static inline uint32_t net_rx_packets(void)
    { return __sys->net_rx_packets(); }
static inline uint32_t net_tx_packets(void)
    { return __sys->net_tx_packets(); }

static inline int dns_resolve(const char *name, uint32_t *ip_out)
    { return __sys->dns_resolve(name, ip_out); }
static inline uint16_t htons(uint16_t v)   { return __sys->htons(v); }
static inline uint32_t htonl(uint32_t v)   { return __sys->htonl(v); }
static inline uint16_t ntohs(uint16_t v)   { return __sys->ntohs(v); }
static inline uint32_t ntohl(uint32_t v)   { return __sys->ntohl(v); }

static inline int sock_socket(int type)
    { return __sys->sock_socket(type); }
static inline int sock_connect(int fd, uint32_t ip, uint16_t port)
    { return __sys->sock_connect(fd, ip, port); }
static inline int sock_send(int fd, const void *buf, uint32_t len)
    { return __sys->sock_send(fd, buf, len); }
static inline int sock_recv(int fd, void *buf, uint32_t len)
    { return __sys->sock_recv(fd, buf, len); }
static inline int sock_close(int fd)
    { return __sys->sock_close(fd); }
static inline int sock_avail(int fd)
    { return __sys->sock_avail(fd); }
static inline int sock_state(int fd)
    { return __sys->sock_state(fd); }
static inline int sock_setsockopt(int fd, int level, int optname,
                                  const void *val, uint32_t vlen)
    { return __sys->sock_setsockopt(fd, level, optname, val, vlen); }

#endif /* CUPID_H */
