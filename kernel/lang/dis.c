/**
 * dis.c - CupidDis x86-32 disassembler
 */

#include "dis.h"
#include "exec.h"
#include "kernel.h"
#include "memory.h"
#include "string.h"
#include "vfs.h"
#include "vfs_helpers.h"

static const char *const dis_reg32[8] = {
    "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"
};

static const char *const dis_reg8[8] = {
    "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"
};

static const char *const dis_grp1[8] = {
    "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"
};

static const char *const dis_grp2[8] = {
    "rol", "ror", "rcl", "rcr", "shl", "shr", NULL, "sar"
};

static const char *const dis_grp3[8] = {
    "test", NULL, "not", "neg", "mul", "imul", "div", "idiv"
};

static const char *const dis_grp5[8] = {
    "inc", "dec", "call", NULL, "jmp", NULL, "push", NULL
};

static const dis_entry_t dis_table_1[256] = {
    [0x00] = {"add", DIS_FORM_RM_REG},
    [0x01] = {"add", DIS_FORM_RM_REG},
    [0x02] = {"add", DIS_FORM_REG_RM},
    [0x03] = {"add", DIS_FORM_REG_RM},
    [0x08] = {"or", DIS_FORM_RM_REG},
    [0x09] = {"or", DIS_FORM_RM_REG},
    [0x0A] = {"or", DIS_FORM_REG_RM},
    [0x0B] = {"or", DIS_FORM_REG_RM},
    [0x20] = {"and", DIS_FORM_RM_REG},
    [0x21] = {"and", DIS_FORM_RM_REG},
    [0x22] = {"and", DIS_FORM_REG_RM},
    [0x23] = {"and", DIS_FORM_REG_RM},
    [0x28] = {"sub", DIS_FORM_RM_REG},
    [0x29] = {"sub", DIS_FORM_RM_REG},
    [0x2A] = {"sub", DIS_FORM_REG_RM},
    [0x2B] = {"sub", DIS_FORM_REG_RM},
    [0x30] = {"xor", DIS_FORM_RM_REG},
    [0x31] = {"xor", DIS_FORM_RM_REG},
    [0x32] = {"xor", DIS_FORM_REG_RM},
    [0x33] = {"xor", DIS_FORM_REG_RM},
    [0x38] = {"cmp", DIS_FORM_RM_REG},
    [0x39] = {"cmp", DIS_FORM_RM_REG},
    [0x3A] = {"cmp", DIS_FORM_REG_RM},
    [0x3B] = {"cmp", DIS_FORM_REG_RM},

    [0x50] = {"push", DIS_FORM_REG_OPC},
    [0x51] = {"push", DIS_FORM_REG_OPC},
    [0x52] = {"push", DIS_FORM_REG_OPC},
    [0x53] = {"push", DIS_FORM_REG_OPC},
    [0x54] = {"push", DIS_FORM_REG_OPC},
    [0x55] = {"push", DIS_FORM_REG_OPC},
    [0x56] = {"push", DIS_FORM_REG_OPC},
    [0x57] = {"push", DIS_FORM_REG_OPC},
    [0x58] = {"pop", DIS_FORM_REG_OPC},
    [0x59] = {"pop", DIS_FORM_REG_OPC},
    [0x5A] = {"pop", DIS_FORM_REG_OPC},
    [0x5B] = {"pop", DIS_FORM_REG_OPC},
    [0x5C] = {"pop", DIS_FORM_REG_OPC},
    [0x5D] = {"pop", DIS_FORM_REG_OPC},
    [0x5E] = {"pop", DIS_FORM_REG_OPC},
    [0x5F] = {"pop", DIS_FORM_REG_OPC},

    [0x68] = {"push", DIS_FORM_IMM32},
    [0x6A] = {"push", DIS_FORM_IMM8},

    [0x70] = {"jo", DIS_FORM_REL8},
    [0x71] = {"jno", DIS_FORM_REL8},
    [0x72] = {"jb", DIS_FORM_REL8},
    [0x73] = {"jae", DIS_FORM_REL8},
    [0x74] = {"je", DIS_FORM_REL8},
    [0x75] = {"jne", DIS_FORM_REL8},
    [0x76] = {"jbe", DIS_FORM_REL8},
    [0x77] = {"ja", DIS_FORM_REL8},
    [0x78] = {"js", DIS_FORM_REL8},
    [0x79] = {"jns", DIS_FORM_REL8},
    [0x7A] = {"jp", DIS_FORM_REL8},
    [0x7B] = {"jnp", DIS_FORM_REL8},
    [0x7C] = {"jl", DIS_FORM_REL8},
    [0x7D] = {"jge", DIS_FORM_REL8},
    [0x7E] = {"jle", DIS_FORM_REL8},
    [0x7F] = {"jg", DIS_FORM_REL8},

    [0x80] = {NULL, DIS_FORM_RM_DIGIT},
    [0x81] = {NULL, DIS_FORM_RM_DIGIT},
    [0x83] = {NULL, DIS_FORM_RM_DIGIT},

    [0x88] = {"mov", DIS_FORM_RM_REG},
    [0x89] = {"mov", DIS_FORM_RM_REG},
    [0x8A] = {"mov", DIS_FORM_REG_RM},
    [0x8B] = {"mov", DIS_FORM_REG_RM},
    [0x8D] = {"lea", DIS_FORM_REG_RM},

    [0x90] = {"nop", DIS_FORM_NONE},
    [0x99] = {"cdq", DIS_FORM_NONE},
    [0x9C] = {"pushf", DIS_FORM_NONE},
    [0x9D] = {"popf", DIS_FORM_NONE},

    [0xB0] = {"mov", DIS_FORM_MOV_R8_IMM},
    [0xB1] = {"mov", DIS_FORM_MOV_R8_IMM},
    [0xB2] = {"mov", DIS_FORM_MOV_R8_IMM},
    [0xB3] = {"mov", DIS_FORM_MOV_R8_IMM},
    [0xB4] = {"mov", DIS_FORM_MOV_R8_IMM},
    [0xB5] = {"mov", DIS_FORM_MOV_R8_IMM},
    [0xB6] = {"mov", DIS_FORM_MOV_R8_IMM},
    [0xB7] = {"mov", DIS_FORM_MOV_R8_IMM},

    [0xB8] = {"mov", DIS_FORM_MOV_R_IMM},
    [0xB9] = {"mov", DIS_FORM_MOV_R_IMM},
    [0xBA] = {"mov", DIS_FORM_MOV_R_IMM},
    [0xBB] = {"mov", DIS_FORM_MOV_R_IMM},
    [0xBC] = {"mov", DIS_FORM_MOV_R_IMM},
    [0xBD] = {"mov", DIS_FORM_MOV_R_IMM},
    [0xBE] = {"mov", DIS_FORM_MOV_R_IMM},
    [0xBF] = {"mov", DIS_FORM_MOV_R_IMM},

    [0xC3] = {"ret", DIS_FORM_NONE},
    [0xC7] = {"mov", DIS_FORM_RM_IMM32},
    [0xCC] = {"int3", DIS_FORM_NONE},
    [0xCD] = {"int", DIS_FORM_IMM8},

    [0xD0] = {NULL, DIS_FORM_RM_DIGIT},
    [0xD1] = {NULL, DIS_FORM_RM_DIGIT},
    [0xD2] = {NULL, DIS_FORM_RM_DIGIT},
    [0xD3] = {NULL, DIS_FORM_RM_DIGIT},

    [0xE4] = {"in", DIS_FORM_PORT_IN8},
    [0xE5] = {"in", DIS_FORM_PORT_IN32},
    [0xE6] = {"out", DIS_FORM_PORT_OUT8},
    [0xE7] = {"out", DIS_FORM_PORT_OUT32},
    [0xE8] = {"call", DIS_FORM_REL32},
    [0xE9] = {"jmp", DIS_FORM_REL32},
    [0xEB] = {"jmp", DIS_FORM_REL8},
    [0xEC] = {"in", DIS_FORM_PORT_DX_IN8},
    [0xED] = {"in", DIS_FORM_PORT_DX_IN32},
    [0xEE] = {"out", DIS_FORM_PORT_DX_OUT8},
    [0xEF] = {"out", DIS_FORM_PORT_DX_OUT32},

    [0xF4] = {"hlt", DIS_FORM_NONE},
    [0xF6] = {NULL, DIS_FORM_RM_DIGIT},
    [0xF7] = {NULL, DIS_FORM_RM_DIGIT},
    [0xFA] = {"cli", DIS_FORM_NONE},
    [0xFB] = {"sti", DIS_FORM_NONE},
    [0xFF] = {NULL, DIS_FORM_RM_DIGIT}
};

