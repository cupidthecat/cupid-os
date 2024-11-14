# Compiler settings
ASM=nasm
CC=gcc
CFLAGS=-m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc -ffreestanding -c -I./kernel -I./drivers
LDFLAGS=-m elf_i386 -T link.ld --oformat binary

# Files
BOOTLOADER=boot/boot.bin
KERNEL=kernel/kernel.bin
OS_IMAGE=cupidos.img
KERNEL_OBJS=kernel/kernel.o kernel/idt.o kernel/isr.o kernel/irq.o kernel/pic.o drivers/keyboard.o drivers/timer.o kernel/math.o drivers/pit.o

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

# Link kernel objects
$(KERNEL): $(KERNEL_OBJS)
	ld $(LDFLAGS) -o $(KERNEL) $(KERNEL_OBJS)

# Create disk image
$(OS_IMAGE): $(BOOTLOADER) $(KERNEL)
	dd if=/dev/zero of=$(OS_IMAGE) bs=512 count=2880
	dd if=$(BOOTLOADER) of=$(OS_IMAGE) conv=notrunc bs=512 count=1
	dd if=$(KERNEL) of=$(OS_IMAGE) conv=notrunc bs=512 seek=1

run: $(OS_IMAGE)
	qemu-system-i386 -boot a -fda $(OS_IMAGE)

clean:
	rm -f $(BOOTLOADER) $(KERNEL) kernel/*.o drivers/*.o $(OS_IMAGE)

.PHONY: all run clean
