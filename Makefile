# Compiler settings
ASM=nasm
CC=gcc
CFLAGS=-m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc -ffreestanding

# Files
BOOTLOADER=boot/boot.bin
KERNEL=kernel/kernel.bin
OS_IMAGE=cupidos.img

all: $(OS_IMAGE)

$(BOOTLOADER): boot/boot.asm
	$(ASM) -f bin boot/boot.asm -o $(BOOTLOADER)

$(KERNEL): kernel/kernel.c
	$(CC) $(CFLAGS) -c kernel/kernel.c -o kernel/kernel.o
	ld -m elf_i386 -T kernel/link.ld -o $(KERNEL) kernel/kernel.o --oformat binary

$(OS_IMAGE): $(BOOTLOADER) $(KERNEL)
	# Create a fresh floppy image
	dd if=/dev/zero of=$(OS_IMAGE) bs=512 count=2880
	# Write bootloader to first sector
	dd if=$(BOOTLOADER) of=$(OS_IMAGE) conv=notrunc bs=512 count=1
	# Write kernel starting at second sector
	dd if=$(KERNEL) of=$(OS_IMAGE) conv=notrunc bs=512 seek=1

run: $(OS_IMAGE)
	qemu-system-i386 -boot a -fda $(OS_IMAGE)

clean:
	rm -f $(BOOTLOADER) $(KERNEL) kernel/*.o $(OS_IMAGE)
