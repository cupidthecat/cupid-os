# cupid-os
A modern, 32-bit operating system written in C and x86 Assembly that combines clean design with nostalgic aesthetics. The project implements core OS functionality while serving as both a learning platform and a foundation for experimental OS concepts. Inspired by systems like TempleOS and OsakaOS, cupid-os aims to provide a transparent, hands-on environment where users can directly interact with hardware.

## Example of cupid-os
![alt text for cupid-os img](img/basic_shell.png)

- Custom bootloader with protected mode transition
- Comprehensive interrupt handling system
- Advanced PS/2 keyboard driver with full US layout support
- High-precision programmable timer system
- VGA text mode graphics with custom character rendering

The goal of cupid-os is to create an accessible, well-documented operating system that serves as both a learning platform and a foundation for experimental OS concepts. Drawing inspiration from TempleOS, OsakaOS, and classic game systems, it focuses on combining technical excellence with an engaging user experience.

## Philosophy
cupid-os embraces a philosophy of complete user empowerment and transparency, inspired by TempleOS. Like TempleOS, cupid-os gives users full, unrestricted access to the entire system:

- No security boundaries or privilege levels - all code runs in ring 0
- Direct hardware access from user programs
- Full memory access with no virtual memory restrictions
- Complete visibility into and control over all system internals
- No artificial limitations or "protections" getting in the way

The goal is to create a pure, simple environment where users have complete freedom to explore, experiment, and truly understand how their computer works at the lowest level. While this approach sacrifices security and isolation, it maximizes learning potential and enables direct hardware manipulation that modern OSes restrict.

This design choice reflects our belief that users should be trusted and empowered rather than constrained. For educational and experimental purposes, having full access to bare metal is invaluable.

## Project Structure

