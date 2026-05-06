/**
 * syscall.c - Kernel syscall table implementation for CupidOS
 *
 * Populates the global function-pointer table that is passed to ELF
 * programs at launch.  Each slot in the table points to the real
 * kernel function so user programs can call kernel services without
 * linking against the kernel binary.
 */

#include "syscall.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"
#include "../drivers/ata.h"
#include "../drivers/pit.h"
#include "../drivers/speaker.h"
#include "exec.h"
#include "kernel.h"
#include "memory.h"
#include "process.h"
#include "shell.h"
#include "string.h"
#include "vfs.h"
#include "vfs_helpers.h"
#include "ports.h"
#include "net_if.h"
#include "arp.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "dns.h"
#include "socket.h"
#include "blockdev.h"
#include "pci.h"
#include "lapic.h"
#include "bkl.h"


/* Wrappers for functions that need adaptation */

/**
 * Wrapper for kmalloc - strips the debug file/line tracking since
 * user programs don't have kernel source paths.
 */
static void *syscall_malloc(size_t size) {
  return kmalloc_debug(size, "elf_user", 0);
}

/**
 * Wrapper for process_exit - matches the void(*)(void) signature.
 */
static void syscall_exit(void) { process_exit(); }

/**
 * Wrapper for process_yield.
 */
static void syscall_yield(void) { process_yield(); }

/**
 * Wrapper for process_get_current_pid.
 */
static uint32_t syscall_getpid(void) { return process_get_current_pid(); }

/**
 * Wrapper for process_kill.
 */
static void syscall_kill(uint32_t pid) { process_kill(pid); }

/**
 * Simple busy-wait sleep using uptime.
 */
static void syscall_sleep_ms(uint32_t ms) {
  uint32_t start = timer_get_uptime_ms();
  while ((timer_get_uptime_ms() - start) < ms) {
    process_yield();
  }
}

/**
 * Wrapper for shell_execute_line.
 */
static void syscall_shell_execute(const char *line) {
  shell_execute_line(line);
}

/**
 * Wrapper for exec.
 */
static int syscall_exec(const char *path, const char *name) {
  return exec(path, name);
}

/* ── Phase 4 wrappers (parity with cupidc.c) ───────────────────────── */
static uint32_t sc_net_get_ip(void) {
  net_if_t *n = net_if_primary();
  return n ? n->ipv4_addr : 0u;
}
static uint32_t sc_net_get_gateway(void) {
  net_if_t *n = net_if_primary();
  return n ? n->ipv4_gateway : 0u;
}
static uint32_t sc_net_get_dns(void) {
  net_if_t *n = net_if_primary();
  return n ? n->ipv4_dns : 0u;
}
static uint32_t sc_net_get_mask(void) {
  net_if_t *n = net_if_primary();
  return n ? n->ipv4_mask : 0u;
}
static void sc_net_get_mac(uint8_t *out) {
  net_if_t *n = net_if_primary();
  int i;
  if (!out) return;
  if (!n) { for (i = 0; i < 6; i++) out[i] = 0u; return; }
  for (i = 0; i < 6; i++) out[i] = n->mac[i];
}
static uint32_t sc_net_link_up(void) {
  net_if_t *n = net_if_primary();
  return (n && n->link_up) ? 1u : 0u;
}
static uint32_t sc_net_rx_packets(void) {
  net_if_t *n = net_if_primary();
  return n ? (uint32_t)n->rx_packets : 0u;
}
static uint32_t sc_net_tx_packets(void) {
  net_if_t *n = net_if_primary();
  return n ? (uint32_t)n->tx_packets : 0u;
}
static uint32_t sc_net_rx_drops(void) {
  net_if_t *n = net_if_primary();
  return n ? (uint32_t)n->rx_drops : 0u;
}
static uint32_t sc_net_tx_errors(void) {
  net_if_t *n = net_if_primary();
  return n ? (uint32_t)n->tx_errors : 0u;
}
static int sc_blkdev_read(int idx, uint32_t lba, uint32_t count, void *buf) {
  block_device_t *d = blkdev_get(idx);
  return d ? blkdev_read(d, lba, count, buf) : -1;
}
static int sc_blkdev_write(int idx, uint32_t lba, uint32_t count, const void *buf) {
  block_device_t *d = blkdev_get(idx);
  return d ? blkdev_write(d, lba, count, buf) : -1;
}
static uint32_t sc_pci_vendor(int idx) {
  pci_device_t *d = pci_get_device(idx);
  return d ? d->vendor_id : 0u;
}
static uint32_t sc_pci_device_id(int idx) {
  pci_device_t *d = pci_get_device(idx);
  return d ? d->device_id : 0u;
}
static uint32_t sc_pci_class(int idx) {
  pci_device_t *d = pci_get_device(idx);
  if (!d) return 0u;
  return ((uint32_t)d->class_code << 16) |
         ((uint32_t)d->subclass   <<  8) |
          (uint32_t)d->prog_if;
}
static uint32_t sc_pci_irq(int idx) {
  pci_device_t *d = pci_get_device(idx);
  return d ? (uint32_t)d->irq_line : 0u;
}
static uint32_t sc_pci_bar(int idx, int bar) {
  pci_device_t *d = pci_get_device(idx);
  if (!d || bar < 0 || bar >= 6) return 0u;
  return d->bars[bar];
}

