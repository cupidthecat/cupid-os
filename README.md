# CupidOS
A minimal operating system written in C and x86 Assembly. This project demonstrates basic OS concepts including bootloader implementation, protected mode switching, and basic screen I/O.

## Structure
- `boot/` - Bootloader code (16-bit to 32-bit mode transition)
- `kernel/` - Kernel source code (32-bit protected mode C code)
- `Makefile` - Build system

## Todo
- [ ]
## Features
- Custom bootloader that switches from 16-bit real mode to 32-bit protected mode
- Basic VGA text mode driver (80x25 characters)
- Screen output with support for:
  - Character printing
  - String printing
  - Newline handling
  - Screen scrolling
  - Screen clearing

## Requirements
- NASM (Netwide Assembler) for bootloader compilation
- GCC (32-bit support required)
- GNU Make
- QEMU for testing (qemu-system-i386)
- Linux environment (or equivalent Unix-like system)

## Building
1. Install dependencies (Ubuntu/Debian):
```bash
sudo apt-get install nasm gcc make qemu-system-x86
```

2. Build the OS:
```bash
make
```

3. Run in QEMU:
```bash
make run
```

## Project Structure Details
### Bootloader (`boot/boot.asm`)
- Loads at 0x7C00 (BIOS loading point)
- Sets up initial environment
- Loads kernel from disk
- Switches to protected mode
- Jumps to kernel at 0x1000

### Kernel (`kernel/kernel.c`)
- Entry point at 0x1000
- Implements basic screen I/O
- VGA text mode driver
- Basic system initialization

### Memory Layout
- Bootloader: 0x7C00
- Kernel: 0x1000
- Stack: 0x90000

## Development
To modify or extend the OS:

1. Bootloader changes:
   - Edit `boot/boot.asm`
   - Modify GDT if adding memory segments
   - Update kernel loading if kernel size changes

2. Kernel changes:
   - Edit `kernel/kernel.c`
   - Update `kernel/link.ld` if changing memory layout
   - Modify Makefile if adding new source files

## Debugging
1. Debug with QEMU monitor:
```bash
make run
# Press Ctrl+Alt+2 for QEMU monitor
```

2. Debug with GDB:
```bash
# Terminal 1
qemu-system-i386 -s -S -boot a -fda cupidos.img

# Terminal 2
gdb
(gdb) target remote localhost:1234
```

## Contributing
1. Fork the repository
2. Create your feature branch
3. Commit your changes
4. Push to the branch
5. Create a Pull Request

## License
[Your chosen license]

## Acknowledgments
- [Any resources or inspirations you used]