static const dis_entry_t dis_table_0f[256] = {
    [0x84] = {"je", DIS_FORM_REL32},
    [0x85] = {"jne", DIS_FORM_REL32},
    [0x86] = {"jbe", DIS_FORM_REL32},
    [0x87] = {"ja", DIS_FORM_REL32},
    [0x8C] = {"jl", DIS_FORM_REL32},
    [0x8D] = {"jge", DIS_FORM_REL32},
    [0x8E] = {"jle", DIS_FORM_REL32},
    [0x8F] = {"jg", DIS_FORM_REL32},
    [0xAF] = {"imul", DIS_FORM_REG_RM},
    [0xB6] = {"movzx", DIS_FORM_REG_RM},
    [0xB7] = {"movzx", DIS_FORM_REG_RM}
};

static void dis_append_char(char *dst, int *pos, int max_len, char c) {
    if (*pos >= max_len - 1) {
        return;
    }
    dst[*pos] = c;
    *pos = *pos + 1;
    dst[*pos] = '\0';
}

static void dis_append_str(char *dst, int *pos, int max_len, const char *src) {
    while (*src && *pos < max_len - 1) {
        dst[*pos] = *src;
        *pos = *pos + 1;
        src++;
    }
    dst[*pos] = '\0';
}

static void dis_append_hex_u8(char *dst, int *pos, int max_len, uint8_t v) {
    static const char hex[] = "0123456789ABCDEF";
    dis_append_char(dst, pos, max_len, '0');
    dis_append_char(dst, pos, max_len, 'x');
    dis_append_char(dst, pos, max_len, hex[(v >> 4) & 0x0Fu]);
    dis_append_char(dst, pos, max_len, hex[v & 0x0Fu]);
}

