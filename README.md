# cupid-os
A minimal operating system written in C and x86 Assembly. This project demonstrates basic OS concepts including bootloader implementation, protected mode switching, interrupt handling, and basic screen I/O.

What I want cupid-os to be: 
- A multitasking, 32bit OS with a graphical user interface, that focuses on simplicity with inspiration from video games like menus and osakaOS
- Another inspiration is the unix and linux kernel as well as the distros that comes from the linux kernel
- I want it to be a space where I can grow as a operating systems engineer and explore new ideas

## Structure
- `boot/` - Bootloader code (16-bit to 32-bit mode transition)
- `kernel/` - Kernel source code (32-bit protected mode C code)
  - `kernel.c` - Main kernel file
  - `idt.c/h` - Interrupt Descriptor Table implementation
  - `isr.c/h` - Interrupt Service Routines
  - `types.h` - Custom type definitions
  - `isr.asm` - Assembly interrupt service routines
- `Makefile` - Build system

## Features
- Custom bootloader that switches from 16-bit real mode to 32-bit protected mode
- Basic VGA text mode driver (80x25 characters)
- Screen output with support for:
  - Character printing
  - String printing
  - Newline handling
  - Screen scrolling
  - Screen clearing
- Interrupt handling:
  - Interrupt Descriptor Table (IDT)
  - Basic exception handlers
  - Hardware interrupt support
  - PIC (Programmable Interrupt Controller) configuration
- Keyboard support:
  - PS/2 keyboard driver
  - Scancode to ASCII mapping
  - Key state tracking
  - Event buffer system

## Development Roadmap
### Phase 1 - Core System Infrastructure
1. **Interrupt Handling** (✅ Complete)
   - ✅ Implement IDT (Interrupt Descriptor Table)
   - ✅ Set up basic exception handlers
   - ✅ Handle hardware interrupts
   - ✅ Implement PIC configuration

2. **Keyboard Input** (✅ Complete)
   - ✅ Implement PS/2 keyboard driver
   - ✅ Basic input buffer
   - ✅ Scancode handling
   - ✅ Input event processing
   - ✅ Keyboard state management
   - ✅ Modifier key support (Shift, Caps Lock)
   - ✅ Additional modifier keys (Ctrl, Alt)
   - ✅ Key repeat handling
   - ✅ Function keys support

3. **Timer Support** (🔄 In Progress - Next Priority)
   - ⭕ PIT (Programmable Interval Timer) implementation
   - ⭕ Basic system clock
   - ⭕ Timer interrupts
   - ⭕ System tick counter
   - ⭕ Sleep/delay functions

4. **Memory Management** (⭕ Planned)
   - ⭕ Physical memory manager
   - ⭕ Simple memory allocation/deallocation
   - ⭕ Basic paging setup
   - ⭕ Memory protection
   - ⭕ Heap management

### Phase 2 - Extended Features
5. **Shell Interface** (⭕ Planned)
   - ⭕ Command parsing
   - ⭕ Basic shell commands
   - ⭕ Command history
   - ⭕ Tab completion

6. **Process Management** (⭕ Planned)
   - ⭕ Process creation/termination
   - ⭕ Basic scheduling
   - ⭕ Process states
   - ⭕ Context switching

7. **Basic Device Drivers** (⭕ Planned)
   - ✅ PS/2 Keyboard
   - ⭕ VGA graphics
   - ⭕ Serial port
   - ⭕ Real-time clock

8. **Simple Filesystem** (⭕ Planned)
   - ⭕ Basic file operations
   - ⭕ Directory structure
   - ⭕ File permissions
   
### Phase 3 - Advanced Features
10. Custom compiler
11. Advanced memory management
12. Extended device support
13. Multi-process scheduling

## Example of cupid-os
![alt text for cupid-os img](img/os.png)

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

### Kernel Components
#### Main Kernel (`kernel/kernel.c`)
- Entry point at 0x1000
- Implements basic screen I/O
- VGA text mode driver
- System initialization
- IDT initialization

#### Interrupt System (`kernel/idt.c`, `kernel/isr.asm`)
- Complete IDT setup and management
- Exception handlers with detailed error messages
- Hardware interrupt support (IRQ0-15)
- Programmable Interrupt Controller (PIC) configuration
- Custom interrupt handler registration
- Debug exception handling

#### Input System (`drivers/keyboard.c`, `drivers/keyboard.h`)
- PS/2 keyboard driver with:
  - Full US keyboard layout support
  - Shift and Caps Lock modifiers
  - Key state tracking
  - Interrupt-driven input handling (IRQ1)
  - Debouncing support
  - Extended key support (e.g. right ctrl/alt)
  - Circular buffer for key events
  - Support for special keys (backspace, tab, enter)

### Memory Layout
- Bootloader: 0x7C00
- Kernel: 0x1000
- Stack: 0x90000
- IDT: Dynamically allocated

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
GNU v3

