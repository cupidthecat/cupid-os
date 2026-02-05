/**
 * ATA/IDE Disk Driver
 *
 * Implements PIO mode (Programmed I/O) for reading and writing sectors
 * from ATA hard disks. Supports primary master and slave drives using
 * 28-bit LBA addressing.
 *
 * Features:
 * - Drive detection via IDENTIFY command
 * - PIO mode sector read/write
 * - Error handling with timeout detection
 * - Integration with block device layer
 */

#include "ata.h"
#include "../kernel/ports.h"
#include "../kernel/kernel.h"
#include "../kernel/debug.h"
#include "../kernel/blockdev.h"

// Driver state
static ata_drive_t drives[4];  // Primary master/slave, secondary master/slave (only primary implemented)
static uint8_t num_drives = 0;
static block_device_t ata_block_devices[4];

/**
 * ata_400ns_delay - Insert small delay for ATA timing
 *
 * ATA spec requires 400ns delay after drive selection.
 * Reading alternate status register 4 times provides this delay.
 */
static void ata_400ns_delay(void) {
    for (int i = 0; i < 4; i++) {
        inb(ATA_PRIMARY_ALT_STATUS);
    }
}

/**
 * ata_wait_bsy - Wait for drive to clear BSY flag
 *
 * Polls status register until BSY bit is clear or timeout occurs.
 *
 * @return 0 on success, -1 on timeout
 */
static int ata_wait_bsy(void) {
    uint32_t timeout = ATA_TIMEOUT;
    while (timeout--) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return 0;
        }
    }
    return -1;  // Timeout
}

/**
 * ata_wait_drq - Wait for drive to set DRQ flag
 *
 * Polls status register until DRQ bit is set or timeout/error occurs.
 *
 * @return 0 on success, -1 on timeout/error
 */
static int ata_wait_drq(void) {
    uint32_t timeout = ATA_TIMEOUT;
    while (timeout--) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_SR_DRQ) {
            return 0;
        }
        if (status & ATA_SR_ERR) {
            return -1;  // Error
        }
    }
    return -1;  // Timeout
}

/**
 * ata_select_drive - Select master or slave drive
 *
 * @param is_slave: 0 for master, 1 for slave
 */
static void ata_select_drive(uint8_t is_slave) {
    uint8_t drive_select = is_slave ? ATA_DRIVE_SLAVE : ATA_DRIVE_MASTER;
    outb(ATA_PRIMARY_DRIVE_HEAD, drive_select);
    ata_400ns_delay();
}

/**
 * ata_identify - Send IDENTIFY command to drive
 *
 * Reads drive information including model string and sector count.
 *
 * @param is_slave: 0 for master, 1 for slave
 * @param drive: Pointer to drive structure to fill
 * @return 0 on success, -1 if drive doesn't exist
 */
static int ata_identify(uint8_t is_slave, ata_drive_t* drive) {
    ata_select_drive(is_slave);

    // Clear sector count and LBA registers
    outb(ATA_PRIMARY_SECCOUNT, 0);
    outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HI, 0);

    // Send IDENTIFY command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);
    ata_400ns_delay();

    // Check if drive exists
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        return -1;  // No drive
    }

    // Wait for BSY to clear
    if (ata_wait_bsy() != 0) {
        return -1;
    }

    // Check for non-ATA devices (ATAPI will set LBA_MID or LBA_HI)
    if (inb(ATA_PRIMARY_LBA_MID) != 0 || inb(ATA_PRIMARY_LBA_HI) != 0) {
        return -1;  // Not an ATA device
    }

    // Wait for DRQ (data ready)
    if (ata_wait_drq() != 0) {
        return -1;
    }

    // Read IDENTIFY data (256 words = 512 bytes)
    uint16_t identify_data[256];
    insw(ATA_PRIMARY_DATA, identify_data, 256);

    // Parse model string (words 27-46, 40 characters)
    for (int i = 0; i < 20; i++) {
        uint16_t word = identify_data[27 + i];
        drive->model[i * 2] = (char)(word >> 8);
        drive->model[i * 2 + 1] = (char)(word & 0xFF);
    }
    drive->model[40] = '\0';

    // Trim trailing spaces from model string
    for (int i = 39; i >= 0; i--) {
        if (drive->model[i] == ' ') {
            drive->model[i] = '\0';
        } else {
            break;
        }
    }

    // Parse sector count (words 60-61, 28-bit LBA)
    drive->sectors = (uint32_t)identify_data[60] | ((uint32_t)identify_data[61] << 16);

    drive->exists = 1;
    drive->is_slave = is_slave;

    return 0;
}

