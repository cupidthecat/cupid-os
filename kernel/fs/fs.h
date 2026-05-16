#ifndef FS_H
#define FS_H

#include "types.h"

typedef struct {
    const char* name;
    const char* data;
    uint32_t size;
} fs_file_t;

void fs_init(void);
uint32_t fs_get_file_count(void);
const fs_file_t* fs_get_file(uint32_t index);
const fs_file_t* fs_find(const char* name);

#endif
