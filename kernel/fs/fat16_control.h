#ifndef FAT16_CONTROL_H
#define FAT16_CONTROL_H

#include "fat16.h"

/* Distinguished results for fat16_open_checked(). */
#define FAT16_OPEN_OK          0
#define FAT16_OPEN_NOT_FOUND  -1
#define FAT16_OPEN_NO_HANDLES -2
#define FAT16_OPEN_IO_ERROR   -3
#define FAT16_OPEN_INVALID    -4
#define FAT16_OPEN_BUSY       -5

/* Mutation was rejected because the directory entry has a live owner. */
#define FAT16_BUSY            -2

int fat16_open_checked(const char *filename, fat16_file_t **out_file);
int fat16_write_reserved_file(const char *filename, const void *data,
                              uint32_t size);
int fat16_reserve_file(const char *filename);
int fat16_release_file_reservation(const char *filename);
int fat16_file_is_reserved(const char *filename);

#endif /* FAT16_CONTROL_H */
