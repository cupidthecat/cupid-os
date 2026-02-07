//help: Reboot the machine
//help: Usage: reboot
//help: Sends the reset command (0xFE) to the keyboard
//help: controller to perform a hardware reboot.

void main() {
    println("Rebooting...");
    asm { cli; }
    // Wait for keyboard controller ready
    int status = inb(0x64);
    while (status & 2) {
        status = inb(0x64);
    }
    // Send reset command
    outb(0x64, 0xFE);
    asm { hlt; }
}
