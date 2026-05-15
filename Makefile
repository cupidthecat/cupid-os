# Compiler settings
.SUFFIXES:
ASM=nasm
CC=gcc
.DEFAULT_GOAL := all
# NASA Power of 10 compliant flags: pedantic, warnings as errors, strict checks
EXTRA_CFLAGS ?=
KERNEL_INCLUDES=-I./kernel -I./kernel/audio -I./kernel/core -I./kernel/cpu \
                -I./kernel/crypto -I./kernel/doom -I./kernel/fs -I./kernel/gfx \
                -I./kernel/gui -I./kernel/lang -I./kernel/mm -I./kernel/network \
                -I./kernel/smp -I./kernel/tls -I./kernel/usb -I./kernel/util \
                -I./drivers
CFLAGS=-m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc -ffreestanding -c $(KERNEL_INCLUDES) \
	-mfpmath=sse -msse -msse2 -mstackrealign -fno-omit-frame-pointer \
       -DDEBUG -pedantic -Werror -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
       -Wmissing-prototypes -Wconversion -Wsign-conversion -Wwrite-strings $(EXTRA_CFLAGS)
# Relaxed flags for vendored / DOOM-tree code that won't pass our strict gates
CFLAGS_DOOM := -m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc \
               -ffreestanding -c $(KERNEL_INCLUDES) \
               -I./kernel/doom/src \
               -I./kernel/doom/src/include_stubs \
               -mfpmath=sse -msse -msse2 -mstackrealign -fno-omit-frame-pointer \
               -O2 -Wno-unused -Wno-unused-result \
               -Wno-implicit-function-declaration \
               -Wno-sign-compare -Wno-strict-prototypes \
               -Wno-unused-parameter
# DOOM source tree flags — extends CFLAGS_DOOM with the dglibc_compat.h alias
# header and extra suppressions needed for the DOOM upstream source files.
CFLAGS_DOOM_TREE := $(CFLAGS_DOOM) \
               -include kernel/doom/dglibc_compat.h \
               -Wno-unused-variable -Wno-type-limits \
               -Wno-missing-field-initializers \
               -DDEFAULT_SAVEGAMEDIR=\"/home/doom/\" \
               -DDOOM_PORT_CUPIDOS=1
# Optimisation flags for rendering/computation-only files (no hw I/O or IRQs)
OPT=-O2
LDFLAGS=-m elf_i386 -T link.ld --oformat binary
# ELF link variant — used for the pass-1 kernel.elf so mksyms.sh can read symbols.
LDFLAGS_ELF=-m elf_i386 -T link.ld

