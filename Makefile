# Compiler settings
ASM=nasm
CC=gcc
# NASA Power of 10 compliant flags: pedantic, warnings as errors, strict checks
CFLAGS=-m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc -ffreestanding -c -I./kernel -I./drivers \
       -pedantic -Werror -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
       -Wmissing-prototypes -Wconversion -Wsign-conversion -Wwrite-strings
LDFLAGS=-m elf_i386 -T link.ld --oformat binary

# Files
BOOTLOADER=boot/boot.bin
KERNEL=kernel/kernel.bin
OS_IMAGE=cupidos.img
KERNEL_OBJS=kernel/kernel.o kernel/idt.o kernel/isr.o kernel/irq.o kernel/pic.o \
            kernel/fs.o drivers/keyboard.o drivers/timer.o kernel/math.o drivers/pit.o \
            drivers/speaker.o kernel/shell.o kernel/string.o kernel/memory.o \
            kernel/paging.o drivers/ata.o kernel/blockdev.o kernel/blockcache.o kernel/fat16.o

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

# Link kernel objects
$(KERNEL): $(KERNEL_OBJS)
	ld $(LDFLAGS) -o $(KERNEL) $(KERNEL_OBJS)

# Create disk image
$(OS_IMAGE): $(BOOTLOADER) $(KERNEL)
	dd if=/dev/zero of=$(OS_IMAGE) bs=512 count=2880
	dd if=$(BOOTLOADER) of=$(OS_IMAGE) conv=notrunc bs=512 count=1
	dd if=$(KERNEL) of=$(OS_IMAGE) conv=notrunc bs=512 seek=1

run: $(OS_IMAGE)
	qemu-system-i386 -boot a -fda $(OS_IMAGE) -audiodev none,id=speaker -machine pcspk-audiodev=speaker

run-disk: $(OS_IMAGE)
	qemu-system-i386 -boot a -fda $(OS_IMAGE) -hda test-disk.img -audiodev none,id=speaker -machine pcspk-audiodev=speaker

clean:
	rm -f $(BOOTLOADER) $(KERNEL) kernel/*.o drivers/*.o filesystem/*.o $(OS_IMAGE)

.PHONY: all run clean