- **boot/**  
  - **`boot.asm`** – The bootloader that runs at 0x7C00. It displays an initial boot message, loads the kernel from disk, sets up the initial environment, and transitions from 16‑bit real mode to 32‑bit protected mode.

- **kernel/**  
  - **`kernel.c`** – The main kernel file responsible for initializing the VGA, setting up the system (IDT, PIC, timer, keyboard, filesystem), calibrating the CPU timer using the TSC, and entering the shell loop.
  - **`idt.c/h`** – IDT setup and gate configuration, including exception and IRQ stubs.
  - **`isr.asm`** – Assembly routines providing common ISR/IRQ stubs that save processor state and call C‑handlers.
  - **`irq.c/h`** – IRQ handler dispatch and registration functions.
  - **`pic.c/h`** – PIC initialization, remapping, and masking/unmasking functions.
  - **`math.c/h`** – Math utilities including 64‑bit division (`udiv64`), integer-to-string conversion (`itoa`), and hexadecimal printing.
  - **`shell.c/h`** – A basic command‑line shell that parses user input and supports commands like `echo`, `mkdir`, `ls`, `cd`, and `pwd`.
  - **`string.c/h`** – Standard string manipulation functions (`strlen`, `strcmp`, `strcpy`, `strncpy`, etc.).
  - **`cpu.h`** – CPU utilities including an inline `rdtsc()` for high‑precision timing and CPU frequency detection.
  - **`kernel.h`** – Core kernel definitions and shared globals (e.g., VGA parameters, boot constants).

- **drivers/**  
  - **`keyboard.c/h`** – The PS/2 keyboard driver with:
    - Full US keyboard layout support
    - Interrupt‑driven input (IRQ1)
    - Detailed handling of modifier keys (Shift, Ctrl, Alt, Caps Lock) and extended keys (arrow keys, delete)
    - Circular buffer implementation for key event storage and configurable key repeat/debouncing
  - **`timer.c/h`** – Timer routines that configure the Intel 8253/8254 PIT:
    - System tick counter and sleep/delay functions (both millisecond and microsecond delays via TSC busy‑wait)
    - Multi‑channel support with callback registration for additional timer events
    - CPU calibration to determine frequency via the TSC
  - **`pit.c/h`** – Functions to program the PIT for square‑wave generation and configurable frequencies.
  - **`speaker.c/h`** – PC speaker driver offering functions to:
    - Turn the speaker on/off at a specified frequency
    - Play simple beeps and tones (note: the test routine is disabled to prevent interference with the keyboard driver)

- **filesystem/**  
  - **`fs.c/h`** – A basic filesystem module supporting file and directory creation, listing, and management.
  - **`path.c/h`** – Path resolution functions, including support for absolute and relative paths and maintaining the current working directory.

- **link.ld** – The linker script that defines the memory layout of the kernel image.
- **Makefile** – Build configuration for compiling the bootloader, kernel, drivers, and filesystem into a bootable disk image.
- **LICENSE** – GNU General Public License v3.

# Features

### Custom Bootloader & Protected Mode Transition 
  - Loads at `0x7C00` and sets up a simple boot message.
  - Loads the kernel from disk and switches from 16-bit real mode to 32-bit protected mode.
  - Sets up a Global Descriptor Table (GDT) for proper memory segmentation.

### Interrupt & Exception Handling  
  - Comprehensive Interrupt Descriptor Table (IDT) configuration.
  - Exception handlers with detailed error messages.
  - IRQ management with PIC remapping and custom handler registration.
  - A common ISR/IRQ stub that saves processor state before dispatch.

### PS/2 Keyboard Driver
  - Full US keyboard layout support.
  - Interrupt-driven input processing (IRQ1) with scancode-to-ASCII conversion.
  - **Enhanced Key Handling:**  
    - Support for modifier keys (Shift, Ctrl, Alt, Caps Lock) with proper state tracking.
    - Extended key support including arrow keys and the delete key.
    - Circular buffer implementation for key event storage.
    - Configurable key repeat and debouncing via timestamping.

### Timer & CPU Calibration 
  - Uses the Intel 8253/8254 Programmable Interval Timer (PIT) for system timing.
  - Provides system tick counters, sleep/delay functions, and multi-channel timer callbacks.
  - **High-Precision Timing:**  
    - Calibrates using the CPU’s Time Stamp Counter (TSC) to measure the CPU frequency.
    - Exposes `get_cpu_freq()` and `get_pit_ticks_per_ms()` for accurate timing calculations.
    
### VGA Text Mode Graphics 
  - Basic VGA driver for an 80×25 text display.
  - Functions for printing characters, strings, and integers.
  - Automatic screen scrolling and hardware cursor updates.

### PC Speaker Driver
  - **Note:** Not yet implemented.
  - Implements basic tone and beep functionality.
  - Supports configuring PIT channel 2 to generate square waves for sound output.

### Shell Interface
- **Built‑in Command Shell:** Presents a command‑line interface with a prompt.
- **Implemented Commands:**
  - `echo` – Prints text back to the console.
  - `mkdir` – Creates a new directory in the filesystem.
  - `ls` – Lists files and directories in the current (or specified) directory.
  - `cd` – Changes the current working directory.
  - `pwd` – Displays the current working directory.

- **Filesystem Integration:** Commands interact with a simple filesystem module for file and directory management.

### Utility Libraries
- **Math Library:** Offers 64‑bit division (`udiv64`), integer‑to‑string conversion (`itoa`), and hexadecimal printing.
- **String Library:** Implements common routines like `strlen`, `strcmp`, `strcpy`, and `memset`.

## Development Roadmap
The development roadmap outlined below represents our current plans and priorities. However, it's important to note that this roadmap is flexible and will evolve based on:

- New requirements discovered during development
- Technical challenges and learning opportunities encountered
- Community feedback and contributions
- Integration needs between different system components
- Performance optimization requirements
- Hardware support requirements
- Testing and debugging needs

As we progress, new phases and tasks may be added, existing ones may be modified, and priorities may shift to ensure we're building the most robust and useful system possible.

### Phase 1 - Core System Infrastructure
1. **Interrupt Handling** (✅ Complete)
   - ✅ Implement IDT (Interrupt Descriptor Table)
   - ✅ Set up basic exception handlers
   - ✅ Handle hardware interrupts
   - ✅ Implement PIC configuration
   - ✅ Add detailed error messages for exceptions
   - ✅ Support for custom interrupt handlers
   - ⭕ Basic boot sequence logging
     
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
   - ✅ Extended key support
   - ✅ Key debouncing
   - ✅ Circular buffer implementation
   - ⭕ Additional key features and custom key maps
   - ⭕ Arrow keys

3. **Timer Support** (🔄 In Progress)
   - ✅ PIT (Programmable Interval Timer) implementation
   - ✅ Basic system clock
   - ✅ Timer interrupts
   - ✅ System tick counter
   - ✅ Sleep/delay functions
   - ✅ Timer calibration
   - ✅ Multiple timer channels
   - ✅ Variable frequency support
   - X PC Speaker support [WILL BE SUPPORTED LATER]
   - 🔄 High-precision timing modes

4. **Memory Management** (⭕ Planned)
   - ⭕ Physical memory manager
   - ⭕ Simple memory allocation/deallocation
   - ⭕ Basic paging setup
   - ⭕ Memory protection
   - ⭕ Heap management
   - ⭕ Memory mapping
   - ⭕ Virtual memory support
   - ⭕ Memory statistics tracking

### Phase 2 - Extended Features
5. **Shell Interface** (🔄 In-Progress)
   - ✅ Basic command parsing and prompt display.
   - ✅ Currently supports the `echo` command.
   - ⭕ Advanced command parsing
   - ⭕ Basic shell commands
   - ⭕ Command history
   - ⭕ Tab completion

7. **Process Management** (⭕ Planned)
   - ⭕ Process creation/termination
   - ⭕ Basic scheduling
   - ⭕ Process states
   - ⭕ Context switching

8. **Basic Device Drivers** (⭕ Planned)
   - ✅ PS/2 Keyboard
   - ⭕ VGA graphics
   - ⭕ Serial port
   - ⭕ Real-time clock

9. **Simple Filesystem** (⭕ Planned)
   - ✅ Basic file operations and directory structure  
   - ✅ Path resolution and current directory tracking  
   - ⭕ File permissions and extended attributes


### Phase 3 - Advanced Features
10. Custom compiler
11. Advanced memory management
12. Extended device support
13. Multi-process scheduling
14. Custom music program in dedication to Terry A. Davis

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

## Recent Updates
- Implemented comprehensive keyboard driver with full modifier key support
- Added function key handling (F1-F12)
- Implemented key repeat functionality with configurable delays
- Added debouncing support for more reliable key input
- Enhanced exception handling with detailed error messages
- Implemented basic PIT timer with system tick counter
- Added initial delay/sleep functionality using timer ticks
