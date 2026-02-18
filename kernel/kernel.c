/**
 * kernel.c - Core kernel functionality for cupid-os
 * 
 * This file implements the main kernel functionality including:
 * - Core kernel initialization and entry point (_start and kmain)
 * - VGA text mode driver with 80x25 character display
 * - Screen output functions (print, putchar)
 * - Screen manipulation (clear_screen, cursor movement) 
 * - Port I/O functions (inb/outb) for hardware interaction
 * - Interrupt handling setup (PIC, IDT initialization)
 * - PS/2 keyboard driver initialization and interrupt handling
 * - Timer calibration and frequency measurement
 * - System timing services via PIT channels
 * - Main kernel loop with interrupt handling and power management
 */

#include "idt.h"
#include "pic.h"
#include "kernel.h"
#include "debug.h"
#include "ports.h"
#include "shell.h"
#include "types.h"
#include "../drivers/speaker.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../drivers/ata.h"
#include "../drivers/serial.h"
#include "../drivers/vga.h"
#include "../drivers/mouse.h"
#include "blockdev.h"
#include "blockcache.h"
#include "fat16.h"
#include "fs.h"
#include "memory.h"
#include "simd.h"
#include "string.h"
#include "graphics.h"
#include "gfx2d.h"
#include "gui.h"
#include "desktop.h"
#include "clipboard.h"
#include "process.h"
#include "vfs.h"
#include "ramfs.h"
#include "devfs.h"
#include "fat16_vfs.h"
#include "exec.h"
#include "syscall.h"
#include "../drivers/pit.h"
#include "../drivers/rtc.h"

#define PIT_FREQUENCY 1193180     // Base PIT frequency in Hz
#define CALIBRATION_MS 250        // Time to calibrate over (in milliseconds)

static uint64_t tsc_freq = 0;    // Stores CPU timestamp frequency
static uint32_t pit_ticks_per_ms = 0; // Stores PIT ticks per millisecond

// Assembly entry point
void _start(void) __attribute__((section(".text.start")));
// Main kernel function
void kmain(void);

// Screen dimensions
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

// Colors
#define VGA_BLACK 0
#define VGA_LIGHT_GREY 7
#define VGA_WHITE 15

// Screen position
int cursor_x = 0;
int cursor_y = 0;

// Global tick counters
static uint32_t ticks_channel0 = 0;
static uint32_t ticks_channel1 = 0;

/* Deferred reschedule flag - set inside IRQ, checked at safe points */
static volatile bool need_reschedule = false;

extern uint32_t _kernel_end;
extern uint32_t _bss_start;  /* Linker symbol: start of BSS at 0x100000 */

/* The Makefile auto-discovers bin/ .cc files and generates
 * kernel/bin_programs_gen.c with all extern symbols and an
 * install_bin_programs() function.  To add a new CupidC program:
 *   1. Create bin/<name>.cc
 *   2. Run make
 * That's it - everything else is automatic. 
 */
extern void install_bin_programs(void *fs_private);
extern void install_docs_programs(void *fs_private);
extern void install_demo_programs(void *fs_private);

/**
 * timer_callback_channel0 - Timer callback for channel 0
 * 
 * Increments the ticks_channel0 counter when called with channel 0.
 * 
 * @param r: Pointer to the registers structure
 * @param channel: The timer channel (0 in this case)
 */
void timer_callback_channel0(struct registers* r, uint32_t channel) {
    (void)r; /* Unused parameter */
    if (channel == 0) {
        ticks_channel0++;

        /* Mark that a reschedule is needed - the actual context switch
         * happens at a safe voluntary point (desktop_run loop / yield),
         * NOT inside the IRQ handler where stack manipulation is unsafe. */
        if (process_is_active()) {
            need_reschedule = true;
        }
    }
}
/**
 * timer_callback_channel1 - Timer callback for channel 1
 * 
 * Increments the ticks_channel1 counter when called with channel 1.
 * 
 * @param r: Pointer to the registers structure
 * @param channel: The timer channel (1 in this case)
 */
void timer_callback_channel1(struct registers* r, uint32_t channel) {
    (void)r; /* Unused parameter */
    if (channel == 1) {
        ticks_channel1++;
    }
}