static cupid_syscall_table_t syscall_table;

/* Static asserts: AOT asm programs hard-code these offsets via SYS_*
 * equ constants in as.c. If anyone reorders cupid_syscall_table_t these
 * fail at compile time so we never silently ship a mismatched layout. */
#define SC_OFF(field) __builtin_offsetof(cupid_syscall_table_t, field)
_Static_assert(SC_OFF(print)               ==   8, "SYS_PRINT offset drift");
_Static_assert(SC_OFF(memstats)            == 152, "SYS_MEMSTATS offset drift");
_Static_assert(SC_OFF(net_get_ip)          == 156, "Phase 4: SYS_NET_GET_IP drift");
_Static_assert(SC_OFF(ipv4_send)           == 200, "Phase 4: SYS_IPV4_SEND drift");
_Static_assert(SC_OFF(sock_socket)         == 248, "Phase 4: SYS_SOCKET drift");
_Static_assert(SC_OFF(blkdev_count)        == 288, "Phase 4: SYS_BLKDEV_COUNT drift");
_Static_assert(SC_OFF(pci_device_count)    == 340, "Phase 4: SYS_PCI_DEVICE_COUNT drift");
_Static_assert(SC_OFF(inb_io)              == 396, "Phase 4: SYS_INB drift (table size mismatch)");
#undef SC_OFF

