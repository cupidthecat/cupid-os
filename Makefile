# Compiler settings
.SUFFIXES:
ASM=nasm
CC=gcc
# NASA Power of 10 compliant flags: pedantic, warnings as errors, strict checks
CFLAGS=-m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc -ffreestanding -c -I./kernel -I./drivers \
       -DDEBUG -pedantic -Werror -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
       -Wmissing-prototypes -Wconversion -Wsign-conversion -Wwrite-strings
# Optimisation flags for rendering/computation-only files (no hw I/O or IRQs)
LDFLAGS=-m elf_i386 -T link.ld --oformat binary

# Auto-discover all CupidC programs in bin/
BIN_CC_SRCS := $(wildcard bin/*.cc)
$(info BIN_CC_SRCS=$(BIN_CC_SRCS))
BIN_CC_OBJS := $(BIN_CC_SRCS:.cc=.o)
BIN_CC_NAMES := $(notdir $(basename $(BIN_CC_SRCS)))

# Files
BOOTLOADER=boot/boot.bin
KERNEL=kernel/kernel.bin
OS_IMAGE=cupidos.img
KERNEL_OBJS=kernel/kernel.o kernel/idt.o kernel/isr.o kernel/irq.o kernel/pic.o \
            kernel/fs.o drivers/keyboard.o drivers/timer.o kernel/math.o drivers/pit.o \
            drivers/speaker.o kernel/shell.o kernel/string.o kernel/memory.o \
            kernel/paging.o drivers/ata.o kernel/blockdev.o kernel/blockcache.o kernel/fat16.o \
            drivers/serial.o kernel/panic.o kernel/ed.o \
            drivers/vga.o drivers/mouse.o kernel/font_8x8.o kernel/graphics.o \
            kernel/gui.o kernel/desktop.o kernel/terminal_app.o kernel/process.o kernel/context_switch.o \
            kernel/clipboard.o kernel/notepad.o kernel/ui.o \
            kernel/cupidscript_lex.o kernel/cupidscript_parse.o \
            kernel/cupidscript_exec.o kernel/cupidscript_runtime.o \
            kernel/cupidscript_streams.o kernel/cupidscript_strings.o \
            kernel/cupidscript_arrays.o kernel/cupidscript_jobs.o \
            kernel/terminal_ansi.o \
            kernel/vfs.o kernel/ramfs.o kernel/devfs.o kernel/fat16_vfs.o kernel/exec.o \
            kernel/syscall.o \
            kernel/cupidc.o kernel/cupidc_lex.o kernel/cupidc_parse.o \
            kernel/cupidc_elf.o \
            kernel/as.o kernel/as_lex.o kernel/as_parse.o kernel/as_elf.o \
            kernel/gfx2d.o \
            kernel/bmp.o \
            kernel/vfs_helpers.o \
            drivers/rtc.o kernel/calendar.o \
            kernel/gfx2d_assets.o kernel/gfx2d_transform.o kernel/gfx2d_effects.o \
            kernel/gfx2d_icons.o \
            kernel/gui_widgets.o kernel/gui_containers.o kernel/gui_menus.o \
            kernel/gui_events.o kernel/gui_themes.o \
            kernel/bin_programs_gen.o \
            $(BIN_CC_OBJS)

all: $(OS_IMAGE)

# Compile bootloader
$(BOOTLOADER): boot/boot.asm
	$(ASM) -f bin boot/boot.asm -o $(BOOTLOADER)

# Compile C source files
kernel/kernel.o: kernel/kernel.c kernel/kernel.h kernel/cpu.h
	$(CC) $(CFLAGS) kernel/kernel.c -o kernel/kernel.o

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
kernel/gui.o: kernel/gui.c kernel/gui.h
	$(CC) $(CFLAGS) $(OPT) kernel/gui.c -o kernel/gui.o

# Calendar math and formatting
kernel/calendar.o: kernel/calendar.c kernel/calendar.h
	$(CC) $(CFLAGS) $(OPT) kernel/calendar.c -o kernel/calendar.o

# Desktop shell
kernel/desktop.o: kernel/desktop.c kernel/desktop.h kernel/gfx2d_icons.h kernel/cupidc.h
	$(CC) $(CFLAGS) $(OPT) kernel/desktop.c -o kernel/desktop.o

# Terminal application
kernel/terminal_app.o: kernel/terminal_app.c kernel/terminal_app.h
	$(CC) $(CFLAGS) $(OPT) kernel/terminal_app.c -o kernel/terminal_app.o

# Process management and scheduler
kernel/process.o: kernel/process.c kernel/process.h
	$(CC) $(CFLAGS) kernel/process.c -o kernel/process.o

# Context switch (assembly)
kernel/context_switch.o: kernel/context_switch.asm
	$(ASM) -f elf32 kernel/context_switch.asm -o kernel/context_switch.o

# Clipboard
kernel/clipboard.o: kernel/clipboard.c kernel/clipboard.h
	$(CC) $(CFLAGS) $(OPT) kernel/clipboard.c -o kernel/clipboard.o

# Notepad application
kernel/notepad.o: kernel/notepad.c kernel/notepad.h
	$(CC) $(CFLAGS) $(OPT) kernel/notepad.c -o kernel/notepad.o

# UI widget toolkit
kernel/ui.o: kernel/ui.c kernel/ui.h
	$(CC) $(CFLAGS) $(OPT) kernel/ui.c -o kernel/ui.o

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

kernel/terminal_ansi.o: kernel/terminal_ansi.c kernel/terminal_ansi.h
	$(CC) $(CFLAGS) $(OPT) kernel/terminal_ansi.c -o kernel/terminal_ansi.o

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
kernel/cupidc.o: kernel/cupidc.c kernel/cupidc.h kernel/vfs.h kernel/vfs_helpers.h kernel/memory.h kernel/exec.h kernel/gfx2d_icons.h
	$(CC) $(CFLAGS) kernel/cupidc.c -o kernel/cupidc.o

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

# Auto-generate bin_programs_gen.c from all bin/*.cc files
# This generates extern declarations + install function automatically.
# To add a new CupidC program: just create bin/<name>.cc — that's it!
kernel/bin_programs_gen.c: $(BIN_CC_SRCS)
	@echo "/* Auto-generated -- do not edit. */" > $@
	@echo "/* Lists all embedded CupidC programs from bin/ directory */" >> $@
	@echo '#include "ramfs.h"' >> $@
	@echo '#include "types.h"' >> $@
	@echo '#include "../drivers/serial.h"' >> $@
	@$(foreach n,$(BIN_CC_NAMES),echo 'extern const char _binary_bin_$(n)_cc_start[];' >> $@;)
	@$(foreach n,$(BIN_CC_NAMES),echo 'extern const char _binary_bin_$(n)_cc_end[];' >> $@;)
	@echo 'void install_bin_programs(void *fs_private);' >> $@
	@echo 'void install_bin_programs(void *fs_private) {' >> $@
	@$(foreach n,$(BIN_CC_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_bin_$(n)_cc_end - _binary_bin_$(n)_cc_start); ramfs_add_file(fs_private, "bin/$(n).cc", _binary_bin_$(n)_cc_start, sz); serial_printf("[kernel] Installed /bin/$(n).cc (%u bytes)\\n", sz); }' >> $@;)
	@echo '}' >> $@

kernel/bin_programs_gen.o: kernel/bin_programs_gen.c
	$(CC) $(CFLAGS) kernel/bin_programs_gen.c -o kernel/bin_programs_gen.o

# Pattern rule: embed any bin/*.cc file via objcopy
bin/%.o: bin/%.cc
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Link kernel objects
$(KERNEL): $(KERNEL_OBJS)
	ld $(LDFLAGS) -o $(KERNEL) $(KERNEL_OBJS)

# Create disk image (boot.bin = 5 sectors: 1 boot + 4 stage2, kernel at sector 5)
$(OS_IMAGE): $(BOOTLOADER) $(KERNEL)
	dd if=/dev/zero of=$(OS_IMAGE) bs=512 count=2880
	dd if=$(BOOTLOADER) of=$(OS_IMAGE) conv=notrunc bs=512
	dd if=$(KERNEL) of=$(OS_IMAGE) conv=notrunc bs=512 seek=5

run: $(OS_IMAGE)
	qemu-system-i386 -boot a -fda $(OS_IMAGE) -rtc base=localtime -audiodev none,id=speaker -machine pcspk-audiodev=speaker -serial stdio

# Create a fresh 50MB HDD image with an MBR + FAT16 partition (type 0x06).
# Run this once before using run-disk.  Safe to re-run: destroys all disk data.
disk:
	dd if=/dev/zero of=test-disk.img bs=512 count=102400
	printf "2048,,6\n" | sfdisk test-disk.img
	mkfs.fat -F 16 --offset=2048 test-disk.img 100352

run-disk: $(OS_IMAGE)
	qemu-system-i386 -boot a -fda $(OS_IMAGE) -hda test-disk.img -rtc base=localtime -audiodev none,id=speaker -machine pcspk-audiodev=speaker -serial stdio
run-log: $(OS_IMAGE)
	qemu-system-i386 -boot a -fda $(OS_IMAGE) -rtc base=localtime -audiodev none,id=speaker -machine pcspk-audiodev=speaker -serial file:debug.log
clean:
	rm -f $(BOOTLOADER) $(KERNEL) kernel/*.o drivers/*.o filesystem/*.o bin/*.o \
	      kernel/bin_programs_gen.c $(OS_IMAGE)

.PHONY: all run clean