# Auto-discover all CupidC programs in bin/.
# Exclude legacy cc2-bootstrap fixtures (old_cc2*) — they're superseded
# by the production CupidC compiler and embed ~265 KB of fixture text
# into the kernel binary, eating the bootloader's reserved kernel-area.
BIN_CC_SRCS := $(filter-out bin/old_cc2.cc bin/old_cc2_single.cc, $(wildcard bin/*.cc))
$(info BIN_CC_SRCS=$(BIN_CC_SRCS))
BIN_CC_OBJS := $(BIN_CC_SRCS:.cc=.o)
BIN_CC_NAMES := $(notdir $(basename $(BIN_CC_SRCS)))

# Auto-discover embeddable headers used by CupidC demos/programs
BIN_HDR_SRCS := $(wildcard bin/*.h)
$(info BIN_HDR_SRCS=$(BIN_HDR_SRCS))
BIN_HDR_OBJS := $(BIN_HDR_SRCS:.h=.h.o)
BIN_HDR_NAMES := $(notdir $(basename $(BIN_HDR_SRCS)))

# Auto-discover browser library sub-files (bin/browser/*.cc).
# These are #include'd by bin/browser.cc and embedded in ramfs at
# /bin/browser/<n>.cc so the CupidC preprocessor can resolve them
# at JIT time. They are NOT runnable programs (not added to BIN_CC_NAMES).
BROWSER_SUB_SRCS := $(wildcard bin/browser/*.cc)
$(info BROWSER_SUB_SRCS=$(BROWSER_SUB_SRCS))
BROWSER_SUB_OBJS := $(BROWSER_SUB_SRCS:.cc=.o)
BROWSER_SUB_NAMES := $(notdir $(basename $(BROWSER_SUB_SRCS)))

# Auto-discover CupidDoc files to embed at boot (/docs/*.ctxt in ramfs)
DOC_CTXT_SRCS := $(wildcard cupidos-txt/*.CTXT)
$(info DOC_CTXT_SRCS=$(DOC_CTXT_SRCS))
DOC_CTXT_OBJS := $(DOC_CTXT_SRCS:.CTXT=.o)
DOC_CTXT_NAMES := $(notdir $(basename $(DOC_CTXT_SRCS)))

# Explicit doc assets embedded alongside the manuals
DOC_ASSET_SRCS := image.bmp
$(info DOC_ASSET_SRCS=$(DOC_ASSET_SRCS))
DOC_ASSET_OBJS := $(DOC_ASSET_SRCS:.bmp=.bmp.o)
DOC_ASSET_NAMES := $(notdir $(basename $(DOC_ASSET_SRCS)))

# Auto-discover CupidASM demos to embed at boot (/demos/*.asm in ramfs)
DEMO_ASM_SRCS := $(wildcard demos/*.asm)
$(info DEMO_ASM_SRCS=$(DEMO_ASM_SRCS))
DEMO_ASM_OBJS := $(DEMO_ASM_SRCS:.asm=.o)
DEMO_ASM_NAMES := $(notdir $(basename $(DEMO_ASM_SRCS)))

# TempleOS God vocabulary data (embedded at boot)
GOD_DD_SRCS := $(wildcard god/*.DD)
$(info GOD_DD_SRCS=$(GOD_DD_SRCS))
GOD_DD_OBJS := $(GOD_DD_SRCS:.DD=.o)

# System fonts. Bundled TTFs are embedded directly into the kernel
# binary so fontsys can register them at boot without depending on the
# filesystem coming up first.
FONT_TTF_SRCS := $(wildcard system/fonts/*.ttf)
$(info FONT_TTF_SRCS=$(FONT_TTF_SRCS))
FONT_TTF_OBJS := $(FONT_TTF_SRCS:.ttf=.ttf.o)

# Files
BOOTLOADER=boot/boot.bin
KERNEL=kernel/kernel.bin
OS_IMAGE=cupidos.img
QEMU_AUDIODEV ?= alsa,id=speaker
HDD_MB ?= 200
FAT_START_LBA ?= 16384
OS_IMAGE_SECTORS := $(shell expr $(HDD_MB) \* 1024 \* 1024 / 512)
FAT_BLOCKS := $(shell expr \( $(OS_IMAGE_SECTORS) - $(FAT_START_LBA) \) / 2)
FAT_OFFSET_BYTES := $(shell expr $(FAT_START_LBA) \* 512)
WAD_SRCS := $(wildcard /usr/share/games/doom/freedoom*.wad)
KERNEL_OBJS=kernel/core/kernel.o kernel/cpu/idt.o kernel/cpu/isr.o kernel/cpu/irq.o kernel/cpu/pic.o \
            kernel/fs/fs.o drivers/keyboard.o drivers/timer.o kernel/cpu/math.o drivers/pit.o \
            drivers/speaker.o kernel/lang/shell.o kernel/core/string.o kernel/mm/memory.o drivers/pci.o kernel/usb/usb.o kernel/usb/uhci.o kernel/usb/ehci.o kernel/usb/usb_hid.o kernel/usb/usb_hub.o kernel/usb/usb_msc.o \
            kernel/mm/paging.o drivers/ata.o kernel/fs/blockdev.o kernel/fs/blockcache.o kernel/fs/fat16.o \
            drivers/serial.o kernel/core/panic.o kernel/gui/ed.o \
            drivers/vga.o drivers/mouse.o kernel/gfx/font_8x8.o kernel/gfx/graphics.o \
			kernel/gui/gui.o kernel/gui/desktop.o kernel/core/app_launch.o kernel/core/process.o kernel/core/context_switch.o \
			kernel/gui/clipboard.o kernel/gui/ui.o \
			kernel/lang/godspeak.o \
			kernel/cpu/fpu.o kernel/cpu/libm.o \
            kernel/lang/cupidscript_lex.o kernel/lang/cupidscript_parse.o \
            kernel/lang/cupidscript_exec.o kernel/lang/cupidscript_runtime.o \
            kernel/lang/cupidscript_streams.o kernel/lang/cupidscript_strings.o \
            kernel/lang/cupidscript_arrays.o kernel/lang/cupidscript_jobs.o \
			kernel/gui/ansi.o \
			kernel/gui/terminal_app.o \
            kernel/fs/vfs.o kernel/fs/ramfs.o kernel/fs/devfs.o kernel/fs/fat16_vfs.o kernel/lang/exec.o \
            kernel/fs/homefs.o kernel/fs/loopdev.o kernel/fs/iso9660.o kernel/fs/iso9660_vfs.o \
            kernel/mm/swap_disk.o kernel/mm/swap.o \
            kernel/smp/percpu.o \
            kernel/smp/smp_trampoline.o \
            kernel/smp/lapic.o \
            kernel/smp/ioapic.o \
            kernel/smp/bkl.o \
            kernel/smp/mp_tables.o \
            kernel/smp/acpi.o \
            kernel/smp/smp.o \
            kernel/network/net_if.o \
            kernel/network/arp.o \
            kernel/network/ip.o \
            kernel/network/icmp.o \
            kernel/network/udp.o \
            kernel/network/socket.o \
            kernel/network/tcp.o \
            kernel/network/dhcp.o \
            kernel/network/dns.o \
            drivers/rtl8139.o \
            drivers/e1000.o \
            kernel/core/syscall.o \
            kernel/crypto/chacha20.o kernel/crypto/csprng.o \
            kernel/crypto/sha256.o kernel/crypto/sha512.o kernel/crypto/hmac.o kernel/crypto/hkdf.o \
            kernel/crypto/ct.o kernel/crypto/poly1305.o \
            kernel/crypto/chacha20poly1305.o \
            kernel/crypto/aes.o kernel/crypto/aes_gcm.o \
            kernel/crypto/bigint.o kernel/crypto/rsa.o \
            kernel/crypto/x25519.o kernel/crypto/p256.o kernel/crypto/ecdsa.o \
            kernel/crypto/ed25519.o \
            kernel/crypto/asn1.o kernel/crypto/x509.o \
            kernel/crypto/x509_chain.o kernel/tls/tls_ca_bundle.o \
            kernel/tls/tls_record.o kernel/tls/tls_kdf.o \
            kernel/tls/tls_ctx.o kernel/tls/tls_handshake.o \
            kernel/tls/tls12_handshake.o \
            kernel/tls/tls_selftest.o \
			kernel/lang/cupidc.o kernel/lang/cupidc_lex.o kernel/lang/cupidc_parse.o \
			kernel/lang/cupidc_string.o \
            kernel/lang/cupidc_elf.o kernel/lang/ssh_io.o \
			kernel/lang/as.o kernel/lang/as_lex.o kernel/lang/as_parse.o kernel/lang/as_elf.o \
			kernel/lang/dis.o \
            kernel/gfx/gfx2d.o \
            kernel/gfx/bmp.o \
            kernel/gfx/png.o \
            kernel/gfx/deflate.o \
            kernel/gfx/jpeg.o \
            kernel/gfx/ttf.o \
            kernel/gfx/glyph_raster.o \
            kernel/gfx/fontsys.o \
            kernel/fs/vfs_helpers.o \
            drivers/rtc.o kernel/util/calendar.o \
            kernel/gfx/gfx2d_assets.o kernel/gfx/gfx2d_transform.o kernel/gfx/gfx2d_effects.o \
			kernel/cpu/simd.o \
            kernel/gfx/gfx2d_icons.o \
            kernel/gui/gui_widgets.o kernel/gui/gui_containers.o kernel/gui/gui_menus.o \
            kernel/gui/gui_events.o kernel/gui/gui_themes.o \
            kernel/util/bin_programs_gen.o \
			kernel/util/docs_programs_gen.o \
			kernel/util/demos_programs_gen.o \
			kernel/cpu/ksyms.o \
			kernel/audio/ac97.o \
			kernel/audio/mixer.o \
			kernel/audio/nuked_opl3.o \
			kernel/audio/opl_smoke.o \
			kernel/audio/memio.o \
			kernel/audio/mus2midi.o \
			kernel/audio/midiopl.o \
			$(BIN_CC_OBJS) $(BIN_HDR_OBJS) $(BROWSER_SUB_OBJS) $(DOC_CTXT_OBJS) $(DOC_ASSET_OBJS) $(DEMO_ASM_OBJS) $(GOD_DD_OBJS) $(FONT_TTF_OBJS)

.PHONY: FORCE
FORCE:

# The repository currently contains checked-in build artifacts such as
# drivers/*.o, boot/boot.bin, and kernel/kernel.bin. Their mtimes can match
# source checkout mtimes closely enough that make may incorrectly reuse stale
# objects from older source revisions, producing bad kernels or link failures.
# Force kernel-related artifacts to rebuild from source on each invocation.
$(KERNEL_OBJS) $(BOOTLOADER) $(KERNEL): FORCE

# Keep tracked binary artifacts intact if a later build step fails.
.PRECIOUS: $(BOOTLOADER) $(KERNEL)

check-mtools:
	@command -v mcopy >/dev/null 2>&1 || { \
	  echo "ERROR: mtools not installed. Run: pacman -S mtools"; exit 1; }

all: $(OS_IMAGE)

# Compile bootloader
$(BOOTLOADER): boot/boot.asm
	$(ASM) -f bin boot/boot.asm -o $(BOOTLOADER)

# Compile C source files
kernel/core/kernel.o: kernel/core/kernel.c kernel/core/kernel.h kernel/cpu/cpu.h
	$(CC) $(CFLAGS) kernel/core/kernel.c -o kernel/core/kernel.o

# simd.c uses SSE2 inline asm helpers; keep freestanding include policy
SIMD_CFLAGS=$(filter-out -pedantic,$(CFLAGS)) -msse2 -O2
kernel/cpu/simd.o: kernel/cpu/simd.c kernel/cpu/simd.h
	$(CC) $(SIMD_CFLAGS) kernel/cpu/simd.c -o kernel/cpu/simd.o

kernel/cpu/idt.o: kernel/cpu/idt.c kernel/cpu/idt.h kernel/cpu/isr.h kernel/core/kernel.h
	$(CC) $(CFLAGS) kernel/cpu/idt.c -o kernel/cpu/idt.o

# Compile assembly files
kernel/cpu/isr.o: kernel/cpu/isr.asm
	$(ASM) -f elf32 kernel/cpu/isr.asm -o kernel/cpu/isr.o

kernel/cpu/pic.o: kernel/cpu/pic.c kernel/cpu/pic.h
	$(CC) $(CFLAGS) -c kernel/cpu/pic.c -o kernel/cpu/pic.o

kernel/cpu/irq.o: kernel/cpu/irq.c kernel/cpu/isr.h kernel/cpu/pic.h
	$(CC) $(CFLAGS) kernel/cpu/irq.c -o kernel/cpu/irq.o

# Symbol-table runtime + (weak) blob fallback. The strong blob lives in
# the auto-generated kernel/cpu/ksyms_data.o; see the kernel link rule.
kernel/cpu/ksyms.o: kernel/cpu/ksyms.c kernel/cpu/ksyms.h
	$(CC) $(CFLAGS) kernel/cpu/ksyms.c -o kernel/cpu/ksyms.o

# Add new rule for keyboard.o
drivers/keyboard.o: drivers/keyboard.c drivers/keyboard.h
	$(CC) $(CFLAGS) drivers/keyboard.c -o drivers/keyboard.o

# Add new rule for timer.o
drivers/timer.o: drivers/timer.c drivers/timer.h
	$(CC) $(CFLAGS) drivers/timer.c -o drivers/timer.o

# Add a rule to compile math.c
kernel/cpu/math.o: kernel/cpu/math.c kernel/cpu/math.h
	$(CC) $(CFLAGS) kernel/cpu/math.c -o kernel/cpu/math.o

# Add new rule for pit.o
drivers/pit.o: drivers/pit.c drivers/pit.h
	$(CC) $(CFLAGS) drivers/pit.c -o drivers/pit.o

# Add new rule for speaker.o
drivers/speaker.o: drivers/speaker.c drivers/speaker.h
	$(CC) $(CFLAGS) drivers/speaker.c -o drivers/speaker.o

# Add new rule for ata.o
drivers/ata.o: drivers/ata.c drivers/ata.h
	$(CC) $(CFLAGS) drivers/ata.c -o drivers/ata.o

# Add new rule for shell.o
kernel/lang/shell.o: kernel/lang/shell.c kernel/lang/shell.h
	$(CC) $(CFLAGS) kernel/lang/shell.c -o kernel/lang/shell.o

# Add new rule for string.o
kernel/core/string.o: kernel/core/string.c kernel/core/string.h
	$(CC) $(CFLAGS) $(OPT) kernel/core/string.c -o kernel/core/string.o

# Add new rule for fs.o
kernel/fs/fs.o: kernel/fs/fs.c kernel/fs/fs.h
	$(CC) $(CFLAGS) kernel/fs/fs.c -o kernel/fs/fs.o

# Add new rule for memory.o
kernel/mm/memory.o: kernel/mm/memory.c kernel/mm/memory.h
	$(CC) $(CFLAGS) kernel/mm/memory.c -o kernel/mm/memory.o

# PCI configuration space layer
drivers/pci.o: drivers/pci.c drivers/pci.h kernel/core/ports.h
	$(CC) $(CFLAGS) drivers/pci.c -o drivers/pci.o

# AP trampoline raw binary blob (P5 SMP T8)
kernel/smp_trampoline.bin: kernel/smp/smp_trampoline.S
	$(ASM) -f bin -o $@ $<

kernel/smp/smp_trampoline.o: kernel/smp_trampoline.bin
	objcopy -I binary -O elf32-i386 -B i386 \
	  --redefine-sym _binary_kernel_smp_trampoline_bin_start=smp_trampoline_start \
	  --redefine-sym _binary_kernel_smp_trampoline_bin_end=smp_trampoline_end \
	  --redefine-sym _binary_kernel_smp_trampoline_bin_size=smp_trampoline_size \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# Per-CPU data infrastructure (P5 SMP)
kernel/smp/percpu.o: kernel/smp/percpu.c kernel/smp/percpu.h kernel/core/process.h
	$(CC) $(CFLAGS) kernel/smp/percpu.c -o kernel/smp/percpu.o

# Local APIC BSP init + timer calibration (P5 SMP)
kernel/smp/lapic.o: kernel/smp/lapic.c kernel/smp/lapic.h kernel/core/ports.h kernel/mm/memory.h
	$(CC) $(CFLAGS) kernel/smp/lapic.c -o kernel/smp/lapic.o

# IOAPIC redirection table + 8259 mask (P5 SMP)
kernel/smp/ioapic.o: kernel/smp/ioapic.c kernel/smp/ioapic.h kernel/mm/memory.h
	$(CC) $(CFLAGS) kernel/smp/ioapic.c -o kernel/smp/ioapic.o

# Big Kernel Lock: recursive ticket spinlock, IRQ-save (P5 T7)
kernel/smp/bkl.o: kernel/smp/bkl.c kernel/smp/bkl.h kernel/smp/percpu.h
	$(CC) $(CFLAGS) kernel/smp/bkl.c -o kernel/smp/bkl.o

# MP tables discovery (P5 SMP)
kernel/smp/mp_tables.o: kernel/smp/mp_tables.c kernel/smp/mp_tables.h kernel/smp/ioapic.h kernel/smp/percpu.h
	$(CC) $(CFLAGS) kernel/smp/mp_tables.c -o kernel/smp/mp_tables.o

# ACPI MADT fallback discovery (P5 SMP)
kernel/smp/acpi.o: kernel/smp/acpi.c kernel/smp/acpi.h kernel/smp/mp_tables.h kernel/smp/ioapic.h
	$(CC) $(CFLAGS) kernel/smp/acpi.c -o kernel/smp/acpi.o

# SMP discovery orchestration + AP bringup (P5 T9)
kernel/smp/smp.o: kernel/smp/smp.c kernel/smp/smp.h kernel/smp/mp_tables.h kernel/smp/acpi.h \
              kernel/smp/lapic.h kernel/smp/ioapic.h kernel/smp/bkl.h kernel/smp/percpu.h kernel/mm/memory.h
	$(CC) $(CFLAGS) kernel/smp/smp.c -o kernel/smp/smp.o

# NIC interface scaffold + 64-slot lockless RX ring (P6 T1)
kernel/network/net_if.o: kernel/network/net_if.c kernel/network/net_if.h kernel/network/arp.h kernel/network/ip.h kernel/network/tcp.h kernel/network/dhcp.h kernel/mm/memory.h
	$(CC) $(CFLAGS) kernel/network/net_if.c -o kernel/network/net_if.o

# ARP: 16-entry cache + blocking resolve + Ethernet dispatch (P6 T6)
kernel/network/arp.o: kernel/network/arp.c kernel/network/arp.h kernel/network/net_if.h
	$(CC) $(CFLAGS) kernel/network/arp.c -o kernel/network/arp.o

# IPv4: parse + build + dispatch to ICMP/UDP/TCP (P6 T7)
kernel/network/ip.o: kernel/network/ip.c kernel/network/ip.h kernel/network/tcp.h kernel/network/net_if.h kernel/network/arp.h
	$(CC) $(CFLAGS) kernel/network/ip.c -o kernel/network/ip.o

# ICMP: echo reply (P6 T8)
kernel/network/icmp.o: kernel/network/icmp.c kernel/network/icmp.h kernel/network/ip.h
	$(CC) $(CFLAGS) kernel/network/icmp.c -o kernel/network/icmp.o

# UDP: send + recv + pseudo-header checksum (P6 T9)
kernel/network/udp.o: kernel/network/udp.c kernel/network/udp.h kernel/network/ip.h kernel/network/net_if.h kernel/network/dhcp.h
	$(CC) $(CFLAGS) kernel/network/udp.c -o kernel/network/udp.o

# Socket table + BSD UDP API (P6 T10)
kernel/network/socket.o: kernel/network/socket.c kernel/network/socket.h kernel/network/tcp.h kernel/network/udp.h kernel/smp/bkl.h kernel/core/process.h
	$(CC) $(CFLAGS) kernel/network/socket.c -o kernel/network/socket.o

# TCP client state machine (P6 T13)
kernel/network/tcp.o: kernel/network/tcp.c kernel/network/tcp.h kernel/network/ip.h kernel/network/socket.h kernel/smp/bkl.h kernel/core/process.h
	$(CC) $(CFLAGS) kernel/network/tcp.c -o kernel/network/tcp.o

# DHCP client with static fallback (P6 T11)
kernel/network/dhcp.o: kernel/network/dhcp.c kernel/network/dhcp.h kernel/network/net_if.h kernel/network/ip.h
	$(CC) $(CFLAGS) kernel/network/dhcp.c -o kernel/network/dhcp.o

# DNS A-record resolver + 16-entry cache (P6 T12)
kernel/network/dns.o: kernel/network/dns.c kernel/network/dns.h kernel/network/socket.h kernel/network/net_if.h
	$(CC) $(CFLAGS) kernel/network/dns.c -o kernel/network/dns.o

# RTL8139 NIC driver: PCI probe, reset, RX/TX buffers, MAC read (P6 T3)
drivers/rtl8139.o: drivers/rtl8139.c kernel/network/net_if.h drivers/pci.h kernel/mm/memory.h kernel/core/ports.h
	$(CC) $(CFLAGS) drivers/rtl8139.c -o drivers/rtl8139.o

# E1000 (Intel 82540EM) NIC driver: MMIO probe, RX/TX rings, MAC read (P6 T15)
drivers/e1000.o: drivers/e1000.c kernel/network/net_if.h drivers/pci.h kernel/mm/memory.h kernel/cpu/irq.h kernel/cpu/isr.h
	$(CC) $(CFLAGS) drivers/e1000.c -o drivers/e1000.o

# TLS subsystem: crypto primitives, X.509, handshake state machine.
# Built phase by phase under kernel/tls/. See plan in
# /home/frank/.claude/plans/implementy-tls-into-the-breezy-biscuit.md.
kernel/crypto/chacha20.o: kernel/crypto/chacha20.c kernel/crypto/chacha20.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/chacha20.c -o kernel/crypto/chacha20.o

kernel/crypto/csprng.o: kernel/crypto/csprng.c kernel/crypto/csprng.h kernel/crypto/chacha20.h kernel/core/types.h drivers/serial.h
	$(CC) $(CFLAGS) kernel/crypto/csprng.c -o kernel/crypto/csprng.o

kernel/crypto/sha256.o: kernel/crypto/sha256.c kernel/crypto/sha256.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/sha256.c -o kernel/crypto/sha256.o

kernel/crypto/sha512.o: kernel/crypto/sha512.c kernel/crypto/sha512.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/sha512.c -o kernel/crypto/sha512.o

kernel/crypto/hmac.o: kernel/crypto/hmac.c kernel/crypto/hmac.h kernel/crypto/sha256.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/hmac.c -o kernel/crypto/hmac.o

kernel/crypto/hkdf.o: kernel/crypto/hkdf.c kernel/crypto/hkdf.h kernel/crypto/hmac.h kernel/crypto/sha256.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/hkdf.c -o kernel/crypto/hkdf.o

kernel/crypto/ct.o: kernel/crypto/ct.c kernel/crypto/ct.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/ct.c -o kernel/crypto/ct.o

kernel/crypto/poly1305.o: kernel/crypto/poly1305.c kernel/crypto/poly1305.h kernel/crypto/ct.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/poly1305.c -o kernel/crypto/poly1305.o

kernel/crypto/chacha20poly1305.o: kernel/crypto/chacha20poly1305.c kernel/crypto/chacha20poly1305.h kernel/crypto/chacha20.h kernel/crypto/poly1305.h kernel/crypto/ct.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/chacha20poly1305.c -o kernel/crypto/chacha20poly1305.o

kernel/crypto/aes.o: kernel/crypto/aes.c kernel/crypto/aes.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/aes.c -o kernel/crypto/aes.o

kernel/crypto/aes_gcm.o: kernel/crypto/aes_gcm.c kernel/crypto/aes_gcm.h kernel/crypto/aes.h kernel/crypto/ct.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/aes_gcm.c -o kernel/crypto/aes_gcm.o

kernel/crypto/bigint.o: kernel/crypto/bigint.c kernel/crypto/bigint.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/bigint.c -o kernel/crypto/bigint.o

kernel/crypto/rsa.o: kernel/crypto/rsa.c kernel/crypto/rsa.h kernel/crypto/bigint.h kernel/crypto/sha256.h kernel/crypto/ct.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/rsa.c -o kernel/crypto/rsa.o

kernel/crypto/x25519.o: kernel/crypto/x25519.c kernel/crypto/x25519.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/x25519.c -o kernel/crypto/x25519.o

kernel/crypto/p256.o: kernel/crypto/p256.c kernel/crypto/p256.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/p256.c -o kernel/crypto/p256.o

kernel/crypto/ecdsa.o: kernel/crypto/ecdsa.c kernel/crypto/ecdsa.h kernel/crypto/p256.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/ecdsa.c -o kernel/crypto/ecdsa.o

kernel/crypto/ed25519.o: kernel/crypto/ed25519.c kernel/crypto/ed25519.h kernel/crypto/sha512.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/ed25519.c -o kernel/crypto/ed25519.o

kernel/crypto/asn1.o: kernel/crypto/asn1.c kernel/crypto/asn1.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/asn1.c -o kernel/crypto/asn1.o

kernel/crypto/x509.o: kernel/crypto/x509.c kernel/crypto/x509.h kernel/crypto/asn1.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/x509.c -o kernel/crypto/x509.o

kernel/crypto/x509_chain.o: kernel/crypto/x509_chain.c kernel/crypto/x509_chain.h kernel/crypto/x509.h kernel/crypto/sha256.h kernel/crypto/rsa.h kernel/crypto/p256.h kernel/crypto/ecdsa.h kernel/crypto/asn1.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/crypto/x509_chain.c -o kernel/crypto/x509_chain.o

kernel/tls/tls_ca_bundle.o: kernel/tls/tls_ca_bundle.c kernel/crypto/x509_chain.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/tls/tls_ca_bundle.c -o kernel/tls/tls_ca_bundle.o

kernel/tls/tls_record.o: kernel/tls/tls_record.c kernel/tls/tls_record.h kernel/crypto/chacha20poly1305.h kernel/crypto/aes_gcm.h kernel/crypto/ct.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/tls/tls_record.c -o kernel/tls/tls_record.o

kernel/tls/tls_kdf.o: kernel/tls/tls_kdf.c kernel/tls/tls_kdf.h kernel/crypto/hkdf.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/tls/tls_kdf.c -o kernel/tls/tls_kdf.o

kernel/tls/tls_ctx.o: kernel/tls/tls_ctx.c kernel/tls/tls_ctx.h kernel/tls/tls_record.h kernel/crypto/x509_chain.h kernel/crypto/sha256.h kernel/crypto/ct.h kernel/crypto/csprng.h kernel/crypto/x25519.h kernel/crypto/p256.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/tls/tls_ctx.c -o kernel/tls/tls_ctx.o

kernel/tls/tls_handshake.o: kernel/tls/tls_handshake.c kernel/tls/tls_ctx.h kernel/tls/tls_record.h kernel/tls/tls_kdf.h kernel/crypto/sha256.h kernel/crypto/hmac.h kernel/crypto/hkdf.h kernel/crypto/ct.h kernel/crypto/csprng.h kernel/crypto/x25519.h kernel/crypto/p256.h kernel/crypto/ecdsa.h kernel/crypto/x509.h kernel/crypto/x509_chain.h kernel/crypto/rsa.h kernel/crypto/asn1.h kernel/tls/tls12_handshake.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/tls/tls_handshake.c -o kernel/tls/tls_handshake.o

kernel/tls/tls12_handshake.o: kernel/tls/tls12_handshake.c kernel/tls/tls12_handshake.h kernel/tls/tls_ctx.h kernel/tls/tls_record.h kernel/tls/tls_kdf.h kernel/crypto/sha256.h kernel/crypto/ct.h kernel/crypto/x25519.h kernel/crypto/p256.h kernel/crypto/ecdsa.h kernel/crypto/x509.h kernel/crypto/x509_chain.h kernel/crypto/rsa.h kernel/crypto/asn1.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/tls/tls12_handshake.c -o kernel/tls/tls12_handshake.o

# Optional auto-generated bundle blob; only built if the file exists
# (run tools/fetch_ca_bundle.sh to populate it).
ifneq (,$(wildcard kernel/tls/tls_ca_bundle_data.c))
KERNEL_OBJS += kernel/tls/tls_ca_bundle_data.o
kernel/tls/tls_ca_bundle_data.o: kernel/tls/tls_ca_bundle_data.c kernel/crypto/x509_chain.h kernel/core/types.h
	$(CC) $(CFLAGS) -Os kernel/tls/tls_ca_bundle_data.c -o kernel/tls/tls_ca_bundle_data.o
endif

kernel/tls/tls_selftest.o: kernel/tls/tls_selftest.c kernel/tls/tls_selftest.h kernel/crypto/sha256.h kernel/crypto/hmac.h kernel/crypto/hkdf.h kernel/crypto/chacha20poly1305.h kernel/crypto/aes.h kernel/crypto/aes_gcm.h kernel/crypto/x25519.h kernel/crypto/p256.h kernel/crypto/ecdsa.h kernel/crypto/asn1.h kernel/core/panic.h drivers/serial.h
	$(CC) $(CFLAGS) -Os kernel/tls/tls_selftest.c -o kernel/tls/tls_selftest.o

# USB core scaffold
kernel/usb/usb.o: kernel/usb/usb.c kernel/usb/usb.h kernel/usb/usb_hc.h
	$(CC) $(CFLAGS) kernel/usb/usb.c -o kernel/usb/usb.o

# UHCI host controller init + port ops
kernel/usb/uhci.o: kernel/usb/uhci.c kernel/usb/usb.h kernel/usb/usb_hc.h drivers/pci.h kernel/core/ports.h kernel/cpu/irq.h kernel/cpu/isr.h
	$(CC) $(CFLAGS) kernel/usb/uhci.c -o kernel/usb/uhci.o

# EHCI host controller init + BIOS handoff + port ops
kernel/usb/ehci.o: kernel/usb/ehci.c kernel/usb/usb.h kernel/usb/usb_hc.h drivers/pci.h kernel/core/ports.h kernel/cpu/irq.h kernel/cpu/isr.h
	$(CC) $(CFLAGS) kernel/usb/ehci.c -o kernel/usb/ehci.o

# USB HID boot-protocol keyboard driver
kernel/usb/usb_hid.o: kernel/usb/usb_hid.c kernel/usb/usb.h kernel/usb/usb_hc.h drivers/keyboard.h drivers/serial.h
	$(CC) $(CFLAGS) kernel/usb/usb_hid.c -o kernel/usb/usb_hid.o

# USB hub class driver (recursive enumeration + TT routing)
kernel/usb/usb_hub.o: kernel/usb/usb_hub.c kernel/usb/usb.h kernel/usb/usb_hc.h drivers/serial.h
	$(CC) $(CFLAGS) kernel/usb/usb_hub.c -o kernel/usb/usb_hub.o

# USB mass storage class driver (BBB + SCSI)
kernel/usb/usb_msc.o: kernel/usb/usb_msc.c kernel/usb/usb.h kernel/usb/usb_hc.h kernel/fs/blockdev.h drivers/serial.h
	$(CC) $(CFLAGS) kernel/usb/usb_msc.c -o kernel/usb/usb_msc.o

# AC97 audio — BDL DMA + IRQ + smoke helper
kernel/audio/ac97.o: kernel/audio/ac97.c kernel/audio/ac97.h drivers/pci.h \
	kernel/core/ports.h kernel/cpu/irq.h kernel/mm/memory.h kernel/core/kernel.h drivers/serial.h drivers/timer.h
	$(CC) $(CFLAGS) kernel/audio/ac97.c -o kernel/audio/ac97.o

# Mixer — 16-slot s16 stereo software mixer
kernel/audio/mixer.o: kernel/audio/mixer.c kernel/audio/mixer.h kernel/core/types.h \
	drivers/serial.h
	$(CC) $(CFLAGS) kernel/audio/mixer.c -o kernel/audio/mixer.o

# Nuked-OPL3 emulator — vendored LGPL-2.1, built with relaxed CFLAGS_DOOM
kernel/audio/nuked_opl3.o: kernel/audio/nuked_opl3.c kernel/audio/nuked_opl3.h
	$(CC) $(CFLAGS_DOOM) -o $@ $<

# mus2midi + memio — vendored GPL-2, built with relaxed CFLAGS_DOOM
kernel/audio/memio.o: kernel/audio/memio.c kernel/audio/memio.h \
	kernel/core/types.h kernel/core/string.h kernel/mm/memory.h
	$(CC) $(CFLAGS_DOOM) -o $@ $<

kernel/audio/mus2midi.o: kernel/audio/mus2midi.c kernel/audio/mus2midi.h \
	kernel/audio/memio.h kernel/core/types.h kernel/core/string.h kernel/mm/memory.h
	$(CC) $(CFLAGS_DOOM) -o $@ $<

# midiopl — MIDI → OPL3 synth (our code; built with strict CFLAGS)
kernel/audio/midiopl.o: kernel/audio/midiopl.c kernel/audio/midiopl.h \
	kernel/audio/nuked_opl3.h kernel/core/types.h kernel/core/string.h drivers/serial.h
	$(CC) $(CFLAGS) -o $@ $<

# OPL smoke test — Nuked-OPL3 → mixer → AC97 path verification
kernel/audio/opl_smoke.o: kernel/audio/opl_smoke.c kernel/audio/opl_smoke.h \
	kernel/audio/nuked_opl3.h kernel/audio/mixer.h kernel/audio/ac97.h \
	kernel/core/types.h drivers/serial.h
	$(CC) $(CFLAGS) -o $@ $<

# dglibc — DOOM libc shim (heap/string/stdio/fmt/setjmp) built with relaxed CFLAGS_DOOM
kernel/doom/dglibc.o: kernel/doom/dglibc.c kernel/doom/dglibc.h kernel/core/types.h \
                      kernel/mm/memory.h kernel/fs/vfs.h kernel/core/string.h \
                      drivers/serial.h drivers/timer.h
	$(CC) $(CFLAGS_DOOM) -o $@ $<

KERNEL_OBJS += kernel/doom/dglibc.o

# doomgeneric cupidos platform shim (Task 13)
kernel/doom/doomgeneric_cupidos.o: kernel/doom/doomgeneric_cupidos.c \
                                    kernel/doom/doomgeneric_cupidos.h \
                                    kernel/doom/dglibc.h \
                                    kernel/core/types.h \
                                    drivers/vga.h \
                                    drivers/keyboard.h \
                                    drivers/serial.h \
                                    drivers/timer.h \
                                    kernel/fs/vfs.h
	$(CC) $(CFLAGS_DOOM) -o $@ $<

KERNEL_OBJS += kernel/doom/doomgeneric_cupidos.o

# doom_libc_stubs — atoi/sscanf/puts/etc. + i_music stubs (SFX moved to Task 16)
kernel/doom/doom_libc_stubs.o: kernel/doom/doom_libc_stubs.c \
                                kernel/core/types.h kernel/core/string.h kernel/doom/dglibc.h \
                                drivers/serial.h
	$(CC) $(CFLAGS_DOOM) -o $@ $<

KERNEL_OBJS += kernel/doom/doom_libc_stubs.o

# i_sound_cupidos — SFX path: lump cache + mixer bridge (Task 16)
kernel/doom/i_sound_cupidos.o: kernel/doom/i_sound_cupidos.c \
                                kernel/core/types.h kernel/mm/memory.h \
                                drivers/serial.h \
                                kernel/audio/mixer.h \
                                kernel/doom/src/i_sound.h \
                                kernel/doom/src/w_wad.h
	$(CC) $(CFLAGS_DOOM_TREE) -o $@ $<

KERNEL_OBJS += kernel/doom/i_sound_cupidos.o

# DOOM source tree — all .c files under kernel/doom/src/
DOOM_SRC := $(wildcard kernel/doom/src/*.c)
DOOM_SRC_OBJS := $(DOOM_SRC:.c=.o)

kernel/doom/src/%.o: kernel/doom/src/%.c
	$(CC) $(CFLAGS_DOOM_TREE) -o $@ $<

KERNEL_OBJS += $(DOOM_SRC_OBJS)

# Add new rule for paging.o
kernel/mm/paging.o: kernel/mm/paging.c kernel/mm/memory.h
	$(CC) $(CFLAGS) kernel/mm/paging.c -o kernel/mm/paging.o

# Add new rule for blockdev.o
kernel/fs/blockdev.o: kernel/fs/blockdev.c kernel/fs/blockdev.h
	$(CC) $(CFLAGS) kernel/fs/blockdev.c -o kernel/fs/blockdev.o

# Add new rule for blockcache.o
kernel/fs/blockcache.o: kernel/fs/blockcache.c kernel/fs/blockcache.h
	$(CC) $(CFLAGS) kernel/fs/blockcache.c -o kernel/fs/blockcache.o

# Add new rule for fat16.o
kernel/fs/fat16.o: kernel/fs/fat16.c kernel/fs/fat16.h
	$(CC) $(CFLAGS) kernel/fs/fat16.c -o kernel/fs/fat16.o

# RTC (Real-Time Clock) driver
drivers/rtc.o: drivers/rtc.c drivers/rtc.h
	$(CC) $(CFLAGS) drivers/rtc.c -o drivers/rtc.o

# Serial port driver
drivers/serial.o: drivers/serial.c drivers/serial.h
	$(CC) $(CFLAGS) drivers/serial.c -o drivers/serial.o

# Panic handler
kernel/core/panic.o: kernel/core/panic.c kernel/core/panic.h
	$(CC) $(CFLAGS) kernel/core/panic.c -o kernel/core/panic.o

# Ed line editor
kernel/gui/ed.o: kernel/gui/ed.c kernel/gui/ed.h
	$(CC) $(CFLAGS) kernel/gui/ed.c -o kernel/gui/ed.o

# VGA graphics mode driver (no -O2: physical address reads trigger array-bounds)
drivers/vga.o: drivers/vga.c drivers/vga.h
	$(CC) $(CFLAGS) drivers/vga.c -o drivers/vga.o

# PS/2 mouse driver
drivers/mouse.o: drivers/mouse.c drivers/mouse.h
	$(CC) $(CFLAGS) drivers/mouse.c -o drivers/mouse.o

# 8x8 bitmap font
kernel/gfx/font_8x8.o: kernel/gfx/font_8x8.c kernel/gfx/font_8x8.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/font_8x8.c -o kernel/gfx/font_8x8.o

# Graphics primitives
kernel/gfx/graphics.o: kernel/gfx/graphics.c kernel/gfx/graphics.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/graphics.c -o kernel/gfx/graphics.o

# GUI / window manager
kernel/gui/gui.o: kernel/gui/gui.c kernel/gui/gui.h kernel/core/process.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui/gui.c -o kernel/gui/gui.o

# Calendar math and formatting
kernel/util/calendar.o: kernel/util/calendar.c kernel/util/calendar.h
	$(CC) $(CFLAGS) $(OPT) kernel/util/calendar.c -o kernel/util/calendar.o

# Desktop shell
kernel/gui/desktop.o: kernel/gui/desktop.c kernel/gui/desktop.h kernel/gfx/gfx2d_icons.h kernel/lang/cupidc.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui/desktop.c -o kernel/gui/desktop.o

kernel/core/app_launch.o: kernel/core/app_launch.c kernel/core/app_launch.h kernel/lang/cupidc.h kernel/core/process.h kernel/lang/shell.h kernel/gui/terminal_app.h
	$(CC) $(CFLAGS) kernel/core/app_launch.c -o kernel/core/app_launch.o

# Terminal application
kernel/gui/ansi.o: kernel/gui/ansi.c kernel/gui/ansi.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui/ansi.c -o kernel/gui/ansi.o

kernel/gui/terminal_ansi.o: kernel/gui/terminal_ansi.c kernel/gui/terminal_ansi.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui/terminal_ansi.c -o kernel/gui/terminal_ansi.o

kernel/gui/terminal_app.o: kernel/gui/terminal_app.c kernel/gui/terminal_app.h kernel/gui/terminal_ansi.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui/terminal_app.c -o kernel/gui/terminal_app.o

# Process management and round-robin scheduler (process.c)
kernel/core/process.o: kernel/core/process.c kernel/core/process.h
	$(CC) $(CFLAGS) kernel/core/process.c -o kernel/core/process.o

# Context switch (assembly)
kernel/core/context_switch.o: kernel/core/context_switch.asm
	$(ASM) -f elf32 kernel/core/context_switch.asm -o kernel/core/context_switch.o

# Clipboard
kernel/gui/clipboard.o: kernel/gui/clipboard.c kernel/gui/clipboard.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui/clipboard.c -o kernel/gui/clipboard.o

# UI widget toolkit
kernel/gui/ui.o: kernel/gui/ui.c kernel/gui/ui.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui/ui.c -o kernel/gui/ui.o

# GodSpeak helper
kernel/lang/godspeak.o: kernel/lang/godspeak.c kernel/lang/godspeak.h
	$(CC) $(CFLAGS) $(OPT) kernel/lang/godspeak.c -o kernel/lang/godspeak.o

kernel/cpu/fpu.o: kernel/cpu/fpu.c kernel/cpu/fpu.h kernel/core/panic.h drivers/serial.h
	$(CC) $(CFLAGS) kernel/cpu/fpu.c -o kernel/cpu/fpu.o

kernel/cpu/libm.o: kernel/cpu/libm.c kernel/cpu/libm.h
	$(CC) $(CFLAGS) $(OPT) kernel/cpu/libm.c -o kernel/cpu/libm.o

kernel/lang/cupidscript_lex.o: kernel/lang/cupidscript_lex.c kernel/lang/cupidscript.h
	$(CC) $(CFLAGS) kernel/lang/cupidscript_lex.c -o kernel/lang/cupidscript_lex.o

kernel/lang/cupidscript_parse.o: kernel/lang/cupidscript_parse.c kernel/lang/cupidscript.h
	$(CC) $(CFLAGS) kernel/lang/cupidscript_parse.c -o kernel/lang/cupidscript_parse.o

kernel/lang/cupidscript_exec.o: kernel/lang/cupidscript_exec.c kernel/lang/cupidscript.h kernel/lang/shell.h
	$(CC) $(CFLAGS) kernel/lang/cupidscript_exec.c -o kernel/lang/cupidscript_exec.o

kernel/lang/cupidscript_runtime.o: kernel/lang/cupidscript_runtime.c kernel/lang/cupidscript.h
	$(CC) $(CFLAGS) kernel/lang/cupidscript_runtime.c -o kernel/lang/cupidscript_runtime.o

kernel/lang/cupidscript_streams.o: kernel/lang/cupidscript_streams.c kernel/lang/cupidscript_streams.h kernel/lang/cupidscript.h
	$(CC) $(CFLAGS) kernel/lang/cupidscript_streams.c -o kernel/lang/cupidscript_streams.o

kernel/lang/cupidscript_strings.o: kernel/lang/cupidscript_strings.c kernel/lang/cupidscript.h
	$(CC) $(CFLAGS) kernel/lang/cupidscript_strings.c -o kernel/lang/cupidscript_strings.o

kernel/lang/cupidscript_arrays.o: kernel/lang/cupidscript_arrays.c kernel/lang/cupidscript_arrays.h
	$(CC) $(CFLAGS) kernel/lang/cupidscript_arrays.c -o kernel/lang/cupidscript_arrays.o

kernel/lang/cupidscript_jobs.o: kernel/lang/cupidscript_jobs.c kernel/lang/cupidscript_jobs.h kernel/core/process.h
	$(CC) $(CFLAGS) kernel/lang/cupidscript_jobs.c -o kernel/lang/cupidscript_jobs.o

# VFS core
kernel/fs/vfs.o: kernel/fs/vfs.c kernel/fs/vfs.h
	$(CC) $(CFLAGS) kernel/fs/vfs.c -o kernel/fs/vfs.o

# RamFS
kernel/fs/ramfs.o: kernel/fs/ramfs.c kernel/fs/ramfs.h kernel/fs/vfs.h
	$(CC) $(CFLAGS) kernel/fs/ramfs.c -o kernel/fs/ramfs.o

# DevFS
kernel/fs/devfs.o: kernel/fs/devfs.c kernel/fs/devfs.h kernel/fs/vfs.h
	$(CC) $(CFLAGS) kernel/fs/devfs.c -o kernel/fs/devfs.o

# FAT16 VFS wrapper
kernel/fs/fat16_vfs.o: kernel/fs/fat16_vfs.c kernel/fs/fat16_vfs.h kernel/fs/vfs.h kernel/fs/fat16.h
	$(CC) $(CFLAGS) kernel/fs/fat16_vfs.c -o kernel/fs/fat16_vfs.o

kernel/fs/homefs.o: kernel/fs/homefs.c kernel/fs/homefs.h kernel/fs/fat16.h kernel/fs/vfs.h
	$(CC) $(CFLAGS) kernel/fs/homefs.c -o kernel/fs/homefs.o

# File-backed loop block device (for ISO9660 mounting)
kernel/fs/loopdev.o: kernel/fs/loopdev.c kernel/fs/loopdev.h kernel/fs/blockdev.h kernel/fs/vfs.h kernel/mm/memory.h drivers/serial.h
	$(CC) $(CFLAGS) kernel/fs/loopdev.c -o kernel/fs/loopdev.o

# ISO9660 / ECMA-119 + Rock Ridge parser
kernel/fs/iso9660.o: kernel/fs/iso9660.c kernel/fs/iso9660.h kernel/fs/blockdev.h kernel/fs/vfs.h kernel/core/string.h drivers/serial.h
	$(CC) $(CFLAGS) $(OPT) kernel/fs/iso9660.c -o kernel/fs/iso9660.o

kernel/fs/iso9660_vfs.o: kernel/fs/iso9660_vfs.c kernel/fs/iso9660_vfs.h kernel/fs/iso9660.h kernel/fs/loopdev.h kernel/fs/vfs.h drivers/serial.h
	$(CC) $(CFLAGS) kernel/fs/iso9660_vfs.c -o kernel/fs/iso9660_vfs.o

kernel/mm/swap_disk.o: kernel/mm/swap_disk.c kernel/mm/swap_disk.h kernel/fs/vfs.h drivers/serial.h
	$(CC) $(CFLAGS) $(OPT) kernel/mm/swap_disk.c -o kernel/mm/swap_disk.o

kernel/mm/swap.o: kernel/mm/swap.c kernel/mm/swap.h kernel/mm/swap_disk.h kernel/mm/memory.h kernel/fs/vfs.h drivers/serial.h
	$(CC) $(CFLAGS) $(OPT) kernel/mm/swap.c -o kernel/mm/swap.o

# Program loader (ELF + CUPD)
kernel/lang/exec.o: kernel/lang/exec.c kernel/lang/exec.h kernel/fs/vfs.h kernel/core/process.h kernel/core/syscall.h
	$(CC) $(CFLAGS) kernel/lang/exec.c -o kernel/lang/exec.o

# Syscall table for ELF programs
kernel/core/syscall.o: kernel/core/syscall.c kernel/core/syscall.h kernel/fs/vfs.h kernel/core/process.h kernel/lang/shell.h
	$(CC) $(CFLAGS) kernel/core/syscall.c -o kernel/core/syscall.o

# BMP image encoding/decoding
kernel/gfx/bmp.o: kernel/gfx/bmp.c kernel/gfx/bmp.h kernel/fs/vfs.h kernel/mm/memory.h drivers/vga.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/bmp.c -o kernel/gfx/bmp.o

kernel/gfx/png.o: kernel/gfx/png.c kernel/gfx/png.h kernel/gfx/deflate.h kernel/mm/memory.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/png.c -o kernel/gfx/png.o

kernel/gfx/deflate.o: kernel/gfx/deflate.c kernel/gfx/deflate.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/deflate.c -o kernel/gfx/deflate.o

kernel/gfx/jpeg.o: kernel/gfx/jpeg.c kernel/gfx/jpeg.h kernel/mm/memory.h kernel/cpu/libm.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/jpeg.c -o kernel/gfx/jpeg.o

# TrueType font system: parser, rasterizer, registry/cache.
kernel/gfx/ttf.o: kernel/gfx/ttf.c kernel/gfx/ttf.h drivers/serial.h kernel/core/string.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/ttf.c -o kernel/gfx/ttf.o

kernel/gfx/glyph_raster.o: kernel/gfx/glyph_raster.c kernel/gfx/glyph_raster.h kernel/mm/memory.h kernel/core/string.h kernel/cpu/libm.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/glyph_raster.c -o kernel/gfx/glyph_raster.o

kernel/gfx/fontsys.o: kernel/gfx/fontsys.c kernel/gfx/fontsys.h kernel/gfx/ttf.h kernel/gfx/glyph_raster.h kernel/gfx/gfx2d.h kernel/mm/memory.h kernel/core/string.h drivers/serial.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/fontsys.c -o kernel/gfx/fontsys.o

# VFS helpers (read_all, write_all, read_text, write_text)
kernel/fs/vfs_helpers.o: kernel/fs/vfs_helpers.c kernel/fs/vfs_helpers.h kernel/fs/vfs.h
	$(CC) $(CFLAGS) kernel/fs/vfs_helpers.c -o kernel/fs/vfs_helpers.o

# 2D graphics library (includes file dialog)
kernel/gfx/gfx2d.o: kernel/gfx/gfx2d.c kernel/gfx/gfx2d.h kernel/gfx/font_8x8.h drivers/vga.h kernel/fs/vfs.h kernel/gui/ui.h kernel/core/process.h drivers/keyboard.h drivers/mouse.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/gfx2d.c -o kernel/gfx/gfx2d.o

# gfx2d subsystems
kernel/gfx/gfx2d_assets.o: kernel/gfx/gfx2d_assets.c kernel/gfx/gfx2d_assets.h kernel/gfx/gfx2d.h kernel/gfx/bmp.h kernel/gfx/png.h kernel/gfx/jpeg.h kernel/fs/vfs.h kernel/fs/vfs_helpers.h kernel/mm/memory.h kernel/gfx/font_8x8.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/gfx2d_assets.c -o kernel/gfx/gfx2d_assets.o

kernel/gfx/gfx2d_transform.o: kernel/gfx/gfx2d_transform.c kernel/gfx/gfx2d_transform.h kernel/gfx/gfx2d.h kernel/gfx/gfx2d_assets.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/gfx2d_transform.c -o kernel/gfx/gfx2d_transform.o

kernel/gfx/gfx2d_effects.o: kernel/gfx/gfx2d_effects.c kernel/gfx/gfx2d_effects.h kernel/gfx/gfx2d.h kernel/mm/memory.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/gfx2d_effects.c -o kernel/gfx/gfx2d_effects.o

# Desktop icon system
kernel/gfx/gfx2d_icons.o: kernel/gfx/gfx2d_icons.c kernel/gfx/gfx2d_icons.h kernel/gfx/gfx2d.h kernel/fs/vfs.h kernel/core/string.h kernel/mm/memory.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx/gfx2d_icons.c -o kernel/gfx/gfx2d_icons.o

# GUI subsystems
kernel/gui/gui_widgets.o: kernel/gui/gui_widgets.c kernel/gui/gui_widgets.h kernel/gfx/gfx2d.h kernel/gui/ui.h kernel/gfx/font_8x8.h drivers/vga.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui/gui_widgets.c -o kernel/gui/gui_widgets.o

kernel/gui/gui_containers.o: kernel/gui/gui_containers.c kernel/gui/gui_containers.h kernel/gfx/gfx2d.h kernel/gui/ui.h kernel/gfx/font_8x8.h drivers/vga.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui/gui_containers.c -o kernel/gui/gui_containers.o

kernel/gui/gui_menus.o: kernel/gui/gui_menus.c kernel/gui/gui_menus.h kernel/gfx/gfx2d.h kernel/gui/ui.h kernel/gfx/font_8x8.h drivers/vga.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui/gui_menus.c -o kernel/gui/gui_menus.o

kernel/gui/gui_events.o: kernel/gui/gui_events.c kernel/gui/gui_events.h kernel/gfx/gfx2d.h kernel/gui/ui.h kernel/gui/gui.h kernel/gfx/font_8x8.h drivers/vga.h kernel/mm/memory.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui/gui_events.c -o kernel/gui/gui_events.o

kernel/gui/gui_themes.o: kernel/gui/gui_themes.c kernel/gui/gui_themes.h kernel/core/string.h kernel/mm/memory.h kernel/fs/fs.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui/gui_themes.c -o kernel/gui/gui_themes.o

# CupidC compiler
kernel/lang/cupidc.o: kernel/lang/cupidc.c kernel/lang/cupidc.h kernel/lang/cupidc_string.h kernel/fs/vfs.h kernel/fs/vfs_helpers.h kernel/mm/memory.h kernel/lang/exec.h kernel/gfx/gfx2d_icons.h
	$(CC) $(CFLAGS) kernel/lang/cupidc.c -o kernel/lang/cupidc.o

kernel/lang/cupidc_string.o: kernel/lang/cupidc_string.c kernel/lang/cupidc_string.h kernel/core/types.h
	$(CC) $(CFLAGS) kernel/lang/cupidc_string.c -o kernel/lang/cupidc_string.o

kernel/lang/cupidc_lex.o: kernel/lang/cupidc_lex.c kernel/lang/cupidc.h
	$(CC) $(CFLAGS) kernel/lang/cupidc_lex.c -o kernel/lang/cupidc_lex.o

kernel/lang/cupidc_parse.o: kernel/lang/cupidc_parse.c kernel/lang/cupidc.h
	$(CC) $(CFLAGS) kernel/lang/cupidc_parse.c -o kernel/lang/cupidc_parse.o

kernel/lang/cupidc_elf.o: kernel/lang/cupidc_elf.c kernel/lang/cupidc.h kernel/lang/exec.h kernel/fs/vfs.h
	$(CC) $(CFLAGS) kernel/lang/cupidc_elf.c -o kernel/lang/cupidc_elf.o

kernel/lang/ssh_io.o: kernel/lang/ssh_io.c kernel/lang/ssh_io.h kernel/lang/shell.h drivers/keyboard.h kernel/core/process.h kernel/core/kernel.h kernel/crypto/p256.h kernel/crypto/ecdsa.h kernel/core/types.h
	$(CC) $(CFLAGS) kernel/lang/ssh_io.c -o kernel/lang/ssh_io.o

# CupidASM assembler
kernel/lang/as.o: kernel/lang/as.c kernel/lang/as.h kernel/fs/vfs.h kernel/fs/vfs_helpers.h kernel/mm/memory.h kernel/lang/exec.h
	$(CC) $(CFLAGS) kernel/lang/as.c -o kernel/lang/as.o

kernel/lang/as_lex.o: kernel/lang/as_lex.c kernel/lang/as.h
	$(CC) $(CFLAGS) kernel/lang/as_lex.c -o kernel/lang/as_lex.o

kernel/lang/as_parse.o: kernel/lang/as_parse.c kernel/lang/as.h
	$(CC) $(CFLAGS) kernel/lang/as_parse.c -o kernel/lang/as_parse.o

kernel/lang/as_elf.o: kernel/lang/as_elf.c kernel/lang/as.h kernel/lang/exec.h kernel/fs/vfs.h
	$(CC) $(CFLAGS) kernel/lang/as_elf.c -o kernel/lang/as_elf.o

kernel/lang/dis.o: kernel/lang/dis.c kernel/lang/dis.h kernel/core/types.h kernel/lang/exec.h kernel/fs/vfs.h kernel/fs/vfs_helpers.h
	$(CC) $(CFLAGS) kernel/lang/dis.c -o kernel/lang/dis.o

# Auto-generate browser CSS data tables from Blink .in files.
# Produces gen_css_properties.h, gen_css_keywords.h, gen_media_features.h
# consumed by bin/browser parser + style code.
BROWSER_CSS_GEN := bin/browser/gen_css_properties.h \
                   bin/browser/gen_css_keywords.h \
                   bin/browser/gen_media_features.h
BLINK_CSS_INS := blink/Source/core/css/CSSProperties.in \
                 blink/Source/core/css/CSSValueKeywords.in \
                 blink/Source/core/css/MediaFeatureNames.in

$(BROWSER_CSS_GEN): $(BLINK_CSS_INS) tools/gen_css_props.py
	python3 tools/gen_css_props.py blink bin/browser

browser_css_gen: $(BROWSER_CSS_GEN)

# Auto-generate bin_programs_gen.c from all bin/*.cc files
# This generates extern declarations + install function automatically.
# To add a new CupidC program: just create bin/<name>.cc - that's it!
kernel/util/bin_programs_gen.c: $(BIN_CC_SRCS) $(BIN_HDR_SRCS) $(BROWSER_SUB_SRCS) Makefile
	@echo "/* Auto-generated -- do not edit. */" > $@
	@echo "/* Lists all embedded CupidC programs from bin/ directory */" >> $@
	@echo '#include "ramfs.h"' >> $@
	@echo '#include "types.h"' >> $@
	@echo '#include "../drivers/serial.h"' >> $@
	@$(foreach n,$(BIN_CC_NAMES),echo 'extern const char _binary_bin_$(n)_cc_start[];' >> $@;)
	@$(foreach n,$(BIN_HDR_NAMES),echo 'extern const char _binary_bin_$(n)_h_start[];' >> $@;)
	@$(foreach n,$(BIN_CC_NAMES),echo 'extern const char _binary_bin_$(n)_cc_end[];' >> $@;)
	@$(foreach n,$(BIN_HDR_NAMES),echo 'extern const char _binary_bin_$(n)_h_end[];' >> $@;)
	@$(foreach n,$(BROWSER_SUB_NAMES),echo 'extern const char _binary_bin_browser_$(n)_cc_start[];' >> $@;)
	@$(foreach n,$(BROWSER_SUB_NAMES),echo 'extern const char _binary_bin_browser_$(n)_cc_end[];' >> $@;)
	@echo 'void install_bin_programs(void *fs_private);' >> $@
	@echo 'void install_bin_programs(void *fs_private) {' >> $@
	@$(foreach n,$(BIN_CC_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_bin_$(n)_cc_end - _binary_bin_$(n)_cc_start); ramfs_add_file(fs_private, "bin/$(n).cc", _binary_bin_$(n)_cc_start, sz); serial_printf("[kernel] Installed /bin/$(n).cc (%u bytes)\n", sz); }' >> $@;)
	@$(foreach n,$(BIN_HDR_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_bin_$(n)_h_end - _binary_bin_$(n)_h_start); ramfs_add_file(fs_private, "bin/$(n).h", _binary_bin_$(n)_h_start, sz); serial_printf("[kernel] Installed /bin/$(n).h (%u bytes)\n", sz); }' >> $@;)
	@$(foreach n,$(BROWSER_SUB_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_bin_browser_$(n)_cc_end - _binary_bin_browser_$(n)_cc_start); ramfs_add_file(fs_private, "bin/browser/$(n).cc", _binary_bin_browser_$(n)_cc_start, sz); serial_printf("[kernel] Installed /bin/browser/$(n).cc (%u bytes)\n", sz); }' >> $@;)
	@echo '}' >> $@

kernel/util/bin_programs_gen.o: kernel/util/bin_programs_gen.c
	$(CC) $(CFLAGS) kernel/util/bin_programs_gen.c -o kernel/util/bin_programs_gen.o

# Auto-generate docs_programs_gen.c from cupidos-txt/*.CTXT files
kernel/util/docs_programs_gen.c: $(DOC_CTXT_SRCS) $(DOC_ASSET_SRCS) Makefile
	@echo "/* Auto-generated -- do not edit. */" > $@
	@echo "/* Lists all embedded CupidDoc files from cupidos-txt/ directory */" >> $@
	@echo '#include "ramfs.h"' >> $@
	@echo '#include "types.h"' >> $@
	@echo '#include "../drivers/serial.h"' >> $@
	@$(foreach n,$(DOC_CTXT_NAMES),echo 'extern const char _binary_cupidos_txt_$(subst -,_,$(n))_CTXT_start[];' >> $@;)
	@$(foreach n,$(DOC_ASSET_NAMES),echo 'extern const char _binary_$(subst -,_,$(n))_bmp_start[];' >> $@;)
	@$(foreach n,$(DOC_CTXT_NAMES),echo 'extern const char _binary_cupidos_txt_$(subst -,_,$(n))_CTXT_end[];' >> $@;)
	@$(foreach n,$(DOC_ASSET_NAMES),echo 'extern const char _binary_$(subst -,_,$(n))_bmp_end[];' >> $@;)
	@echo 'void install_docs_programs(void *fs_private);' >> $@
	@echo 'void install_docs_programs(void *fs_private) {' >> $@
	@$(foreach n,$(DOC_CTXT_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_cupidos_txt_$(subst -,_,$(n))_CTXT_end - _binary_cupidos_txt_$(subst -,_,$(n))_CTXT_start); ramfs_add_file(fs_private, "docs/$(n).ctxt", _binary_cupidos_txt_$(subst -,_,$(n))_CTXT_start, sz); serial_printf("[kernel] Installed /docs/$(n).ctxt (%u bytes)\n", sz); }' >> $@;)
	@$(foreach n,$(DOC_ASSET_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_$(subst -,_,$(n))_bmp_end - _binary_$(subst -,_,$(n))_bmp_start); ramfs_add_file(fs_private, "docs/$(n).bmp", _binary_$(subst -,_,$(n))_bmp_start, sz); serial_printf("[kernel] Installed /docs/$(n).bmp (%u bytes)\n", sz); }' >> $@;)
	@echo '}' >> $@

kernel/util/docs_programs_gen.o: kernel/util/docs_programs_gen.c
	$(CC) $(CFLAGS) kernel/util/docs_programs_gen.c -o kernel/util/docs_programs_gen.o

# Auto-generate demos_programs_gen.c from demos/*.asm files
kernel/util/demos_programs_gen.c: $(DEMO_ASM_SRCS) Makefile
	@echo "/* Auto-generated -- do not edit. */" > $@
	@echo "/* Lists all embedded CupidASM demos from demos/ directory */" >> $@
	@echo '#include "ramfs.h"' >> $@
	@echo '#include "types.h"' >> $@
	@echo '#include "../drivers/serial.h"' >> $@
	@$(foreach n,$(DEMO_ASM_NAMES),echo 'extern const char _binary_demos_$(n)_asm_start[];' >> $@;)
	@$(foreach n,$(DEMO_ASM_NAMES),echo 'extern const char _binary_demos_$(n)_asm_end[];' >> $@;)
	@echo 'void install_demo_programs(void *fs_private);' >> $@
	@echo 'void install_demo_programs(void *fs_private) {' >> $@
	@$(foreach n,$(DEMO_ASM_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_demos_$(n)_asm_end - _binary_demos_$(n)_asm_start); ramfs_add_file(fs_private, "demos/$(n).asm", _binary_demos_$(n)_asm_start, sz); serial_printf("[kernel] Installed /demos/$(n).asm (%u bytes)\n", sz); ramfs_add_file(fs_private, "docs/demos/$(n).asm", _binary_demos_$(n)_asm_start, sz); serial_printf("[kernel] Installed /docs/demos/$(n).asm (%u bytes)\n", sz); }' >> $@;)
	@echo '}' >> $@

kernel/util/demos_programs_gen.o: kernel/util/demos_programs_gen.c
	$(CC) $(CFLAGS) kernel/util/demos_programs_gen.c -o kernel/util/demos_programs_gen.o

# Pattern rule: embed any bin/*.cc file via objcopy
bin/%.o: bin/%.cc
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Pattern rule: embed any bin/browser/*.cc library file via objcopy.
# These live in ramfs at /bin/browser/<n>.cc and are #include'd by
# bin/browser.cc at JIT time. They are NOT in BIN_CC_NAMES.
bin/browser/%.o: bin/browser/%.cc
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Pattern rule: embed any bin/*.h file via objcopy (output keeps .h in name)
bin/%.h.o: bin/%.h
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Pattern rule: embed any cupidos-txt/*.CTXT file via objcopy
cupidos-txt/%.o: cupidos-txt/%.CTXT
	objcopy -I binary -O elf32-i386 -B i386 $< $@

%.bmp.o: %.bmp
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Pattern rule: embed any system/fonts/*.ttf file via objcopy.
# Object exposes _binary_system_fonts_<name>_ttf_{start,end} symbols
# (dashes in the filename get translated to underscores by objcopy).
system/fonts/%.ttf.o: system/fonts/%.ttf
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Pattern rule: embed any demos/*.asm file via objcopy
demos/%.o: demos/%.asm
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Pattern rule: embed any god/*.DD file via objcopy
god/%.o: god/%.DD
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Link kernel objects.
#
# Two-pass link so the panic backtrace can decode addresses to function
# names:
#   Pass 1: link all KERNEL_OBJS into kernel.elf.pass1.  ksyms.o ships a
#           weak empty .ksyms blob, so this link succeeds with no symbol
#           data.
#   mksyms: read kernel.elf.pass1's symbol table, generate
#           kernel/cpu/ksyms_data.c with the populated blob.
#   Pass 2: link kernel.elf again with ksyms_data.o added — the strong
#           ksym_blob symbol overrides the weak one from ksyms.o.  The
#           .ksyms section is placed after .data in link.ld so code
#           addresses don't shift between passes; only .bss start
#           moves, which is fine.
#   objcopy kernel.elf -> kernel.bin (raw binary the bootloader expects).
kernel/kernel.elf.pass1: $(KERNEL_OBJS)
	ld $(LDFLAGS_ELF) -o $@ $(KERNEL_OBJS)

kernel/cpu/ksyms_data.c: kernel/kernel.elf.pass1 tools/mksyms.sh
	bash tools/mksyms.sh $< $@

kernel/cpu/ksyms_data.o: kernel/cpu/ksyms_data.c kernel/cpu/ksyms.h
	$(CC) $(CFLAGS) kernel/cpu/ksyms_data.c -o kernel/cpu/ksyms_data.o

kernel/kernel.elf: $(KERNEL_OBJS) kernel/cpu/ksyms_data.o
	ld $(LDFLAGS_ELF) -o $@ $(KERNEL_OBJS) kernel/cpu/ksyms_data.o

$(KERNEL): kernel/kernel.elf
	objcopy -O binary $< $(KERNEL)

# Create HDD image: MBR + Stage2 + kernel area + FAT16 partition (size via HDD_MB, default 200MB)
$(OS_IMAGE): $(BOOTLOADER) $(KERNEL)
	@if [ ! -f $(OS_IMAGE) ]; then \
		echo "[make] Creating new persistent image $(OS_IMAGE) ($(HDD_MB)MB)"; \
		dd if=/dev/zero of=$(OS_IMAGE) bs=512 count=$(OS_IMAGE_SECTORS); \
		printf "$(FAT_START_LBA),,6\\n" | sfdisk $(OS_IMAGE); \
		mkfs.fat -F 16 --offset=$(FAT_START_LBA) $(OS_IMAGE) $(FAT_BLOCKS); \
	else \
		actual_lba=$$(sfdisk -d $(OS_IMAGE) 2>/dev/null | sed -n 's/.*start=[[:space:]]*\([0-9]*\),.*/\1/p' | head -1); \
		if [ -n "$$actual_lba" ] && [ "$$actual_lba" != "$(FAT_START_LBA)" ]; then \
			echo "[make] Stale image (FAT at LBA $$actual_lba, expected $(FAT_START_LBA)) — recreating"; \
			rm -f $(OS_IMAGE); \
			dd if=/dev/zero of=$(OS_IMAGE) bs=512 count=$(OS_IMAGE_SECTORS); \
			printf "$(FAT_START_LBA),,6\\n" | sfdisk $(OS_IMAGE); \
			mkfs.fat -F 16 --offset=$(FAT_START_LBA) $(OS_IMAGE) $(FAT_BLOCKS); \
		else \
			echo "[make] Reusing existing image $(OS_IMAGE) (preserving /home data)"; \
		fi; \
	fi
	dd if=$(BOOTLOADER) of=$(OS_IMAGE) conv=notrunc bs=1 count=446
	dd if=$(BOOTLOADER) of=$(OS_IMAGE) conv=notrunc bs=512 seek=1 skip=1 count=4
	dd if=$(KERNEL) of=$(OS_IMAGE) conv=notrunc bs=512 seek=5
	@if [ -z "$(WAD_SRCS)" ]; then \
	  echo "Skipping WAD staging (no /usr/share/games/doom/freedoom*.wad on host)"; \
	else \
	  need_stage=0; \
	  for w in $(WAD_SRCS); do \
	    base=$$(basename "$$w"); \
	    MTOOLS_SKIP_CHECK=1 mdir -i $(OS_IMAGE)@@$(FAT_OFFSET_BYTES) ::/wads/$$base >/dev/null 2>&1 || need_stage=1; \
	  done; \
	  if [ $$need_stage -eq 0 ]; then \
	    echo "WADs already staged in image (use 'make stage-wads' to force re-copy)"; \
	  else \
	    echo "Staging WADs into FAT16 partition..."; \
	    MTOOLS_SKIP_CHECK=1 mmd -i $(OS_IMAGE)@@$(FAT_OFFSET_BYTES) ::/wads >/dev/null 2>&1 || true; \
	    for w in $(WAD_SRCS); do \
	      echo "  mcopy $$w -> /wads/"; \
	      MTOOLS_SKIP_CHECK=1 mcopy -Q -o -i $(OS_IMAGE)@@$(FAT_OFFSET_BYTES) "$$w" ::/wads/ </dev/null; \
	    done; \
	  fi; \
	fi

# Common QEMU flags for CupidOS. USB HCs (UHCI + EHCI) + HID devices
# let the P4 USB stack enumerate on boot. Add -device usb-storage + -drive
# for mass-storage testing (see run-usb target).
QEMU_COMMON = -m 512M -boot c \
	-drive file=$(OS_IMAGE),format=raw,if=ide,index=0,media=disk \
	-rtc base=localtime \
	-audiodev $(QEMU_AUDIODEV) -machine pcspk-audiodev=speaker \
	-device AC97,audiodev=speaker \
	-device piix3-usb-uhci -device usb-ehci \
	-device usb-kbd -device usb-mouse

run: $(OS_IMAGE)
	qemu-system-i386 $(QEMU_COMMON) -serial stdio

run-log: $(OS_IMAGE)
	qemu-system-i386 $(QEMU_COMMON) -serial file:debug.log

# Headless build: kernel routes to shell_run() over COM1 instead of desktop.
# Rebuilds kernel with -DHEADLESS, leaves FAT16 /home intact.
headless-image:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS=-DHEADLESS

# Boot headless shell over stdio (no GUI, no VBE). Use for scripted testing.
run-headless: headless-image
	qemu-system-i386 $(QEMU_COMMON) -display none -serial stdio

# Full P4 test: UHCI + EHCI + kbd + mouse + 32MB USB stick with FAT16 MBR.
# Creates test_usb_partitioned.img on first use.
run-usb: $(OS_IMAGE) test_usb_partitioned.img
	qemu-system-i386 $(QEMU_COMMON) \
		-drive if=none,id=ustick,file=test_usb_partitioned.img,format=raw \
		-device usb-storage,drive=ustick \
		-serial stdio

run-smp: $(OS_IMAGE)
	qemu-system-i386 $(QEMU_COMMON) -smp cpus=4 -serial stdio

run-net: $(OS_IMAGE)
	qemu-system-i386 $(QEMU_COMMON) \
		-netdev user,id=n0,hostfwd=tcp::8080-:80 \
		-device rtl8139,netdev=n0 \
		-serial stdio

run-smp-net: $(OS_IMAGE)
	qemu-system-i386 $(QEMU_COMMON) -smp cpus=4 \
		-netdev user,id=n0,hostfwd=tcp::8080-:80 \
		-device rtl8139,netdev=n0 \
		-serial stdio

run-net-e1000: $(OS_IMAGE)
	qemu-system-i386 $(QEMU_COMMON) \
		-netdev user,id=n0 \
		-device e1000,netdev=n0 \
		-serial stdio

# Headless image specifically for the net-test harness. Same as headless-image
# but kept as a separate target so callers can re-build deliberately.
headless-net-image: headless-image

# Network integration test on rtl8139 (default) and e1000.
# net_test.py drives the headless shell, runs feature21/22, host-curls the
# forwarded port. net_pcap.py then re-validates the captured frames at the
# protocol level (ARP, DHCP, ICMP, TCP handshake, IP checksums).
test-net-quick: headless-image
	python3 tools/net_test.py --nic rtl8139
	python3 tools/net_pcap.py tests/rtl8139.pcap

test-net: headless-image
	python3 tools/net_test.py --nic rtl8139
	python3 tools/net_test.py --nic e1000
	python3 tools/net_pcap.py tests/rtl8139.pcap tests/e1000.pcap

test_usb_partitioned.img:
	dd if=/dev/zero of=$@ bs=1M count=32 status=none
	python3 -c "\
import sys;\
f=open('$@','r+b');\
f.seek(0x1BE); f.write(bytes([0x80,0,1,0,0x06,0,1,0]));\
f.write((2048).to_bytes(4,'little'));\
f.write((63488).to_bytes(4,'little'));\
f.seek(0x1FE); f.write(bytes([0x55,0xAA]));\
f.close()"
	@echo "Built $@ (32MB, MBR with one FAT16 partition at LBA 2048)"

# Sync local demos/*.asm into FAT16 partition in cupidos image at /home/demos/
sync-demos: $(OS_IMAGE)
	-@mmd -i $(OS_IMAGE)@@$(FAT_OFFSET_BYTES) ::/home
	-@mmd -i $(OS_IMAGE)@@$(FAT_OFFSET_BYTES) ::/home/demos
	mcopy -o -i $(OS_IMAGE)@@$(FAT_OFFSET_BYTES) demos/*.asm ::/home/demos/
	@echo "Synced demos/*.asm -> $(OS_IMAGE):/home/demos/"

# Test-only ISO - built from test_iso/fixtures/, mounted via
# `mount /disk/hello.iso /iso` in the shell for feature17.
TEST_ISO_FIXTURES := $(shell find test_iso/fixtures -type f 2>/dev/null)

test_iso/fixtures/big.bin:
	@test_iso/fixtures/gen_big.sh

test_iso/hello.iso: $(TEST_ISO_FIXTURES) test_iso/fixtures/big.bin
	@which mkisofs >/dev/null 2>&1 || which genisoimage >/dev/null 2>&1 || which xorrisofs >/dev/null 2>&1 || \
	  (echo "ERROR: need mkisofs, genisoimage, or xorrisofs (apt install genisoimage)"; false)
	@ISO_TOOL=$$(which mkisofs 2>/dev/null || which genisoimage 2>/dev/null || which xorrisofs); \
	  $$ISO_TOOL -R -quiet -o test_iso/hello.iso test_iso/fixtures

sync-iso: $(OS_IMAGE) test_iso/hello.iso
	@mcopy -o -i $(OS_IMAGE)@@$(FAT_OFFSET_BYTES) test_iso/hello.iso ::/hello.iso
	@echo "Synced test_iso/hello.iso -> $(OS_IMAGE):/hello.iso"

# Stage DOOM WADs into FAT16 partition at /wads/.
# No-op (warning only) if no freedoom*.wad present on host.
stage-wads: $(OS_IMAGE) check-mtools
	@if [ -n "$(WAD_SRCS)" ]; then \
	  echo "Staging WADs into FAT16 partition (forced)..."; \
	  MTOOLS_SKIP_CHECK=1 mmd -i $(OS_IMAGE)@@$(FAT_OFFSET_BYTES) ::/wads >/dev/null 2>&1 || true; \
	  for w in $(WAD_SRCS); do \
	    echo "  mcopy $$w -> /wads/"; \
	    MTOOLS_SKIP_CHECK=1 mcopy -Q -o -i $(OS_IMAGE)@@$(FAT_OFFSET_BYTES) "$$w" ::/wads/ </dev/null; \
	  done; \
	else \
	  echo "Skipping WAD staging (no /usr/share/games/doom/freedoom*.wad on host)"; \
	fi

clean:
	rm -f $(BOOTLOADER) $(KERNEL) \
	      kernel/*.o kernel/audio/*.o kernel/core/*.o kernel/cpu/*.o \
	      kernel/crypto/*.o kernel/doom/*.o kernel/doom/src/*.o \
	      kernel/fs/*.o kernel/gfx/*.o kernel/gui/*.o kernel/lang/*.o \
	      kernel/mm/*.o kernel/network/*.o kernel/smp/*.o kernel/tls/*.o \
	      kernel/usb/*.o kernel/util/*.o \
	      drivers/*.o filesystem/*.o bin/*.o bin/browser/*.o \
	      cupidos-txt/*.o demos/*.o god/*.o image.bmp.o \
	      kernel/kernel.elf kernel/kernel.elf.pass1 kernel/kernel.bin \
	      kernel/smp/smp_trampoline.bin \
	      kernel/util/bin_programs_gen.c kernel/util/docs_programs_gen.c \
	      kernel/util/demos_programs_gen.c kernel/cpu/ksyms_data.c \
	      debug.log

clean-image:
	rm -f $(OS_IMAGE)

distclean: clean clean-image

.PHONY: all check-mtools run run-log sync-demos sync-iso stage-wads clean clean-image distclean
