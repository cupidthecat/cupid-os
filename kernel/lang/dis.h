/**
 * dis.h - CupidDis kernel adapters
 */
#ifndef DIS_H
#define DIS_H

#include "types.h"
#include "cupiddis.h"

#define DIS_MAX_SYMS       512

typedef struct {
    uint32_t addr;
    char     name[64];
} dis_sym_t;

typedef void (*dis_output_fn)(const char *s);

/* Raw requests borrow their range map until the call returns. Use the shared
 * CupidDis range kinds so the core remains the only map validator. Strict
 * requests render nothing unless every selected code instruction is known. */
typedef struct {
    ctool_x86_mode_t mode;
    uint32_t base_address;
    const ctool_dis_raw_range_t *ranges;
    uint32_t range_count;
    ctool_bool require_known;
} dis_raw_request_t;

int dis_disassemble_raw(const uint8_t *buf, uint32_t size,
                        const dis_raw_request_t *request,
                        const dis_sym_t *syms, int nsyms,
                        dis_output_fn out_fn);

void dis_disassemble(const uint8_t *buf, uint32_t size, uint32_t base_addr,
                     const dis_sym_t *syms, int nsyms,
                     dis_output_fn out_fn);

int dis_elf(const char *path, dis_output_fn out_fn);

#endif /* DIS_H */