/**
 * timer_get_ticks_channel - Get the tick count for a specific timer channel
 * 
 * Returns the current tick count for the specified timer channel.
 * - Channel 0: System tick counter
 * - Channel 1: Reserved for future use (currently unused)
 * 
 * @param channel: The timer channel to get ticks for (0 or 1)
 * @return: The current tick count for the specified channel
 */
/**
 * kernel_check_reschedule - Check and perform deferred context switch
 *
 * Called from safe voluntary points (desktop event loop, process_yield)
 * where ESP/EBP manipulation won't corrupt an IRQ stack frame.
 */
void kernel_check_reschedule(void) {
    if (need_reschedule && process_is_active()) {
        need_reschedule = false;
        schedule();
    }
}

void kernel_clear_reschedule(void) {
    need_reschedule = false;
}

uint32_t timer_get_ticks_channel(uint32_t channel) {
    if (channel == 0) {
        return ticks_channel0;
    } else if (channel == 1) {
        return ticks_channel1;
    }
    return 0;
}

#define VGA_CTRL_REGISTER 0x3D4
#define VGA_DATA_REGISTER 0x3D5
#define VGA_OFFSET_LOW 0x0F
#define VGA_OFFSET_HIGH 0x0E

void print(const char* str);
void putchar(char c);
void clear_screen(void);
void init_vga(void);

/**
 * init_vga - Initialize the VGA text mode display
 * 
 * This function initializes the VGA text mode display by:
 * - Resetting the hardware cursor position to (0,0)
 * - Clearing the screen with light grey text on black background
 * - Resetting the software cursor position variables
 * - Printing an initialization message
 */
void init_vga(void) {
    // Reset the cursor position
    outb(VGA_CTRL_REGISTER, VGA_OFFSET_HIGH);
    outb(VGA_DATA_REGISTER, 0);
    outb(VGA_CTRL_REGISTER, VGA_OFFSET_LOW);
    outb(VGA_DATA_REGISTER, 0);
    
    // Clear the screen with a known good attribute
    volatile char* vidmem = (char*)VGA_MEMORY;
    for(int i = 0; i < VGA_WIDTH * VGA_HEIGHT * 2; i += 2) {
        vidmem[i] = ' ';           // Space character
        vidmem[i + 1] = 0x07;      // Light grey on black
    }
    
    // Reset cursor position variables
    cursor_x = 0;
    cursor_y = 0;
    
    print("VGA initialized.\n");
}

/**
 * clear_screen - Clears the entire VGA text buffer and resets cursor position
 * 
 * This function:
 * - Fills the entire VGA text buffer with space characters
 * - Sets each character's attribute to light grey on black (0x07)
 * - Resets both X and Y cursor coordinates to 0
 * 
 * Implementation details:
 * - VGA text buffer is accessed directly at VGA_MEMORY
 * - Each character cell takes 2 bytes:
 *   - First byte: ASCII character (space in this case)
 *   - Second byte: Attribute byte (0x07 = light grey on black)
 * - Buffer size is VGA_WIDTH * VGA_HEIGHT characters
 */
void clear_screen() {
    volatile char* vidmem = (char*)VGA_MEMORY;
    for(int i = 0; i < VGA_WIDTH * VGA_HEIGHT * 2; i += 2) {
        vidmem[i] = ' ';           // Space character
        vidmem[i + 1] = 0x07;      // Light grey on black
    }
    cursor_x = 0;
    cursor_y = 0;
}

/**
 * putchar - Outputs a single character to the VGA text buffer
 * 
 * Displays a character at the current cursor position and advances the cursor.
 * Handles special characters like newline, screen wrapping, and scrolling.
 * Updates both the software cursor position and hardware cursor.
 *
 * @param c: The character to display
 *
 * Implementation details:
 * - Each character takes 2 bytes in video memory:
 *   - First byte: ASCII character
 *   - Second byte: Attribute (color/style)
 * - Uses light grey on black (0x07) for character attributes
 * - Handles screen boundaries:
 *   - Wraps to next line when reaching end of line
 *   - Scrolls screen up when reaching bottom
 * - Updates hardware cursor position via VGA registers
 */