/**
 * ata_init - Initialize ATA driver
 *
 * Probes primary master and slave drives, detects presence,
 * and reads drive information.
 */
void ata_init(void) {
    print("Initializing ATA driver...\n");

    num_drives = 0;

    // Initialize drive structures
    for (int i = 0; i < 4; i++) {
        drives[i].exists = 0;
        drives[i].is_slave = 0;
        drives[i].sectors = 0;
        drives[i].model[0] = '\0';
    }

    // Probe primary master (drive 0)
    if (ata_identify(0, &drives[0]) == 0) {
        num_drives++;
        print("ATA: Primary master - ");
        print(drives[0].model);
        print(" (");
        print_int(drives[0].sectors / 2048);
        print(" MB)\n");
    }

    // Probe primary slave (drive 1)
    if (ata_identify(1, &drives[1]) == 0) {
        num_drives++;
        print("ATA: Primary slave - ");
        print(drives[1].model);
        print(" (");
        print_int(drives[1].sectors / 2048);
        print(" MB)\n");
    }

    if (num_drives == 0) {
        print("ATA: No drives detected\n");
    } else {
        print("ATA: Found ");
        print_int(num_drives);
        print(" drive(s)\n");
    }
}

/**
 * ata_read_sectors - Read sectors from ATA drive using PIO mode
 *
 * @param drive: Drive number (0 = primary master, 1 = primary slave)
 * @param lba: Logical block address (28-bit)
 * @param count: Number of sectors to read
 * @param buffer: Buffer to read data into
 * @return 0 on success, -1 on error
 */
int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buffer) {
    if (drive >= 4 || !drives[drive].exists) {
        return -1;
    }

    if (count == 0) {
        return 0;
    }

    // Select drive and set LBA mode
    uint8_t drive_select = (drives[drive].is_slave ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F);
    outb(ATA_PRIMARY_DRIVE_HEAD, drive_select);
    ata_400ns_delay();

    // Wait for drive ready
    if (ata_wait_bsy() != 0) {
        print("ATA: Timeout waiting for drive ready (read)\n");
        return -1;
    }

    // Set sector count and LBA
    outb(ATA_PRIMARY_SECCOUNT, count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));

    // Send READ SECTORS command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_SECTORS);

    // Read each sector
    uint16_t* buf = (uint16_t*)buffer;
    for (int i = 0; i < count; i++) {
        // Wait for DRQ (data ready)
        if (ata_wait_drq() != 0) {
            print("ATA: Timeout/error waiting for DRQ (read)\n");
            debug_print_int("  Drive: ", drive);
            debug_print_int("  LBA: ", lba + (uint32_t)i);
            uint8_t status = inb(ATA_PRIMARY_STATUS);
            debug_print_int("  Status: ", status);
            if (status & ATA_SR_ERR) {
                uint8_t error = inb(ATA_PRIMARY_ERROR);
                debug_print_int("  Error: ", error);
            }
            return -1;
        }

        // Read 256 words (512 bytes) from data port
        insw(ATA_PRIMARY_DATA, buf, 256);
        buf += 256;
    }

    return 0;
}

/**
 * ata_write_sectors - Write sectors to ATA drive using PIO mode
 *
 * @param drive: Drive number (0 = primary master, 1 = primary slave)
 * @param lba: Logical block address (28-bit)
 * @param count: Number of sectors to write
 * @param buffer: Buffer containing data to write
 * @return 0 on success, -1 on error
 */
