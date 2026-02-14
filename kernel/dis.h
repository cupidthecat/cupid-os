/**
 * dis.h — CupidDis x86-32 disassembler
 */
#ifndef DIS_H
#define DIS_H

#include "types.h"

#define DIS_MAX_INSN_BYTES 15
#define DIS_MAX_MNEMONIC   16
#define DIS_MAX_OPERANDS   64
#define DIS_MAX_SYMS       512

typedef enum {
    DIS_FORM_NONE,
    DIS_FORM_REG_OPC,
    DIS_FORM_IMM8,
    DIS_FORM_IMM32,
    DIS_FORM_REL8,
    DIS_FORM_REL32,
    DIS_FORM_RM_REG,
    DIS_FORM_REG_RM,
    DIS_FORM_RM_IMM8,
    DIS_FORM_RM_IMM32,
    DIS_FORM_MOV_R_IMM,
    DIS_FORM_MOV_R8_IMM,
    DIS_FORM_PORT_IN8,
    DIS_FORM_PORT_IN32,
    DIS_FORM_PORT_OUT8,
    DIS_FORM_PORT_OUT32,
    DIS_FORM_PORT_DX_IN8,
    DIS_FORM_PORT_DX_IN32,
    DIS_FORM_PORT_DX_OUT8,
    DIS_FORM_PORT_DX_OUT32,
    DIS_FORM_RM_DIGIT
} dis_form_t;

typedef struct {
    const char *mnemonic;
    dis_form_t  form;
} dis_entry_t;

typedef struct {
    uint32_t addr;
    uint8_t  bytes[DIS_MAX_INSN_BYTES];
    int      byte_count;
    char     mnemonic[DIS_MAX_MNEMONIC];
    char     operands[DIS_MAX_OPERANDS];
} dis_insn_t;

typedef struct {
    uint32_t addr;
    char     name[64];
} dis_sym_t;

typedef void (*dis_output_fn)(const char *s);

int dis_decode_one(const uint8_t *buf, uint32_t offset, uint32_t size,
                   uint32_t base_addr, dis_insn_t *out);

void dis_disassemble(const uint8_t *buf, uint32_t size, uint32_t base_addr,
                     const dis_sym_t *syms, int nsyms,
                     dis_output_fn out_fn);

int dis_elf(const char *path, dis_output_fn out_fn);

#endif /* DIS_H */