void putchar(char c) {
    /* Route to GUI buffer when in GUI mode */
    if (shell_get_output_mode() == SHELL_OUTPUT_GUI) {
        shell_gui_putchar_ext(c);
        return;
    }

    volatile unsigned char* vidmem = (unsigned char*)VGA_MEMORY;
    
    if(c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if(c == '\b') {  // Handle backspace (move cursor only)
        if(cursor_x > 0) {
            cursor_x--;
        } else if(cursor_y > 0) {
            cursor_y--;
            cursor_x = VGA_WIDTH - 1;
        }
    } else {
        int offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;
        vidmem[offset] = (unsigned char)c;
        vidmem[offset + 1] = 0x07;  // Light grey on black
        cursor_x++;
    }
    
    // Handle screen scrolling
    if(cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    
    if(cursor_y >= VGA_HEIGHT) {
        // Scroll the screen
        for(int i = 0; i < (VGA_HEIGHT-1) * VGA_WIDTH * 2; i++) {
            vidmem[i] = vidmem[i + VGA_WIDTH * 2];
        }
        
        // Clear the last line
        int last_line = (VGA_HEIGHT-1) * VGA_WIDTH * 2;
        for(int i = 0; i < VGA_WIDTH * 2; i += 2) {
            vidmem[last_line + i] = ' ';
            vidmem[last_line + i + 1] = 0x07;
        }
        cursor_y = VGA_HEIGHT - 1;
    }
    
    // Update hardware cursor
    int pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(VGA_CTRL_REGISTER, VGA_OFFSET_HIGH);
    outb(VGA_DATA_REGISTER, (uint8_t)((pos >> 8) & 0xFF));
    outb(VGA_CTRL_REGISTER, VGA_OFFSET_LOW);
    outb(VGA_DATA_REGISTER, (uint8_t)(pos & 0xFF));
}

/**
 * print_int - Prints an unsigned 32-bit integer to the screen
 * 
 * Converts the number to a string by repeatedly dividing by 10 and storing
 * the digits in a buffer. Since division gives digits in reverse order,
 * stores them in a buffer first then prints in reverse to display correctly.
 * Special cases the value 0 to avoid buffer manipulation.
 *
 * @param num: The unsigned 32-bit integer to print
 *
 * Implementation notes:
 * - Uses a fixed 10-byte buffer which is sufficient for 32-bit integers
 * - Converts digits to ASCII by adding '0' (0x30)
 * - Prints digits in reverse order to maintain correct number representation
 */
void print_int(uint32_t num) {
    serial_printf("[print_int] num=%u (0x%x) gui_mode=%d\n",
                  num, num, shell_get_output_mode() == SHELL_OUTPUT_GUI);

    /* Route to GUI buffer when in GUI mode */
    if (shell_get_output_mode() == SHELL_OUTPUT_GUI) {
        shell_gui_print_int_ext(num);
        return;
    }
    char buffer[10];
    int i = 0;
    if (num == 0) {
        putchar('0');
        return;
    }
    while (num > 0) {
        buffer[i++] = (char)((num % 10) + (uint32_t)'0');
        num /= 10;
    }
    while (i > 0) {
        putchar(buffer[--i]);
    }
}

/**
 * print - Outputs a null-terminated string to the VGA text buffer
 * 
 * Iterates through each character in the provided string and displays it
 * on screen using putchar(). Handles special characters like newlines and
 * automatically wraps text and scrolls when reaching screen boundaries.
 * 
 * @str: Pointer to the null-terminated string to print
 */
void print(const char* str) {
    /* Route to GUI buffer when in GUI mode */
    if (shell_get_output_mode() == SHELL_OUTPUT_GUI) {
        shell_gui_print_ext(str);
        return;
    }
    for(int i = 0; str[i] != '\0'; i++) {
        putchar(str[i]);
    }
}
/**
 * _start - Entry point for the kernel
 * 
 * This is the first function called after the bootloader hands control to the kernel.
 * It sets up the initial execution environment by:
 *
 * 1. Setting up the stack:
 *    - Sets stack pointer (ESP) to 0x190000 for kernel stack space (512KB stack)
 *    - Stack grows downward from 0x190000 to 0x110000
 *    - Initializes base pointer (EBP) to match stack pointer
 *
 * 2. Transferring control:
 *    - Calls kmain() to begin kernel initialization
 *    - kmain() never returns as it contains the main kernel loop
 *
 * Note: When this function runs, we are already in 32-bit protected mode
 * with basic segment registers configured by the bootloader.
 */
void _start(void) {
    // We're already in protected mode with segments set up.
    // BSS follows text+data above 1MB and must be zeroed explicitly.
    __asm__ volatile(
        "mov $0x880000, %%esp\n"
        "mov %%esp, %%ebp\n"
        /* Zero BSS region (_bss_start to _kernel_end) */
        "mov $_bss_start, %%edi\n"
        "mov $_kernel_end, %%ecx\n"
        "sub %%edi, %%ecx\n"
        "shr $2, %%ecx\n"
        "xor %%eax, %%eax\n"
        "cld\n"
        "rep stosl\n"
        ::: "eax", "ecx", "edi", "memory"
    );
    
    // Call main kernel function
    kmain();
}

/**
 * rdtsc - Read the CPU's Time Stamp Counter
 * 
 * Uses the RDTSC instruction to read the CPU's internal timestamp counter,
 * which increments at the CPU's frequency. The counter value is returned
 * as a 64-bit number combining the high and low 32-bit parts.
 * 
 * Used for high-precision timing and CPU frequency calibration.
 * 
 * @return: 64-bit TSC value
 */
static inline uint64_t rdtsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

/**
 * calibrate_timer - Calibrate system timer using CPU timestamp counter
 * 
 * This function calibrates the system timer by:
 * 1. Configuring PIT channel 0 in one-shot mode
 * 2. Using the CPU's timestamp counter (TSC) to measure elapsed time
 * 3. Calculating the CPU frequency based on TSC measurements
 * 4. Computing PIT ticks per millisecond for timing calculations
 * 
 * The calibration process:
 * - Sets PIT to maximum count and waits for CALIBRATION_MS milliseconds
 * - Measures TSC values before and after to determine CPU frequency
 * - Handles both high and low frequency CPUs by adjusting calculation method
 * - Resets PIT to normal operation when complete
 * - Prints calibration results showing CPU frequency in MHz
 * 
 * Results are stored in global variables:
 * - tsc_freq: CPU frequency in Hz
 * - pit_ticks_per_ms: PIT ticks per millisecond for timing
 */
void calibrate_timer(void) {
    // Configure PIT channel 0 for one-shot mode
    outb(0x43, 0x30);    // Channel 0, one-shot mode, binary
    
    // Calculate maximum safe duration for one-shot mode (55ms)
    uint32_t max_ticks = 0xFFFF;
    uint32_t actual_ms = (max_ticks * 1000) / PIT_FREQUENCY;
    if (actual_ms == 0) actual_ms = 1; // Avoid divide-by-zero
    if(actual_ms > 50) actual_ms = 50;  // Clamp to 50ms max
    
    // Set initial count to maximum safe value
    uint16_t initial_count = (uint16_t)((PIT_FREQUENCY * actual_ms) / 1000);
    if (initial_count == 0) initial_count = 1; // Ensure non-zero reload
    outb(0x40, (uint8_t)(initial_count & 0xFF));
    outb(0x40, (uint8_t)((initial_count >> 8) & 0xFF));
    
    // Get starting TSC value
    uint64_t start_tsc = rdtsc();
    
    // Wait for PIT to count down
    uint32_t count;
    do {
        outb(0x43, 0x00); // Latch counter
        count = inb(0x40);
        count |= inb(0x40) << 8;
    } while(count > 0 && count < 0xFFFF);
    
    // Get ending TSC value
    uint64_t end_tsc = rdtsc();
    
    // Calculate CPU frequency with 128-bit intermediate
    uint64_t tsc_diff = end_tsc - start_tsc;
    uint64_t freq_hz = (tsc_diff * 1000) / actual_ms;
    
    // Fallback checks
    if(freq_hz < 1000000) {  // Below 1MHz is impossible for modern CPUs
        freq_hz = 1000000000; // Default to 1GHz
    }
    
    tsc_freq = freq_hz;
    pit_ticks_per_ms = PIT_FREQUENCY / 1000;
    
    // Reset PIT to normal operation
    timer_init(100);

    // Register channel 0 callback for preemptive scheduling
    timer_configure_channel(0, 100, timer_callback_channel0);
    
    //print("Timer calibration complete:\n");
    //debug_print_int("Calibration Window (ms): ", actual_ms);
    //debug_print_int("CPU Frequency (MHz): ", (uint32_t)(tsc_freq / 1000000));
}

/**
 * get_cpu_freq - Get the calibrated CPU frequency
 * 
 * Returns the CPU frequency in Hz that was measured during timer calibration.
 * This value represents the number of CPU cycles per second and is used for
 * precise timing calculations.
 *
 * @return: The CPU frequency in Hz as measured by the TSC during calibration
 */
uint64_t get_cpu_freq(void) {
    return tsc_freq;
}

/**
 * get_pit_ticks_per_ms - Get the number of PIT ticks per millisecond
 * 
 * Returns the calibrated number of PIT (Programmable Interval Timer) ticks
 * that occur in one millisecond. This value is determined during timer
 * calibration and is used for accurate timing calculations.
 *
 * @return: The number of PIT ticks per millisecond
 */
uint32_t get_pit_ticks_per_ms(void) {
    return pit_ticks_per_ms;
}

/**
 * kmain - Main kernel entry point
 * 
 * This function initializes core kernel subsystems and drivers:
 * - VGA text mode display initialization for console output
 * - PIC (Programmable Interrupt Controller) setup for hardware interrupts
 * - IDT (Interrupt Descriptor Table) configuration for interrupt handling
 * - PS/2 keyboard driver initialization with event buffer
 * - PIT (Programmable Interval Timer) calibration and setup
 * - System timer configuration for accurate timing
 *
 * After initialization, the kernel enters an idle loop where interrupts
 * drive system activity through keyboard input processing and timer events.
 * The system remains in this state indefinitely, with the CPU halted between
 * interrupt events to conserve power.
 */
void kmain(void) {
    // Initialize serial port first for debug output
    serial_init();
    KINFO("cupid-os booting...");

    // Initialize memory management
    pmm_init((uint32_t)&_kernel_end);
    heap_init(HEAP_INITIAL_PAGES);
    paging_init();
    KINFO("Memory management initialized");

    // Initialize interrupts and drivers
    idt_init();
    pic_init();
    keyboard_init();
    calibrate_timer();
    KINFO("Interrupts and timers initialized");

    // Initialize filesystem
    fs_init();

    // Initialize RTC (Real-Time Clock)
    rtc_init();

    // Initialize ATA disk driver
    ata_init();

    // Initialize block device layer and register ATA drives
    blkdev_init();
    ata_register_devices();

    int fat16_ready = 0;

    // Initialize block cache for first drive
    block_device_t* hdd = blkdev_get(0);
    if (hdd) {
        if (blockcache_init(hdd) != 0) {
            KERROR("Block cache initialization failed");
        } else {
            // Set up periodic flush every 5 seconds using timer channel 1
            timer_configure_channel(1, 100, blockcache_periodic_flush);
            KINFO("Block cache: periodic flush enabled (5s interval)");
        }

        // Initialize FAT16 filesystem
        if (fat16_init() == 0) {
            fat16_ready = 1;
            KINFO("FAT16 mounted at /disk");
        } else {
            KERROR("FAT16 init failed; /home will not be mounted");
        }
    } else {
        KERROR("No block device available; /home FAT16 mount skipped");
    }

    /* VFS initialization */
    vfs_init();
    vfs_register_fs(ramfs_get_ops());
    vfs_register_fs(devfs_get_ops());
    vfs_register_fs(fat16_vfs_get_ops());

    /* Mount root filesystem (ramfs) */
    if (vfs_mount(NULL, "/", "ramfs") == VFS_OK) {
        KINFO("VFS: mounted ramfs on /");
    }

    /* Create standard directories */
    vfs_mkdir("/bin");
    vfs_mkdir("/docs");
    vfs_mkdir("/docs/demos");
    vfs_mkdir("/demos");
    vfs_mkdir("/tmp");
    vfs_mkdir("/home");

    /* Mount devfs at /dev */
    devfs_register_builtins();
    if (vfs_mount(NULL, "/dev", "devfs") == VFS_OK) {
        KINFO("VFS: mounted devfs on /dev");
    }

    /* Mount FAT16 at /home (persistent user files) */
    if (hdd && fat16_ready) {
        int rc = vfs_mount(NULL, "/home", "fat16");
        if (rc == VFS_OK) {
            KINFO("VFS: mounted fat16 on /home");
        } else {
            KERROR("VFS: failed to mount fat16 on /home (%d)", rc);
            KERROR("/home is not persistent (using root ramfs directory)");
        }
    } else {
        KERROR("/home is not persistent (FAT16 unavailable)");
    }

    /* Pre-populate ramfs with in-memory files */
    {
        const vfs_mount_t *root_mnt = vfs_get_mount(0);
        if (root_mnt && root_mnt->fs_private) {
            uint32_t fcount = fs_get_file_count();
            for (uint32_t fi = 0; fi < fcount; fi++) {
                const fs_file_t *f = fs_get_file(fi);
                if (f && f->name && f->data) {
                    ramfs_add_file(root_mnt->fs_private,
                                   f->name, f->data, f->size);
                }
            }

            /* Populate /bin with built-in application stubs.
             * Each stub contains a description so ls shows distinct sizes. */
            static const struct { const char *name; const char *desc; } bin_apps[] = {
                {"terminal", "CupidOS GUI terminal emulator"},
                {"notepad",  "CupidOS GUI text editor (Notepad)"},
                {"cupid",    "CupidScript interpreter (.cup files)"},
                {"shell",    "CupidOS interactive shell"},
                {"exec",     "Execute an ELF or CUPD binary"},
                {NULL, NULL}
            };
            for (int bi = 0; bin_apps[bi].name; bi++) {
                char bpath[64];
                int bp = 0;
                const char *pfx = "bin/";
                while (*pfx) bpath[bp++] = *pfx++;
                const char *bn = bin_apps[bi].name;
                while (*bn && bp < 62) bpath[bp++] = *bn++;
                bpath[bp] = '\0';
                ramfs_add_file(root_mnt->fs_private,
                               bpath, bin_apps[bi].desc,
                               (uint32_t)strlen(bin_apps[bi].desc));
            }
            KINFO("Populated /bin with built-in stubs");

            /* Embedded CupidC programs 
             * Auto-installed from all bin/ .cc files via generated code.
             * To add a new program: just create bin/<name>.cc 
             */
            install_bin_programs(root_mnt->fs_private);
            KINFO("Installed embedded CupidC programs");

            /* Embedded CupidDoc files
            * Auto-installed from cupidos-txt .CTXT files into /docs .ctxt 
            */
            install_docs_programs(root_mnt->fs_private);
            KINFO("Installed embedded CupidDoc files");

            /* Embedded CupidASM demo files
            * Auto-installed from demos .asm files into /demos .asm */
            install_demo_programs(root_mnt->fs_private);
            KINFO("Installed embedded CupidASM demos");
        }
    }
    KINFO("VFS initialized");

    KINFO("System Timer Frequency: %u Hz", timer_get_frequency());
    KINFO("CPU Frequency: %u MHz", (uint32_t)(get_cpu_freq() / 1000000));
    KINFO("Total Memory: %u MB", TOTAL_MEMORY_BYTES / 1024 / 1024);
    KINFO("Total Pages: %u", pmm_total_pages());
    KINFO("Free Pages: %u", pmm_free_pages());

    simd_init();
    KINFO("SIMD SSE2: %s", simd_enabled() ? "enabled" : "disabled");

    // Initialize VBE graphics (mode set by bootloader, LFB addr at 0x0500)
    vga_init_vbe();          // Allocates back buffer and clears screen
    gfx_init();              // Initialize graphics primitives
    gfx2d_init();            // Initialize 2D graphics library
#ifdef SIMD_BENCH
    simd_benchmark();
#endif
    KINFO("VBE graphics initialized (640x480, 32bpp)");

    // Initialize mouse driver
    mouse_init();
    KINFO("PS/2 mouse initialized");

    // Initialize process subsystem (creates idle process PID 1)
    process_init();

    // Initialize syscall table for ELF program support
    syscall_init();

    // Switch PIT to 100Hz for 10ms scheduler time slices
    pit_set_scheduler_mode();

    // Initialize clipboard subsystem
    clipboard_init();

    // Initialize GUI and desktop
    gui_init();
    desktop_init();
    KINFO("GUI and desktop initialized");

    // Enable keyboard interrupt
    pic_clear_mask(1);
    __asm__ volatile("sti");

    // Start the process scheduler
    process_register_current("desktop");   // Register main thread as PID 2
    process_start_scheduler();

    KINFO("Entering desktop environment");

    // Launch desktop (never returns)
    desktop_run();
}
