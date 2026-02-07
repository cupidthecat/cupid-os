# Compiler settings
ASM=nasm
CC=gcc
# NASA Power of 10 compliant flags: pedantic, warnings as errors, strict checks
CFLAGS=-m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc -ffreestanding -c -I./kernel -I./drivers \
       -DDEBUG -pedantic -Werror -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
       -Wmissing-prototypes -Wconversion -Wsign-conversion -Wwrite-strings
LDFLAGS=-m elf_i386 -T link.ld --oformat binary

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
            drivers/rtc.o kernel/calendar.o \
            bin/mv.o

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
	$(CC) $(CFLAGS) kernel/string.c -o kernel/string.o

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

# VGA graphics mode driver
drivers/vga.o: drivers/vga.c drivers/vga.h
	$(CC) $(CFLAGS) drivers/vga.c -o drivers/vga.o

# PS/2 mouse driver
drivers/mouse.o: drivers/mouse.c drivers/mouse.h
	$(CC) $(CFLAGS) drivers/mouse.c -o drivers/mouse.o

# 8x8 bitmap font
kernel/font_8x8.o: kernel/font_8x8.c kernel/font_8x8.h
	$(CC) $(CFLAGS) kernel/font_8x8.c -o kernel/font_8x8.o

# Graphics primitives
kernel/graphics.o: kernel/graphics.c kernel/graphics.h
	$(CC) $(CFLAGS) kernel/graphics.c -o kernel/graphics.o

# GUI / window manager
kernel/gui.o: kernel/gui.c kernel/gui.h
	$(CC) $(CFLAGS) kernel/gui.c -o kernel/gui.o

# Calendar math and formatting
kernel/calendar.o: kernel/calendar.c kernel/calendar.h
	$(CC) $(CFLAGS) kernel/calendar.c -o kernel/calendar.o

# Desktop shell
kernel/desktop.o: kernel/desktop.c kernel/desktop.h
	$(CC) $(CFLAGS) kernel/desktop.c -o kernel/desktop.o

# Terminal application
kernel/terminal_app.o: kernel/terminal_app.c kernel/terminal_app.h
	$(CC) $(CFLAGS) kernel/terminal_app.c -o kernel/terminal_app.o

# Process management and scheduler
kernel/process.o: kernel/process.c kernel/process.h
	$(CC) $(CFLAGS) kernel/process.c -o kernel/process.o

# Context switch (assembly)
kernel/context_switch.o: kernel/context_switch.asm
	$(ASM) -f elf32 kernel/context_switch.asm -o kernel/context_switch.o

# Clipboard
kernel/clipboard.o: kernel/clipboard.c kernel/clipboard.h
	$(CC) $(CFLAGS) kernel/clipboard.c -o kernel/clipboard.o

# Notepad application
kernel/notepad.o: kernel/notepad.c kernel/notepad.h
	$(CC) $(CFLAGS) kernel/notepad.c -o kernel/notepad.o

# UI widget toolkit
kernel/ui.o: kernel/ui.c kernel/ui.h
	$(CC) $(CFLAGS) kernel/ui.c -o kernel/ui.o

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
	$(CC) $(CFLAGS) kernel/terminal_ansi.c -o kernel/terminal_ansi.o

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

# CupidC compiler
kernel/cupidc.o: kernel/cupidc.c kernel/cupidc.h kernel/vfs.h kernel/memory.h kernel/exec.h
	$(CC) $(CFLAGS) kernel/cupidc.c -o kernel/cupidc.o

kernel/cupidc_lex.o: kernel/cupidc_lex.c kernel/cupidc.h
	$(CC) $(CFLAGS) kernel/cupidc_lex.c -o kernel/cupidc_lex.o

kernel/cupidc_parse.o: kernel/cupidc_parse.c kernel/cupidc.h
	$(CC) $(CFLAGS) kernel/cupidc_parse.c -o kernel/cupidc_parse.o

kernel/cupidc_elf.o: kernel/cupidc_elf.c kernel/cupidc.h kernel/exec.h kernel/vfs.h
	$(CC) $(CFLAGS) kernel/cupidc_elf.c -o kernel/cupidc_elf.o

# Embed CupidC source files into kernel binary via objcopy
# These become available at /bin/<name>.cc in ramfs at boot.
bin/mv.o: bin/mv.cc
	objcopy -I binary -O elf32-i386 -B i386 bin/mv.cc bin/mv.o

# Link kernel objects
$(KERNEL): $(KERNEL_OBJS)
	ld $(LDFLAGS) -o $(KERNEL) $(KERNEL_OBJS)

# Create disk image
$(OS_IMAGE): $(BOOTLOADER) $(KERNEL)
	dd if=/dev/zero of=$(OS_IMAGE) bs=512 count=2880
	dd if=$(BOOTLOADER) of=$(OS_IMAGE) conv=notrunc bs=512 count=1
	dd if=$(KERNEL) of=$(OS_IMAGE) conv=notrunc bs=512 seek=1

run: $(OS_IMAGE)
	qemu-system-i386 -boot a -fda $(OS_IMAGE) -rtc base=localtime -audiodev none,id=speaker -machine pcspk-audiodev=speaker -serial stdio

run-disk: $(OS_IMAGE)
	qemu-system-i386 -boot a -fda $(OS_IMAGE) -hda test-disk.img -rtc base=localtime -audiodev none,id=speaker -machine pcspk-audiodev=speaker -serial stdio

run-log: $(OS_IMAGE)
	qemu-system-i386 -boot a -fda $(OS_IMAGE) -rtc base=localtime -audiodev none,id=speaker -machine pcspk-audiodev=speaker -serial file:debug.log

clean:
	rm -f $(BOOTLOADER) $(KERNEL) kernel/*.o drivers/*.o filesystem/*.o $(OS_IMAGE)

.PHONY: all run clean