void syscall_init(void) {
  memset(&syscall_table, 0, sizeof(syscall_table));

  /* Version / identification */
  syscall_table.version = CUPID_SYSCALL_VERSION;
  syscall_table.table_size = (uint32_t)sizeof(cupid_syscall_table_t);

  /* Console output */
  syscall_table.print = print;
  syscall_table.putchar = putchar;
  syscall_table.print_int = print_int;
  syscall_table.print_hex = print_hex;
  syscall_table.clear_screen = clear_screen;

  /* Memory management */
  syscall_table.malloc = syscall_malloc;
  syscall_table.free = kfree;

  /* String operations */
  syscall_table.strlen = strlen;
  syscall_table.strcmp = strcmp;
  syscall_table.strncmp = strncmp;
  syscall_table.memset = memset;
  syscall_table.memcpy = memcpy;

  /* VFS file operations */
  syscall_table.vfs_open = vfs_open;
  syscall_table.vfs_close = vfs_close;
  syscall_table.vfs_read = vfs_read;
  syscall_table.vfs_write = vfs_write;
  syscall_table.vfs_seek = vfs_seek;
  syscall_table.vfs_stat = vfs_stat;
  syscall_table.vfs_readdir = vfs_readdir;
  syscall_table.vfs_mkdir = vfs_mkdir;
  syscall_table.vfs_unlink = vfs_unlink;
  syscall_table.vfs_rename = vfs_rename;
  syscall_table.vfs_copy_file = vfs_copy_file;
  syscall_table.vfs_read_all = vfs_read_all;
  syscall_table.vfs_write_all = vfs_write_all;
  syscall_table.vfs_read_text = vfs_read_text;
  syscall_table.vfs_write_text = vfs_write_text;

  /* Process management */
  syscall_table.exit = syscall_exit;
  syscall_table.yield = syscall_yield;
  syscall_table.getpid = syscall_getpid;
  syscall_table.kill = syscall_kill;
  syscall_table.sleep_ms = syscall_sleep_ms;

  /* Shell integration */
  syscall_table.shell_execute = syscall_shell_execute;
  syscall_table.shell_get_cwd = shell_get_cwd;

  /* Time */
  syscall_table.uptime_ms = timer_get_uptime_ms;

  /* Program execution */
  syscall_table.exec = syscall_exec;

  /* Diagnostics */
  syscall_table.memstats = print_memory_stats;

  /* ── Phase 4: networking + drivers + low-level ──────────────────── */
  syscall_table.net_get_ip       = sc_net_get_ip;
  syscall_table.net_get_gateway  = sc_net_get_gateway;
  syscall_table.net_get_dns      = sc_net_get_dns;
  syscall_table.net_get_mask     = sc_net_get_mask;
  syscall_table.net_get_mac      = sc_net_get_mac;
  syscall_table.net_link_up      = sc_net_link_up;
  syscall_table.net_rx_packets   = sc_net_rx_packets;
  syscall_table.net_tx_packets   = sc_net_tx_packets;
  syscall_table.net_rx_drops     = sc_net_rx_drops;
  syscall_table.net_tx_errors    = sc_net_tx_errors;

  syscall_table.ip_parse         = ip_parse;
  syscall_table.ipv4_send        = ipv4_send;
  syscall_table.arp_resolve      = arp_resolve;
  syscall_table.arp_dump         = arp_dump;
  syscall_table.arp_get_entries  = arp_get_entries;
  syscall_table.icmp_send_echo   = icmp_send_echo;
  syscall_table.icmp_wait_reply  = icmp_wait_reply;
  syscall_table.udp_send_raw     = udp_send_raw;

  syscall_table.dns_resolve      = dns_resolve;
  syscall_table.htons            = htons;
  syscall_table.htonl            = htonl;
  syscall_table.ntohs            = htons;   /* same byte-swap on LE */
  syscall_table.ntohl            = htonl;

  syscall_table.sock_socket      = socket_create;
  syscall_table.sock_bind        = socket_bind;
  syscall_table.sock_listen      = socket_listen;
  syscall_table.sock_accept      = socket_accept;
  syscall_table.sock_connect     = socket_connect;
  syscall_table.sock_send        = socket_send;
  syscall_table.sock_recv        = socket_recv;
  syscall_table.sock_sendto      = socket_sendto;
  syscall_table.sock_recvfrom    = socket_recvfrom;
  syscall_table.sock_close       = socket_close;

  syscall_table.blkdev_count       = blkdev_count;
  syscall_table.blkdev_read        = sc_blkdev_read;
  syscall_table.blkdev_write       = sc_blkdev_write;
  syscall_table.ata_read_sectors   = ata_read_sectors;
  syscall_table.ata_write_sectors  = ata_write_sectors;

  syscall_table.serial_read_char    = serial_read_char;
  syscall_table.serial_write_char   = serial_write_char;
  syscall_table.serial_write_string = serial_write_string;
  syscall_table.serial_has_rx       = serial_has_rx;

  syscall_table.pc_speaker_on     = pc_speaker_on;
  syscall_table.pc_speaker_off    = pc_speaker_off;
  syscall_table.pit_set_frequency = pit_set_frequency;
  syscall_table.timer_delay_us    = timer_delay_us;

  syscall_table.pci_device_count = pci_device_count;
  syscall_table.pci_get_vendor   = sc_pci_vendor;
  syscall_table.pci_get_device_id= sc_pci_device_id;
  syscall_table.pci_get_class    = sc_pci_class;
  syscall_table.pci_get_irq      = sc_pci_irq;
  syscall_table.pci_get_bar      = sc_pci_bar;

  syscall_table.lapic_get_id     = lapic_get_id;
  syscall_table.lapic_eoi        = lapic_eoi;
  syscall_table.bkl_lock         = bkl_lock;
  syscall_table.bkl_unlock       = bkl_unlock;

  syscall_table.paging_map_mmio  = paging_map_mmio;
  syscall_table.pmm_alloc_page   = pmm_alloc_page;
  syscall_table.pmm_free_page    = pmm_free_page;

  syscall_table.outb_io          = outb;
  syscall_table.inb_io           = inb;

  syscall_table.sock_setsockopt  = socket_setsockopt;

  serial_printf("[SYSCALL] Syscall table initialized (v%u, %u bytes)\n",
                syscall_table.version, syscall_table.table_size);
}

cupid_syscall_table_t *syscall_get_table(void) { return &syscall_table; }