static void dis_append_hex_u32(char *dst, int *pos, int max_len, uint32_t v) {
    int shift;
    static const char hex[] = "0123456789ABCDEF";
    dis_append_char(dst, pos, max_len, '0');
    dis_append_char(dst, pos, max_len, 'x');
    for (shift = 28; shift >= 0; shift -= 4) {
        uint32_t nibble = (v >> (uint32_t)shift) & 0x0Fu;
        dis_append_char(dst, pos, max_len, hex[nibble]);
    }
}

static uint32_t dis_read_u32(const uint8_t *buf, uint32_t off) {
    uint32_t v0 = (uint32_t)buf[off + 0u];
    uint32_t v1 = (uint32_t)buf[off + 1u] << 8;
    uint32_t v2 = (uint32_t)buf[off + 2u] << 16;
    uint32_t v3 = (uint32_t)buf[off + 3u] << 24;
    return v0 | v1 | v2 | v3;
}

static int dis_decode_modrm(const uint8_t *buf, uint32_t off, uint32_t size,
                            int is_8bit, int *reg_field,
                            char *rm_out, int rm_out_len) {
    uint8_t modrm;
    uint8_t mod;
    uint8_t reg;
    uint8_t rm;
    int pos = 0;
    int extra = 1;

    if (off >= size) {
        return 0;
    }

    modrm = buf[off];
    mod = (uint8_t)((modrm >> 6) & 0x03u);
    reg = (uint8_t)((modrm >> 3) & 0x07u);
    rm = (uint8_t)(modrm & 0x07u);

    *reg_field = (int)reg;
    rm_out[0] = '\0';

    if (mod == 3u) {
        if (is_8bit) {
            dis_append_str(rm_out, &pos, rm_out_len, dis_reg8[rm]);
        } else {
            dis_append_str(rm_out, &pos, rm_out_len, dis_reg32[rm]);
        }
        return extra;
    }

    dis_append_char(rm_out, &pos, rm_out_len, '[');

    if (rm == 4u) {
        uint8_t sib;
        uint8_t scale;
        uint8_t index;
        uint8_t base;

        if (off + (uint32_t)extra >= size) {
            return 0;
        }
        sib = buf[off + (uint32_t)extra];
        extra++;

        scale = (uint8_t)((sib >> 6) & 0x03u);
        index = (uint8_t)((sib >> 3) & 0x07u);
        base = (uint8_t)(sib & 0x07u);

        if (!(mod == 0u && base == 5u)) {
            dis_append_str(rm_out, &pos, rm_out_len, dis_reg32[base]);
        }

        if (index != 4u) {
            if (pos > 1) {
                dis_append_char(rm_out, &pos, rm_out_len, '+');
            }
            dis_append_str(rm_out, &pos, rm_out_len, dis_reg32[index]);
            if (scale != 0u) {
                dis_append_char(rm_out, &pos, rm_out_len, '*');
                dis_append_char(rm_out, &pos, rm_out_len,
                                (char)('1' << (int)scale));
            }
        }

        if (mod == 0u && base == 5u) {
            if (off + (uint32_t)extra + 4u > size) {
                return 0;
            }
            if (pos > 1) {
                dis_append_char(rm_out, &pos, rm_out_len, '+');
            }
            dis_append_hex_u32(rm_out, &pos, rm_out_len,
                               dis_read_u32(buf, off + (uint32_t)extra));
            extra += 4;
        }
    } else if (mod == 0u && rm == 5u) {
        if (off + (uint32_t)extra + 4u > size) {
            return 0;
        }
        dis_append_hex_u32(rm_out, &pos, rm_out_len,
                           dis_read_u32(buf, off + (uint32_t)extra));
        extra += 4;
    } else {
        dis_append_str(rm_out, &pos, rm_out_len, dis_reg32[rm]);
    }

    if (mod == 1u) {
        int8_t disp8;
        if (off + (uint32_t)extra >= size) {
            return 0;
        }
        disp8 = (int8_t)buf[off + (uint32_t)extra];
        extra++;
        if (disp8 < 0) {
            dis_append_char(rm_out, &pos, rm_out_len, '-');
            dis_append_hex_u8(rm_out, &pos, rm_out_len, (uint8_t)(-disp8));
        } else if (disp8 > 0) {
            dis_append_char(rm_out, &pos, rm_out_len, '+');
            dis_append_hex_u8(rm_out, &pos, rm_out_len, (uint8_t)disp8);
        }
    } else if (mod == 2u) {
        uint32_t disp32;
        if (off + (uint32_t)extra + 4u > size) {
            return 0;
        }
        disp32 = dis_read_u32(buf, off + (uint32_t)extra);
        extra += 4;
        dis_append_char(rm_out, &pos, rm_out_len, '+');
        dis_append_hex_u32(rm_out, &pos, rm_out_len, disp32);
    }

    dis_append_char(rm_out, &pos, rm_out_len, ']');
    return extra;
}