int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void* buffer) {
    if (drive >= 4 || !drives[drive].exists) {
        return -1;
    }

    if (count == 0) {
        return 0;
    }

    // Select drive and set LBA mode
    uint8_t drive_select = (drives[drive].is_slave ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F);
    outb(ATA_PRIMARY_DRIVE_HEAD, drive_select);
    ata_400ns_delay();

    // Wait for drive ready
    if (ata_wait_bsy() != 0) {
        print("ATA: Timeout waiting for drive ready (write)\n");
        return -1;
    }

    // Set sector count and LBA
    outb(ATA_PRIMARY_SECCOUNT, count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));

    // Send WRITE SECTORS command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);

    // Write each sector
    const uint16_t* buf = (const uint16_t*)buffer;
    for (int i = 0; i < count; i++) {
        // Wait for DRQ (ready to accept data)
        if (ata_wait_drq() != 0) {
            print("ATA: Timeout/error waiting for DRQ (write)\n");
            debug_print_int("  Drive: ", drive);
            debug_print_int("  LBA: ", lba + (uint32_t)i);
            uint8_t status = inb(ATA_PRIMARY_STATUS);
            debug_print_int("  Status: ", status);
            if (status & ATA_SR_ERR) {
                uint8_t error = inb(ATA_PRIMARY_ERROR);
                debug_print_int("  Error: ", error);
            }
            return -1;
        }

        // Write 256 words (512 bytes) to data port
        outsw(ATA_PRIMARY_DATA, buf, 256);
        buf += 256;
    }

    // Wait for write completion
    if (ata_wait_bsy() != 0) {
        print("ATA: Timeout waiting for write completion\n");
        return -1;
    }

    // Check for errors
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status & ATA_SR_ERR) {
        print("ATA: Write error\n");
        uint8_t error = inb(ATA_PRIMARY_ERROR);
        debug_print_int("  Error register: ", error);
        return -1;
    }

    return 0;
}

/**
 * ata_get_drive - Get drive information
 *
 * @param drive: Drive number (0-3)
 * @return Pointer to drive structure, or NULL if invalid
 */
ata_drive_t* ata_get_drive(uint8_t drive) {
    if (drive >= 4) {
        return NULL;
    }
    if (!drives[drive].exists) {
        return NULL;
    }
    return &drives[drive];
}

/**
 * ata_blkdev_read - Block device read wrapper for ATA
 *
 * @param driver_data: Drive number cast to void*
 * @param lba: Logical block address
 * @param count: Number of sectors to read
 * @param buffer: Buffer to read into
 * @return 0 on success, -1 on error
 */
static int ata_blkdev_read(void* driver_data, uint32_t lba, uint32_t count, void* buffer) {
    uint8_t drive = (uint8_t)(uint32_t)driver_data;
    return ata_read_sectors(drive, lba, (uint8_t)count, buffer);
}

/**
 * ata_blkdev_write - Block device write wrapper for ATA
 *
 * @param driver_data: Drive number cast to void*
 * @param lba: Logical block address
 * @param count: Number of sectors to write
 * @param buffer: Buffer containing data to write
 * @return 0 on success, -1 on error
 */
static int ata_blkdev_write(void* driver_data, uint32_t lba, uint32_t count, const void* buffer) {
    uint8_t drive = (uint8_t)(uint32_t)driver_data;
    return ata_write_sectors(drive, lba, (uint8_t)count, buffer);
}

/**
 * ata_register_devices - Register ATA drives with block device layer
 *
 * Registers each detected ATA drive as a block device so they can be
 * accessed through the generic block device interface.
 */
void ata_register_devices(void) {
    for (uint8_t i = 0; i < 4; i++) {
        if (drives[i].exists) {
            ata_block_devices[i].name = drives[i].model;
            ata_block_devices[i].sector_count = drives[i].sectors;
            ata_block_devices[i].sector_size = 512;
            ata_block_devices[i].driver_data = (void*)(uint32_t)i;
            ata_block_devices[i].read = ata_blkdev_read;
            ata_block_devices[i].write = ata_blkdev_write;
            blkdev_register(&ata_block_devices[i]);
        }
    }
}
