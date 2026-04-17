# Compiler settings
.SUFFIXES:
ASM=nasm
CC=gcc
.DEFAULT_GOAL := all
# NASA Power of 10 compliant flags: pedantic, warnings as errors, strict checks
CFLAGS=-m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc -ffreestanding -c -I./kernel -I./drivers \
	-mfpmath=sse -msse -msse2 -mstackrealign \
       -DDEBUG -pedantic -Werror -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
       -Wmissing-prototypes -Wconversion -Wsign-conversion -Wwrite-strings
# Optimisation flags for rendering/computation-only files (no hw I/O or IRQs)
OPT=-O2
LDFLAGS=-m elf_i386 -T link.ld --oformat binary

# Auto-discover all CupidC programs in bin/
BIN_CC_SRCS := $(wildcard bin/*.cc)
$(info BIN_CC_SRCS=$(BIN_CC_SRCS))
BIN_CC_OBJS := $(BIN_CC_SRCS:.cc=.o)
BIN_CC_NAMES := $(notdir $(basename $(BIN_CC_SRCS)))

# Auto-discover embeddable headers used by CupidC demos/programs
BIN_HDR_SRCS := $(wildcard bin/*.h)
$(info BIN_HDR_SRCS=$(BIN_HDR_SRCS))
BIN_HDR_OBJS := $(BIN_HDR_SRCS:.h=.h.o)
BIN_HDR_NAMES := $(notdir $(basename $(BIN_HDR_SRCS)))

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

# Files
BOOTLOADER=boot/boot.bin
KERNEL=kernel/kernel.bin
OS_IMAGE=cupidos.img
QEMU_AUDIODEV ?= alsa,id=speaker
HDD_MB ?= 200
FAT_START_LBA ?= 4096
OS_IMAGE_SECTORS := $(shell expr $(HDD_MB) \* 1024 \* 1024 / 512)
FAT_BLOCKS := $(shell expr \( $(OS_IMAGE_SECTORS) - $(FAT_START_LBA) \) / 2)
FAT_OFFSET_BYTES := $(shell expr $(FAT_START_LBA) \* 512)
KERNEL_OBJS=kernel/kernel.o kernel/idt.o kernel/isr.o kernel/irq.o kernel/pic.o \
            kernel/fs.o drivers/keyboard.o drivers/timer.o kernel/math.o drivers/pit.o \
            drivers/speaker.o kernel/shell.o kernel/string.o kernel/memory.o kernel/pci.o kernel/usb.o kernel/uhci.o kernel/ehci.o kernel/usb_hid.o kernel/usb_hub.o kernel/usb_msc.o \
            kernel/paging.o drivers/ata.o kernel/blockdev.o kernel/blockcache.o kernel/fat16.o \
            drivers/serial.o kernel/panic.o kernel/ed.o \
            drivers/vga.o drivers/mouse.o kernel/font_8x8.o kernel/graphics.o \
			kernel/gui.o kernel/desktop.o kernel/app_launch.o kernel/process.o kernel/context_switch.o \
			kernel/clipboard.o kernel/ui.o \
			kernel/godspeak.o \
			kernel/fpu.o kernel/libm.o \
            kernel/cupidscript_lex.o kernel/cupidscript_parse.o \
            kernel/cupidscript_exec.o kernel/cupidscript_runtime.o \
            kernel/cupidscript_streams.o kernel/cupidscript_strings.o \
            kernel/cupidscript_arrays.o kernel/cupidscript_jobs.o \
			kernel/ansi.o \
			kernel/terminal_app.o \
            kernel/vfs.o kernel/ramfs.o kernel/devfs.o kernel/fat16_vfs.o kernel/exec.o \
            kernel/homefs.o kernel/loopdev.o kernel/iso9660.o kernel/iso9660_vfs.o \
            kernel/swap_disk.o kernel/swap.o \
            kernel/syscall.o \
			kernel/cupidc.o kernel/cupidc_lex.o kernel/cupidc_parse.o \
			kernel/cupidc_string.o \
            kernel/cupidc_elf.o \
			kernel/as.o kernel/as_lex.o kernel/as_parse.o kernel/as_elf.o \
			kernel/dis.o \
            kernel/gfx2d.o \
            kernel/bmp.o \
            kernel/vfs_helpers.o \
            drivers/rtc.o kernel/calendar.o \
            kernel/gfx2d_assets.o kernel/gfx2d_transform.o kernel/gfx2d_effects.o \
			kernel/simd.o \
            kernel/gfx2d_icons.o \
            kernel/gui_widgets.o kernel/gui_containers.o kernel/gui_menus.o \
            kernel/gui_events.o kernel/gui_themes.o \
            kernel/bin_programs_gen.o \
			kernel/docs_programs_gen.o \
			kernel/demos_programs_gen.o \
			$(BIN_CC_OBJS) $(BIN_HDR_OBJS) $(DOC_CTXT_OBJS) $(DOC_ASSET_OBJS) $(DEMO_ASM_OBJS) $(GOD_DD_OBJS)

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

all: $(OS_IMAGE)

# Compile bootloader
$(BOOTLOADER): boot/boot.asm
	$(ASM) -f bin boot/boot.asm -o $(BOOTLOADER)

# Compile C source files
kernel/kernel.o: kernel/kernel.c kernel/kernel.h kernel/cpu.h
	$(CC) $(CFLAGS) kernel/kernel.c -o kernel/kernel.o

# simd.c uses SSE2 inline asm helpers; keep freestanding include policy
SIMD_CFLAGS=$(filter-out -pedantic,$(CFLAGS)) -msse2 -O2
kernel/simd.o: kernel/simd.c kernel/simd.h
	$(CC) $(SIMD_CFLAGS) kernel/simd.c -o kernel/simd.o

kernel/idt.o: kernel/idt.c kernel/idt.h kernel/isr.h kernel/kernel.h
	$(CC) $(CFLAGS) kernel/idt.c -o kernel/idt.o

# Compile assembly files
kernel/isr.o: kernel/isr.asm
	$(ASM) -f elf32 kernel/isr.asm -o kernel/isr.o

kernel/pic.o: kernel/pic.c kernel/pic.h
	$(CC) $(CFLAGS) -c kernel/pic.c -o kernel/pic.o

kernel/irq.o: kernel/irq.c kernel/isr.h kernel/pic.h
	$(CC) $(CFLAGS) kernel/irq.c -o kernel/irq.o

# Add new rule for keyboard.o
drivers/keyboard.o: drivers/keyboard.c drivers/keyboard.h
	$(CC) $(CFLAGS) drivers/keyboard.c -o drivers/keyboard.o

# Add new rule for timer.o
drivers/timer.o: drivers/timer.c drivers/timer.h
	$(CC) $(CFLAGS) drivers/timer.c -o drivers/timer.o

# Add a rule to compile math.c
kernel/math.o: kernel/math.c kernel/math.h
	$(CC) $(CFLAGS) kernel/math.c -o kernel/math.o

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
kernel/shell.o: kernel/shell.c kernel/shell.h
	$(CC) $(CFLAGS) kernel/shell.c -o kernel/shell.o

# Add new rule for string.o
kernel/string.o: kernel/string.c kernel/string.h
	$(CC) $(CFLAGS) $(OPT) kernel/string.c -o kernel/string.o

# Add new rule for fs.o
kernel/fs.o: kernel/fs.c kernel/fs.h
	$(CC) $(CFLAGS) kernel/fs.c -o kernel/fs.o

# Add new rule for memory.o
kernel/memory.o: kernel/memory.c kernel/memory.h
	$(CC) $(CFLAGS) kernel/memory.c -o kernel/memory.o

# PCI configuration space layer
kernel/pci.o: kernel/pci.c kernel/pci.h kernel/ports.h
	$(CC) $(CFLAGS) kernel/pci.c -o kernel/pci.o

# USB core scaffold
kernel/usb.o: kernel/usb.c kernel/usb.h kernel/usb_hc.h
	$(CC) $(CFLAGS) kernel/usb.c -o kernel/usb.o

# UHCI host controller init + port ops
kernel/uhci.o: kernel/uhci.c kernel/usb.h kernel/usb_hc.h kernel/pci.h kernel/ports.h kernel/irq.h kernel/isr.h
	$(CC) $(CFLAGS) kernel/uhci.c -o kernel/uhci.o

# EHCI host controller init + BIOS handoff + port ops
kernel/ehci.o: kernel/ehci.c kernel/usb.h kernel/usb_hc.h kernel/pci.h kernel/ports.h kernel/irq.h kernel/isr.h
	$(CC) $(CFLAGS) kernel/ehci.c -o kernel/ehci.o

# USB HID boot-protocol keyboard driver
kernel/usb_hid.o: kernel/usb_hid.c kernel/usb.h kernel/usb_hc.h drivers/keyboard.h drivers/serial.h
	$(CC) $(CFLAGS) kernel/usb_hid.c -o kernel/usb_hid.o

# USB hub class driver (recursive enumeration + TT routing)
kernel/usb_hub.o: kernel/usb_hub.c kernel/usb.h kernel/usb_hc.h drivers/serial.h
	$(CC) $(CFLAGS) kernel/usb_hub.c -o kernel/usb_hub.o

# USB mass storage class driver (BBB + SCSI)
kernel/usb_msc.o: kernel/usb_msc.c kernel/usb.h kernel/usb_hc.h kernel/blockdev.h drivers/serial.h
	$(CC) $(CFLAGS) kernel/usb_msc.c -o kernel/usb_msc.o

# Add new rule for paging.o
kernel/paging.o: kernel/paging.c kernel/memory.h
	$(CC) $(CFLAGS) kernel/paging.c -o kernel/paging.o

# Add new rule for blockdev.o
kernel/blockdev.o: kernel/blockdev.c kernel/blockdev.h
	$(CC) $(CFLAGS) kernel/blockdev.c -o kernel/blockdev.o

# Add new rule for blockcache.o
kernel/blockcache.o: kernel/blockcache.c kernel/blockcache.h
	$(CC) $(CFLAGS) kernel/blockcache.c -o kernel/blockcache.o

# Add new rule for fat16.o
kernel/fat16.o: kernel/fat16.c kernel/fat16.h
	$(CC) $(CFLAGS) kernel/fat16.c -o kernel/fat16.o

# RTC (Real-Time Clock) driver
drivers/rtc.o: drivers/rtc.c drivers/rtc.h
	$(CC) $(CFLAGS) drivers/rtc.c -o drivers/rtc.o

# Serial port driver
drivers/serial.o: drivers/serial.c drivers/serial.h
	$(CC) $(CFLAGS) drivers/serial.c -o drivers/serial.o

# Panic handler
kernel/panic.o: kernel/panic.c kernel/panic.h
	$(CC) $(CFLAGS) kernel/panic.c -o kernel/panic.o

# Ed line editor
kernel/ed.o: kernel/ed.c kernel/ed.h
	$(CC) $(CFLAGS) kernel/ed.c -o kernel/ed.o

# VGA graphics mode driver (no -O2: physical address reads trigger array-bounds)
drivers/vga.o: drivers/vga.c drivers/vga.h
	$(CC) $(CFLAGS) drivers/vga.c -o drivers/vga.o

# PS/2 mouse driver
drivers/mouse.o: drivers/mouse.c drivers/mouse.h
	$(CC) $(CFLAGS) drivers/mouse.c -o drivers/mouse.o

# 8x8 bitmap font
kernel/font_8x8.o: kernel/font_8x8.c kernel/font_8x8.h
	$(CC) $(CFLAGS) $(OPT) kernel/font_8x8.c -o kernel/font_8x8.o

# Graphics primitives
kernel/graphics.o: kernel/graphics.c kernel/graphics.h
	$(CC) $(CFLAGS) $(OPT) kernel/graphics.c -o kernel/graphics.o

# GUI / window manager
kernel/gui.o: kernel/gui.c kernel/gui.h kernel/process.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui.c -o kernel/gui.o

# Calendar math and formatting
kernel/calendar.o: kernel/calendar.c kernel/calendar.h
	$(CC) $(CFLAGS) $(OPT) kernel/calendar.c -o kernel/calendar.o

# Desktop shell
kernel/desktop.o: kernel/desktop.c kernel/desktop.h kernel/gfx2d_icons.h kernel/cupidc.h
	$(CC) $(CFLAGS) $(OPT) kernel/desktop.c -o kernel/desktop.o

kernel/app_launch.o: kernel/app_launch.c kernel/app_launch.h kernel/cupidc.h kernel/process.h kernel/shell.h kernel/terminal_app.h
	$(CC) $(CFLAGS) kernel/app_launch.c -o kernel/app_launch.o

# Terminal application
kernel/ansi.o: kernel/ansi.c kernel/ansi.h
	$(CC) $(CFLAGS) $(OPT) kernel/ansi.c -o kernel/ansi.o

kernel/terminal_ansi.o: kernel/terminal_ansi.c kernel/terminal_ansi.h
	$(CC) $(CFLAGS) $(OPT) kernel/terminal_ansi.c -o kernel/terminal_ansi.o

kernel/terminal_app.o: kernel/terminal_app.c kernel/terminal_app.h kernel/terminal_ansi.h
	$(CC) $(CFLAGS) $(OPT) kernel/terminal_app.c -o kernel/terminal_app.o

# Process management and round-robin scheduler (process.c)
kernel/process.o: kernel/process.c kernel/process.h
	$(CC) $(CFLAGS) kernel/process.c -o kernel/process.o

# Context switch (assembly)
kernel/context_switch.o: kernel/context_switch.asm
	$(ASM) -f elf32 kernel/context_switch.asm -o kernel/context_switch.o

# Clipboard
kernel/clipboard.o: kernel/clipboard.c kernel/clipboard.h
	$(CC) $(CFLAGS) $(OPT) kernel/clipboard.c -o kernel/clipboard.o

# UI widget toolkit
kernel/ui.o: kernel/ui.c kernel/ui.h
	$(CC) $(CFLAGS) $(OPT) kernel/ui.c -o kernel/ui.o

# GodSpeak helper
kernel/godspeak.o: kernel/godspeak.c kernel/godspeak.h
	$(CC) $(CFLAGS) $(OPT) kernel/godspeak.c -o kernel/godspeak.o

kernel/fpu.o: kernel/fpu.c kernel/fpu.h kernel/panic.h drivers/serial.h
	$(CC) $(CFLAGS) kernel/fpu.c -o kernel/fpu.o

kernel/libm.o: kernel/libm.c kernel/libm.h
	$(CC) $(CFLAGS) $(OPT) kernel/libm.c -o kernel/libm.o

kernel/cupidscript_lex.o: kernel/cupidscript_lex.c kernel/cupidscript.h
	$(CC) $(CFLAGS) kernel/cupidscript_lex.c -o kernel/cupidscript_lex.o

kernel/cupidscript_parse.o: kernel/cupidscript_parse.c kernel/cupidscript.h
	$(CC) $(CFLAGS) kernel/cupidscript_parse.c -o kernel/cupidscript_parse.o

kernel/cupidscript_exec.o: kernel/cupidscript_exec.c kernel/cupidscript.h kernel/shell.h
	$(CC) $(CFLAGS) kernel/cupidscript_exec.c -o kernel/cupidscript_exec.o

kernel/cupidscript_runtime.o: kernel/cupidscript_runtime.c kernel/cupidscript.h
	$(CC) $(CFLAGS) kernel/cupidscript_runtime.c -o kernel/cupidscript_runtime.o

kernel/cupidscript_streams.o: kernel/cupidscript_streams.c kernel/cupidscript_streams.h kernel/cupidscript.h
	$(CC) $(CFLAGS) kernel/cupidscript_streams.c -o kernel/cupidscript_streams.o

kernel/cupidscript_strings.o: kernel/cupidscript_strings.c kernel/cupidscript.h
	$(CC) $(CFLAGS) kernel/cupidscript_strings.c -o kernel/cupidscript_strings.o

kernel/cupidscript_arrays.o: kernel/cupidscript_arrays.c kernel/cupidscript_arrays.h
	$(CC) $(CFLAGS) kernel/cupidscript_arrays.c -o kernel/cupidscript_arrays.o

kernel/cupidscript_jobs.o: kernel/cupidscript_jobs.c kernel/cupidscript_jobs.h kernel/process.h
	$(CC) $(CFLAGS) kernel/cupidscript_jobs.c -o kernel/cupidscript_jobs.o

# VFS core
kernel/vfs.o: kernel/vfs.c kernel/vfs.h
	$(CC) $(CFLAGS) kernel/vfs.c -o kernel/vfs.o

# RamFS
kernel/ramfs.o: kernel/ramfs.c kernel/ramfs.h kernel/vfs.h
	$(CC) $(CFLAGS) kernel/ramfs.c -o kernel/ramfs.o

# DevFS
kernel/devfs.o: kernel/devfs.c kernel/devfs.h kernel/vfs.h
	$(CC) $(CFLAGS) kernel/devfs.c -o kernel/devfs.o

# FAT16 VFS wrapper
kernel/fat16_vfs.o: kernel/fat16_vfs.c kernel/fat16_vfs.h kernel/vfs.h kernel/fat16.h
	$(CC) $(CFLAGS) kernel/fat16_vfs.c -o kernel/fat16_vfs.o

kernel/homefs.o: kernel/homefs.c kernel/homefs.h kernel/fat16.h kernel/vfs.h
	$(CC) $(CFLAGS) kernel/homefs.c -o kernel/homefs.o

# File-backed loop block device (for ISO9660 mounting)
kernel/loopdev.o: kernel/loopdev.c kernel/loopdev.h kernel/blockdev.h kernel/vfs.h kernel/memory.h drivers/serial.h
	$(CC) $(CFLAGS) kernel/loopdev.c -o kernel/loopdev.o

# ISO9660 / ECMA-119 + Rock Ridge parser
kernel/iso9660.o: kernel/iso9660.c kernel/iso9660.h kernel/blockdev.h kernel/vfs.h kernel/string.h drivers/serial.h
	$(CC) $(CFLAGS) $(OPT) kernel/iso9660.c -o kernel/iso9660.o

kernel/iso9660_vfs.o: kernel/iso9660_vfs.c kernel/iso9660_vfs.h kernel/iso9660.h kernel/loopdev.h kernel/vfs.h drivers/serial.h
	$(CC) $(CFLAGS) kernel/iso9660_vfs.c -o kernel/iso9660_vfs.o

kernel/swap_disk.o: kernel/swap_disk.c kernel/swap_disk.h kernel/vfs.h drivers/serial.h
	$(CC) $(CFLAGS) $(OPT) kernel/swap_disk.c -o kernel/swap_disk.o

kernel/swap.o: kernel/swap.c kernel/swap.h kernel/swap_disk.h kernel/memory.h kernel/vfs.h drivers/serial.h
	$(CC) $(CFLAGS) $(OPT) kernel/swap.c -o kernel/swap.o

# Program loader (ELF + CUPD)
kernel/exec.o: kernel/exec.c kernel/exec.h kernel/vfs.h kernel/process.h kernel/syscall.h
	$(CC) $(CFLAGS) kernel/exec.c -o kernel/exec.o

# Syscall table for ELF programs
kernel/syscall.o: kernel/syscall.c kernel/syscall.h kernel/vfs.h kernel/process.h kernel/shell.h
	$(CC) $(CFLAGS) kernel/syscall.c -o kernel/syscall.o

# BMP image encoding/decoding
kernel/bmp.o: kernel/bmp.c kernel/bmp.h kernel/vfs.h kernel/memory.h drivers/vga.h
	$(CC) $(CFLAGS) $(OPT) kernel/bmp.c -o kernel/bmp.o

# VFS helpers (read_all, write_all, read_text, write_text)
kernel/vfs_helpers.o: kernel/vfs_helpers.c kernel/vfs_helpers.h kernel/vfs.h
	$(CC) $(CFLAGS) kernel/vfs_helpers.c -o kernel/vfs_helpers.o

# 2D graphics library (includes file dialog)
kernel/gfx2d.o: kernel/gfx2d.c kernel/gfx2d.h kernel/font_8x8.h drivers/vga.h kernel/vfs.h kernel/ui.h kernel/process.h drivers/keyboard.h drivers/mouse.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx2d.c -o kernel/gfx2d.o

# gfx2d subsystems
kernel/gfx2d_assets.o: kernel/gfx2d_assets.c kernel/gfx2d_assets.h kernel/gfx2d.h kernel/bmp.h kernel/vfs.h kernel/memory.h kernel/font_8x8.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx2d_assets.c -o kernel/gfx2d_assets.o

kernel/gfx2d_transform.o: kernel/gfx2d_transform.c kernel/gfx2d_transform.h kernel/gfx2d.h kernel/gfx2d_assets.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx2d_transform.c -o kernel/gfx2d_transform.o

kernel/gfx2d_effects.o: kernel/gfx2d_effects.c kernel/gfx2d_effects.h kernel/gfx2d.h kernel/memory.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx2d_effects.c -o kernel/gfx2d_effects.o

# Desktop icon system
kernel/gfx2d_icons.o: kernel/gfx2d_icons.c kernel/gfx2d_icons.h kernel/gfx2d.h kernel/vfs.h kernel/string.h kernel/memory.h
	$(CC) $(CFLAGS) $(OPT) kernel/gfx2d_icons.c -o kernel/gfx2d_icons.o

# GUI subsystems
kernel/gui_widgets.o: kernel/gui_widgets.c kernel/gui_widgets.h kernel/gfx2d.h kernel/ui.h kernel/font_8x8.h drivers/vga.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui_widgets.c -o kernel/gui_widgets.o

kernel/gui_containers.o: kernel/gui_containers.c kernel/gui_containers.h kernel/gfx2d.h kernel/ui.h kernel/font_8x8.h drivers/vga.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui_containers.c -o kernel/gui_containers.o

kernel/gui_menus.o: kernel/gui_menus.c kernel/gui_menus.h kernel/gfx2d.h kernel/ui.h kernel/font_8x8.h drivers/vga.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui_menus.c -o kernel/gui_menus.o

kernel/gui_events.o: kernel/gui_events.c kernel/gui_events.h kernel/gfx2d.h kernel/ui.h kernel/gui.h kernel/font_8x8.h drivers/vga.h kernel/memory.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui_events.c -o kernel/gui_events.o

kernel/gui_themes.o: kernel/gui_themes.c kernel/gui_themes.h kernel/string.h kernel/memory.h kernel/fs.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui_themes.c -o kernel/gui_themes.o

# CupidC compiler
kernel/cupidc.o: kernel/cupidc.c kernel/cupidc.h kernel/cupidc_string.h kernel/vfs.h kernel/vfs_helpers.h kernel/memory.h kernel/exec.h kernel/gfx2d_icons.h
	$(CC) $(CFLAGS) kernel/cupidc.c -o kernel/cupidc.o

kernel/cupidc_string.o: kernel/cupidc_string.c kernel/cupidc_string.h kernel/types.h
	$(CC) $(CFLAGS) kernel/cupidc_string.c -o kernel/cupidc_string.o

kernel/cupidc_lex.o: kernel/cupidc_lex.c kernel/cupidc.h
	$(CC) $(CFLAGS) kernel/cupidc_lex.c -o kernel/cupidc_lex.o

kernel/cupidc_parse.o: kernel/cupidc_parse.c kernel/cupidc.h
	$(CC) $(CFLAGS) kernel/cupidc_parse.c -o kernel/cupidc_parse.o

kernel/cupidc_elf.o: kernel/cupidc_elf.c kernel/cupidc.h kernel/exec.h kernel/vfs.h
	$(CC) $(CFLAGS) kernel/cupidc_elf.c -o kernel/cupidc_elf.o

# CupidASM assembler
kernel/as.o: kernel/as.c kernel/as.h kernel/vfs.h kernel/vfs_helpers.h kernel/memory.h kernel/exec.h
	$(CC) $(CFLAGS) kernel/as.c -o kernel/as.o

kernel/as_lex.o: kernel/as_lex.c kernel/as.h
	$(CC) $(CFLAGS) kernel/as_lex.c -o kernel/as_lex.o

kernel/as_parse.o: kernel/as_parse.c kernel/as.h
	$(CC) $(CFLAGS) kernel/as_parse.c -o kernel/as_parse.o

kernel/as_elf.o: kernel/as_elf.c kernel/as.h kernel/exec.h kernel/vfs.h
	$(CC) $(CFLAGS) kernel/as_elf.c -o kernel/as_elf.o

kernel/dis.o: kernel/dis.c kernel/dis.h kernel/types.h kernel/exec.h kernel/vfs.h kernel/vfs_helpers.h
	$(CC) $(CFLAGS) kernel/dis.c -o kernel/dis.o

# Auto-generate bin_programs_gen.c from all bin/*.cc files
# This generates extern declarations + install function automatically.
# To add a new CupidC program: just create bin/<name>.cc - that's it!
kernel/bin_programs_gen.c: $(BIN_CC_SRCS) $(BIN_HDR_SRCS) Makefile
	@echo "/* Auto-generated -- do not edit. */" > $@
	@echo "/* Lists all embedded CupidC programs from bin/ directory */" >> $@
	@echo '#include "ramfs.h"' >> $@
	@echo '#include "types.h"' >> $@
	@echo '#include "../drivers/serial.h"' >> $@
	@$(foreach n,$(BIN_CC_NAMES),echo 'extern const char _binary_bin_$(n)_cc_start[];' >> $@;)
	@$(foreach n,$(BIN_HDR_NAMES),echo 'extern const char _binary_bin_$(n)_h_start[];' >> $@;)
	@$(foreach n,$(BIN_CC_NAMES),echo 'extern const char _binary_bin_$(n)_cc_end[];' >> $@;)
	@$(foreach n,$(BIN_HDR_NAMES),echo 'extern const char _binary_bin_$(n)_h_end[];' >> $@;)
	@echo 'void install_bin_programs(void *fs_private);' >> $@
	@echo 'void install_bin_programs(void *fs_private) {' >> $@
	@$(foreach n,$(BIN_CC_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_bin_$(n)_cc_end - _binary_bin_$(n)_cc_start); ramfs_add_file(fs_private, "bin/$(n).cc", _binary_bin_$(n)_cc_start, sz); serial_printf("[kernel] Installed /bin/$(n).cc (%u bytes)\n", sz); }' >> $@;)
	@$(foreach n,$(BIN_HDR_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_bin_$(n)_h_end - _binary_bin_$(n)_h_start); ramfs_add_file(fs_private, "bin/$(n).h", _binary_bin_$(n)_h_start, sz); serial_printf("[kernel] Installed /bin/$(n).h (%u bytes)\n", sz); }' >> $@;)
	@echo '}' >> $@

kernel/bin_programs_gen.o: kernel/bin_programs_gen.c
	$(CC) $(CFLAGS) kernel/bin_programs_gen.c -o kernel/bin_programs_gen.o

# Auto-generate docs_programs_gen.c from cupidos-txt/*.CTXT files
kernel/docs_programs_gen.c: $(DOC_CTXT_SRCS) $(DOC_ASSET_SRCS) Makefile
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

kernel/docs_programs_gen.o: kernel/docs_programs_gen.c
	$(CC) $(CFLAGS) kernel/docs_programs_gen.c -o kernel/docs_programs_gen.o

# Auto-generate demos_programs_gen.c from demos/*.asm files
kernel/demos_programs_gen.c: $(DEMO_ASM_SRCS) Makefile
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

kernel/demos_programs_gen.o: kernel/demos_programs_gen.c
	$(CC) $(CFLAGS) kernel/demos_programs_gen.c -o kernel/demos_programs_gen.o

# Pattern rule: embed any bin/*.cc file via objcopy
bin/%.o: bin/%.cc
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Pattern rule: embed any bin/*.h file via objcopy (output keeps .h in name)
bin/%.h.o: bin/%.h
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Pattern rule: embed any cupidos-txt/*.CTXT file via objcopy
cupidos-txt/%.o: cupidos-txt/%.CTXT
	objcopy -I binary -O elf32-i386 -B i386 $< $@

%.bmp.o: %.bmp
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Pattern rule: embed any demos/*.asm file via objcopy
demos/%.o: demos/%.asm
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Pattern rule: embed any god/*.DD file via objcopy
god/%.o: god/%.DD
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Link kernel objects
$(KERNEL): $(KERNEL_OBJS)
	ld $(LDFLAGS) -o $(KERNEL) $(KERNEL_OBJS)

# Create HDD image: MBR + Stage2 + kernel area + FAT16 partition (size via HDD_MB, default 200MB)
$(OS_IMAGE): $(BOOTLOADER) $(KERNEL)
	@if [ ! -f $(OS_IMAGE) ]; then \
		echo "[make] Creating new persistent image $(OS_IMAGE) ($(HDD_MB)MB)"; \
		dd if=/dev/zero of=$(OS_IMAGE) bs=512 count=$(OS_IMAGE_SECTORS); \
		printf "$(FAT_START_LBA),,6\\n" | sfdisk $(OS_IMAGE); \
		mkfs.fat -F 16 --offset=$(FAT_START_LBA) $(OS_IMAGE) $(FAT_BLOCKS); \
	else \
		echo "[make] Reusing existing image $(OS_IMAGE) (preserving /home data)"; \
	fi
	dd if=$(BOOTLOADER) of=$(OS_IMAGE) conv=notrunc bs=1 count=446
	dd if=$(BOOTLOADER) of=$(OS_IMAGE) conv=notrunc bs=512 seek=1 skip=1 count=4
	dd if=$(KERNEL) of=$(OS_IMAGE) conv=notrunc bs=512 seek=5

# Common QEMU flags for CupidOS. USB HCs (UHCI + EHCI) + HID devices
# let the P4 USB stack enumerate on boot. Add -device usb-storage + -drive
# for mass-storage testing (see run-usb target).
QEMU_COMMON = -m 128M -boot c \
	-drive file=$(OS_IMAGE),format=raw,if=ide,index=0,media=disk \
	-rtc base=localtime \
	-audiodev $(QEMU_AUDIODEV) -machine pcspk-audiodev=speaker \
	-device piix3-usb-uhci -device usb-ehci \
	-device usb-kbd -device usb-mouse

run: $(OS_IMAGE)
	qemu-system-i386 $(QEMU_COMMON) -serial stdio

run-log: $(OS_IMAGE)
	qemu-system-i386 $(QEMU_COMMON) -serial file:debug.log

# Full P4 test: UHCI + EHCI + kbd + mouse + 32MB USB stick with FAT16 MBR.
# Creates test_usb_partitioned.img on first use.
run-usb: $(OS_IMAGE) test_usb_partitioned.img
	qemu-system-i386 $(QEMU_COMMON) \
		-drive if=none,id=ustick,file=test_usb_partitioned.img,format=raw \
		-device usb-storage,drive=ustick \
		-serial stdio

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

clean:
	rm -f $(BOOTLOADER) $(KERNEL) kernel/*.o drivers/*.o filesystem/*.o bin/*.o cupidos-txt/*.o demos/*.o \
	      kernel/bin_programs_gen.c kernel/docs_programs_gen.c kernel/demos_programs_gen.c debug.log

clean-image:
	rm -f $(OS_IMAGE)

distclean: clean clean-image

.PHONY: all run run-log sync-demos sync-iso clean clean-image distclean