static const char *dis_find_sym(const dis_sym_t *syms, int nsyms, uint32_t addr) {
    int i;
    if (!syms || nsyms <= 0) {
        return NULL;
    }
    for (i = 0; i < nsyms; i++) {
        if (syms[i].addr == addr) {
            return syms[i].name;
        }
    }
    return NULL;
}

static void dis_copy_text(char *dst, int dst_len, const char *src) {
    int i = 0;
    while (src[i] && i < dst_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int dis_decode_one(const uint8_t *buf, uint32_t offset, uint32_t size,
                   uint32_t base_addr, dis_insn_t *out) {
    uint32_t off = offset;
    uint8_t opcode;
    int is_0f = 0;
    dis_entry_t entry;
    int operand_pos = 0;

    if (!buf || !out || offset >= size) {
        return 0;
    }

    out->addr = base_addr + offset;
    out->byte_count = 0;
    out->mnemonic[0] = '\0';
    out->operands[0] = '\0';

    while (off < size) {
        uint8_t p = buf[off];
        if (p == 0x66u || p == 0x67u || p == 0xF2u || p == 0xF3u) {
            off++;
            continue;
        }
        break;
    }

    if (off >= size) {
        goto truncated;
    }

    opcode = buf[off++];

    if (opcode == 0x0Fu) {
        if (off >= size) {
            goto truncated;
        }
        opcode = buf[off++];
        is_0f = 1;
        entry = dis_table_0f[opcode];
    } else {
        entry = dis_table_1[opcode];
    }

    if (!entry.mnemonic && entry.form != DIS_FORM_RM_DIGIT) {
        goto unknown;
    }

    if (entry.mnemonic) {
        dis_copy_text(out->mnemonic, DIS_MAX_MNEMONIC, entry.mnemonic);
    }

    switch (entry.form) {
    case DIS_FORM_NONE:
        break;

    case DIS_FORM_REG_OPC: {
        uint8_t r = (uint8_t)(opcode & 0x07u);
        dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS,
                       dis_reg32[r]);
        break;
    }

    case DIS_FORM_IMM8: {
        uint8_t imm8;
        if (off + 1u > size) {
            goto truncated;
        }
        imm8 = buf[off++];
        dis_append_hex_u8(out->operands, &operand_pos, DIS_MAX_OPERANDS, imm8);
        break;
    }

    case DIS_FORM_IMM32: {
        uint32_t imm32;
        if (off + 4u > size) {
            goto truncated;
        }
        imm32 = dis_read_u32(buf, off);
        off += 4;
        dis_append_hex_u32(out->operands, &operand_pos, DIS_MAX_OPERANDS, imm32);
        break;
    }

    case DIS_FORM_REL8: {
        int8_t rel8;
        int32_t rel;
        uint32_t target;
        if (off + 1u > size) {
            goto truncated;
        }
        rel8 = (int8_t)buf[off++];
        rel = (int32_t)rel8;
        target = base_addr + off + (uint32_t)rel;
        dis_append_hex_u32(out->operands, &operand_pos, DIS_MAX_OPERANDS, target);
        break;
    }

    case DIS_FORM_REL32: {
        int32_t rel32;
        uint32_t target;
        if (off + 4u > size) {
            goto truncated;
        }
        rel32 = (int32_t)dis_read_u32(buf, off);
        off += 4;
        target = base_addr + off + (uint32_t)rel32;
        dis_append_hex_u32(out->operands, &operand_pos, DIS_MAX_OPERANDS, target);
        break;
    }

    case DIS_FORM_RM_REG:
    case DIS_FORM_REG_RM: {
        char rm_op[DIS_MAX_OPERANDS];
        int reg_field = 0;
        int used = 0;
        int is8 = 0;

        if (!is_0f && (opcode == 0x88u || opcode == 0x8Au)) {
            is8 = 1;
        }

        used = dis_decode_modrm(buf, off, size, is8, &reg_field, rm_op,
                                (int)sizeof(rm_op));
        if (used == 0) {
            goto truncated;
        }
        off += (uint32_t)used;

        if (entry.form == DIS_FORM_RM_REG) {
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, rm_op);
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, ", ");
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS,
                           is8 ? dis_reg8[(uint8_t)reg_field]
                               : dis_reg32[(uint8_t)reg_field]);
        } else {
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS,
                           is8 ? dis_reg8[(uint8_t)reg_field]
                               : dis_reg32[(uint8_t)reg_field]);
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, ", ");
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, rm_op);
        }
        break;
    }

    case DIS_FORM_RM_IMM8:
    case DIS_FORM_RM_IMM32: {
        char rm_op[DIS_MAX_OPERANDS];
        int reg_field = 0;
        int used = dis_decode_modrm(buf, off, size, 0, &reg_field, rm_op,
                                    (int)sizeof(rm_op));
        (void)reg_field;
        if (used == 0) {
            goto truncated;
        }
        off += (uint32_t)used;
        dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, rm_op);
        dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, ", ");
        if (entry.form == DIS_FORM_RM_IMM8) {
            uint8_t imm8;
            if (off + 1u > size) {
                goto truncated;
            }
            imm8 = buf[off++];
            dis_append_hex_u8(out->operands, &operand_pos, DIS_MAX_OPERANDS, imm8);
        } else {
            uint32_t imm32;
            if (off + 4u > size) {
                goto truncated;
            }
            imm32 = dis_read_u32(buf, off);
            off += 4;
            dis_append_hex_u32(out->operands, &operand_pos, DIS_MAX_OPERANDS, imm32);
        }
        break;
    }

    case DIS_FORM_MOV_R_IMM: {
        uint8_t r = (uint8_t)(opcode & 0x07u);
        uint32_t imm32;
        if (off + 4u > size) {
            goto truncated;
        }
        imm32 = dis_read_u32(buf, off);
        off += 4;
        dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, dis_reg32[r]);
        dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, ", ");
        dis_append_hex_u32(out->operands, &operand_pos, DIS_MAX_OPERANDS, imm32);
        break;
    }

    case DIS_FORM_MOV_R8_IMM: {
        uint8_t r = (uint8_t)(opcode & 0x07u);
        uint8_t imm8;
        if (off + 1u > size) {
            goto truncated;
        }
        imm8 = buf[off++];
        dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, dis_reg8[r]);
        dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, ", ");
        dis_append_hex_u8(out->operands, &operand_pos, DIS_MAX_OPERANDS, imm8);
        break;
    }

    case DIS_FORM_PORT_IN8:
    case DIS_FORM_PORT_IN32:
    case DIS_FORM_PORT_OUT8:
    case DIS_FORM_PORT_OUT32: {
        uint8_t port;
        if (off + 1u > size) {
            goto truncated;
        }
        port = buf[off++];
        if (entry.form == DIS_FORM_PORT_IN8) {
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, "al, ");
            dis_append_hex_u8(out->operands, &operand_pos, DIS_MAX_OPERANDS, port);
        } else if (entry.form == DIS_FORM_PORT_IN32) {
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, "eax, ");
            dis_append_hex_u8(out->operands, &operand_pos, DIS_MAX_OPERANDS, port);
        } else if (entry.form == DIS_FORM_PORT_OUT8) {
            dis_append_hex_u8(out->operands, &operand_pos, DIS_MAX_OPERANDS, port);
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, ", al");
        } else {
            dis_append_hex_u8(out->operands, &operand_pos, DIS_MAX_OPERANDS, port);
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, ", eax");
        }
        break;
    }

    case DIS_FORM_PORT_DX_IN8:
        dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, "al, dx");
        break;

    case DIS_FORM_PORT_DX_IN32:
        dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, "eax, dx");
        break;

    case DIS_FORM_PORT_DX_OUT8:
        dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, "dx, al");
        break;

    case DIS_FORM_PORT_DX_OUT32:
        dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, "dx, eax");
        break;

    case DIS_FORM_RM_DIGIT: {
        char rm_op[DIS_MAX_OPERANDS];
        int reg_field = 0;
        int used = dis_decode_modrm(buf, off, size, 0, &reg_field, rm_op,
                                    (int)sizeof(rm_op));
        const char *mn = NULL;

        if (used == 0) {
            goto truncated;
        }
        off += (uint32_t)used;

        if (!is_0f && (opcode == 0x80u || opcode == 0x81u || opcode == 0x83u)) {
            mn = dis_grp1[(uint8_t)reg_field];
            if (!mn) {
                goto unknown;
            }
            dis_copy_text(out->mnemonic, DIS_MAX_MNEMONIC, mn);
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, rm_op);
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, ", ");
            if (opcode == 0x80u || opcode == 0x83u) {
                if (off + 1u > size) {
                    goto truncated;
                }
                dis_append_hex_u8(out->operands, &operand_pos, DIS_MAX_OPERANDS,
                                  buf[off]);
                off += 1;
            } else {
                if (off + 4u > size) {
                    goto truncated;
                }
                dis_append_hex_u32(out->operands, &operand_pos, DIS_MAX_OPERANDS,
                                   dis_read_u32(buf, off));
                off += 4;
            }
            break;
        }

        if (!is_0f && (opcode == 0xD0u || opcode == 0xD1u ||
                       opcode == 0xD2u || opcode == 0xD3u)) {
            mn = dis_grp2[(uint8_t)reg_field];
            if (!mn) {
                goto unknown;
            }
            dis_copy_text(out->mnemonic, DIS_MAX_MNEMONIC, mn);
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, rm_op);
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, ", ");
            if (opcode == 0xD0u || opcode == 0xD1u) {
                dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, "1");
            } else {
                dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, "cl");
            }
            break;
        }

        if (!is_0f && (opcode == 0xF6u || opcode == 0xF7u)) {
            mn = dis_grp3[(uint8_t)reg_field];
            if (!mn) {
                goto unknown;
            }
            dis_copy_text(out->mnemonic, DIS_MAX_MNEMONIC, mn);
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, rm_op);
            if ((uint8_t)reg_field == 0u) {
                dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, ", ");
                if (opcode == 0xF6u) {
                    if (off + 1u > size) {
                        goto truncated;
                    }
                    dis_append_hex_u8(out->operands, &operand_pos, DIS_MAX_OPERANDS,
                                      buf[off]);
                    off += 1;
                } else {
                    if (off + 4u > size) {
                        goto truncated;
                    }
                    dis_append_hex_u32(out->operands, &operand_pos, DIS_MAX_OPERANDS,
                                       dis_read_u32(buf, off));
                    off += 4;
                }
            }
            break;
        }

        if (!is_0f && opcode == 0xFFu) {
            mn = dis_grp5[(uint8_t)reg_field];
            if (!mn) {
                goto unknown;
            }
            dis_copy_text(out->mnemonic, DIS_MAX_MNEMONIC, mn);
            dis_append_str(out->operands, &operand_pos, DIS_MAX_OPERANDS, rm_op);
            break;
        }

        goto unknown;
    }
    }

    goto done;

