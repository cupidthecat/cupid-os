/**
 * dis.h - CupidDis kernel adapters
 */
#ifndef DIS_H
#define DIS_H

#include "types.h"

#define DIS_MAX_SYMS       512

typedef struct {
    uint32_t addr;
    char     name[64];
} dis_sym_t;

typedef void (*dis_output_fn)(const char *s);

void dis_disassemble(const uint8_t *buf, uint32_t size, uint32_t base_addr,
                     const dis_sym_t *syms, int nsyms,
                     dis_output_fn out_fn);

int dis_elf(const char *path, dis_output_fn out_fn);

#endif /* DIS_H */
