/**
 * syscall.h - Kernel syscall table for CupidOS ELF programs
 *
 * Provides a function-pointer table that the ELF loader passes to
 * user programs.  Since CupidOS runs everything in ring 0 with a
 * flat address space (TempleOS-style), there is no privilege boundary
 * - the table simply gives ELF programs clean access to kernel APIs
 * without needing to know fixed addresses.
 *
 * Usage from an ELF program:
 *   void _start(cupid_syscall_table_t *sys) {
 *       sys->print("Hello from ELF!\n");
 *       sys->exit();
 *   }
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "vfs.h"

/* Bumped to 4 for the TLS additions: sock_setsockopt is appended at the
 * end of the table. Programs built against v3 still see the same prefix
 * and only the new field is invisible to them. */
#define CUPID_SYSCALL_VERSION 4

typedef struct cupid_syscall_table {
  /*  Version / identification  */
  uint32_t version;    /* CUPID_SYSCALL_VERSION             */
  uint32_t table_size; /* sizeof(cupid_syscall_table_t)     */

  /*  Console output  */
  void (*print)(const char *str);
  void (*putchar)(char c);
  void (*print_int)(uint32_t num);
  void (*print_hex)(uint32_t num);
  void (*clear_screen)(void);

  /*  Memory management  */
  void *(*malloc)(size_t size);
  void (*free)(void *ptr);

  /*  String operations  */
  size_t (*strlen)(const char *s);
  int (*strcmp)(const char *a, const char *b);
  int (*strncmp)(const char *a, const char *b, size_t n);
  void *(*memset)(void *ptr, int value, size_t num);
  void *(*memcpy)(void *dest, const void *src, size_t n);

  /*  VFS file operations  */
  int (*vfs_open)(const char *path, uint32_t flags);
  int (*vfs_close)(int fd);
  int (*vfs_read)(int fd, void *buffer, uint32_t count);
  int (*vfs_write)(int fd, const void *buffer, uint32_t count);
  int (*vfs_seek)(int fd, int32_t offset, int whence);
  int (*vfs_stat)(const char *path, vfs_stat_t *st);
  int (*vfs_readdir)(int fd, vfs_dirent_t *dirent);
  int (*vfs_mkdir)(const char *path);
  int (*vfs_unlink)(const char *path);
  int (*vfs_rename)(const char *old_path, const char *new_path);
  int (*vfs_copy_file)(const char *src, const char *dest);
  int (*vfs_read_all)(const char *path, void *buffer, uint32_t max_size);
  int (*vfs_write_all)(const char *path, const void *buffer, uint32_t size);
  int (*vfs_read_text)(const char *path, char *buffer, uint32_t max_size);
  int (*vfs_write_text)(const char *path, const char *text);

  /*  Process management  */
  void (*exit)(void);
  void (*yield)(void);
  uint32_t (*getpid)(void);
  void (*kill)(uint32_t pid);
  void (*sleep_ms)(uint32_t ms);

  /*  Shell integration  */
  void (*shell_execute)(const char *line);
  const char *(*shell_get_cwd)(void);

  /*  Time  */
  uint32_t (*uptime_ms)(void);

  /*  Program execution  */
  int (*exec)(const char *path, const char *name);

  /*  Diagnostics  */
  void (*memstats)(void);

  /*    * Phase 4 - networking + drivers + low-level access.
   *
   * Append-only: do not reorder/remove fields above this line, so
   * older programs still find their slots at the offsets they expect.
   * Pointer types match the kernel APIs they wrap; opaque structs
   * (block_device_t, pci_device_t, net_if_t) are exposed as `void *`
   * here so consumers don't need the matching headers.
   *  */

  /* Network interface info (primary NIC) */
  uint32_t (*net_get_ip)(void);
  uint32_t (*net_get_gateway)(void);
  uint32_t (*net_get_dns)(void);
  uint32_t (*net_get_mask)(void);
  void     (*net_get_mac)(uint8_t *out);          /* fills 6 bytes */
  uint32_t (*net_link_up)(void);                  /* 1=up, 0=down  */
  uint32_t (*net_rx_packets)(void);
  uint32_t (*net_tx_packets)(void);
  uint32_t (*net_rx_drops)(void);
  uint32_t (*net_tx_errors)(void);

  /* IP / ARP / ICMP / UDP raw access */
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

  /* DNS + byte-order helpers */
  int      (*dns_resolve)(const char *name, uint32_t *ip_out);
  uint16_t (*htons)(uint16_t v);
  uint32_t (*htonl)(uint32_t v);
  uint16_t (*ntohs)(uint16_t v);
  uint32_t (*ntohl)(uint32_t v);

  /* BSD sockets */
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

  /* Block devices (ATA / loopdev / USB-MSC) */
  int (*blkdev_count)(void);
  int (*blkdev_read)(int idx, uint32_t lba, uint32_t count, void *buf);
  int (*blkdev_write)(int idx, uint32_t lba, uint32_t count, const void *buf);
  int (*ata_read_sectors)(uint8_t drive, uint32_t lba, uint8_t count,
                          void *buf);
  int (*ata_write_sectors)(uint8_t drive, uint32_t lba, uint8_t count,
                           const void *buf);

  /* Serial direct */
  int  (*serial_read_char)(void);
  void (*serial_write_char)(char c);
  void (*serial_write_string)(const char *str);
  int  (*serial_has_rx)(void);

  /* Speaker / PIT */
  void (*pc_speaker_on)(uint32_t freq);
  void (*pc_speaker_off)(void);
  void (*pit_set_frequency)(uint32_t channel, uint32_t freq);
  void (*timer_delay_us)(uint32_t us);

  /* PCI introspection (by index) */
  int      (*pci_device_count)(void);
  uint32_t (*pci_get_vendor)(int idx);
  uint32_t (*pci_get_device_id)(int idx);
  uint32_t (*pci_get_class)(int idx);   /* class<<16 | sub<<8 | prog_if */
  uint32_t (*pci_get_irq)(int idx);
  uint32_t (*pci_get_bar)(int idx, int bar);

  /* SMP / LAPIC / BKL */
  uint8_t  (*lapic_get_id)(void);
  void     (*lapic_eoi)(void);
  void     (*bkl_lock)(void);
  void     (*bkl_unlock)(void);

  /* Memory / paging */
  void  (*paging_map_mmio)(uint32_t phys, uint32_t size);
  void *(*pmm_alloc_page)(void);
  void  (*pmm_free_page)(void *page);

  /* Port I/O for driver authors */
  void    (*outb_io)(uint16_t port, uint8_t v);
  uint8_t (*inb_io)(uint16_t port);

  /* TLS - setsockopt(SOL_TLS, TLS_ENABLE, hostname, hostname_len) drives
   * a synchronous TLS 1.3 handshake over an already-connected TCP socket.
   * After it returns 0, sock_send/sock_recv route through the record
   * layer transparently. */
  int (*sock_setsockopt)(int fd, int level, int optname,
                         const void *val, uint32_t vlen);

} cupid_syscall_table_t;


/**
 * syscall_init - Initialize the global syscall table.
 * Must be called during kernel boot after all subsystems are ready.
 */
void syscall_init(void);

/**
 * syscall_get_table - Get a pointer to the global syscall table.
 * The ELF loader passes this to user programs.
 */
cupid_syscall_table_t *syscall_get_table(void);

#endif /* SYSCALL_H */