unknown:
    out->mnemonic[0] = 'd';
    out->mnemonic[1] = 'b';
    out->mnemonic[2] = '\0';
    out->operands[0] = '\0';
    operand_pos = 0;
    dis_append_hex_u8(out->operands, &operand_pos, DIS_MAX_OPERANDS, buf[offset]);
    off = offset + 1u;
    goto done;

truncated:
    out->mnemonic[0] = 'd';
    out->mnemonic[1] = 'b';
    out->mnemonic[2] = '\0';
    out->operands[0] = '\0';
    operand_pos = 0;
    dis_append_hex_u8(out->operands, &operand_pos, DIS_MAX_OPERANDS, buf[offset]);
    off = offset + 1u;

 done:
    out->byte_count = (int)(off - offset);
    if (out->byte_count > DIS_MAX_INSN_BYTES) {
        out->byte_count = DIS_MAX_INSN_BYTES;
    }
    if (out->byte_count < 1) {
        out->byte_count = 1;
    }
    memcpy(out->bytes, buf + offset, (uint32_t)out->byte_count);
    return out->byte_count;
}

static void dis_fmt_bytes(char *dst, const uint8_t *bytes, int count) {
    int i;
    int pos = 0;
    static const char hex[] = "0123456789ABCDEF";

    for (i = 0; i < 20; i++) {
        dst[i] = ' ';
    }
    dst[20] = '\0';

    for (i = 0; i < count && i < 5; i++) {
        int base = i * 4;
        dst[base + 0] = hex[(bytes[i] >> 4) & 0x0Fu];
        dst[base + 1] = hex[bytes[i] & 0x0Fu];
        dst[base + 2] = ' ';
        dst[base + 3] = ' ';
        pos = base + 4;
    }

    (void)pos;
}

