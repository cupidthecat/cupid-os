#ifndef ATA_H
#define ATA_H

#include "../kernel/types.h"

// ATA ports for primary bus
#define ATA_PRIMARY_DATA       0x1F0
#define ATA_PRIMARY_ERROR      0x1F1
#define ATA_PRIMARY_SECCOUNT   0x1F2
#define ATA_PRIMARY_LBA_LO     0x1F3
#define ATA_PRIMARY_LBA_MID    0x1F4
#define ATA_PRIMARY_LBA_HI     0x1F5
#define ATA_PRIMARY_DRIVE_HEAD 0x1F6
#define ATA_PRIMARY_STATUS     0x1F7
#define ATA_PRIMARY_COMMAND    0x1F7
#define ATA_PRIMARY_ALT_STATUS 0x3F6

// ATA status bits
#define ATA_SR_BSY  0x80  // Busy
#define ATA_SR_DRDY 0x40  // Drive ready
#define ATA_SR_DRQ  0x08  // Data request
#define ATA_SR_ERR  0x01  // Error

// ATA commands
#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_IDENTIFY      0xEC

// Drive selection
#define ATA_DRIVE_MASTER 0xA0
#define ATA_DRIVE_SLAVE  0xB0

// Timeout (5 seconds at ~1MHz I/O)
#define ATA_TIMEOUT 5000000

typedef struct {
    uint8_t exists;
    uint8_t is_slave;
    uint32_t sectors;
    char model[41];
} ata_drive_t;

// Public API
void ata_init(void);
int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buffer);
int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void* buffer);
ata_drive_t* ata_get_drive(uint8_t drive);
void ata_register_devices(void);

#endif
