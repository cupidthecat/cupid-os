# Compiler settings
ASM=nasm
CC=gcc
CFLAGS=-m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc -ffreestanding -c -I./kernel -I./drivers
LDFLAGS=-m elf_i386 -T link.ld --oformat binary

# Files
BOOTLOADER=boot/boot.bin
KERNEL=kernel/kernel.bin
OS_IMAGE=cupidos.img
KERNEL_OBJS=kernel/kernel.o kernel/idt.o kernel/isr.o kernel/irq.o kernel/pic.o \
            drivers/keyboard.o drivers/timer.o kernel/math.o drivers/pit.o \
            drivers/mouse.o drivers/speaker.o kernel/shell.o kernel/string.o \
            filesystem/fs.o filesystem/path.o drivers/vga.o drivers/desktop.o \
            drivers/window.o kernel/font.o

# Add objcopy definition at the top
OBJCOPY=objcopy

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
# Add new rule for desktop.o
drivers/desktop.o: drivers/desktop.c drivers/desktop.h
	$(CC) $(CFLAGS) drivers/desktop.c -o drivers/desktop.o

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

# Add new rule for shell.o
kernel/shell.o: kernel/shell.c kernel/shell.h
	$(CC) $(CFLAGS) kernel/shell.c -o kernel/shell.o

# Add new rule for string.o
kernel/string.o: kernel/string.c kernel/string.h
	$(CC) $(CFLAGS) kernel/string.c -o kernel/string.o

# Add new rule for fs.o
filesystem/fs.o: filesystem/fs.c filesystem/fs.h
	$(CC) $(CFLAGS) filesystem/fs.c -o filesystem/fs.o

# Add new rule for path.o
filesystem/path.o: filesystem/path.c filesystem/path.h
	$(CC) $(CFLAGS) filesystem/path.c -o filesystem/path.o

# Add new rule for vga.o
drivers/vga.o: drivers/vga.c drivers/vga.h
	$(CC) $(CFLAGS) drivers/vga.c -o drivers/vga.o

# Add new rule for mouse.o
drivers/mouse.o: drivers/mouse.c drivers/mouse.h
	$(CC) $(CFLAGS) drivers/mouse.c -o drivers/mouse.o

# Add new rule for font.o
kernel/font.o: zap-light16.psf
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 zap-light16.psf drivers/font.o

# Add new rule for window.o
drivers/window.o: drivers/window.c drivers/window.h
	$(CC) $(CFLAGS) drivers/window.c -o drivers/window.o

# Link kernel objects
$(KERNEL): $(KERNEL_OBJS)
	ld $(LDFLAGS) -o $(KERNEL) $(KERNEL_OBJS)

# Create disk image
$(OS_IMAGE): $(BOOTLOADER) $(KERNEL)
	dd if=/dev/zero of=$(OS_IMAGE) bs=512 count=2880
	dd if=$(BOOTLOADER) of=$(OS_IMAGE) conv=notrunc bs=512 count=1
	dd if=$(KERNEL) of=$(OS_IMAGE) conv=notrunc bs=512 seek=1

run: $(OS_IMAGE)
	qemu-system-i386 -boot a -fda $(OS_IMAGE) -audiodev pa,id=speaker -machine pcspk-audiodev=speaker

clean:
	rm -f $(BOOTLOADER) $(KERNEL) kernel/*.o drivers/*.o filesystem/*.o $(OS_IMAGE)

.PHONY: all run clean