static void dis_fmt_addr(char *dst, uint32_t addr) {
    int pos = 0;
    dis_append_hex_u32(dst, &pos, 12, addr);
}

static void dis_out(dis_output_fn out_fn, const char *s) {
    if (out_fn) {
        out_fn(s);
    } else {
        print(s);
    }
}

void dis_disassemble(const uint8_t *buf, uint32_t size, uint32_t base_addr,
                     const dis_sym_t *syms, int nsyms,
                     dis_output_fn out_fn) {
    uint32_t off = 0;

    if (!buf || size == 0u) {
        dis_out(out_fn, "dis: empty code buffer\n");
        return;
    }

    while (off < size) {
        dis_insn_t insn;
        char line[256];
        char bytes_col[21];
        char addr_col[12];
        const char *sym = dis_find_sym(syms, nsyms, base_addr + off);
        int pos = 0;
        int i;
        int n;

        if (sym && sym[0]) {
            char sym_line[96];
            int sp = 0;
            dis_fmt_addr(addr_col, base_addr + off);
            dis_append_str(sym_line, &sp, (int)sizeof(sym_line), addr_col);
            dis_append_str(sym_line, &sp, (int)sizeof(sym_line), " <");
            dis_append_str(sym_line, &sp, (int)sizeof(sym_line), sym);
            dis_append_str(sym_line, &sp, (int)sizeof(sym_line), ">:\n");
            dis_out(out_fn, sym_line);
        }

        n = dis_decode_one(buf, off, size, base_addr, &insn);
        if (n <= 0) {
            break;
        }

        dis_fmt_addr(addr_col, insn.addr);
        dis_fmt_bytes(bytes_col, insn.bytes, insn.byte_count);

        dis_append_str(line, &pos, (int)sizeof(line), addr_col);
        dis_append_str(line, &pos, (int)sizeof(line), ":  ");
        dis_append_str(line, &pos, (int)sizeof(line), bytes_col);
        dis_append_char(line, &pos, (int)sizeof(line), ' ');
        dis_append_str(line, &pos, (int)sizeof(line), insn.mnemonic);

        i = (int)strlen(insn.mnemonic);
        while (i < 8) {
            dis_append_char(line, &pos, (int)sizeof(line), ' ');
            i++;
        }

        if (insn.operands[0] != '\0') {
            dis_append_char(line, &pos, (int)sizeof(line), ' ');
            dis_append_str(line, &pos, (int)sizeof(line), insn.operands);
        }
        dis_append_char(line, &pos, (int)sizeof(line), '\n');

        dis_out(out_fn, line);
        off += (uint32_t)n;
    }
}

int dis_elf(const char *path, dis_output_fn out_fn) {
    vfs_stat_t st;
    int rc;
    uint8_t *file_buf;
    elf32_ehdr_t *eh;
    uint32_t i;
    uint8_t *code_ptr = NULL;
    uint32_t code_size = 0;
    uint32_t code_base = 0;
    dis_sym_t syms[DIS_MAX_SYMS];
    int nsyms = 0;

    if (!path || path[0] == '\0') {
        dis_out(out_fn, "dis: invalid path\n");
        return VFS_EINVAL;
    }

    rc = vfs_stat(path, &st);
    if (rc < 0) {
        dis_out(out_fn, "dis: file not found\n");
        return rc;
    }

    if (st.size < (uint32_t)sizeof(elf32_ehdr_t)) {
        dis_out(out_fn, "dis: file too small for ELF header\n");
        return VFS_EINVAL;
    }

    file_buf = kmalloc(st.size);
    if (!file_buf) {
        dis_out(out_fn, "dis: out of memory\n");
        return VFS_ENOSPC;
    }

    rc = vfs_read_all(path, file_buf, st.size);
    if (rc < 0) {
        kfree(file_buf);
        dis_out(out_fn, "dis: read failed\n");
        return rc;
    }

    eh = (elf32_ehdr_t *)file_buf;
    if (eh->e_ident[0] != ELF_MAGIC_0 || eh->e_ident[1] != ELF_MAGIC_1 ||
        eh->e_ident[2] != ELF_MAGIC_2 || eh->e_ident[3] != ELF_MAGIC_3 ||
        eh->e_ident[4] != ELF_CLASS_32 || eh->e_machine != ELF_MACHINE_386) {
        kfree(file_buf);
        dis_out(out_fn, "dis: not a valid ELF32 i386 file\n");
        return VFS_EINVAL;
    }

    if (eh->e_phoff + (uint32_t)eh->e_phnum * (uint32_t)eh->e_phentsize <= st.size) {
        for (i = 0; i < (uint32_t)eh->e_phnum; i++) {
            uint32_t poff = eh->e_phoff + i * (uint32_t)eh->e_phentsize;
            elf32_phdr_t ph;
            if (poff + (uint32_t)sizeof(ph) > st.size) {
                break;
            }
            memcpy(&ph, file_buf + poff, (uint32_t)sizeof(ph));
            if (ph.p_type == ELF_PT_LOAD && ph.p_filesz > 0u &&
                ph.p_offset + ph.p_filesz <= st.size) {
                code_ptr = file_buf + ph.p_offset;
                code_size = ph.p_filesz;
                code_base = ph.p_vaddr;
                break;
            }
        }
    }

    if (!code_ptr) {
        kfree(file_buf);
        dis_out(out_fn, "dis: no loadable code segment found\n");
        return VFS_EINVAL;
    }

    if (eh->e_shoff != 0u && eh->e_shnum != 0u && eh->e_shentsize != 0u &&
        eh->e_shoff + (uint32_t)eh->e_shnum * (uint32_t)eh->e_shentsize <= st.size) {
        for (i = 0; i < (uint32_t)eh->e_shnum; i++) {
            uint32_t sh_off = eh->e_shoff + i * (uint32_t)eh->e_shentsize;
            elf32_shdr_t sh;
            if (sh_off + (uint32_t)sizeof(sh) > st.size) {
                continue;
            }
            memcpy(&sh, file_buf + sh_off, (uint32_t)sizeof(sh));
            if (sh.sh_type != ELF_SHT_SYMTAB || sh.sh_entsize == 0u ||
                sh.sh_offset + sh.sh_size > st.size) {
                continue;
            }
            if (sh.sh_link >= (uint32_t)eh->e_shnum) {
                continue;
            }
            {
                uint32_t l_off = eh->e_shoff + sh.sh_link * (uint32_t)eh->e_shentsize;
                elf32_shdr_t lsh;
                uint8_t *strtab;
                uint32_t strsz;
                uint32_t nent;
                uint32_t j;

                if (l_off + (uint32_t)sizeof(lsh) > st.size) {
                    continue;
                }
                memcpy(&lsh, file_buf + l_off, (uint32_t)sizeof(lsh));
                if (lsh.sh_type != ELF_SHT_STRTAB ||
                    lsh.sh_offset + lsh.sh_size > st.size) {
                    continue;
                }

                strtab = file_buf + lsh.sh_offset;
                strsz = lsh.sh_size;
                nent = sh.sh_size / sh.sh_entsize;

                for (j = 0; j < nent && nsyms < DIS_MAX_SYMS; j++) {
                    uint32_t one_off = sh.sh_offset + j * sh.sh_entsize;
                    elf32_sym_t sym;
                    uint8_t st_type;

                    if (one_off + (uint32_t)sizeof(sym) > st.size) {
                        break;
                    }

                    memcpy(&sym, file_buf + one_off, (uint32_t)sizeof(sym));
                    if (sym.st_name == 0u || sym.st_name >= strsz || sym.st_value == 0u) {
                        continue;
                    }

                    st_type = (uint8_t)(sym.st_info & 0x0Fu);
                    if (st_type != ELF_STT_FUNC) {
                        continue;
                    }

                    {
                        const char *name = (const char *)(strtab + sym.st_name);
                        int k = 0;
                        syms[nsyms].addr = sym.st_value;
                        while (name[k] && k < (int)sizeof(syms[nsyms].name) - 1) {
                            syms[nsyms].name[k] = name[k];
                            k++;
                        }
                        syms[nsyms].name[k] = '\0';
                        nsyms++;
                    }
                }
            }
        }
    }

    dis_disassemble(code_ptr, code_size, code_base, syms, nsyms, out_fn);
    kfree(file_buf);
    return 0;
}
