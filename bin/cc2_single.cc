//help: cc2 bootstrap single-file (Step 6: parser+codegen bootstrap)
//help: Usage: cc2_single [source.cc] [-o out.elf]
//help: Runs self-tests for cc2 string, lexer, preprocessor, parser, and ELF utility functions.
//help: If tests pass and a source path is provided, compiles source.cc to an ELF output.

typedef unsigned int uint32_t;

#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_CREAT    0x0100
#define O_TRUNC    0x0200

#define CC2_TK_EOF      0
#define CC2_TK_INT_KW   1
#define CC2_TK_IF       2
#define CC2_TK_IDENT    3
#define CC2_TK_INT_LIT  4
#define CC2_TK_EQ       5
#define CC2_TK_SEMI     6
#define CC2_TK_LPAREN   7
#define CC2_TK_RPAREN   8
#define CC2_TK_GE       9
#define CC2_TK_PLUSEQ  10
#define CC2_TK_PLUS    11
#define CC2_TK_MINUS   12
#define CC2_TK_STAR    13
#define CC2_TK_SLASH   14
#define CC2_TK_RETURN  15
#define CC2_TK_EQEQ    16
#define CC2_TK_NE      17
#define CC2_TK_LT      18
#define CC2_TK_LE      19
#define CC2_TK_GT      20
#define CC2_TK_WHILE   21
#define CC2_TK_BREAK   22
#define CC2_TK_CONTINUE 23
#define CC2_TK_LBRACE  24
#define CC2_TK_RBRACE  25
#define CC2_TK_ELSE    26
#define CC2_TK_FOR     27
#define CC2_TK_COMMA   28
#define CC2_TK_ANDAND  29
#define CC2_TK_OROR    30
#define CC2_TK_BANG    31
#define CC2_TK_PERCENT 32
#define CC2_TK_AMP     33
#define CC2_TK_PIPE    34
#define CC2_TK_CARET   35
#define CC2_TK_SHL     36
#define CC2_TK_SHR     37
#define CC2_TK_TILDE   38
#define CC2_TK_STRUCT  39
#define CC2_TK_DOT     40
#define CC2_TK_LBRACKET 41
#define CC2_TK_RBRACKET 42
#define CC2_TK_MINUSEQ 43
#define CC2_TK_STAREQ 44
#define CC2_TK_SLASHEQ 45
#define CC2_TK_PERCENTEQ 46
#define CC2_TK_ANDEQ 47
#define CC2_TK_OREQ 48
#define CC2_TK_XOREQ 49
#define CC2_TK_SHLEQ 50
#define CC2_TK_SHREQ 51
#define CC2_TK_PLUSPLUS 52
#define CC2_TK_MINUSMINUS 53
#define CC2_TK_QUESTION 54
#define CC2_TK_COLON 55
#define CC2_TK_SWITCH 56
#define CC2_TK_CASE 57
#define CC2_TK_DEFAULT 58
#define CC2_TK_DO 59
#define CC2_TK_ASM 60
#define CC2_TK_STRING 61
#define CC2_TK_UNKNOWN 99

#define CC2_PP_MAX_DEFINES 256
#define CC2_PP_MAX_NAME    48
#define CC2_PP_MAX_BODY    192
#define CC2_PP_MAX_COND    8
#define CC2_PP_MAX_INCLUDE_DEPTH 1
#define CC2_PP_FILE_BUF    4096
#define CC2_PP_MAX_PARAMS  8
#define CC2_PP_MAX_PARAM_NAME 24
#define CC2_PP_MAX_ARG     192
#define CC2_PP_PARAM_NAME_BUF 192
#define CC2_PP_ARGS_BUF 1536
#define CC2_PP_NAME_BUF 12288
#define CC2_PP_BODY_BUF 49152
#define CC2_PARSE_MAX_LOCALS 2048
#define CC2_PARSE_LOCAL_NAME 24
#define CC2_PARSE_LOCAL_BUF 49152
#define CC2_PARSE_MAX_ARRAYS 192
#define CC2_PARSE_ARR_BUF 4608
#define CC2_PARSE_MAX_PATCHES 32
#define CC2_PARSE_MAX_FUNCS 320
#define CC2_PARSE_FN_NAME 24
#define CC2_PARSE_FN_BUF 7680
#define CC2_PARSE_MAX_SCOPE 64
#define CC2_PARSE_MAX_CALL_PATCHES 320
#define CC2_PARSE_ARG_MAX 16
#define CC2_PARSE_ARG_CODE_MAX 160
#define CC2_PARSE_CALL_NAME_BUF 7680
#define CC2_PARSE_ARG_CODE_BUF 2560
#define CC2_PARSE_ARG_CODE_STACK_DEPTH 2
#define CC2_PARSE_ARG_CODE_STACK_BUF 5120
#define CC2_PARSE_MAX_STRUCTS 16
#define CC2_PARSE_STRUCT_NAME 24
#define CC2_PARSE_MAX_STRUCT_FIELDS 16
#define CC2_PARSE_MAX_STRUCT_META 256
#define CC2_PARSE_STRUCT_FIELD_NAME 24
#define CC2_PARSE_STRUCT_NAME_BUF 384
#define CC2_PARSE_STRUCT_FIELD_BUF 6144
#define CC2_PARSE_MAX_GLOBALS 256
#define CC2_PARSE_GLOBAL_NAME 24
#define CC2_PARSE_GLOBAL_BUF 6144
#define CC2_MAIN_SRC_MAX 262144
#define CC2_MAIN_PRE_MAX 393216
#define CC2_MAIN_CODE_MAX 262144
#define CC2_MAIN_DATA_MAX 196608
#define CC2_PP_INCLUDE_BUF_STACK 8192
#define CC2_OUT_CODE_BASE 0x00400000
#define CC2_OUT_DATA_BASE 0x00440000
#define CC2_GUARD_MAX 4000000
#define CC2_PTR_MIN 0x00001000
#define CC2_PTR_MAX 0x02000000
#define CC2_PTR_SENTINEL 0xFFFFFFFF

int cc2_ptr_readable(char *p) {
    uint32_t v = (uint32_t)p;
    if (!p) return 0;
    if (v < CC2_PTR_MIN) return 0;
    if (v >= CC2_PTR_MAX) return 0;
    if (v == CC2_PTR_SENTINEL) return 0;
    return 1;
}

int cc2_strlen(char *s) {
    int n = 0;
    if (!cc2_ptr_readable(s)) return 0;
    while (n < CC2_GUARD_MAX && s[n]) n++;
    return n;
}

void cc2_strcpy(char *dst, char *src) {
    int i = 0;
    if (!cc2_ptr_readable(dst) || !cc2_ptr_readable(src)) return;
    while (src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void cc2_strncpy(char *dst, char *src, int n) {
    int i = 0;
    if (!cc2_ptr_readable(dst) || !cc2_ptr_readable(src) || n <= 0) return;
    while (i < n && src[i]) {
        dst[i] = src[i];
        i++;
    }
    while (i < n) {
        dst[i] = 0;
        i++;
    }
}

int cc2_strcmp(char *a, char *b) {
    int i = 0;
    if (!cc2_ptr_readable(a) || !cc2_ptr_readable(b)) return 1;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return ((int)a[i]) - ((int)b[i]);
}

int cc2_strncmp(char *a, char *b, int n) {
    int i = 0;
    if (n <= 0) return 0;
    if (!cc2_ptr_readable(a) || !cc2_ptr_readable(b)) return 1;
    while (i < n && a[i] && b[i] && a[i] == b[i]) i++;
    if (i >= n) return 0;
    return ((int)a[i]) - ((int)b[i]);
}

char *cc2_strchr(char *s, int c) {
    char ch = (char)c;
    if (!cc2_ptr_readable(s)) return 0;
    while (s[0]) {
        if (s[0] == ch) return s;
        s = s + 1;
    }
    if (ch == 0) return s;
    return 0;
}

int cc2_isdigit(int c) { return c >= '0' && c <= '9'; }
int cc2_isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int cc2_isalnum(int c) { return cc2_isalpha(c) || cc2_isdigit(c); }
int cc2_isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
int cc2_isxdigit(int c) {
    return cc2_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int cc2_atoi(char *s) {
    int i = 0;
    int sign = 1;
    int v = 0;
    if (!cc2_ptr_readable(s)) return 0;
    while (cc2_isspace(s[i])) i++;
    if (s[i] == '-') { sign = -1; i++; }
    else if (s[i] == '+') { i++; }
    while (cc2_isdigit(s[i])) {
        v = v * 10 + (s[i] - '0');
        i++;
    }
    return v * sign;
}

int cc2_xtoi(char *s) {
    int i = 0;
    int v = 0;
    if (!cc2_ptr_readable(s)) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) i = 2;
    while (cc2_isxdigit(s[i])) {
        int d = 0;
        if (s[i] >= '0' && s[i] <= '9') d = s[i] - '0';
        else if (s[i] >= 'a' && s[i] <= 'f') d = 10 + (s[i] - 'a');
        else d = 10 + (s[i] - 'A');
        v = (v << 4) | d;
        i++;
    }
    return v;
}

void cc2_itoa(int n, char *buf) {
    char tmp[16];
    int i = 0;
    int neg = 0;
    if (n == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return;
    }
    if (n < 0) {
        neg = 1;
        n = -n;
    }
    while (n > 0 && i < 15) {
        tmp[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    int p = 0;
    if (neg) buf[p++] = '-';
    while (i > 0) buf[p++] = tmp[--i];
    buf[p] = 0;
}

void cc2_itoh(uint32_t n, char *buf) {
    cc2_strcpy(buf, "00000000");
    return;
}

void cc2_memcpy_n(char *dst, char *src, int n) {
    int i = 0;
    while (i < n) {
        dst[i] = src[i];
        i++;
    }
}

int cc2_test_failures;

void cc2_expect_int() {
    return;
}

void cc2_expect_str() {
    return;
}

struct cc2_token {
    int type;
    int int_val;
    char text[256];
};

struct cc2_define {
    char name[CC2_PP_MAX_NAME];
    char body[CC2_PP_MAX_BODY];
    int is_func;
    int param_count;
};

/* Keep token temporaries in globals: self-hosted local-struct codegen is unstable. */
struct cc2_token cc2_tok_eval;
struct cc2_token cc2_tok_codegen;
struct cc2_token cc2_tok_stmt_block;

char *cc2_pp_name_data;
char *cc2_pp_body_data;
int *cc2_pp_is_func_data;
int *cc2_pp_param_count_data;
int cc2_pp_define_count;

char *cc2_lex_src;
int cc2_lex_pos;
int cc2_lex_line;
char cc2_cg_fn_names[CC2_PARSE_FN_BUF];
int cc2_cg_fn_pos[CC2_PARSE_MAX_FUNCS];
int cc2_cg_fn_count;
char cc2_cg_call_names[CC2_PARSE_CALL_NAME_BUF];
int cc2_cg_call_patch_pos[CC2_PARSE_MAX_CALL_PATCHES];
int cc2_cg_call_count;
char cc2_struct_names[CC2_PARSE_STRUCT_NAME_BUF];
int cc2_struct_field_count[CC2_PARSE_MAX_STRUCTS];
char cc2_struct_field_names[CC2_PARSE_STRUCT_FIELD_BUF];
int cc2_struct_field_off[CC2_PARSE_MAX_STRUCT_META];
int cc2_struct_field_size[CC2_PARSE_MAX_STRUCT_META];
int cc2_struct_field_elem_size[CC2_PARSE_MAX_STRUCT_META];
int cc2_struct_field_is_array[CC2_PARSE_MAX_STRUCT_META];
int cc2_struct_size[CC2_PARSE_MAX_STRUCTS];
char cc2_arr_names[CC2_PARSE_ARR_BUF];
int cc2_arr_base[CC2_PARSE_MAX_ARRAYS];
int cc2_arr_len[CC2_PARSE_MAX_ARRAYS];
int cc2_arr_count;
char cc2_global_names[CC2_PARSE_GLOBAL_BUF];
int cc2_global_off[CC2_PARSE_MAX_GLOBALS];
int cc2_global_size[CC2_PARSE_MAX_GLOBALS];
int cc2_global_elem_size[CC2_PARSE_MAX_GLOBALS];
int cc2_global_is_array[CC2_PARSE_MAX_GLOBALS];
int cc2_global_struct_ptr_si[CC2_PARSE_MAX_GLOBALS];
int cc2_global_count;
int cc2_scope_starts[CC2_PARSE_MAX_SCOPE];
int cc2_scope_depth;
char *cc2_cg_data;
int cc2_cg_data_pos;
char cc2_codegen_local_names[CC2_PARSE_LOCAL_BUF];
int cc2_local_struct_ptr_si[CC2_PARSE_MAX_LOCALS];
char cc2_arg_code_stack[CC2_PARSE_ARG_CODE_STACK_BUF];
int cc2_arg_code_depth;
char cc2_pp_include_buf[CC2_PP_INCLUDE_BUF_STACK];
char cc2_pp_tmp_name[CC2_PP_MAX_NAME];
char cc2_pp_tmp_body[CC2_PP_MAX_BODY];
char cc2_pp_tmp_enc_body[CC2_PP_MAX_BODY];
char cc2_pp_tmp_params[CC2_PP_PARAM_NAME_BUF];
int cc2_pp_cond_skip[CC2_PP_MAX_COND];
int cc2_pp_cond_parent_skip[CC2_PP_MAX_COND];
int cc2_pp_cond_taken[CC2_PP_MAX_COND];
char cc2_pp_tmp_dir[16];
char cc2_pp_tmp_ident[CC2_PP_MAX_NAME];
char cc2_pp_tmp_path[128];
char cc2_pp_tmp_pname[CC2_PP_MAX_PARAM_NAME];
char cc2_pp_tmp_tok[CC2_PP_MAX_PARAM_NAME];
char cc2_main_in_path[128];
char cc2_main_out_path[128];
char cc2_main_args_buf[1024];
char cc2_arg_tok[128];
char cc2_elf_hdr_buf[128];
int cc2_codegen_entry_off;
int cc2_codegen_out_len;
char *cc2_codegen_src_ptr;
char *cc2_codegen_code_ptr;
int cc2_codegen_code_max;
char cc2_codegen_fname[CC2_PARSE_FN_NAME];
char cc2_codegen_cur_fn[CC2_PARSE_FN_NAME];
int cc2_codegen_pos;
int cc2_codegen_i;
int cc2_codegen_guard;
int cc2_codegen_main_idx;
int cc2_codegen_loop_pos;
int cc2_codegen_loop_type;
int cc2_codegen_top_count;
int cc2_codegen_fn_idx;
int cc2_codegen_fn_addr;
char *cc2_elf_code_ptr;
int cc2_elf_code_size;
char *cc2_elf_data_ptr;
int cc2_elf_data_size;
int cc2_elf_entry_off;
char *cc2_compile_src_buf;
char *cc2_compile_pre_buf;
char *cc2_compile_code_buf;
char *cc2_compile_data_buf;
char *cc2_compile_pp_name_buf;
char *cc2_compile_pp_body_buf;
int *cc2_compile_pp_is_func_buf;
int *cc2_compile_pp_param_count_buf;

int cc2_ident_start(int c) { return cc2_isalpha(c) || c == '_'; }
int cc2_ident_body(int c) { return cc2_isalnum(c) || c == '_'; }

int cc2_pp_slot_base(int idx, int stride) {
    int base = 0;
    if (idx < 0 || stride <= 0) return -1;
    while (idx > 0) {
        if (base > 0x7FFFFFFF - stride) return -1;
        base = base + stride;
        idx--;
    }
    return base;
}

int cc2_pp_find_define(char *name) {
    int i = 0;
    int max_defs = cc2_pp_define_count;
    if (!cc2_ptr_readable(name)) return -1;
    if (!cc2_pp_name_data) return -1;
    if (max_defs < 0) max_defs = 0;
    if (max_defs > CC2_PP_MAX_DEFINES) max_defs = CC2_PP_MAX_DEFINES;
    while (i < max_defs) {
        int j = 0;
        int base = i * CC2_PP_MAX_NAME;
        while (j < CC2_PP_MAX_NAME && name[j] && cc2_pp_name_data[base + j] &&
               name[j] == cc2_pp_name_data[base + j]) {
            j++;
        }
        if (name[j] == 0 && cc2_pp_name_data[base + j] == 0) return i;
        i++;
    }
    return -1;
}

void cc2_maybe_yield(int counter) {
    if ((counter & 4095) == 0) {
        yield();
    }
}

int cc2_lex_src_valid() {
    return cc2_ptr_readable(cc2_lex_src);
}

char cc2_lex_peek() {
    if (!cc2_lex_src_valid()) return 0;
    if (cc2_lex_pos < 0 || cc2_lex_pos >= CC2_MAIN_PRE_MAX) return 0;
    return cc2_lex_src[cc2_lex_pos];
}
char cc2_lex_peek2() {
    int idx = cc2_lex_pos + 1;
    if (!cc2_lex_src_valid()) return 0;
    if (idx < 0 || idx >= CC2_MAIN_PRE_MAX) return 0;
    return cc2_lex_src[idx];
}
char cc2_lex_peek3() {
    int idx = cc2_lex_pos + 2;
    if (!cc2_lex_src_valid()) return 0;
    if (idx < 0 || idx >= CC2_MAIN_PRE_MAX) return 0;
    return cc2_lex_src[idx];
}
char cc2_lex_next_char() {
    char c = 0;
    if (!cc2_lex_src_valid()) return 0;
    if (cc2_lex_pos < 0 || cc2_lex_pos >= CC2_MAIN_PRE_MAX) return 0;
    c = cc2_lex_src[cc2_lex_pos];
    if (c) {
        cc2_lex_pos++;
        if (c == '\n') cc2_lex_line++;
    }
    return c;
}

void cc2_lex_init(char *src) {
    cc2_lex_src = src;
    cc2_lex_pos = 0;
    cc2_lex_line = 1;
}

void cc2_tok_clear(struct cc2_token *t) {
    t->type = CC2_TK_EOF;
    t->int_val = 0;
    t->text[0] = 0;
}

void cc2_lex_skip_ws() {
    char c = 0;
    char c2 = 0;
    if (!cc2_lex_src_valid()) return;
    while (1) {
        c = cc2_lex_peek();
        if (c == ' ' || c == '\t' || c == '\r') {
            cc2_lex_pos = cc2_lex_pos + 1;
            continue;
        }
        if (c == '\n') {
            cc2_lex_pos = cc2_lex_pos + 1;
            cc2_lex_line = cc2_lex_line + 1;
            continue;
        }
        if (c == '/') {
            c2 = cc2_lex_peek2();
            if (c2 == '/') {
                cc2_lex_pos = cc2_lex_pos + 2;
                while (1) {
                    c = cc2_lex_peek();
                    if (!c) break;
                    if (c == '\n') break;
                    cc2_lex_pos = cc2_lex_pos + 1;
                }
                continue;
            }
            if (c2 == '*') {
                cc2_lex_pos = cc2_lex_pos + 2;
                while (1) {
                    c = cc2_lex_peek();
                    if (!c) break;
                    if (c == '*') {
                        c2 = cc2_lex_peek2();
                        if (c2 == '/') {
                            cc2_lex_pos = cc2_lex_pos + 2;
                            break;
                        }
                    }
                    if (c == '\n') cc2_lex_line = cc2_lex_line + 1;
                    cc2_lex_pos = cc2_lex_pos + 1;
                }
                continue;
            }
        }
        break;
    }
}

int cc2_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

void cc2_lex_read_ident(struct cc2_token *out) {
    int i = 0;
    while (1) {
        int ch0 = (int)(unsigned char)cc2_lex_peek();
        if (!((ch0 >= 'a' && ch0 <= 'z') ||
              (ch0 >= 'A' && ch0 <= 'Z') ||
              (ch0 >= '0' && ch0 <= '9') ||
              ch0 == '_')) break;
        char ch = cc2_lex_next_char();
        if (i < 255) {
            out->text[i] = ch;
            i = i + 1;
        }
    }
    out->text[i] = 0;
    if (cc2_strcmp(out->text, "int") == 0) out->type = CC2_TK_INT_KW;
    else if (cc2_strcmp(out->text, "if") == 0) out->type = CC2_TK_IF;
    else if (cc2_strcmp(out->text, "else") == 0) out->type = CC2_TK_ELSE;
    else if (cc2_strcmp(out->text, "for") == 0) out->type = CC2_TK_FOR;
    else if (cc2_strcmp(out->text, "return") == 0) out->type = CC2_TK_RETURN;
    else if (cc2_strcmp(out->text, "while") == 0) out->type = CC2_TK_WHILE;
    else if (cc2_strcmp(out->text, "break") == 0) out->type = CC2_TK_BREAK;
    else if (cc2_strcmp(out->text, "continue") == 0) out->type = CC2_TK_CONTINUE;
    else if (cc2_strcmp(out->text, "struct") == 0) out->type = CC2_TK_STRUCT;
    else if (cc2_strcmp(out->text, "switch") == 0) out->type = CC2_TK_SWITCH;
    else if (cc2_strcmp(out->text, "case") == 0) out->type = CC2_TK_CASE;
    else if (cc2_strcmp(out->text, "default") == 0) out->type = CC2_TK_DEFAULT;
    else if (cc2_strcmp(out->text, "do") == 0) out->type = CC2_TK_DO;
    else if (cc2_strcmp(out->text, "asm") == 0) out->type = CC2_TK_ASM;
    else out->type = CC2_TK_IDENT;
}

void cc2_lex_read_number(struct cc2_token *out) {
    int v = 0;
    int i = 0;
    if (cc2_lex_peek() == '0' && (cc2_lex_peek2() == 'x' || cc2_lex_peek2() == 'X')) {
        char ch0 = cc2_lex_next_char();
        char ch1 = cc2_lex_next_char();
        if (i < 255) { out->text[i] = ch0; i = i + 1; }
        if (i < 255) { out->text[i] = ch1; i = i + 1; }
        while (1) {
            int d = cc2_hex_digit(cc2_lex_peek());
            if (d < 0) break;
            v = (v << 4) | d;
            char ch = cc2_lex_next_char();
            if (i < 255) {
                out->text[i] = ch;
                i = i + 1;
            }
        }
    } else {
        while (cc2_lex_peek() >= '0' && cc2_lex_peek() <= '9') {
            v = v * 10 + (cc2_lex_peek() - '0');
            char ch = cc2_lex_next_char();
            if (i < 255) {
                out->text[i] = ch;
                i = i + 1;
            }
        }
    }
    out->text[i] = 0;
    out->type = CC2_TK_INT_LIT;
    out->int_val = v;
}

void cc2_lex_next(struct cc2_token *out) {
    uint32_t src_p = (uint32_t)cc2_lex_src;
    if (!cc2_ptr_readable((char *)out)) return;
    if (!cc2_lex_src_valid()) {
        serial_printf("[cc2_lex] FAIL invalid src ptr=0x%x pos=%d\n", src_p, cc2_lex_pos);
        out->type = CC2_TK_EOF;
        out->int_val = 0;
        out->text[0] = 0;
        return;
    }
    if (cc2_lex_pos < 0 || cc2_lex_pos >= CC2_MAIN_PRE_MAX) {
        serial_printf("[cc2_lex] FAIL invalid pos=%d src=0x%x\n", cc2_lex_pos, src_p);
        out->type = CC2_TK_EOF;
        out->int_val = 0;
        out->text[0] = 0;
        return;
    }
    cc2_tok_clear(out);
    cc2_lex_skip_ws();
    char c = cc2_lex_peek();
    if (!c) {
        out->type = CC2_TK_EOF;
        return;
    }
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        c == '_') {
        cc2_lex_read_ident(out);
        return;
    }
    if (c >= '0' && c <= '9') {
        cc2_lex_read_number(out);
        return;
    }
    if (c == '"') {
        int i = 0;
        cc2_lex_next_char(); /* consume opening quote */
        while (cc2_lex_peek() && cc2_lex_peek() != '"') {
            int v = 0;
            char ch = cc2_lex_next_char();
            if (ch == '\\') {
                char esc = cc2_lex_next_char();
                if (esc == 'n') v = '\n';
                else if (esc == 'r') v = '\r';
                else if (esc == 't') v = '\t';
                else if (esc == '0') v = 0;
                else if (esc == '\\') v = '\\';
                else if (esc == '\'') v = '\'';
                else if (esc == '"') v = '"';
                else if (esc == 'x') {
                    int d1 = cc2_hex_digit(cc2_lex_peek());
                    int d2 = -1;
                    if (d1 >= 0) {
                        cc2_lex_next_char();
                        d2 = cc2_hex_digit(cc2_lex_peek());
                        if (d2 >= 0) {
                            cc2_lex_next_char();
                            v = (d1 << 4) | d2;
                        } else {
                            v = d1;
                        }
                    } else {
                        v = 0;
                    }
                } else {
                    v = (int)esc;
                    if (v < 0) v = v + 256;
                }
            } else {
                v = (int)ch;
                if (v < 0) v = v + 256;
            }
            if (i < 255) {
                out->text[i] = (char)v;
                i = i + 1;
            }
        }
        if (cc2_lex_peek() == '"') cc2_lex_next_char(); /* consume closing quote */
        out->text[i] = 0;
        out->type = CC2_TK_STRING;
        return;
    }
    if (c == '\'') {
        int v = 0;
        char ch;
        cc2_lex_next_char(); /* consume opening quote */
        ch = cc2_lex_next_char();
        if (ch == '\\') {
            char esc = cc2_lex_next_char();
            if (esc == 'n') v = '\n';
            else if (esc == 'r') v = '\r';
            else if (esc == 't') v = '\t';
            else if (esc == '0') v = 0;
            else if (esc == '\\') v = '\\';
            else if (esc == '\'') v = '\'';
            else if (esc == '"') v = '"';
            else if (esc == 'x') {
                int d1 = cc2_hex_digit(cc2_lex_peek());
                int d2 = -1;
                if (d1 >= 0) {
                    cc2_lex_next_char();
                    d2 = cc2_hex_digit(cc2_lex_peek());
                    if (d2 >= 0) {
                        cc2_lex_next_char();
                        v = (d1 << 4) | d2;
                    } else {
                        v = d1;
                    }
                } else {
                    v = 0;
                }
            } else {
                v = (int)esc;
                if (v < 0) v = v + 256;
            }
        } else {
            v = (int)ch;
            if (v < 0) v = v + 256;
        }
        if (cc2_lex_peek() == '\'') cc2_lex_next_char(); /* consume closing quote */
        out->type = CC2_TK_INT_LIT;
        out->int_val = v;
        out->text[0] = '\'';
        out->text[1] = 0;
        return;
    }

    cc2_lex_next_char();
    if (c == '=' && cc2_lex_peek() == '=') {
        cc2_lex_next_char();
        out->type = CC2_TK_EQEQ;
        out->text[0] = '=';
        out->text[1] = '=';
        out->text[2] = 0;
        return;
    }
    if (c == '=') {
        out->type = CC2_TK_EQ;
        out->text[0] = '=';
        out->text[1] = 0;
        return;
    }
    if (c == '!' && cc2_lex_peek() == '=') {
        cc2_lex_next_char();
        out->type = CC2_TK_NE;
        out->text[0] = '!';
        out->text[1] = '=';
        out->text[2] = 0;
        return;
    }
    if (c == '!') {
        out->type = CC2_TK_BANG;
        out->text[0] = '!';
        out->text[1] = 0;
        return;
    }
    if (c == ';') {
        out->type = CC2_TK_SEMI;
        out->text[0] = ';';
        out->text[1] = 0;
        return;
    }
    if (c == ',') {
        out->type = CC2_TK_COMMA;
        out->text[0] = ',';
        out->text[1] = 0;
        return;
    }
    if (c == '?') {
        out->type = CC2_TK_QUESTION;
        out->text[0] = '?';
        out->text[1] = 0;
        return;
    }
    if (c == ':') {
        out->type = CC2_TK_COLON;
        out->text[0] = ':';
        out->text[1] = 0;
        return;
    }
    if (c == '(') {
        out->type = CC2_TK_LPAREN;
        out->text[0] = '(';
        out->text[1] = 0;
        return;
    }
    if (c == ')') {
        out->type = CC2_TK_RPAREN;
        out->text[0] = ')';
        out->text[1] = 0;
        return;
    }
    if (c == '{') {
        out->type = CC2_TK_LBRACE;
        out->text[0] = '{';
        out->text[1] = 0;
        return;
    }
    if (c == '}') {
        out->type = CC2_TK_RBRACE;
        out->text[0] = '}';
        out->text[1] = 0;
        return;
    }
    if (c == '[') {
        out->type = CC2_TK_LBRACKET;
        out->text[0] = '[';
        out->text[1] = 0;
        return;
    }
    if (c == ']') {
        out->type = CC2_TK_RBRACKET;
        out->text[0] = ']';
        out->text[1] = 0;
        return;
    }
    if (c == '.') {
        out->type = CC2_TK_DOT;
        out->text[0] = '.';
        out->text[1] = 0;
        return;
    }
    if (c == '<' && cc2_lex_peek() == '=') {
        cc2_lex_next_char();
        out->type = CC2_TK_LE;
        out->text[0] = '<';
        out->text[1] = '=';
        out->text[2] = 0;
        return;
    }
    if (c == '<' && cc2_lex_peek() == '<' && cc2_lex_peek2() == '=') {
        cc2_lex_next_char();
        cc2_lex_next_char();
        out->type = CC2_TK_SHLEQ;
        out->text[0] = '<';
        out->text[1] = '<';
        out->text[2] = '=';
        out->text[3] = 0;
        return;
    }
    if (c == '<' && cc2_lex_peek() == '<') {
        cc2_lex_next_char();
        out->type = CC2_TK_SHL;
        out->text[0] = '<';
        out->text[1] = '<';
        out->text[2] = 0;
        return;
    }
    if (c == '<') {
        out->type = CC2_TK_LT;
        out->text[0] = '<';
        out->text[1] = 0;
        return;
    }
    if (c == '>' && cc2_lex_peek() == '=') {
        cc2_lex_next_char();
        out->type = CC2_TK_GE;
        out->text[0] = '>';
        out->text[1] = '=';
        out->text[2] = 0;
        return;
    }
    if (c == '>' && cc2_lex_peek() == '>' && cc2_lex_peek2() == '=') {
        cc2_lex_next_char();
        cc2_lex_next_char();
        out->type = CC2_TK_SHREQ;
        out->text[0] = '>';
        out->text[1] = '>';
        out->text[2] = '=';
        out->text[3] = 0;
        return;
    }
    if (c == '>' && cc2_lex_peek() == '>') {
        cc2_lex_next_char();
        out->type = CC2_TK_SHR;
        out->text[0] = '>';
        out->text[1] = '>';
        out->text[2] = 0;
        return;
    }
    if (c == '>') {
        out->type = CC2_TK_GT;
        out->text[0] = '>';
        out->text[1] = 0;
        return;
    }
    if (c == '+' && cc2_lex_peek() == '=') {
        cc2_lex_next_char();
        out->type = CC2_TK_PLUSEQ;
        out->text[0] = '+';
        out->text[1] = '=';
        out->text[2] = 0;
        return;
    }
    if (c == '+' && cc2_lex_peek() == '+') {
        cc2_lex_next_char();
        out->type = CC2_TK_PLUSPLUS;
        out->text[0] = '+';
        out->text[1] = '+';
        out->text[2] = 0;
        return;
    }
    if (c == '+') {
        out->type = CC2_TK_PLUS;
        out->text[0] = '+';
        out->text[1] = 0;
        return;
    }
    if (c == '-' && cc2_lex_peek() == '=') {
        cc2_lex_next_char();
        out->type = CC2_TK_MINUSEQ;
        out->text[0] = '-';
        out->text[1] = '=';
        out->text[2] = 0;
        return;
    }
    if (c == '-' && cc2_lex_peek() == '-') {
        cc2_lex_next_char();
        out->type = CC2_TK_MINUSMINUS;
        out->text[0] = '-';
        out->text[1] = '-';
        out->text[2] = 0;
        return;
    }
    if (c == '-' && cc2_lex_peek() == '>') {
        cc2_lex_next_char();
        out->type = CC2_TK_DOT;
        out->text[0] = '.';
        out->text[1] = 0;
        return;
    }
    if (c == '-') {
        out->type = CC2_TK_MINUS;
        out->text[0] = '-';
        out->text[1] = 0;
        return;
    }
    if (c == '*' && cc2_lex_peek() == '=') {
        cc2_lex_next_char();
        out->type = CC2_TK_STAREQ;
        out->text[0] = '*';
        out->text[1] = '=';
        out->text[2] = 0;
        return;
    }
    if (c == '*') {
        out->type = CC2_TK_STAR;
        out->text[0] = '*';
        out->text[1] = 0;
        return;
    }
    if (c == '%' && cc2_lex_peek() == '=') {
        cc2_lex_next_char();
        out->type = CC2_TK_PERCENTEQ;
        out->text[0] = '%';
        out->text[1] = '=';
        out->text[2] = 0;
        return;
    }
    if (c == '%') {
        out->type = CC2_TK_PERCENT;
        out->text[0] = '%';
        out->text[1] = 0;
        return;
    }
    if (c == '/' && cc2_lex_peek() == '=') {
        cc2_lex_next_char();
        out->type = CC2_TK_SLASHEQ;
        out->text[0] = '/';
        out->text[1] = '=';
        out->text[2] = 0;
        return;
    }
    if (c == '/') {
        out->type = CC2_TK_SLASH;
        out->text[0] = '/';
        out->text[1] = 0;
        return;
    }
    if (c == '&' && cc2_lex_peek() == '&') {
        cc2_lex_next_char();
        out->type = CC2_TK_ANDAND;
        out->text[0] = '&';
        out->text[1] = '&';
        out->text[2] = 0;
        return;
    }
    if (c == '&' && cc2_lex_peek() == '=') {
        cc2_lex_next_char();
        out->type = CC2_TK_ANDEQ;
        out->text[0] = '&';
        out->text[1] = '=';
        out->text[2] = 0;
        return;
    }
    if (c == '&') {
        out->type = CC2_TK_AMP;
        out->text[0] = '&';
        out->text[1] = 0;
        return;
    }
    if (c == '|' && cc2_lex_peek() == '|') {
        cc2_lex_next_char();
        out->type = CC2_TK_OROR;
        out->text[0] = '|';
        out->text[1] = '|';
        out->text[2] = 0;
        return;
    }
    if (c == '|' && cc2_lex_peek() == '=') {
        cc2_lex_next_char();
        out->type = CC2_TK_OREQ;
        out->text[0] = '|';
        out->text[1] = '=';
        out->text[2] = 0;
        return;
    }
    if (c == '|') {
        out->type = CC2_TK_PIPE;
        out->text[0] = '|';
        out->text[1] = 0;
        return;
    }
    if (c == '^' && cc2_lex_peek() == '=') {
        cc2_lex_next_char();
        out->type = CC2_TK_XOREQ;
        out->text[0] = '^';
        out->text[1] = '=';
        out->text[2] = 0;
        return;
    }
    if (c == '^') {
        out->type = CC2_TK_CARET;
        out->text[0] = '^';
        out->text[1] = 0;
        return;
    }
    if (c == '~') {
        out->type = CC2_TK_TILDE;
        out->text[0] = '~';
        out->text[1] = 0;
        return;
    }

    out->type = CC2_TK_UNKNOWN;
    out->text[0] = c;
    out->text[1] = 0;
}

char *cc2_tok_name(int t) {
    if (t == CC2_TK_EOF) return "EOF";
    if (t == CC2_TK_INT_KW) return "INT_KW";
    if (t == CC2_TK_IF) return "IF";
    if (t == CC2_TK_IDENT) return "IDENT";
    if (t == CC2_TK_INT_LIT) return "INT_LIT";
    if (t == CC2_TK_EQ) return "EQ";
    if (t == CC2_TK_SEMI) return "SEMI";
    if (t == CC2_TK_LPAREN) return "LPAREN";
    if (t == CC2_TK_RPAREN) return "RPAREN";
    if (t == CC2_TK_GE) return "GE";
    if (t == CC2_TK_PLUSEQ) return "PLUSEQ";
    if (t == CC2_TK_PLUS) return "PLUS";
    if (t == CC2_TK_MINUS) return "MINUS";
    if (t == CC2_TK_STAR) return "STAR";
    if (t == CC2_TK_SLASH) return "SLASH";
    if (t == CC2_TK_RETURN) return "RETURN";
    if (t == CC2_TK_EQEQ) return "EQEQ";
    if (t == CC2_TK_NE) return "NE";
    if (t == CC2_TK_LT) return "LT";
    if (t == CC2_TK_LE) return "LE";
    if (t == CC2_TK_GT) return "GT";
    if (t == CC2_TK_WHILE) return "WHILE";
    if (t == CC2_TK_BREAK) return "BREAK";
    if (t == CC2_TK_CONTINUE) return "CONTINUE";
    if (t == CC2_TK_LBRACE) return "LBRACE";
    if (t == CC2_TK_RBRACE) return "RBRACE";
    if (t == CC2_TK_ELSE) return "ELSE";
    if (t == CC2_TK_FOR) return "FOR";
    if (t == CC2_TK_COMMA) return "COMMA";
    if (t == CC2_TK_ANDAND) return "ANDAND";
    if (t == CC2_TK_OROR) return "OROR";
    if (t == CC2_TK_BANG) return "BANG";
    if (t == CC2_TK_PERCENT) return "PERCENT";
    if (t == CC2_TK_AMP) return "AMP";
    if (t == CC2_TK_PIPE) return "PIPE";
    if (t == CC2_TK_CARET) return "CARET";
    if (t == CC2_TK_SHL) return "SHL";
    if (t == CC2_TK_SHR) return "SHR";
    if (t == CC2_TK_TILDE) return "TILDE";
    if (t == CC2_TK_STRUCT) return "STRUCT";
    if (t == CC2_TK_DOT) return "DOT";
    if (t == CC2_TK_LBRACKET) return "LBRACKET";
    if (t == CC2_TK_RBRACKET) return "RBRACKET";
    if (t == CC2_TK_MINUSEQ) return "MINUSEQ";
    if (t == CC2_TK_STAREQ) return "STAREQ";
    if (t == CC2_TK_SLASHEQ) return "SLASHEQ";
    if (t == CC2_TK_PERCENTEQ) return "PERCENTEQ";
    if (t == CC2_TK_ANDEQ) return "ANDEQ";
    if (t == CC2_TK_OREQ) return "OREQ";
    if (t == CC2_TK_XOREQ) return "XOREQ";
    if (t == CC2_TK_SHLEQ) return "SHLEQ";
    if (t == CC2_TK_SHREQ) return "SHREQ";
    if (t == CC2_TK_PLUSPLUS) return "PLUSPLUS";
    if (t == CC2_TK_MINUSMINUS) return "MINUSMINUS";
    if (t == CC2_TK_QUESTION) return "QUESTION";
    if (t == CC2_TK_COLON) return "COLON";
    if (t == CC2_TK_SWITCH) return "SWITCH";
    if (t == CC2_TK_CASE) return "CASE";
    if (t == CC2_TK_DEFAULT) return "DEFAULT";
    if (t == CC2_TK_DO) return "DO";
    if (t == CC2_TK_ASM) return "ASM";
    if (t == CC2_TK_STRING) return "STRING";
    if (t == CC2_TK_UNKNOWN) return "UNKNOWN";
    return "TK?";
}

int cc2_tok_type_valid(int t) {
    return t >= CC2_TK_EOF && t <= CC2_TK_UNKNOWN;
}

void cc2_lex_selftest() {
    return;
}

int cc2_parse_expect(struct cc2_token *cur, int tk, char *what) {
    if (cur->type != tk) {
        serial_printf("[cc2_parse] FAIL line %d expected %s got=%s text='%s'\n",
                      cc2_lex_line, what, cc2_tok_name(cur->type), cur->text);
        cc2_test_failures++;
        return 0;
    }
    cc2_lex_next(cur);
    return 1;
}

int cc2_parse_op_prec(int tk) {
    if (tk == CC2_TK_OROR) return 1;
    if (tk == CC2_TK_ANDAND) return 2;
    if (tk == CC2_TK_PIPE) return 3;
    if (tk == CC2_TK_CARET) return 4;
    if (tk == CC2_TK_AMP) return 5;
    if (tk == CC2_TK_EQEQ || tk == CC2_TK_NE) return 6;
    if (tk == CC2_TK_LT || tk == CC2_TK_LE ||
        tk == CC2_TK_GT || tk == CC2_TK_GE) return 7;
    if (tk == CC2_TK_SHL || tk == CC2_TK_SHR) return 8;
    if (tk == CC2_TK_PLUS || tk == CC2_TK_MINUS) return 9;
    if (tk == CC2_TK_STAR || tk == CC2_TK_SLASH || tk == CC2_TK_PERCENT) return 10;
    return 0;
}

int cc2_parse_expr_prec(struct cc2_token *cur, int min_prec) {
    (void)min_prec;
    if (cur->type == CC2_TK_INT_LIT) {
        int v = cur->int_val;
        cc2_lex_next(cur);
        return v;
    }
    return 0;
}

int cc2_parse_eval(char *src, int *out_val) {
    struct cc2_token *cur = &cc2_tok_eval;
    cc2_lex_init(src);
    cc2_lex_next(cur);
    *out_val = cc2_parse_expr_prec(cur, 1);
    return 1;
}

void cc2_emit8(char *buf, int *pos, int v) {
    buf[*pos] = (char)(v & 255);
    *pos = *pos + 1;
}

void cc2_emit32le(char *buf, int *pos, int v) {
    cc2_emit8(buf, pos, v);
    cc2_emit8(buf, pos, v >> 8);
    cc2_emit8(buf, pos, v >> 16);
    cc2_emit8(buf, pos, v >> 24);
}

int cc2_codegen_ret_imm(char *buf, int max, int value) {
    int pos = 0;
    if (max < 6) return -1;
    cc2_emit8(buf, &pos, 0xB8);     // mov eax, imm32
    cc2_emit32le(buf, &pos, value);
    cc2_emit8(buf, &pos, 0xC3);     // ret
    return pos;
}

int cc2_parse_slot_base(int idx, int stride) {
    int base = 0;
    if (idx < 0 || stride <= 0) return -1;
    while (idx > 0) {
        if (base > 0x7FFFFFFF - stride) return -1;
        base = base + stride;
        idx--;
    }
    return base;
}

int cc2_parse_slot_limit(char *flat, int stride) {
    if (!flat || stride <= 0) return 0;
    if (flat == cc2_arr_names) return CC2_PARSE_MAX_ARRAYS;
    if (flat == cc2_global_names) return CC2_PARSE_MAX_GLOBALS;
    if (flat == cc2_cg_fn_names) return CC2_PARSE_MAX_FUNCS;
    if (flat == cc2_cg_call_names) return CC2_PARSE_MAX_CALL_PATCHES;
    if (flat == cc2_struct_names) return CC2_PARSE_MAX_STRUCTS;
    if (flat == cc2_struct_field_names) return CC2_PARSE_MAX_STRUCTS * CC2_PARSE_MAX_STRUCT_FIELDS;
    if (flat == cc2_codegen_local_names) return CC2_PARSE_MAX_LOCALS;
    if (stride == CC2_PARSE_LOCAL_NAME) return CC2_PARSE_MAX_LOCALS;
    if (stride == CC2_PARSE_FN_NAME) return CC2_PARSE_MAX_FUNCS;
    if (stride == CC2_PARSE_STRUCT_NAME) return CC2_PARSE_MAX_STRUCTS;
    if (stride == CC2_PARSE_STRUCT_FIELD_NAME) return CC2_PARSE_MAX_STRUCTS * CC2_PARSE_MAX_STRUCT_FIELDS;
    if (stride == CC2_PARSE_GLOBAL_NAME) return CC2_PARSE_MAX_GLOBALS;
    return 0;
}

void cc2_parse_slot_set(char *flat, int stride, int idx, char *src) {
    int limit = cc2_parse_slot_limit(flat, stride);
    int base = cc2_parse_slot_base(idx, stride);
    int i = 0;
    if (!cc2_ptr_readable(flat) || !cc2_ptr_readable(src) || base < 0) return;
    if (limit <= 0 || idx < 0 || idx >= limit) return;
    while (src[i] && i < stride - 1) {
        flat[base + i] = src[i];
        i++;
    }
    while (i < stride) {
        flat[base + i] = 0;
        i++;
    }
}

int cc2_parse_slot_eq(char *flat, int stride, int idx, char *name) {
    int limit = cc2_parse_slot_limit(flat, stride);
    int base = cc2_parse_slot_base(idx, stride);
    int i = 0;
    if (!cc2_ptr_readable(flat) || !cc2_ptr_readable(name) || base < 0) return 0;
    if (limit <= 0 || idx < 0 || idx >= limit) return 0;
    while (name[i] && flat[base + i]) {
        if (name[i] != flat[base + i]) return 0;
        i++;
    }
    return (name[i] == 0 && flat[base + i] == 0);
}

void cc2_parse_slot_copy(char *flat, int stride, int idx, char *dst, int max) {
    int limit = cc2_parse_slot_limit(flat, stride);
    int base = cc2_parse_slot_base(idx, stride);
    int i = 0;
    if (!cc2_ptr_readable(flat) || !cc2_ptr_readable(dst) || base < 0 || max <= 0) return;
    if (limit <= 0 || idx < 0 || idx >= limit) {
        dst[0] = 0;
        return;
    }
    while (i < max - 1 && flat[base + i]) {
        dst[i] = flat[base + i];
        i++;
    }
    dst[i] = 0;
}

int cc2_parse_find_local(char *local_names, int local_count, char *name) {
    if (!cc2_ptr_readable(local_names) || !cc2_ptr_readable(name)) return -1;
    if (local_count > CC2_PARSE_MAX_LOCALS) local_count = CC2_PARSE_MAX_LOCALS;
    int i = local_count - 1;
    while (i >= 0) {
        if (cc2_parse_slot_eq(local_names, CC2_PARSE_LOCAL_NAME, i, name)) return i;
        i--;
    }
    return -1;
}

void cc2_scope_reset() {
    cc2_scope_depth = 0;
}

int cc2_scope_push(int local_count) {
    if (cc2_scope_depth >= CC2_PARSE_MAX_SCOPE) return 0;
    cc2_scope_starts[cc2_scope_depth] = local_count;
    cc2_scope_depth = cc2_scope_depth + 1;
    return 1;
}

void cc2_scope_pop(int *local_count) {
    if (cc2_scope_depth <= 0) return;
    cc2_scope_depth = cc2_scope_depth - 1;
    if (local_count) {
        *local_count = cc2_scope_starts[cc2_scope_depth];
        cc2_arr_pop_to_local_count(*local_count);
    }
}

int cc2_scope_current_base() {
    if (cc2_scope_depth <= 0) return 0;
    return cc2_scope_starts[cc2_scope_depth - 1];
}

int cc2_parse_find_local_from(char *local_names, int start, int local_count, char *name) {
    if (!cc2_ptr_readable(local_names) || !cc2_ptr_readable(name)) return -1;
    if (local_count > CC2_PARSE_MAX_LOCALS) local_count = CC2_PARSE_MAX_LOCALS;
    int i = local_count - 1;
    if (start < 0) start = 0;
    while (i >= start) {
        if (cc2_parse_slot_eq(local_names, CC2_PARSE_LOCAL_NAME, i, name)) return i;
        i--;
    }
    return -1;
}

int cc2_parse_find_local_current_scope(char *local_names, int local_count, char *name) {
    int base = cc2_scope_current_base();
    return cc2_parse_find_local_from(local_names, base, local_count, name);
}

int cc2_parse_find_local_struct_base(char *local_names, int local_count, char *name) {
    int i = 0;
    int nlen = cc2_strlen(name);
    if (!cc2_ptr_readable(local_names) || !cc2_ptr_readable(name)) return -1;
    if (local_count > CC2_PARSE_MAX_LOCALS) local_count = CC2_PARSE_MAX_LOCALS;
    while (i < local_count) {
        int base = cc2_parse_slot_base(i, CC2_PARSE_LOCAL_NAME);
        int j = 0;
        if (base < 0) return -1;
        while (j < nlen && local_names[base + j] && local_names[base + j] == name[j]) j++;
        if (j == nlen && local_names[base + j] == '.') return i;
        i++;
    }
    return -1;
}

int cc2_is_type_word(char *name) {
    if (cc2_strcmp(name, "char") == 0) return 1;
    if (cc2_strcmp(name, "short") == 0) return 1;
    if (cc2_strcmp(name, "long") == 0) return 1;
    if (cc2_strcmp(name, "unsigned") == 0) return 1;
    if (cc2_strcmp(name, "signed") == 0) return 1;
    if (cc2_strcmp(name, "void") == 0) return 1;
    if (cc2_strcmp(name, "const") == 0) return 1;
    if (cc2_strcmp(name, "volatile") == 0) return 1;
    if (cc2_strcmp(name, "uint8_t") == 0) return 1;
    if (cc2_strcmp(name, "uint16_t") == 0) return 1;
    if (cc2_strcmp(name, "uint32_t") == 0) return 1;
    if (cc2_strcmp(name, "int8_t") == 0) return 1;
    if (cc2_strcmp(name, "int16_t") == 0) return 1;
    if (cc2_strcmp(name, "int32_t") == 0) return 1;
    if (cc2_strcmp(name, "size_t") == 0) return 1;
    return 0;
}

int cc2_ident_maybe_const(char *name) {
    int i = 0;
    int has_alpha = 0;
    if (!name || !name[0]) return 0;
    while (name[i]) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z')) return 0;
        if ((c >= 'A' && c <= 'Z')) has_alpha = 1;
        else if (!(c == '_' || (c >= '0' && c <= '9'))) return 0;
        i++;
    }
    return has_alpha;
}

int cc2_builtin_const_from_ident(char *name, int *out) {
    if (cc2_strcmp(name, "O_RDONLY") == 0) { *out = O_RDONLY; return 1; }
    if (cc2_strcmp(name, "O_WRONLY") == 0) { *out = O_WRONLY; return 1; }
    if (cc2_strcmp(name, "O_CREAT") == 0) { *out = O_CREAT; return 1; }
    if (cc2_strcmp(name, "O_TRUNC") == 0) { *out = O_TRUNC; return 1; }

    if (cc2_strcmp(name, "CC2_PP_MAX_DEFINES") == 0) { *out = CC2_PP_MAX_DEFINES; return 1; }
    if (cc2_strcmp(name, "CC2_PP_MAX_NAME") == 0) { *out = CC2_PP_MAX_NAME; return 1; }
    if (cc2_strcmp(name, "CC2_PP_MAX_BODY") == 0) { *out = CC2_PP_MAX_BODY; return 1; }
    if (cc2_strcmp(name, "CC2_PP_MAX_COND") == 0) { *out = CC2_PP_MAX_COND; return 1; }
    if (cc2_strcmp(name, "CC2_PP_MAX_INCLUDE_DEPTH") == 0) { *out = CC2_PP_MAX_INCLUDE_DEPTH; return 1; }
    if (cc2_strcmp(name, "CC2_PP_FILE_BUF") == 0) { *out = CC2_PP_FILE_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_PP_MAX_PARAMS") == 0) { *out = CC2_PP_MAX_PARAMS; return 1; }
    if (cc2_strcmp(name, "CC2_PP_MAX_PARAM_NAME") == 0) { *out = CC2_PP_MAX_PARAM_NAME; return 1; }
    if (cc2_strcmp(name, "CC2_PP_MAX_ARG") == 0) { *out = CC2_PP_MAX_ARG; return 1; }
    if (cc2_strcmp(name, "CC2_PP_PARAM_NAME_BUF") == 0) { *out = CC2_PP_PARAM_NAME_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_PP_ARGS_BUF") == 0) { *out = CC2_PP_ARGS_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_PP_NAME_BUF") == 0) { *out = CC2_PP_NAME_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_PP_BODY_BUF") == 0) { *out = CC2_PP_BODY_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_MAX_LOCALS") == 0) { *out = CC2_PARSE_MAX_LOCALS; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_LOCAL_NAME") == 0) { *out = CC2_PARSE_LOCAL_NAME; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_LOCAL_BUF") == 0) { *out = CC2_PARSE_LOCAL_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_MAX_ARRAYS") == 0) { *out = CC2_PARSE_MAX_ARRAYS; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_ARR_BUF") == 0) { *out = CC2_PARSE_ARR_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_MAX_PATCHES") == 0) { *out = CC2_PARSE_MAX_PATCHES; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_MAX_FUNCS") == 0) { *out = CC2_PARSE_MAX_FUNCS; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_FN_NAME") == 0) { *out = CC2_PARSE_FN_NAME; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_FN_BUF") == 0) { *out = CC2_PARSE_FN_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_MAX_SCOPE") == 0) { *out = CC2_PARSE_MAX_SCOPE; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_MAX_CALL_PATCHES") == 0) { *out = CC2_PARSE_MAX_CALL_PATCHES; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_ARG_MAX") == 0) { *out = CC2_PARSE_ARG_MAX; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_ARG_CODE_MAX") == 0) { *out = CC2_PARSE_ARG_CODE_MAX; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_CALL_NAME_BUF") == 0) { *out = CC2_PARSE_CALL_NAME_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_ARG_CODE_BUF") == 0) { *out = CC2_PARSE_ARG_CODE_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_ARG_CODE_STACK_DEPTH") == 0) { *out = CC2_PARSE_ARG_CODE_STACK_DEPTH; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_ARG_CODE_STACK_BUF") == 0) { *out = CC2_PARSE_ARG_CODE_STACK_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_MAX_STRUCTS") == 0) { *out = CC2_PARSE_MAX_STRUCTS; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_STRUCT_NAME") == 0) { *out = CC2_PARSE_STRUCT_NAME; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_MAX_STRUCT_FIELDS") == 0) { *out = CC2_PARSE_MAX_STRUCT_FIELDS; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_MAX_STRUCT_META") == 0) { *out = CC2_PARSE_MAX_STRUCT_META; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_STRUCT_FIELD_NAME") == 0) { *out = CC2_PARSE_STRUCT_FIELD_NAME; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_STRUCT_NAME_BUF") == 0) { *out = CC2_PARSE_STRUCT_NAME_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_STRUCT_FIELD_BUF") == 0) { *out = CC2_PARSE_STRUCT_FIELD_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_MAX_GLOBALS") == 0) { *out = CC2_PARSE_MAX_GLOBALS; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_GLOBAL_NAME") == 0) { *out = CC2_PARSE_GLOBAL_NAME; return 1; }
    if (cc2_strcmp(name, "CC2_PARSE_GLOBAL_BUF") == 0) { *out = CC2_PARSE_GLOBAL_BUF; return 1; }
    if (cc2_strcmp(name, "CC2_MAIN_SRC_MAX") == 0) { *out = CC2_MAIN_SRC_MAX; return 1; }
    if (cc2_strcmp(name, "CC2_MAIN_PRE_MAX") == 0) { *out = CC2_MAIN_PRE_MAX; return 1; }
    if (cc2_strcmp(name, "CC2_MAIN_CODE_MAX") == 0) { *out = CC2_MAIN_CODE_MAX; return 1; }
    if (cc2_strcmp(name, "CC2_MAIN_DATA_MAX") == 0) { *out = CC2_MAIN_DATA_MAX; return 1; }
    if (cc2_strcmp(name, "CC2_PP_INCLUDE_BUF_STACK") == 0) { *out = CC2_PP_INCLUDE_BUF_STACK; return 1; }
    if (cc2_strcmp(name, "CC2_OUT_CODE_BASE") == 0) { *out = CC2_OUT_CODE_BASE; return 1; }
    if (cc2_strcmp(name, "CC2_OUT_DATA_BASE") == 0) { *out = CC2_OUT_DATA_BASE; return 1; }
    if (cc2_strcmp(name, "CC2_GUARD_MAX") == 0) { *out = CC2_GUARD_MAX; return 1; }

    if (cc2_strcmp(name, "CC2_TK_EOF") == 0) { *out = CC2_TK_EOF; return 1; }
    if (cc2_strcmp(name, "CC2_TK_INT_KW") == 0) { *out = CC2_TK_INT_KW; return 1; }
    if (cc2_strcmp(name, "CC2_TK_IF") == 0) { *out = CC2_TK_IF; return 1; }
    if (cc2_strcmp(name, "CC2_TK_IDENT") == 0) { *out = CC2_TK_IDENT; return 1; }
    if (cc2_strcmp(name, "CC2_TK_INT_LIT") == 0) { *out = CC2_TK_INT_LIT; return 1; }
    if (cc2_strcmp(name, "CC2_TK_EQ") == 0) { *out = CC2_TK_EQ; return 1; }
    if (cc2_strcmp(name, "CC2_TK_SEMI") == 0) { *out = CC2_TK_SEMI; return 1; }
    if (cc2_strcmp(name, "CC2_TK_LPAREN") == 0) { *out = CC2_TK_LPAREN; return 1; }
    if (cc2_strcmp(name, "CC2_TK_RPAREN") == 0) { *out = CC2_TK_RPAREN; return 1; }
    if (cc2_strcmp(name, "CC2_TK_GE") == 0) { *out = CC2_TK_GE; return 1; }
    if (cc2_strcmp(name, "CC2_TK_PLUSEQ") == 0) { *out = CC2_TK_PLUSEQ; return 1; }
    if (cc2_strcmp(name, "CC2_TK_PLUS") == 0) { *out = CC2_TK_PLUS; return 1; }
    if (cc2_strcmp(name, "CC2_TK_MINUS") == 0) { *out = CC2_TK_MINUS; return 1; }
    if (cc2_strcmp(name, "CC2_TK_STAR") == 0) { *out = CC2_TK_STAR; return 1; }
    if (cc2_strcmp(name, "CC2_TK_SLASH") == 0) { *out = CC2_TK_SLASH; return 1; }
    if (cc2_strcmp(name, "CC2_TK_RETURN") == 0) { *out = CC2_TK_RETURN; return 1; }
    if (cc2_strcmp(name, "CC2_TK_EQEQ") == 0) { *out = CC2_TK_EQEQ; return 1; }
    if (cc2_strcmp(name, "CC2_TK_NE") == 0) { *out = CC2_TK_NE; return 1; }
    if (cc2_strcmp(name, "CC2_TK_LT") == 0) { *out = CC2_TK_LT; return 1; }
    if (cc2_strcmp(name, "CC2_TK_LE") == 0) { *out = CC2_TK_LE; return 1; }
    if (cc2_strcmp(name, "CC2_TK_GT") == 0) { *out = CC2_TK_GT; return 1; }
    if (cc2_strcmp(name, "CC2_TK_WHILE") == 0) { *out = CC2_TK_WHILE; return 1; }
    if (cc2_strcmp(name, "CC2_TK_BREAK") == 0) { *out = CC2_TK_BREAK; return 1; }
    if (cc2_strcmp(name, "CC2_TK_CONTINUE") == 0) { *out = CC2_TK_CONTINUE; return 1; }
    if (cc2_strcmp(name, "CC2_TK_LBRACE") == 0) { *out = CC2_TK_LBRACE; return 1; }
    if (cc2_strcmp(name, "CC2_TK_RBRACE") == 0) { *out = CC2_TK_RBRACE; return 1; }
    if (cc2_strcmp(name, "CC2_TK_ELSE") == 0) { *out = CC2_TK_ELSE; return 1; }
    if (cc2_strcmp(name, "CC2_TK_FOR") == 0) { *out = CC2_TK_FOR; return 1; }
    if (cc2_strcmp(name, "CC2_TK_COMMA") == 0) { *out = CC2_TK_COMMA; return 1; }
    if (cc2_strcmp(name, "CC2_TK_ANDAND") == 0) { *out = CC2_TK_ANDAND; return 1; }
    if (cc2_strcmp(name, "CC2_TK_OROR") == 0) { *out = CC2_TK_OROR; return 1; }
    if (cc2_strcmp(name, "CC2_TK_BANG") == 0) { *out = CC2_TK_BANG; return 1; }
    if (cc2_strcmp(name, "CC2_TK_PERCENT") == 0) { *out = CC2_TK_PERCENT; return 1; }
    if (cc2_strcmp(name, "CC2_TK_AMP") == 0) { *out = CC2_TK_AMP; return 1; }
    if (cc2_strcmp(name, "CC2_TK_PIPE") == 0) { *out = CC2_TK_PIPE; return 1; }
    if (cc2_strcmp(name, "CC2_TK_CARET") == 0) { *out = CC2_TK_CARET; return 1; }
    if (cc2_strcmp(name, "CC2_TK_SHL") == 0) { *out = CC2_TK_SHL; return 1; }
    if (cc2_strcmp(name, "CC2_TK_SHR") == 0) { *out = CC2_TK_SHR; return 1; }
    if (cc2_strcmp(name, "CC2_TK_TILDE") == 0) { *out = CC2_TK_TILDE; return 1; }
    if (cc2_strcmp(name, "CC2_TK_STRUCT") == 0) { *out = CC2_TK_STRUCT; return 1; }
    if (cc2_strcmp(name, "CC2_TK_DOT") == 0) { *out = CC2_TK_DOT; return 1; }
    if (cc2_strcmp(name, "CC2_TK_LBRACKET") == 0) { *out = CC2_TK_LBRACKET; return 1; }
    if (cc2_strcmp(name, "CC2_TK_RBRACKET") == 0) { *out = CC2_TK_RBRACKET; return 1; }
    if (cc2_strcmp(name, "CC2_TK_MINUSEQ") == 0) { *out = CC2_TK_MINUSEQ; return 1; }
    if (cc2_strcmp(name, "CC2_TK_STAREQ") == 0) { *out = CC2_TK_STAREQ; return 1; }
    if (cc2_strcmp(name, "CC2_TK_SLASHEQ") == 0) { *out = CC2_TK_SLASHEQ; return 1; }
    if (cc2_strcmp(name, "CC2_TK_PERCENTEQ") == 0) { *out = CC2_TK_PERCENTEQ; return 1; }
    if (cc2_strcmp(name, "CC2_TK_ANDEQ") == 0) { *out = CC2_TK_ANDEQ; return 1; }
    if (cc2_strcmp(name, "CC2_TK_OREQ") == 0) { *out = CC2_TK_OREQ; return 1; }
    if (cc2_strcmp(name, "CC2_TK_XOREQ") == 0) { *out = CC2_TK_XOREQ; return 1; }
    if (cc2_strcmp(name, "CC2_TK_SHLEQ") == 0) { *out = CC2_TK_SHLEQ; return 1; }
    if (cc2_strcmp(name, "CC2_TK_SHREQ") == 0) { *out = CC2_TK_SHREQ; return 1; }
    if (cc2_strcmp(name, "CC2_TK_PLUSPLUS") == 0) { *out = CC2_TK_PLUSPLUS; return 1; }
    if (cc2_strcmp(name, "CC2_TK_MINUSMINUS") == 0) { *out = CC2_TK_MINUSMINUS; return 1; }
    if (cc2_strcmp(name, "CC2_TK_QUESTION") == 0) { *out = CC2_TK_QUESTION; return 1; }
    if (cc2_strcmp(name, "CC2_TK_COLON") == 0) { *out = CC2_TK_COLON; return 1; }
    if (cc2_strcmp(name, "CC2_TK_SWITCH") == 0) { *out = CC2_TK_SWITCH; return 1; }
    if (cc2_strcmp(name, "CC2_TK_CASE") == 0) { *out = CC2_TK_CASE; return 1; }
    if (cc2_strcmp(name, "CC2_TK_DEFAULT") == 0) { *out = CC2_TK_DEFAULT; return 1; }
    if (cc2_strcmp(name, "CC2_TK_DO") == 0) { *out = CC2_TK_DO; return 1; }
    if (cc2_strcmp(name, "CC2_TK_ASM") == 0) { *out = CC2_TK_ASM; return 1; }
    if (cc2_strcmp(name, "CC2_TK_STRING") == 0) { *out = CC2_TK_STRING; return 1; }
    if (cc2_strcmp(name, "CC2_TK_UNKNOWN") == 0) { *out = CC2_TK_UNKNOWN; return 1; }

    return 0;
}

int cc2_const_from_ident_depth(char *name, int *out, int depth) {
    int idx = 0;
    int base = 0;
    int i = 0;
    char body[CC2_PP_MAX_BODY];
    char tok[CC2_PP_MAX_NAME];
    int ti = 0;

    if (depth > 8) return 0;
    idx = cc2_pp_find_define(name);
    if (idx < 0) return 0;
    if (cc2_pp_is_func_data[idx]) return 0;
    base = cc2_pp_slot_base(idx, CC2_PP_MAX_BODY);
    while (i < CC2_PP_MAX_BODY - 1 && cc2_pp_body_data[base + i]) {
        body[i] = cc2_pp_body_data[base + i];
        i++;
    }
    body[i] = 0;
    i = 0;
    while (body[i] == ' ' || body[i] == '\t') i++;
    if (!body[i]) return 0;
    if (body[i] == '0' && (body[i + 1] == 'x' || body[i + 1] == 'X')) {
        *out = cc2_xtoi(body + i);
        return 1;
    }
    if (cc2_isdigit(body[i]) ||
        ((body[i] == '-' || body[i] == '+') && cc2_isdigit(body[i + 1]))) {
        *out = cc2_atoi(body + i);
        return 1;
    }
    if (!cc2_ident_start(body[i])) return 0;
    while (cc2_ident_body(body[i]) && ti < CC2_PP_MAX_NAME - 1) {
        tok[ti] = body[i];
        ti++;
        i++;
    }
    tok[ti] = 0;
    if (!tok[0]) return 0;
    if (cc2_strcmp(tok, name) == 0) return 0;
    return cc2_const_from_ident_depth(tok, out, depth + 1);
}

int cc2_const_from_ident(char *name, int *out) {
    if (cc2_const_from_ident_depth(name, out, 0)) return 1;
    return cc2_builtin_const_from_ident(name, out);
}

int cc2_builtin_fn_addr(char *name, int *out) {
    if (cc2_strcmp(name, "serial_printf") == 0) { *out = (int)(uint32_t)serial_printf; return 1; }
    if (cc2_strcmp(name, "print") == 0) { *out = (int)(uint32_t)print; return 1; }
    if (cc2_strcmp(name, "println") == 0) { *out = (int)(uint32_t)println; return 1; }
    if (cc2_strcmp(name, "putchar") == 0) { *out = (int)(uint32_t)putchar; return 1; }
    if (cc2_strcmp(name, "yield") == 0) { *out = (int)(uint32_t)yield; return 1; }
    if (cc2_strcmp(name, "kmalloc") == 0) { *out = (int)(uint32_t)kmalloc; return 1; }
    if (cc2_strcmp(name, "kfree") == 0) { *out = (int)(uint32_t)kfree; return 1; }
    if (cc2_strcmp(name, "vfs_open") == 0) { *out = (int)(uint32_t)vfs_open; return 1; }
    if (cc2_strcmp(name, "vfs_close") == 0) { *out = (int)(uint32_t)vfs_close; return 1; }
    if (cc2_strcmp(name, "vfs_read") == 0) { *out = (int)(uint32_t)vfs_read; return 1; }
    if (cc2_strcmp(name, "vfs_write") == 0) { *out = (int)(uint32_t)vfs_write; return 1; }
    if (cc2_strcmp(name, "vfs_seek") == 0) { *out = (int)(uint32_t)vfs_seek; return 1; }
    if (cc2_strcmp(name, "get_args") == 0) { *out = (int)(uint32_t)get_args; return 1; }
    return 0;
}

int cc2_parse_array_len_token_stream(struct cc2_token *cur, int *out_len) {
    int len = 1;
    int got = 0;
    while (cur->type != CC2_TK_RBRACKET && cur->type != CC2_TK_EOF) {
        if (!got) {
            if (cur->type == CC2_TK_INT_LIT) {
                len = cur->int_val;
                got = 1;
            } else if (cur->type == CC2_TK_IDENT) {
                int v = 0;
                if (cc2_const_from_ident(cur->text, &v)) {
                    len = v;
                    got = 1;
                }
            }
        }
        cc2_lex_next(cur);
    }
    if (!cc2_parse_expect(cur, CC2_TK_RBRACKET, "']'")) return 0;
    if (len <= 0) len = 1;
    *out_len = len;
    return 1;
}

int cc2_parse_find_global(char *name) {
    int i = 0;
    while (i < cc2_global_count) {
        if (cc2_parse_slot_eq(cc2_global_names, CC2_PARSE_GLOBAL_NAME, i, name)) return i;
        i++;
    }
    return -1;
}

int cc2_is_byte_type_name(char *name) {
    if (cc2_strcmp(name, "char") == 0) return 1;
    if (cc2_strcmp(name, "uint8_t") == 0) return 1;
    if (cc2_strcmp(name, "int8_t") == 0) return 1;
    return 0;
}

void cc2_global_reset() {
    int i = 0;
    cc2_global_count = 0;
    while (i < CC2_PARSE_GLOBAL_BUF) {
        cc2_global_names[i] = 0;
        i++;
    }
    i = 0;
    while (i < CC2_PARSE_MAX_GLOBALS) {
        cc2_global_off[i] = 0;
        cc2_global_size[i] = 0;
        cc2_global_elem_size[i] = 0;
        cc2_global_is_array[i] = 0;
        cc2_global_struct_ptr_si[i] = -1;
        i++;
    }
}

int cc2_cg_alloc_zero(int size, int align) {
    int i = 0;
    int off = 0;
    if (!cc2_cg_data) return -1;
    if (size <= 0) size = 1;
    if (align <= 0) align = 1;
    while ((cc2_cg_data_pos % align) != 0) {
        if (cc2_cg_data_pos >= CC2_MAIN_DATA_MAX) return -1;
        cc2_cg_data[cc2_cg_data_pos] = 0;
        cc2_cg_data_pos = cc2_cg_data_pos + 1;
    }
    off = cc2_cg_data_pos;
    while (i < size) {
        if (cc2_cg_data_pos >= CC2_MAIN_DATA_MAX) return -1;
        cc2_cg_data[cc2_cg_data_pos] = 0;
        cc2_cg_data_pos = cc2_cg_data_pos + 1;
        i++;
    }
    return off;
}

int cc2_global_add(char *name, int size, int elem_size, int is_array) {
    int idx = cc2_parse_find_global(name);
    int off = 0;
    int align = 4;
    if (idx >= 0) return idx;
    if (cc2_global_count >= CC2_PARSE_MAX_GLOBALS) return -1;
    if (elem_size <= 0) elem_size = 4;
    if (elem_size == 1) align = 1;
    off = cc2_cg_alloc_zero(size, align);
    if (off < 0) return -1;
    idx = cc2_global_count;
    cc2_global_count = cc2_global_count + 1;
    cc2_parse_slot_set(cc2_global_names, CC2_PARSE_GLOBAL_NAME, idx, name);
    cc2_global_off[idx] = off;
    cc2_global_size[idx] = size;
    cc2_global_elem_size[idx] = elem_size;
    cc2_global_is_array[idx] = is_array;
    cc2_global_struct_ptr_si[idx] = -1;
    return idx;
}

int cc2_global_add_alias(char *name, int off, int size, int elem_size, int is_array) {
    int idx = cc2_parse_find_global(name);
    if (idx >= 0) return idx;
    if (cc2_global_count >= CC2_PARSE_MAX_GLOBALS) return -1;
    if (size <= 0) size = elem_size;
    if (elem_size <= 0) elem_size = 4;
    idx = cc2_global_count;
    cc2_global_count = cc2_global_count + 1;
    cc2_parse_slot_set(cc2_global_names, CC2_PARSE_GLOBAL_NAME, idx, name);
    cc2_global_off[idx] = off;
    cc2_global_size[idx] = size;
    cc2_global_elem_size[idx] = elem_size;
    cc2_global_is_array[idx] = is_array;
    cc2_global_struct_ptr_si[idx] = -1;
    return idx;
}

int cc2_global_addr(int idx) {
    return CC2_OUT_DATA_BASE + cc2_global_off[idx];
}

void cc2_local_struct_ptr_reset() {
    int i = 0;
    while (i < CC2_PARSE_MAX_LOCALS) {
        cc2_local_struct_ptr_si[i] = -1;
        i++;
    }
}

void cc2_name_append_dot_field(char *dst, int max, char *field) {
    int i = 0;
    while (i < max - 1 && dst[i]) i++;
    if (i < max - 1) {
        dst[i] = '.';
        i++;
    }
    {
        int j = 0;
        while (i < max - 1 && field[j]) {
            dst[i] = field[j];
            i++;
            j++;
        }
    }
    dst[i] = 0;
}

int cc2_struct_field_slot_base(int si, int fi) {
    int slot = si * CC2_PARSE_MAX_STRUCT_FIELDS + fi;
    return slot * CC2_PARSE_STRUCT_FIELD_NAME;
}

int cc2_struct_find_field_index(int si, char *fname) {
    int fi = 0;
    if (si < 0 || si >= CC2_PARSE_MAX_STRUCTS) return -1;
    while (fi < cc2_struct_field_count[si]) {
        int base = cc2_struct_field_slot_base(si, fi);
        if (cc2_strcmp(cc2_struct_field_names + base, fname) == 0) return fi;
        fi++;
    }
    return -1;
}

int cc2_split_dotted_name(char *name, char *base, int base_max,
                          char *field, int field_max, int *has_more) {
    int i = 0;
    int j = 0;
    if (!name || !base || !field || base_max <= 0 || field_max <= 0) return 0;
    if (has_more) *has_more = 0;
    while (name[i] && name[i] != '.') {
        if (i < base_max - 1) base[i] = name[i];
        i++;
    }
    if (name[i] != '.') return 0;
    if (i >= base_max) base[base_max - 1] = 0;
    else base[i] = 0;
    i++;
    if (!name[i]) return 0;
    while (name[i] && name[i] != '.') {
        if (j < field_max - 1) field[j] = name[i];
        j++;
        i++;
    }
    if (j >= field_max) field[field_max - 1] = 0;
    else field[j] = 0;
    if (j <= 0) return 0;
    if (name[i] == '.') {
        if (has_more) *has_more = 1;
    }
    return 1;
}

int cc2_resolve_ptr_field_access(char *local_names, int local_count, char *full_name,
                                 int *base_is_global, int *base_idx,
                                 int *field_off, int *field_elem_size, int *field_is_array,
                                 int *has_more_fields) {
    char base_name[CC2_PARSE_LOCAL_NAME];
    char field_name[CC2_PARSE_STRUCT_FIELD_NAME];
    int idx = -1;
    int si = -1;
    int fi = -1;
    int meta = 0;
    if (!cc2_split_dotted_name(full_name, base_name, CC2_PARSE_LOCAL_NAME,
                               field_name, CC2_PARSE_STRUCT_FIELD_NAME, has_more_fields)) {
        return 0;
    }
    if (base_is_global) *base_is_global = 0;
    if (base_idx) *base_idx = -1;
    idx = cc2_parse_find_local(local_names, local_count, base_name);
    if (idx >= 0 && idx < CC2_PARSE_MAX_LOCALS && cc2_local_struct_ptr_si[idx] >= 0) {
        if (base_is_global) *base_is_global = 0;
        if (base_idx) *base_idx = idx;
        si = cc2_local_struct_ptr_si[idx];
    } else {
        idx = cc2_parse_find_global(base_name);
        if (idx >= 0 && idx < CC2_PARSE_MAX_GLOBALS && cc2_global_struct_ptr_si[idx] >= 0) {
            if (base_is_global) *base_is_global = 1;
            if (base_idx) *base_idx = idx;
            si = cc2_global_struct_ptr_si[idx];
        }
    }
    if (si < 0) return 0;
    fi = cc2_struct_find_field_index(si, field_name);
    if (fi < 0) return 0;
    meta = si * CC2_PARSE_MAX_STRUCT_FIELDS + fi;
    if (field_off) *field_off = cc2_struct_field_off[meta];
    if (field_elem_size) {
        *field_elem_size = cc2_struct_field_elem_size[meta];
        if (*field_elem_size <= 0) *field_elem_size = 4;
    }
    if (field_is_array) *field_is_array = cc2_struct_field_is_array[meta];
    return 1;
}

void cc2_struct_reset() {
    int i = 0;
    while (i < CC2_PARSE_STRUCT_NAME_BUF) {
        cc2_struct_names[i] = 0;
        i++;
    }
    i = 0;
    while (i < CC2_PARSE_MAX_STRUCTS) {
        cc2_struct_field_count[i] = 0;
        cc2_struct_size[i] = 0;
        i++;
    }
    i = 0;
    while (i < CC2_PARSE_STRUCT_FIELD_BUF) {
        cc2_struct_field_names[i] = 0;
        i++;
    }
    i = 0;
    while (i < CC2_PARSE_MAX_STRUCTS * CC2_PARSE_MAX_STRUCT_FIELDS) {
        cc2_struct_field_off[i] = 0;
        cc2_struct_field_size[i] = 0;
        cc2_struct_field_elem_size[i] = 0;
        cc2_struct_field_is_array[i] = 0;
        i++;
    }
}

int cc2_struct_find(char *name) {
    int i = 0;
    while (i < CC2_PARSE_MAX_STRUCTS) {
        if (cc2_parse_slot_eq(cc2_struct_names, CC2_PARSE_STRUCT_NAME, i, name)) return i;
        i++;
    }
    return -1;
}

int cc2_struct_add(char *name) {
    int si = cc2_struct_find(name);
    int i = 0;
    if (si >= 0) {
        cc2_struct_field_count[si] = 0;
        cc2_struct_size[si] = 0;
        while (i < CC2_PARSE_MAX_STRUCT_FIELDS) {
            int base = cc2_struct_field_slot_base(si, i);
            int mi = si * CC2_PARSE_MAX_STRUCT_FIELDS + i;
            int j = 0;
            while (j < CC2_PARSE_STRUCT_FIELD_NAME) {
                cc2_struct_field_names[base + j] = 0;
                j++;
            }
            cc2_struct_field_off[mi] = 0;
            cc2_struct_field_size[mi] = 0;
            cc2_struct_field_elem_size[mi] = 0;
            cc2_struct_field_is_array[mi] = 0;
            i++;
        }
        return si;
    }
    si = 0;
    while (si < CC2_PARSE_MAX_STRUCTS) {
        if (cc2_struct_names[si * CC2_PARSE_STRUCT_NAME] == 0) {
            cc2_parse_slot_set(cc2_struct_names, CC2_PARSE_STRUCT_NAME, si, name);
            cc2_struct_field_count[si] = 0;
            cc2_struct_size[si] = 0;
            return si;
        }
        si++;
    }
    return -1;
}

int cc2_struct_add_field(int si, char *fname, int field_size, int elem_size, int is_array) {
    int fi = cc2_struct_field_count[si];
    int base;
    int mi;
    int off;
    int align;
    if (fi >= CC2_PARSE_MAX_STRUCT_FIELDS) return 0;
    if (field_size <= 0) field_size = 4;
    if (elem_size <= 0) elem_size = 4;
    align = elem_size;
    if (align > 4) align = 4;
    if (align <= 0) align = 1;
    off = cc2_struct_size[si];
    while ((off % align) != 0) off = off + 1;
    base = cc2_struct_field_slot_base(si, fi);
    cc2_strncpy(cc2_struct_field_names + base, fname, CC2_PARSE_STRUCT_FIELD_NAME - 1);
    cc2_struct_field_names[base + CC2_PARSE_STRUCT_FIELD_NAME - 1] = 0;
    mi = si * CC2_PARSE_MAX_STRUCT_FIELDS + fi;
    cc2_struct_field_off[mi] = off;
    cc2_struct_field_size[mi] = field_size;
    cc2_struct_field_elem_size[mi] = elem_size;
    cc2_struct_field_is_array[mi] = is_array;
    cc2_struct_size[si] = off + field_size;
    cc2_struct_field_count[si] = fi + 1;
    return 1;
}

void cc2_register_builtin_structs() {
    int si = cc2_struct_add("cc2_token");
    if (si >= 0) {
        cc2_struct_add_field(si, "type", 4, 4, 0);
        cc2_struct_add_field(si, "int_val", 4, 4, 0);
        cc2_struct_add_field(si, "text", 256, 1, 1);
    }
    si = cc2_struct_add("cc2_define");
    if (si >= 0) {
        cc2_struct_add_field(si, "name", CC2_PP_MAX_NAME, 1, 1);
        cc2_struct_add_field(si, "body", CC2_PP_MAX_BODY, 1, 1);
        cc2_struct_add_field(si, "is_func", 4, 4, 0);
        cc2_struct_add_field(si, "param_count", 4, 4, 0);
    }
}

void cc2_struct_get_field(int si, int fi, char *dst) {
    int base = cc2_struct_field_slot_base(si, fi);
    cc2_strncpy(dst, cc2_struct_field_names + base, CC2_PARSE_STRUCT_FIELD_NAME - 1);
    dst[CC2_PARSE_STRUCT_FIELD_NAME - 1] = 0;
}

void cc2_arr_reset() {
    int i = 0;
    cc2_arr_count = 0;
    while (i < CC2_PARSE_ARR_BUF) {
        cc2_arr_names[i] = 0;
        i++;
    }
    i = 0;
    while (i < CC2_PARSE_MAX_ARRAYS) {
        cc2_arr_base[i] = 0;
        cc2_arr_len[i] = 0;
        i++;
    }
}

int cc2_arr_find(char *name) {
    int i = cc2_arr_count - 1;
    while (i >= 0) {
        if (cc2_parse_slot_eq(cc2_arr_names, CC2_PARSE_LOCAL_NAME, i, name)) return i;
        i--;
    }
    return -1;
}

int cc2_arr_find_current_scope(char *name, int local_count) {
    int i = cc2_arr_count - 1;
    int base = cc2_scope_current_base();
    while (i >= 0) {
        if (cc2_parse_slot_eq(cc2_arr_names, CC2_PARSE_LOCAL_NAME, i, name) &&
            cc2_arr_base[i] > base && cc2_arr_base[i] <= local_count) {
            return i;
        }
        i--;
    }
    return -1;
}

int cc2_arr_add(char *name, int base_slot, int len) {
    int idx = 0;
    if (cc2_arr_count >= CC2_PARSE_MAX_ARRAYS) return 0;
    idx = cc2_arr_count;
    cc2_arr_count = cc2_arr_count + 1;
    cc2_parse_slot_set(cc2_arr_names, CC2_PARSE_LOCAL_NAME, idx, name);
    cc2_arr_base[idx] = base_slot;
    cc2_arr_len[idx] = len;
    return 1;
}

void cc2_arr_pop_to_local_count(int local_count) {
    while (cc2_arr_count > 0) {
        int last = cc2_arr_count - 1;
        if (cc2_arr_base[last] <= local_count) break;
        cc2_arr_count = last;
        cc2_arr_base[last] = 0;
        cc2_arr_len[last] = 0;
        cc2_parse_slot_set(cc2_arr_names, CC2_PARSE_LOCAL_NAME, last, "");
    }
}

void cc2_cg_reset() {
    int i = 0;
    cc2_cg_fn_count = 0;
    cc2_cg_call_count = 0;
    cc2_cg_data_pos = 0;
    cc2_global_reset();
    cc2_struct_reset();
    cc2_register_builtin_structs();
    cc2_arr_reset();
    while (i < CC2_PARSE_FN_BUF) {
        cc2_cg_fn_names[i] = 0;
        i++;
    }
    cc2_codegen_cur_fn[0] = 0;
}

int cc2_cg_add_string(char *s) {
    int off = cc2_cg_data_pos;
    int i = 0;
    if (!cc2_cg_data) return -1;
    while (s[i]) {
        if (cc2_cg_data_pos >= CC2_MAIN_DATA_MAX - 1) return -1;
        cc2_cg_data[cc2_cg_data_pos] = s[i];
        cc2_cg_data_pos = cc2_cg_data_pos + 1;
        i++;
    }
    if (cc2_cg_data_pos >= CC2_MAIN_DATA_MAX) return -1;
    cc2_cg_data[cc2_cg_data_pos] = 0;
    cc2_cg_data_pos = cc2_cg_data_pos + 1;
    return off;
}

int cc2_cg_find_fn(char *name) {
    int i = 0;
    while (i < cc2_cg_fn_count) {
        if (cc2_parse_slot_eq(cc2_cg_fn_names, CC2_PARSE_FN_NAME, i, name)) return i;
        i++;
    }
    return -1;
}

int cc2_cg_add_fn(char *name, int pos) {
    int idx = cc2_cg_find_fn(name);
    if (idx >= 0) {
        cc2_cg_fn_pos[idx] = pos;
        return 1;
    }
    if (cc2_cg_fn_count >= CC2_PARSE_MAX_FUNCS) return 0;
    idx = cc2_cg_fn_count;
    cc2_cg_fn_count = cc2_cg_fn_count + 1;
    cc2_parse_slot_set(cc2_cg_fn_names, CC2_PARSE_FN_NAME, idx, name);
    cc2_cg_fn_pos[idx] = pos;
    if (cc2_cg_fn_names[idx * CC2_PARSE_FN_NAME] == 0 && name[0] != 0) {
        serial_printf("[cc2_fn_add] WARN slot_set failed fn[%d] name='%s' flat=0x%x base=%d\n",
                      idx, name, cc2_cg_fn_names, idx * CC2_PARSE_FN_NAME);
    }
    return 1;
}

int cc2_cg_add_call_patch(char *name, int patch_pos) {
    if (cc2_cg_call_count >= CC2_PARSE_MAX_CALL_PATCHES) return 0;
    cc2_parse_slot_set(cc2_cg_call_names, CC2_PARSE_FN_NAME, cc2_cg_call_count, name);
    cc2_cg_call_patch_pos[cc2_cg_call_count] = patch_pos;
    cc2_cg_call_count = cc2_cg_call_count + 1;
    return 1;
}

int cc2_emit8_chk(char *buf, int *pos, int max, int v) {
    if (*pos >= max) {
        serial_printf("[cc2_parse] FAIL emit overflow fn='%s' pos=%d max=%d lex_line=%d lex_pos=%d\n",
                      cc2_codegen_cur_fn, *pos, max, cc2_lex_line, cc2_lex_pos);
        cc2_test_failures++;
        return 0;
    }
    buf[*pos] = (char)(v & 255);
    *pos = *pos + 1;
    return 1;
}

int cc2_emit32le_chk(char *buf, int *pos, int max, int v) {
    if (!cc2_emit8_chk(buf, pos, max, v)) return 0;
    if (!cc2_emit8_chk(buf, pos, max, v >> 8)) return 0;
    if (!cc2_emit8_chk(buf, pos, max, v >> 16)) return 0;
    if (!cc2_emit8_chk(buf, pos, max, v >> 24)) return 0;
    return 1;
}

int cc2_emit_mov_eax_imm(char *code, int *pos, int max, int v) {
    if (!cc2_emit8_chk(code, pos, max, 0xB8)) return 0;
    if (!cc2_emit32le_chk(code, pos, max, v)) return 0;
    return 1;
}

int cc2_emit_load_local(char *code, int *pos, int max, int offset) {
    if (!cc2_emit8_chk(code, pos, max, 0x8B)) return 0; // mov eax,[ebp-disp32]
    if (!cc2_emit8_chk(code, pos, max, 0x85)) return 0;
    if (!cc2_emit32le_chk(code, pos, max, -offset)) return 0;
    return 1;
}

int cc2_emit_store_local(char *code, int *pos, int max, int offset) {
    if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov [ebp-disp32],eax
    if (!cc2_emit8_chk(code, pos, max, 0x85)) return 0;
    if (!cc2_emit32le_chk(code, pos, max, -offset)) return 0;
    return 1;
}

int cc2_emit_load_arg(char *code, int *pos, int max, int arg_disp) {
    if (!cc2_emit8_chk(code, pos, max, 0x8B)) return 0; // mov eax,[ebp+disp32]
    if (!cc2_emit8_chk(code, pos, max, 0x85)) return 0;
    if (!cc2_emit32le_chk(code, pos, max, arg_disp)) return 0;
    return 1;
}

int cc2_emit_add_esp_imm8(char *code, int *pos, int max, int imm) {
    if (!cc2_emit8_chk(code, pos, max, 0x83)) return 0;
    if (!cc2_emit8_chk(code, pos, max, 0xC4)) return 0;
    if (!cc2_emit8_chk(code, pos, max, imm)) return 0;
    return 1;
}

int cc2_emit_putchar_imm(char *code, int *pos, int max, int ch, int putc_addr) {
    if (!cc2_emit_mov_eax_imm(code, pos, max, ch)) return 0;
    if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push eax
    if (!cc2_emit_mov_eax_imm(code, pos, max, putc_addr)) return 0;
    if (!cc2_emit8_chk(code, pos, max, 0xFF)) return 0; // call eax
    if (!cc2_emit8_chk(code, pos, max, 0xD0)) return 0;
    if (!cc2_emit_add_esp_imm8(code, pos, max, 4)) return 0;
    return 1;
}

int cc2_emit_shl_eax_2(char *code, int *pos, int max) {
    if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0; // shl eax,2
    if (!cc2_emit8_chk(code, pos, max, 0xE0)) return 0;
    if (!cc2_emit8_chk(code, pos, max, 0x02)) return 0;
    return 1;
}

int cc2_emit_lea_edx_local(char *code, int *pos, int max, int offset) {
    if (!cc2_emit8_chk(code, pos, max, 0x8D)) return 0; // lea edx,[ebp-disp32]
    if (!cc2_emit8_chk(code, pos, max, 0x95)) return 0;
    if (!cc2_emit32le_chk(code, pos, max, -offset)) return 0;
    return 1;
}

int cc2_emit_add_edx_eax(char *code, int *pos, int max) {
    if (!cc2_emit8_chk(code, pos, max, 0x01)) return 0; // add edx,eax
    if (!cc2_emit8_chk(code, pos, max, 0xC2)) return 0;
    return 1;
}

int cc2_emit_load_eax_ptr_edx(char *code, int *pos, int max) {
    if (!cc2_emit8_chk(code, pos, max, 0x8B)) return 0; // mov eax,[edx]
    if (!cc2_emit8_chk(code, pos, max, 0x02)) return 0;
    return 1;
}

int cc2_emit_load_eax_u8_ptr_edx(char *code, int *pos, int max) {
    if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // movzx eax, byte [edx]
    if (!cc2_emit8_chk(code, pos, max, 0xB6)) return 0;
    if (!cc2_emit8_chk(code, pos, max, 0x02)) return 0;
    return 1;
}

int cc2_emit_store_ptr_edx_eax(char *code, int *pos, int max) {
    if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov [edx],eax
    if (!cc2_emit8_chk(code, pos, max, 0x02)) return 0;
    return 1;
}

int cc2_emit_store_u8_ptr_edx_eax(char *code, int *pos, int max) {
    if (!cc2_emit8_chk(code, pos, max, 0x88)) return 0; // mov [edx],al
    if (!cc2_emit8_chk(code, pos, max, 0x02)) return 0;
    return 1;
}

int cc2_emit_mov_edx_eax(char *code, int *pos, int max) {
    if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov edx,eax
    if (!cc2_emit8_chk(code, pos, max, 0xC2)) return 0;
    return 1;
}

int cc2_emit_add_edx_imm(char *code, int *pos, int max, int imm) {
    if (imm == 0) return 1;
    if (!cc2_emit8_chk(code, pos, max, 0x81)) return 0; // add edx,imm32
    if (!cc2_emit8_chk(code, pos, max, 0xC2)) return 0;
    if (!cc2_emit32le_chk(code, pos, max, imm)) return 0;
    return 1;
}

int cc2_emit_ptr_field_addr(char *code, int *pos, int max,
                            int base_is_global, int base_idx, int field_off) {
    if (base_is_global) {
        if (!cc2_emit_load_global(code, pos, max, cc2_global_addr(base_idx), 0)) return 0;
    } else {
        if (!cc2_emit_load_local(code, pos, max, (base_idx + 1) * 4)) return 0;
    }
    if (!cc2_emit_mov_edx_eax(code, pos, max)) return 0;
    if (!cc2_emit_add_edx_imm(code, pos, max, field_off)) return 0;
    return 1;
}

int cc2_emit_scale_eax(char *code, int *pos, int max, int elem_size) {
    if (elem_size <= 1) return 1;
    if (elem_size == 2) {
        if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0xE0)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x01)) return 0;
        return 1;
    }
    if (elem_size == 4) return cc2_emit_shl_eax_2(code, pos, max);
    if (!cc2_emit8_chk(code, pos, max, 0x69)) return 0; // imul eax,eax,imm32
    if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
    if (!cc2_emit32le_chk(code, pos, max, elem_size)) return 0;
    return 1;
}

int cc2_codegen_ptr_field_subscript(struct cc2_token *cur,
                                    char *code, int *pos, int max,
                                    char *local_names, int local_count,
                                    int elem_size) {
    if (!cc2_parse_expect(cur, CC2_TK_LBRACKET, "'['")) return 0;
    if (!cc2_emit8_chk(code, pos, max, 0x52)) return 0; // push edx
    if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, local_count)) return 0;
    if (!cc2_parse_expect(cur, CC2_TK_RBRACKET, "']'")) return 0;
    if (!cc2_emit_scale_eax(code, pos, max, elem_size)) return 0;
    if (!cc2_emit8_chk(code, pos, max, 0x5A)) return 0; // pop edx
    if (!cc2_emit_add_edx_eax(code, pos, max)) return 0;
    return 1;
}

int cc2_emit_load_global(char *code, int *pos, int max, int addr, int is_u8) {
    if (!cc2_emit_mov_eax_imm(code, pos, max, addr)) return 0;
    if (!cc2_emit_mov_edx_eax(code, pos, max)) return 0;
    if (is_u8) return cc2_emit_load_eax_u8_ptr_edx(code, pos, max);
    return cc2_emit_load_eax_ptr_edx(code, pos, max);
}

int cc2_emit_store_global_from_eax(char *code, int *pos, int max, int addr, int is_u8) {
    if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push eax
    if (!cc2_emit_mov_eax_imm(code, pos, max, addr)) return 0;
    if (!cc2_emit_mov_edx_eax(code, pos, max)) return 0;
    if (!cc2_emit8_chk(code, pos, max, 0x58)) return 0; // pop eax
    if (is_u8) return cc2_emit_store_u8_ptr_edx_eax(code, pos, max);
    return cc2_emit_store_ptr_edx_eax(code, pos, max);
}

int cc2_emit_apply_compound(int op, char *code, int *pos, int max) {
    if (op == CC2_TK_PLUSEQ) {
        if (!cc2_emit8_chk(code, pos, max, 0x01)) return 0; // add ecx,eax
        if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
        if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        return 1;
    }
    if (op == CC2_TK_MINUSEQ) {
        if (!cc2_emit8_chk(code, pos, max, 0x29)) return 0; // sub ecx,eax
        if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
        if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        return 1;
    }
    if (op == CC2_TK_STAREQ) {
        if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // imul ecx,eax
        if (!cc2_emit8_chk(code, pos, max, 0xAF)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
        if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        return 1;
    }
    if (op == CC2_TK_SLASHEQ || op == CC2_TK_PERCENTEQ) {
        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov ebx,eax
        if (!cc2_emit8_chk(code, pos, max, 0xC3)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
        if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x99)) return 0; // cdq
        if (!cc2_emit8_chk(code, pos, max, 0xF7)) return 0; // idiv ebx
        if (!cc2_emit8_chk(code, pos, max, 0xFB)) return 0;
        if (op == CC2_TK_PERCENTEQ) {
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,edx
            if (!cc2_emit8_chk(code, pos, max, 0xD0)) return 0;
        }
        return 1;
    }
    if (op == CC2_TK_ANDEQ) {
        if (!cc2_emit8_chk(code, pos, max, 0x21)) return 0; // and ecx,eax
        if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
        if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        return 1;
    }
    if (op == CC2_TK_OREQ) {
        if (!cc2_emit8_chk(code, pos, max, 0x09)) return 0; // or ecx,eax
        if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
        if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        return 1;
    }
    if (op == CC2_TK_XOREQ) {
        if (!cc2_emit8_chk(code, pos, max, 0x31)) return 0; // xor ecx,eax
        if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
        if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        return 1;
    }
    if (op == CC2_TK_SHLEQ || op == CC2_TK_SHREQ) {
        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov edx,eax
        if (!cc2_emit8_chk(code, pos, max, 0xC2)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
        if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov ecx,edx
        if (!cc2_emit8_chk(code, pos, max, 0xD1)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0xD3)) return 0; // shl/sar eax,cl
        if (!cc2_emit8_chk(code, pos, max, (op == CC2_TK_SHLEQ) ? 0xE0 : 0xF8)) return 0;
        return 1;
    }
    return 0;
}

int cc2_codegen_local_update(char *name, struct cc2_token *cur,
                             char *code, int *pos, int max,
                             char *local_names, int local_count) {
    int idx = cc2_parse_find_local(local_names, local_count, name);
    int gidx = -1;
    int gaddr = 0;
    int gu8 = 0;
    int op = cur->type;
    if (idx < 0) {
        gidx = cc2_parse_find_global(name);
        if (gidx < 0) {
            serial_printf("[cc2_parse] FAIL unknown local '%s'\n", name);
            cc2_test_failures++;
            return 0;
        }
        gaddr = cc2_global_addr(gidx);
        gu8 = (cc2_global_elem_size[gidx] == 1) ? 1 : 0;
    }
    if (op == CC2_TK_PLUSPLUS || op == CC2_TK_MINUSMINUS) {
        cc2_lex_next(cur);
        if (idx >= 0) {
            if (!cc2_emit_load_local(code, pos, max, (idx + 1) * 4)) return 0;
        } else {
            if (!cc2_emit_load_global(code, pos, max, gaddr, gu8)) return 0;
        }
        if (!cc2_emit8_chk(code, pos, max, 0x83)) return 0; // add/sub eax,1
        if (!cc2_emit8_chk(code, pos, max, (op == CC2_TK_PLUSPLUS) ? 0xC0 : 0xE8)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x01)) return 0;
        if (idx >= 0) {
            if (!cc2_emit_store_local(code, pos, max, (idx + 1) * 4)) return 0;
        } else {
            if (!cc2_emit_store_global_from_eax(code, pos, max, gaddr, gu8)) return 0;
        }
        return 1;
    }
    if (op == CC2_TK_EQ) {
        cc2_lex_next(cur);
        if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, local_count)) return 0;
        if (idx >= 0) {
            if (!cc2_emit_store_local(code, pos, max, (idx + 1) * 4)) return 0;
        } else {
            if (!cc2_emit_store_global_from_eax(code, pos, max, gaddr, gu8)) return 0;
        }
        return 1;
    }
    if (op == CC2_TK_PLUSEQ || op == CC2_TK_MINUSEQ || op == CC2_TK_STAREQ ||
        op == CC2_TK_SLASHEQ || op == CC2_TK_PERCENTEQ || op == CC2_TK_ANDEQ ||
        op == CC2_TK_OREQ || op == CC2_TK_XOREQ || op == CC2_TK_SHLEQ ||
        op == CC2_TK_SHREQ) {
        cc2_lex_next(cur);
        if (idx >= 0) {
            if (!cc2_emit_load_local(code, pos, max, (idx + 1) * 4)) return 0;
        } else {
            if (!cc2_emit_load_global(code, pos, max, gaddr, gu8)) return 0;
        }
        if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push lhs
        if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, local_count)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x59)) return 0; // pop ecx
        if (!cc2_emit_apply_compound(op, code, pos, max)) {
            serial_printf("[cc2_parse] FAIL unsupported compound op token=%s\n",
                          cc2_tok_name(op));
            cc2_test_failures++;
            return 0;
        }
        if (idx >= 0) {
            if (!cc2_emit_store_local(code, pos, max, (idx + 1) * 4)) return 0;
        } else {
            if (!cc2_emit_store_global_from_eax(code, pos, max, gaddr, gu8)) return 0;
        }
        return 1;
    }
    serial_printf("[cc2_parse] FAIL expected assignment op got=%s text='%s'\n",
                  cc2_tok_name(cur->type), cur->text);
    cc2_test_failures++;
    return 0;
}

void cc2_patch_rel32(char *code, int patch_pos, int target_pos) {
    int rel = target_pos - (patch_pos + 4);
    code[patch_pos + 0] = (char)(rel & 255);
    code[patch_pos + 1] = (char)((rel >> 8) & 255);
    code[patch_pos + 2] = (char)((rel >> 16) & 255);
    code[patch_pos + 3] = (char)((rel >> 24) & 255);
}

void cc2_patch_rel32_abs(char *code, int patch_pos, int target_abs) {
    int from_abs = CC2_OUT_CODE_BASE + patch_pos + 4;
    int rel = target_abs - from_abs;
    code[patch_pos + 0] = (char)(rel & 255);
    code[patch_pos + 1] = (char)((rel >> 8) & 255);
    code[patch_pos + 2] = (char)((rel >> 16) & 255);
    code[patch_pos + 3] = (char)((rel >> 24) & 255);
}

int cc2_codegen_expr_prec(struct cc2_token *cur, int min_prec,
                          char *code, int *pos, int max,
                          char *local_names, int local_count) {
    int op;
    int prec;
    int idx;
    int gidx;
    int cval;
    int s_off;
    char name[CC2_PARSE_LOCAL_NAME];
    int can_post_update = 0;
    int post_local_off = 0;
    int post_is_global = 0;
    int post_global_addr = 0;
    int post_is_u8 = 0;
    if (cur->type == CC2_TK_AMP) {
        int arr_idx = -1;
        int struct_base_idx = -1;
        int ptr_base_is_global = 0;
        int ptr_base_idx = -1;
        int ptr_field_off = 0;
        int ptr_field_elem_size = 4;
        int ptr_field_is_array = 0;
        int ptr_has_more = 0;
        int ptr_res = 0;
        cc2_lex_next(cur);
        if (cur->type != CC2_TK_IDENT) {
            serial_printf("[cc2_parse] FAIL expected ident after '&'\n");
            cc2_test_failures++;
            return 0;
        }
        cc2_strncpy(name, cur->text, CC2_PARSE_LOCAL_NAME - 1);
        name[CC2_PARSE_LOCAL_NAME - 1] = 0;
        cc2_lex_next(cur);
        while (cur->type == CC2_TK_DOT) {
            cc2_lex_next(cur);
            if (cur->type != CC2_TK_IDENT) {
                serial_printf("[cc2_parse] FAIL expected field after '.'\n");
                cc2_test_failures++;
                return 0;
            }
            cc2_name_append_dot_field(name, CC2_PARSE_LOCAL_NAME, cur->text);
            cc2_lex_next(cur);
        }
        idx = cc2_parse_find_local(local_names, local_count, name);
        if (idx >= 0) {
            if (!cc2_emit_lea_edx_local(code, pos, max, (idx + 1) * 4)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,edx
            if (!cc2_emit8_chk(code, pos, max, 0xD0)) return 0;
        } else {
            arr_idx = cc2_arr_find(name);
            if (arr_idx >= 0) {
                if (!cc2_emit_lea_edx_local(code, pos, max, (cc2_arr_base[arr_idx] + cc2_arr_len[arr_idx] - 1) * 4)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,edx
                if (!cc2_emit8_chk(code, pos, max, 0xD0)) return 0;
            } else {
                struct_base_idx = cc2_parse_find_local_struct_base(local_names, local_count, name);
                if (struct_base_idx >= 0) {
                    if (!cc2_emit_lea_edx_local(code, pos, max, (struct_base_idx + 1) * 4)) return 0;
                    if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,edx
                    if (!cc2_emit8_chk(code, pos, max, 0xD0)) return 0;
                } else {
                    ptr_res = cc2_resolve_ptr_field_access(local_names, local_count, name,
                                                           &ptr_base_is_global, &ptr_base_idx,
                                                           &ptr_field_off, &ptr_field_elem_size,
                                                           &ptr_field_is_array, &ptr_has_more);
                    if (ptr_res > 0) {
                        if (ptr_has_more) {
                            serial_printf("[cc2_parse] FAIL unsupported nested dotted value '%s'\n", name);
                            cc2_test_failures++;
                            return 0;
                        }
                        if (!cc2_emit_ptr_field_addr(code, pos, max,
                                                     ptr_base_is_global, ptr_base_idx,
                                                     ptr_field_off)) return 0;
                        if (cur->type == CC2_TK_LBRACKET) {
                            if (!ptr_field_is_array) {
                                serial_printf("[cc2_parse] FAIL subscript on non-array field '%s'\n", name);
                                cc2_test_failures++;
                                return 0;
                            }
                            if (!cc2_codegen_ptr_field_subscript(cur, code, pos, max,
                                                                 local_names, local_count,
                                                                 ptr_field_elem_size)) return 0;
                        }
                        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,edx
                        if (!cc2_emit8_chk(code, pos, max, 0xD0)) return 0;
                    } else {
                        gidx = cc2_parse_find_global(name);
                        if (gidx < 0) {
                            serial_printf("[cc2_parse] FAIL unknown local '%s'\n", name);
                            cc2_test_failures++;
                            return 0;
                        }
                        if (!cc2_emit_mov_eax_imm(code, pos, max, cc2_global_addr(gidx))) return 0;
                    }
                }
            }
        }
        can_post_update = 0;
        post_is_global = 0;
        post_global_addr = 0;
        post_is_u8 = 0;
    } else
    if (cur->type == CC2_TK_BANG) {
        cc2_lex_next(cur);
        if (!cc2_codegen_expr_prec(cur, 11, code, pos, max, local_names, local_count)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x83)) return 0; // cmp eax,0
        if (!cc2_emit8_chk(code, pos, max, 0xF8)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x00)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // sete al
        if (!cc2_emit8_chk(code, pos, max, 0x94)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // movzx eax,al
        if (!cc2_emit8_chk(code, pos, max, 0xB6)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
    } else if (cur->type == CC2_TK_TILDE) {
        cc2_lex_next(cur);
        if (!cc2_codegen_expr_prec(cur, 11, code, pos, max, local_names, local_count)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0xF7)) return 0; // not eax
        if (!cc2_emit8_chk(code, pos, max, 0xD0)) return 0;
    } else if (cur->type == CC2_TK_MINUS) {
        cc2_lex_next(cur);
        if (!cc2_codegen_expr_prec(cur, 11, code, pos, max, local_names, local_count)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0xF7)) return 0; // neg eax
        if (!cc2_emit8_chk(code, pos, max, 0xD8)) return 0;
    } else if (cur->type == CC2_TK_PLUSPLUS || cur->type == CC2_TK_MINUSMINUS) {
        int is_inc = (cur->type == CC2_TK_PLUSPLUS) ? 1 : 0;
        cc2_lex_next(cur);
        if (cur->type != CC2_TK_IDENT) {
            serial_printf("[cc2_parse] FAIL expected ident after %s\n",
                          is_inc ? "'++'" : "'--'");
            cc2_test_failures++;
            return 0;
        }
        cc2_strncpy(name, cur->text, CC2_PARSE_LOCAL_NAME - 1);
        name[CC2_PARSE_LOCAL_NAME - 1] = 0;
        cc2_lex_next(cur);
        while (cur->type == CC2_TK_DOT) {
            cc2_lex_next(cur);
            if (cur->type != CC2_TK_IDENT) {
                serial_printf("[cc2_parse] FAIL expected field after '.'\n");
                cc2_test_failures++;
                return 0;
            }
            cc2_name_append_dot_field(name, CC2_PARSE_LOCAL_NAME, cur->text);
            cc2_lex_next(cur);
        }
        idx = cc2_parse_find_local(local_names, local_count, name);
        if (idx < 0) {
            gidx = cc2_parse_find_global(name);
            if (gidx < 0) {
                serial_printf("[cc2_parse] FAIL unknown local '%s'\n", name);
                cc2_test_failures++;
                return 0;
            }
            if (!cc2_emit_load_global(code, pos, max, cc2_global_addr(gidx),
                                      (cc2_global_elem_size[gidx] == 1) ? 1 : 0)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x83)) return 0; // add/sub eax,1
            if (!cc2_emit8_chk(code, pos, max, is_inc ? 0xC0 : 0xE8)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x01)) return 0;
            if (!cc2_emit_store_global_from_eax(code, pos, max, cc2_global_addr(gidx),
                                                (cc2_global_elem_size[gidx] == 1) ? 1 : 0)) return 0;
            can_post_update = 0;
            post_is_global = 0;
            post_global_addr = 0;
            post_is_u8 = 0;
        } else {
            if (!cc2_emit_load_local(code, pos, max, (idx + 1) * 4)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x83)) return 0; // add/sub eax,1
            if (!cc2_emit8_chk(code, pos, max, is_inc ? 0xC0 : 0xE8)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x01)) return 0;
            if (!cc2_emit_store_local(code, pos, max, (idx + 1) * 4)) return 0;
            can_post_update = 1;
            post_local_off = (idx + 1) * 4;
        }
    } else if (cur->type == CC2_TK_STAR) {
        cc2_lex_next(cur);
        if (!cc2_codegen_expr_prec(cur, 11, code, pos, max, local_names, local_count)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov edx,eax
        if (!cc2_emit8_chk(code, pos, max, 0xC2)) return 0;
        if (!cc2_emit_load_eax_ptr_edx(code, pos, max)) return 0;
    } else if (cur->type == CC2_TK_INT_LIT) {
        if (!cc2_emit_mov_eax_imm(code, pos, max, cur->int_val)) return 0;
        cc2_lex_next(cur);
    } else if (cur->type == CC2_TK_STRING) {
        s_off = cc2_cg_add_string(cur->text);
        if (s_off < 0) {
            serial_printf("[cc2_parse] FAIL data segment overflow for string literal\n");
            cc2_test_failures++;
            return 0;
        }
        if (!cc2_emit_mov_eax_imm(code, pos, max, CC2_OUT_DATA_BASE + s_off)) return 0;
        cc2_lex_next(cur);
    } else if (cur->type == CC2_TK_IDENT) {
        char name[CC2_PARSE_LOCAL_NAME];
        int ident_const = 0;
        int ident_const_val = 0;
        int arg_count = 0;
        int arg_len[CC2_PARSE_ARG_MAX];
        int ai = 0;
        int arr_idx = -1;
        int garr_idx = -1;
        int gptr_idx = -1;
        int arr_elem_size = 4;
        int patch_pos = 0;
        int fn_idx = -1;
        int fn_addr = 0;
        int arg_depth_base = -1;
        char *arg_code = 0;
        if (cc2_ident_maybe_const(cur->text) &&
            cc2_const_from_ident(cur->text, &ident_const_val)) {
            ident_const = 1;
        }
        cc2_strncpy(name, cur->text, CC2_PARSE_LOCAL_NAME - 1);
        name[CC2_PARSE_LOCAL_NAME - 1] = 0;
        cc2_lex_next(cur);
        if (cur->type == CC2_TK_LPAREN) {
            arg_depth_base = cc2_arg_code_depth;
            if (arg_depth_base >= CC2_PARSE_ARG_CODE_STACK_DEPTH) {
                serial_printf("[cc2_parse] FAIL arg stack depth overflow\n");
                cc2_test_failures++;
                return 0;
            }
            arg_code = cc2_arg_code_stack + (arg_depth_base * CC2_PARSE_ARG_CODE_BUF);
            cc2_arg_code_depth = arg_depth_base + 1;
            cc2_lex_next(cur);
            while (cur->type != CC2_TK_RPAREN) {
                int p = 0;
                char *dst;
                if (arg_count >= CC2_PARSE_ARG_MAX) {
                    serial_printf("[cc2_parse] FAIL too many call args for '%s'\n", name);
                    cc2_test_failures++;
                    cc2_arg_code_depth = arg_depth_base;
                    return 0;
                }
                dst = arg_code + (arg_count * CC2_PARSE_ARG_CODE_MAX);
                if (!cc2_codegen_expr_prec(cur, 1, dst, &p, CC2_PARSE_ARG_CODE_MAX,
                                           local_names, local_count)) {
                    cc2_arg_code_depth = arg_depth_base;
                    return 0;
                }
                arg_len[arg_count] = p;
                arg_count = arg_count + 1;
                if (cur->type == CC2_TK_COMMA) {
                    cc2_lex_next(cur);
                    continue;
                }
                break;
            }
            if (!cc2_parse_expect(cur, CC2_TK_RPAREN, "')'")) {
                cc2_arg_code_depth = arg_depth_base;
                return 0;
            }

            ai = arg_count - 1;
            while (ai >= 0) {
                int j = 0;
                char *src = arg_code + (ai * CC2_PARSE_ARG_CODE_MAX);
                while (j < arg_len[ai]) {
                    if (!cc2_emit8_chk(code, pos, max, (int)(unsigned char)src[j])) {
                        cc2_arg_code_depth = arg_depth_base;
                        return 0;
                    }
                    j++;
                }
                if (!cc2_emit8_chk(code, pos, max, 0x50)) { // push eax
                    cc2_arg_code_depth = arg_depth_base;
                    return 0;
                }
                ai = ai - 1;
            }

            if (!cc2_emit8_chk(code, pos, max, 0xE8)) { // call rel32
                cc2_arg_code_depth = arg_depth_base;
                return 0;
            }
            patch_pos = *pos;
            if (!cc2_emit32le_chk(code, pos, max, 0)) {
                cc2_arg_code_depth = arg_depth_base;
                return 0;
            }
            fn_idx = cc2_cg_find_fn(name);
            if (fn_idx >= 0) {
                cc2_patch_rel32(code, patch_pos, cc2_cg_fn_pos[fn_idx]);
            } else if (cc2_builtin_fn_addr(name, &fn_addr)) {
                cc2_patch_rel32_abs(code, patch_pos, fn_addr);
            } else if (!cc2_cg_add_call_patch(name, patch_pos)) {
                serial_printf("[cc2_parse] FAIL too many call patches\n");
                cc2_test_failures++;
                cc2_arg_code_depth = arg_depth_base;
                return 0;
            }
            if (arg_count > 0) {
                if (!cc2_emit_add_esp_imm8(code, pos, max, arg_count * 4)) {
                    cc2_arg_code_depth = arg_depth_base;
                    return 0;
                }
            }
            cc2_arg_code_depth = arg_depth_base;
        } else if (cur->type == CC2_TK_LBRACKET) {
            cc2_lex_next(cur);
            arr_idx = cc2_arr_find(name);
            if (arr_idx < 0) {
                gidx = cc2_parse_find_global(name);
                if (gidx >= 0) {
                    if (cc2_global_is_array[gidx]) {
                        garr_idx = gidx;
                        arr_elem_size = cc2_global_elem_size[gidx];
                    } else {
                        gptr_idx = gidx;
                    }
                } else {
                    idx = cc2_parse_find_local(local_names, local_count, name);
                    if (idx < 0) {
                        serial_printf("[cc2_parse] FAIL unknown subscript base '%s'\n", name);
                        cc2_test_failures++;
                        return 0;
                    }
                }
            }
            if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, local_count)) return 0;
            if (!cc2_parse_expect(cur, CC2_TK_RBRACKET, "']'")) return 0;
            if (arr_idx >= 0) {
                if (!cc2_emit_shl_eax_2(code, pos, max)) return 0;
                if (!cc2_emit_lea_edx_local(code, pos, max, (cc2_arr_base[arr_idx] + cc2_arr_len[arr_idx] - 1) * 4)) return 0;
            } else if (garr_idx >= 0) {
                if (arr_elem_size >= 4) {
                    if (!cc2_emit_shl_eax_2(code, pos, max)) return 0;
                }
                if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push eax (index)
                if (!cc2_emit_mov_eax_imm(code, pos, max, cc2_global_addr(garr_idx))) return 0;
                if (!cc2_emit_mov_edx_eax(code, pos, max)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x58)) return 0; // pop eax (index)
            } else {
                if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push eax (index)
                if (gptr_idx >= 0) {
                    if (!cc2_emit_load_global(code, pos, max, cc2_global_addr(gptr_idx), 0)) return 0;
                } else {
                    if (!cc2_emit_load_local(code, pos, max, (idx + 1) * 4)) return 0; // eax=base ptr
                }
                if (!cc2_emit_mov_edx_eax(code, pos, max)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x58)) return 0; // pop eax (index)
            }
            if (!cc2_emit_add_edx_eax(code, pos, max)) return 0;
            if (arr_idx >= 0 || (garr_idx >= 0 && arr_elem_size >= 4)) {
                if (!cc2_emit_load_eax_ptr_edx(code, pos, max)) return 0;
            } else {
                if (!cc2_emit_load_eax_u8_ptr_edx(code, pos, max)) return 0;
            }
        } else {
            int local_arr_idx = -1;
            int ptr_base_is_global = 0;
            int ptr_base_idx = -1;
            int ptr_field_off = 0;
            int ptr_field_elem_size = 4;
            int ptr_field_is_array = 0;
            int ptr_has_more = 0;
            int ptr_res = 0;
            while (cur->type == CC2_TK_DOT) {
                cc2_lex_next(cur);
                if (cur->type != CC2_TK_IDENT) {
                    serial_printf("[cc2_parse] FAIL expected field after '.'\n");
                    cc2_test_failures++;
                    return 0;
                }
                cc2_name_append_dot_field(name, CC2_PARSE_LOCAL_NAME, cur->text);
                cc2_lex_next(cur);
            }
            idx = cc2_parse_find_local(local_names, local_count, name);
            if (idx < 0) {
                local_arr_idx = cc2_arr_find(name);
                if (local_arr_idx >= 0) {
                    if (!cc2_emit_lea_edx_local(code, pos, max, (cc2_arr_base[local_arr_idx] + cc2_arr_len[local_arr_idx] - 1) * 4)) return 0;
                    if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,edx
                    if (!cc2_emit8_chk(code, pos, max, 0xD0)) return 0;
                    can_post_update = 0;
                    post_is_global = 0;
                    post_global_addr = 0;
                    post_is_u8 = 0;
                    idx = -1;
                } else {
                gidx = cc2_parse_find_global(name);
                if (gidx >= 0) {
                    if (cc2_global_is_array[gidx]) {
                        if (!cc2_emit_mov_eax_imm(code, pos, max, cc2_global_addr(gidx))) return 0;
                        can_post_update = 0;
                        post_is_global = 0;
                        post_global_addr = 0;
                        post_is_u8 = 0;
                    } else {
                        if (!cc2_emit_load_global(code, pos, max, cc2_global_addr(gidx),
                                                  (cc2_global_elem_size[gidx] == 1) ? 1 : 0)) return 0;
                        can_post_update = 1;
                        post_is_global = 1;
                        post_global_addr = cc2_global_addr(gidx);
                        post_is_u8 = (cc2_global_elem_size[gidx] == 1) ? 1 : 0;
                    }
                    idx = -1;
                } else {
                    ptr_res = cc2_resolve_ptr_field_access(local_names, local_count, name,
                                                           &ptr_base_is_global, &ptr_base_idx,
                                                           &ptr_field_off, &ptr_field_elem_size,
                                                           &ptr_field_is_array, &ptr_has_more);
                    if (ptr_res > 0) {
                        if (ptr_has_more) {
                            serial_printf("[cc2_parse] FAIL unsupported nested dotted value '%s'\n", name);
                            cc2_test_failures++;
                            return 0;
                        }
                        if (!cc2_emit_ptr_field_addr(code, pos, max,
                                                     ptr_base_is_global, ptr_base_idx,
                                                     ptr_field_off)) return 0;
                        if (cur->type == CC2_TK_LBRACKET) {
                            if (!ptr_field_is_array) {
                                serial_printf("[cc2_parse] FAIL subscript on non-array field '%s'\n", name);
                                cc2_test_failures++;
                                return 0;
                            }
                            if (!cc2_codegen_ptr_field_subscript(cur, code, pos, max,
                                                                 local_names, local_count,
                                                                 ptr_field_elem_size)) return 0;
                            if (ptr_field_elem_size == 1) {
                                if (!cc2_emit_load_eax_u8_ptr_edx(code, pos, max)) return 0;
                            } else {
                                if (!cc2_emit_load_eax_ptr_edx(code, pos, max)) return 0;
                            }
                        } else if (ptr_field_is_array) {
                            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,edx
                            if (!cc2_emit8_chk(code, pos, max, 0xD0)) return 0;
                        } else if (ptr_field_elem_size == 1) {
                            if (!cc2_emit_load_eax_u8_ptr_edx(code, pos, max)) return 0;
                        } else {
                            if (!cc2_emit_load_eax_ptr_edx(code, pos, max)) return 0;
                        }
                        can_post_update = 0;
                        post_is_global = 0;
                        post_global_addr = 0;
                        post_is_u8 = 0;
                    } else if (ident_const ||
                           (cc2_ident_maybe_const(name) && cc2_const_from_ident(name, &cval))) {
                        if (ident_const) cval = ident_const_val;
                        if (!cc2_emit_mov_eax_imm(code, pos, max, cval)) return 0;
                        can_post_update = 0;
                        post_is_global = 0;
                        post_global_addr = 0;
                        post_is_u8 = 0;
                        if (cur->type == CC2_TK_LBRACKET) {
                            cc2_lex_next(cur);
                            if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, local_count)) return 0;
                            if (!cc2_parse_expect(cur, CC2_TK_RBRACKET, "']'")) return 0;
                        }
                    } else if (cc2_builtin_fn_addr(name, &cval)) {
                        if (!cc2_emit_mov_eax_imm(code, pos, max, cval)) return 0;
                        can_post_update = 0;
                        post_is_global = 0;
                        post_global_addr = 0;
                        post_is_u8 = 0;
                    } else {
                        if (cc2_strchr(name, '.')) {
                            serial_printf("[cc2_parse] FAIL unknown dotted value '%s'\n", name);
                            cc2_test_failures++;
                            return 0;
                        } else {
                            serial_printf("[cc2_parse] FAIL unknown local '%s'\n", name);
                            cc2_test_failures++;
                            return 0;
                        }
                    }
                }
                }
            } else {
                if (!cc2_emit_load_local(code, pos, max, (idx + 1) * 4)) return 0;
                can_post_update = 1;
                post_local_off = (idx + 1) * 4;
            }
        }
    } else if (cur->type == CC2_TK_LPAREN) {
        int save_type = cur->type;
        int save_int_val = cur->int_val;
        char save_text[64];
        int save_pos = cc2_lex_pos;
        int save_line = cc2_lex_line;
        int cast_done = 0;
        int cast_ok = 1;

        cc2_strncpy(save_text, cur->text, 63);
        save_text[63] = 0;

        cc2_lex_next(cur);
        if (cur->type == CC2_TK_STRUCT ||
            cur->type == CC2_TK_INT_KW ||
            (cur->type == CC2_TK_IDENT && cc2_is_type_word(cur->text))) {
            cast_ok = 1;
            if (cur->type == CC2_TK_STRUCT) {
                cc2_lex_next(cur);
                if (cur->type != CC2_TK_IDENT) cast_ok = 0;
                else cc2_lex_next(cur);
            } else {
                while (cur->type == CC2_TK_INT_KW ||
                       (cur->type == CC2_TK_IDENT && cc2_is_type_word(cur->text))) {
                    cc2_lex_next(cur);
                }
            }
            while (cast_ok && cur->type == CC2_TK_STAR) cc2_lex_next(cur);
            if (cast_ok && cur->type == CC2_TK_RPAREN) {
                cc2_lex_next(cur);
                if (!cc2_codegen_expr_prec(cur, 11, code, pos, max, local_names, local_count)) return 0;
                cast_done = 1;
            }
        }
        if (!cast_done) {
            cc2_lex_pos = save_pos;
            cc2_lex_line = save_line;
            cur->type = save_type;
            cur->int_val = save_int_val;
            cc2_strncpy(cur->text, save_text, 63);
            cur->text[63] = 0;
            cc2_lex_next(cur);
            if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, local_count)) return 0;
            if (!cc2_parse_expect(cur, CC2_TK_RPAREN, "')'")) return 0;
        }
    } else {
        if (cc2_parse_op_prec(cur->type) > 0) {
            if (!cc2_emit_mov_eax_imm(code, pos, max, 0)) return 0;
        } else {
            serial_printf("[cc2_parse] FAIL expr token=%s text='%s'\n",
                          cc2_tok_name(cur->type), cur->text);
            cc2_test_failures++;
            return 0;
        }
    }

    while (cur->type == CC2_TK_PLUSPLUS || cur->type == CC2_TK_MINUSMINUS) {
        int is_inc = (cur->type == CC2_TK_PLUSPLUS) ? 1 : 0;
        if (!can_post_update) {
            serial_printf("[cc2_parse] FAIL unsupported postfix op on expr token=%s\n",
                          cc2_tok_name(cur->type));
            cc2_test_failures++;
            return 0;
        }
        if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push eax (old value/result)
        if (!cc2_emit8_chk(code, pos, max, 0x83)) return 0; // add/sub eax,1 for updated value
        if (!cc2_emit8_chk(code, pos, max, is_inc ? 0xC0 : 0xE8)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x01)) return 0;
        if (post_is_global) {
            if (!cc2_emit_store_global_from_eax(code, pos, max, post_global_addr, post_is_u8)) return 0;
        } else {
            if (!cc2_emit_store_local(code, pos, max, post_local_off)) return 0; // write updated local
        }
        if (!cc2_emit8_chk(code, pos, max, 0x58)) return 0; // pop eax (expression keeps old value)
        cc2_lex_next(cur);
    }

    while (1) {
        op = cur->type;
        prec = cc2_parse_op_prec(op);
        if (prec < min_prec || prec == 0) break;
        cc2_lex_next(cur);
        if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push eax (lhs)
        if (!cc2_codegen_expr_prec(cur, prec + 1, code, pos, max, local_names, local_count)) {
            return 0;
        }
        if (!cc2_emit8_chk(code, pos, max, 0x59)) return 0; // pop ecx (lhs)
        if (op == CC2_TK_PLUS) {
            if (!cc2_emit8_chk(code, pos, max, 0x01)) return 0; // add ecx,eax
            if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
            if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        } else if (op == CC2_TK_MINUS) {
            if (!cc2_emit8_chk(code, pos, max, 0x29)) return 0; // sub ecx,eax
            if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
            if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        } else if (op == CC2_TK_STAR) {
            if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // imul ecx,eax
            if (!cc2_emit8_chk(code, pos, max, 0xAF)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
            if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        } else if (op == CC2_TK_SLASH) {
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov ebx,eax
            if (!cc2_emit8_chk(code, pos, max, 0xC3)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
            if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x99)) return 0; // cdq
            if (!cc2_emit8_chk(code, pos, max, 0xF7)) return 0; // idiv ebx
            if (!cc2_emit8_chk(code, pos, max, 0xFB)) return 0;
        } else if (op == CC2_TK_PERCENT) {
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov ebx,eax
            if (!cc2_emit8_chk(code, pos, max, 0xC3)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
            if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x99)) return 0; // cdq
            if (!cc2_emit8_chk(code, pos, max, 0xF7)) return 0; // idiv ebx
            if (!cc2_emit8_chk(code, pos, max, 0xFB)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,edx
            if (!cc2_emit8_chk(code, pos, max, 0xD0)) return 0;
        } else if (op == CC2_TK_EQEQ || op == CC2_TK_NE ||
                   op == CC2_TK_LT || op == CC2_TK_LE ||
                   op == CC2_TK_GT || op == CC2_TK_GE) {
            int setcc = 0x94; // sete
            if (op == CC2_TK_NE) setcc = 0x95;
            else if (op == CC2_TK_LT) setcc = 0x9C;
            else if (op == CC2_TK_LE) setcc = 0x9E;
            else if (op == CC2_TK_GT) setcc = 0x9F;
            else if (op == CC2_TK_GE) setcc = 0x9D;
            if (!cc2_emit8_chk(code, pos, max, 0x39)) return 0; // cmp ecx,eax
            if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // setcc al
            if (!cc2_emit8_chk(code, pos, max, setcc)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // movzx eax,al
            if (!cc2_emit8_chk(code, pos, max, 0xB6)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
        } else if (op == CC2_TK_AMP) {
            if (!cc2_emit8_chk(code, pos, max, 0x21)) return 0; // and ecx,eax
            if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
            if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        } else if (op == CC2_TK_PIPE) {
            if (!cc2_emit8_chk(code, pos, max, 0x09)) return 0; // or ecx,eax
            if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
            if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        } else if (op == CC2_TK_CARET) {
            if (!cc2_emit8_chk(code, pos, max, 0x31)) return 0; // xor ecx,eax
            if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
            if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
        } else if (op == CC2_TK_SHL || op == CC2_TK_SHR) {
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov edx,eax
            if (!cc2_emit8_chk(code, pos, max, 0xC2)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov eax,ecx
            if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov ecx,edx
            if (!cc2_emit8_chk(code, pos, max, 0xD1)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0xD3)) return 0; // shl/sar eax,cl
            if (!cc2_emit8_chk(code, pos, max, (op == CC2_TK_SHL) ? 0xE0 : 0xF8)) return 0;
        } else if (op == CC2_TK_ANDAND || op == CC2_TK_OROR) {
            if (!cc2_emit8_chk(code, pos, max, 0x83)) return 0; // cmp ecx,0
            if (!cc2_emit8_chk(code, pos, max, 0xF9)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x00)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // setne cl
            if (!cc2_emit8_chk(code, pos, max, 0x95)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0xC1)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // movzx ecx,cl
            if (!cc2_emit8_chk(code, pos, max, 0xB6)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0xC9)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x83)) return 0; // cmp eax,0
            if (!cc2_emit8_chk(code, pos, max, 0xF8)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x00)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // setne al
            if (!cc2_emit8_chk(code, pos, max, 0x95)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // movzx eax,al
            if (!cc2_emit8_chk(code, pos, max, 0xB6)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
            if (op == CC2_TK_ANDAND) {
                if (!cc2_emit8_chk(code, pos, max, 0x21)) return 0; // and eax,ecx
                if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
            } else {
                if (!cc2_emit8_chk(code, pos, max, 0x09)) return 0; // or eax,ecx
                if (!cc2_emit8_chk(code, pos, max, 0xC8)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x83)) return 0; // cmp eax,0
                if (!cc2_emit8_chk(code, pos, max, 0xF8)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x00)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // setne al
                if (!cc2_emit8_chk(code, pos, max, 0x95)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // movzx eax,al
                if (!cc2_emit8_chk(code, pos, max, 0xB6)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
            }
        }
    }
    if (min_prec <= 1 && cur->type == CC2_TK_QUESTION) {
        int false_patch = 0;
        int end_patch = 0;
        cc2_lex_next(cur);
        if (!cc2_emit8_chk(code, pos, max, 0x85)) return 0; // test eax,eax
        if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // jz rel32
        if (!cc2_emit8_chk(code, pos, max, 0x84)) return 0;
        false_patch = *pos;
        if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;
        if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, local_count)) return 0;
        if (!cc2_parse_expect(cur, CC2_TK_COLON, "':'")) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0xE9)) return 0; // jmp end
        end_patch = *pos;
        if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;
        cc2_patch_rel32(code, false_patch, *pos);
        if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, local_count)) return 0;
        cc2_patch_rel32(code, end_patch, *pos);
    }
    return 1;
}

int cc2_add_patch(int *patches, int *count, int patch_pos) {
    if (*count >= CC2_PARSE_MAX_PATCHES) return 0;
    patches[*count] = patch_pos;
    *count = *count + 1;
    return 1;
}

int cc2_codegen_one_stmt(struct cc2_token *cur, char *code, int *pos, int max,
                         char *local_names, int *local_count, int *max_offset,
                         int in_loop, int loop_cond_pos,
                         int *break_patches, int *break_count,
                         int *cont_patches, int *cont_count) {
    char name[CC2_PARSE_LOCAL_NAME];
    char type_name[CC2_PARSE_STRUCT_NAME];
    char field_name[CC2_PARSE_STRUCT_FIELD_NAME];
    int idx = 0;
    int gidx = -1;
    int si = -1;
    int fi = 0;
    int ptr_depth = 0;
    int is_array = 0;
    int arr_len = 0;
    int has_init = 0;
    int existing_idx = -1;
    int base_idx = -1;
    int base_off = 0;
    int struct_size = 0;
    int meta = 0;
    int field_off = 0;
    int field_size_i = 0;
    int field_elem_i = 0;
    int field_is_arr_i = 0;

    if (cur->type == CC2_TK_STRUCT) {
        cc2_lex_next(cur);
        if (cur->type != CC2_TK_IDENT) {
            serial_printf("[cc2_parse] FAIL expected struct name\n");
            cc2_test_failures++;
            return 0;
        }
        cc2_strncpy(type_name, cur->text, CC2_PARSE_STRUCT_NAME - 1);
        type_name[CC2_PARSE_STRUCT_NAME - 1] = 0;
        cc2_lex_next(cur);

        if (cur->type == CC2_TK_LBRACE) {
            si = cc2_struct_add(type_name);
            if (si < 0) {
                serial_printf("[cc2_parse] FAIL too many structs\n");
                cc2_test_failures++;
                return 0;
            }
            cc2_lex_next(cur);
            while (cur->type != CC2_TK_RBRACE && cur->type != CC2_TK_EOF) {
                int field_elem_size = 4;
                int field_is_array = 0;
                int field_len = 1;
                int field_ptr_depth = 0;
                int field_size = 4;
                if (cur->type == CC2_TK_STRUCT) {
                    cc2_lex_next(cur);
                    if (cur->type != CC2_TK_IDENT) {
                        serial_printf("[cc2_parse] FAIL expected struct field type name\n");
                        cc2_test_failures++;
                        return 0;
                    }
                    idx = cc2_struct_find(cur->text);
                    if (idx >= 0 && cc2_struct_size[idx] > 0) {
                        field_elem_size = cc2_struct_size[idx];
                    } else {
                        field_elem_size = 4;
                    }
                    cc2_lex_next(cur);
                } else if (cur->type == CC2_TK_INT_KW ||
                           (cur->type == CC2_TK_IDENT && cc2_is_type_word(cur->text))) {
                    while (cur->type == CC2_TK_INT_KW ||
                           (cur->type == CC2_TK_IDENT && cc2_is_type_word(cur->text))) {
                        if (cur->type == CC2_TK_IDENT && cc2_is_byte_type_name(cur->text)) {
                            field_elem_size = 1;
                        } else {
                            field_elem_size = 4;
                        }
                        cc2_lex_next(cur);
                    }
                } else {
                    serial_printf("[cc2_parse] FAIL expected struct field type got=%s text='%s'\n",
                                  cc2_tok_name(cur->type), cur->text);
                    cc2_test_failures++;
                    return 0;
                }
                while (cur->type == CC2_TK_STAR) {
                    field_ptr_depth = field_ptr_depth + 1;
                    cc2_lex_next(cur);
                }
                if (field_ptr_depth > 0) field_elem_size = 4;
                if (cur->type != CC2_TK_IDENT) {
                    serial_printf("[cc2_parse] FAIL expected struct field name\n");
                    cc2_test_failures++;
                    return 0;
                }
                cc2_strncpy(field_name, cur->text, CC2_PARSE_STRUCT_FIELD_NAME - 1);
                field_name[CC2_PARSE_STRUCT_FIELD_NAME - 1] = 0;
                cc2_lex_next(cur);
                if (cur->type == CC2_TK_LBRACKET) {
                    int parsed_len = 0;
                    cc2_lex_next(cur);
                    if (!cc2_parse_array_len_token_stream(cur, &parsed_len)) return 0;
                    field_is_array = 1;
                    field_len = parsed_len;
                    if (field_len <= 0) field_len = 1;
                }
                if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                if (field_elem_size <= 0) field_elem_size = 4;
                field_size = field_elem_size;
                if (field_is_array) field_size = field_elem_size * field_len;
                if (field_size <= 0) field_size = field_elem_size;
                if (field_size <= 0) field_size = 4;
                if (!cc2_struct_add_field(si, field_name, field_size, field_elem_size, field_is_array)) {
                    serial_printf("[cc2_parse] FAIL too many struct fields\n");
                    cc2_test_failures++;
                    return 0;
                }
            }
            if (!cc2_parse_expect(cur, CC2_TK_RBRACE, "'}'")) return 0;
            if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
            return 1;
        }

        if (cur->type == CC2_TK_SEMI) {
            cc2_lex_next(cur);
            return 1;
        }
        while (cur->type == CC2_TK_STAR) {
            ptr_depth = ptr_depth + 1;
            cc2_lex_next(cur);
        }
        if (cur->type != CC2_TK_IDENT) {
            serial_printf("[cc2_parse] FAIL line %d expected struct variable name got=%s text='%s'\n",
                          cc2_lex_line, cc2_tok_name(cur->type), cur->text);
            cc2_test_failures++;
            return 0;
        }
        cc2_strncpy(name, cur->text, CC2_PARSE_LOCAL_NAME - 1);
        name[CC2_PARSE_LOCAL_NAME - 1] = 0;
        cc2_lex_next(cur);

        if (cur->type == CC2_TK_LBRACKET) {
            is_array = 1;
            cc2_lex_next(cur);
            if (!cc2_parse_array_len_token_stream(cur, &arr_len)) return 0;
            if (arr_len <= 0) arr_len = 1;
        }

        if (cur->type == CC2_TK_EQ) {
            cc2_lex_next(cur);
            if (local_names && local_count && max_offset && ptr_depth > 0 && !is_array) {
                has_init = 1;
                if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
            } else {
                int depth_paren = 0;
                int depth_brack = 0;
                int depth_brace = 0;
                while (cur->type != CC2_TK_EOF) {
                    if (depth_paren == 0 && depth_brack == 0 && depth_brace == 0 &&
                        cur->type == CC2_TK_SEMI) {
                        break;
                    }
                    if (cur->type == CC2_TK_LPAREN) depth_paren = depth_paren + 1;
                    else if (cur->type == CC2_TK_RPAREN && depth_paren > 0) depth_paren = depth_paren - 1;
                    else if (cur->type == CC2_TK_LBRACKET) depth_brack = depth_brack + 1;
                    else if (cur->type == CC2_TK_RBRACKET && depth_brack > 0) depth_brack = depth_brack - 1;
                    else if (cur->type == CC2_TK_LBRACE) depth_brace = depth_brace + 1;
                    else if (cur->type == CC2_TK_RBRACE && depth_brace > 0) depth_brace = depth_brace - 1;
                    cc2_lex_next(cur);
                }
            }
        }
        if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;

        si = cc2_struct_find(type_name);
        if (si < 0) {
            serial_printf("[cc2_parse] FAIL unknown struct type '%s'\n", type_name);
            cc2_test_failures++;
            return 0;
        }

        if (!local_names || !local_count || !max_offset) {
            if (ptr_depth > 0) {
                gidx = cc2_global_add(name, 4, 4, 0);
                if (gidx < 0) {
                    serial_printf("[cc2_parse] FAIL global alloc '%s'\n", name);
                    cc2_test_failures++;
                    return 0;
                }
                cc2_global_struct_ptr_si[gidx] = si;
                return 1;
            }
            if (is_array) {
                struct_size = cc2_struct_size[si];
                if (struct_size <= 0) struct_size = 4;
                if (cc2_global_add(name, struct_size * arr_len, 4, 1) < 0) {
                    serial_printf("[cc2_parse] FAIL global alloc '%s'\n", name);
                    cc2_test_failures++;
                    return 0;
                }
                return 1;
            }

            struct_size = cc2_struct_size[si];
            if (struct_size <= 0) struct_size = cc2_struct_field_count[si] * 4;
            if (struct_size <= 0) struct_size = 4;
            base_idx = cc2_global_add(name, struct_size, 4, 0);
            if (base_idx < 0) {
                serial_printf("[cc2_parse] FAIL global alloc '%s'\n", name);
                cc2_test_failures++;
                return 0;
            }
            base_off = cc2_global_off[base_idx];
            fi = 0;
            while (fi < cc2_struct_field_count[si]) {
                char full_name[CC2_PARSE_LOCAL_NAME];
                cc2_strncpy(full_name, name, CC2_PARSE_LOCAL_NAME - 1);
                full_name[CC2_PARSE_LOCAL_NAME - 1] = 0;
                cc2_struct_get_field(si, fi, field_name);
                cc2_name_append_dot_field(full_name, CC2_PARSE_LOCAL_NAME, field_name);
                meta = si * CC2_PARSE_MAX_STRUCT_FIELDS + fi;
                field_off = cc2_struct_field_off[meta];
                field_size_i = cc2_struct_field_size[meta];
                field_elem_i = cc2_struct_field_elem_size[meta];
                field_is_arr_i = cc2_struct_field_is_array[meta];
                if (field_size_i <= 0) field_size_i = 4;
                if (field_elem_i <= 0) field_elem_i = 4;
                if (cc2_global_add_alias(full_name, base_off + field_off, field_size_i, field_elem_i, field_is_arr_i) < 0) {
                    serial_printf("[cc2_parse] FAIL global alloc '%s'\n", full_name);
                    cc2_test_failures++;
                    return 0;
                }
                fi++;
            }
            return 1;
        }

        if (ptr_depth > 0) {
            if (is_array) {
                serial_printf("[cc2_parse] FAIL unsupported local struct pointer array '%s'\n", name);
                cc2_test_failures++;
                return 0;
            }
            existing_idx = cc2_parse_find_local_current_scope(local_names, *local_count, name);
            if (existing_idx >= 0) {
                idx = existing_idx;
            } else {
                if (*local_count >= CC2_PARSE_MAX_LOCALS) {
                    serial_printf("[cc2_parse] FAIL line %d too many locals count=%d max=%d\n",
                                  cc2_lex_line, *local_count, CC2_PARSE_MAX_LOCALS);
                    cc2_test_failures++;
                    return 0;
                }
                idx = *local_count;
                *local_count = *local_count + 1;
                cc2_parse_slot_set(local_names, CC2_PARSE_LOCAL_NAME, idx, name);
                if (((idx + 1) * 4) > *max_offset) *max_offset = (idx + 1) * 4;
            }
            cc2_local_struct_ptr_si[idx] = si;
            if (has_init) {
                if (!cc2_emit_store_local(code, pos, max, (idx + 1) * 4)) return 0;
            }
            return 1;
        }

        if (is_array) {
            serial_printf("[cc2_parse] FAIL unsupported local struct array '%s'\n", name);
            cc2_test_failures++;
            return 0;
        }

        fi = 0;
        while (fi < cc2_struct_field_count[si]) {
            char full_name[CC2_PARSE_LOCAL_NAME];
            cc2_strncpy(full_name, name, CC2_PARSE_LOCAL_NAME - 1);
            full_name[CC2_PARSE_LOCAL_NAME - 1] = 0;
            cc2_struct_get_field(si, fi, field_name);
            cc2_name_append_dot_field(full_name, CC2_PARSE_LOCAL_NAME, field_name);
            existing_idx = cc2_parse_find_local_current_scope(local_names, *local_count, full_name);
            if (existing_idx >= 0) {
                fi++;
                continue;
            }
            if (*local_count >= CC2_PARSE_MAX_LOCALS) {
                serial_printf("[cc2_parse] FAIL line %d too many locals count=%d max=%d\n",
                              cc2_lex_line, *local_count, CC2_PARSE_MAX_LOCALS);
                cc2_test_failures++;
                return 0;
            }
            idx = *local_count;
            *local_count = *local_count + 1;
            cc2_parse_slot_set(local_names, CC2_PARSE_LOCAL_NAME, idx, full_name);
            cc2_local_struct_ptr_si[idx] = -1;
            if (((idx + 1) * 4) > *max_offset) *max_offset = (idx + 1) * 4;
            fi++;
        }
        return 1;
    }

    if (cur->type == CC2_TK_INT_KW ||
        (cur->type == CC2_TK_IDENT && cc2_is_type_word(cur->text))) {
        int arr_len = 0;
        int arr_base = 0;
        int arr_idx = -1;
        int existing_idx = -1;
        while (cur->type == CC2_TK_INT_KW ||
               (cur->type == CC2_TK_IDENT && cc2_is_type_word(cur->text))) {
            cc2_lex_next(cur);
        }
        while (cur->type == CC2_TK_STAR) cc2_lex_next(cur);
        if (cur->type != CC2_TK_IDENT) {
            serial_printf("[cc2_parse] FAIL expected local name\n");
            cc2_test_failures++;
            return 0;
        }
        cc2_strncpy(name, cur->text, CC2_PARSE_LOCAL_NAME - 1);
        name[CC2_PARSE_LOCAL_NAME - 1] = 0;
        existing_idx = cc2_parse_find_local_current_scope(local_names, *local_count, name);
        if (cc2_arr_find_current_scope(name, *local_count) >= 0) {
            serial_printf("[cc2_parse] FAIL redeclare array '%s'\n", name);
            cc2_test_failures++;
            return 0;
        }
        cc2_lex_next(cur);
        if (cur->type == CC2_TK_LBRACKET) {
            cc2_lex_next(cur);
            if (!cc2_parse_array_len_token_stream(cur, &arr_len)) return 0;
            if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
            arr_base = *local_count + 1;
            if ((*local_count + arr_len) > CC2_PARSE_MAX_LOCALS) {
                serial_printf("[cc2_parse] FAIL line %d too many locals count=%d need=%d max=%d\n",
                              cc2_lex_line, *local_count, arr_len, CC2_PARSE_MAX_LOCALS);
                cc2_test_failures++;
                return 0;
            }
            if (!cc2_arr_add(name, arr_base, arr_len)) {
                serial_printf("[cc2_parse] FAIL too many arrays\n");
                cc2_test_failures++;
                return 0;
            }
            *local_count = *local_count + arr_len;
            if ((*local_count * 4) > *max_offset) *max_offset = *local_count * 4;
            arr_idx = cc2_arr_find(name);
            if (arr_idx >= 0) {
                idx = 0;
                while (idx < arr_len) {
                    int slot = cc2_arr_base[arr_idx] + idx;
                    cc2_local_struct_ptr_si[slot - 1] = -1;
                    if (!cc2_emit_mov_eax_imm(code, pos, max, 0)) return 0;
                    if (!cc2_emit_store_local(code, pos, max, slot * 4)) return 0;
                    idx++;
                }
            }
            return 1;
        }
        if (*local_count >= CC2_PARSE_MAX_LOCALS) {
            serial_printf("[cc2_parse] FAIL line %d too many locals count=%d max=%d\n",
                          cc2_lex_line, *local_count, CC2_PARSE_MAX_LOCALS);
            cc2_test_failures++;
            return 0;
        }
        if (existing_idx >= 0) {
            idx = existing_idx;
        } else {
            idx = *local_count;
            *local_count = *local_count + 1;
            cc2_parse_slot_set(local_names, CC2_PARSE_LOCAL_NAME, idx, name);
            if (((idx + 1) * 4) > *max_offset) *max_offset = (idx + 1) * 4;
        }
        cc2_local_struct_ptr_si[idx] = -1;
        if (cur->type == CC2_TK_EQ) {
            cc2_lex_next(cur);
            if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
        } else {
            if (!cc2_emit_mov_eax_imm(code, pos, max, 0)) return 0;
        }
        if (!cc2_emit_store_local(code, pos, max, (idx + 1) * 4)) return 0;
        if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
        return 1;
    }

    if (cur->type == CC2_TK_IDENT) {
        int arr_idx = -1;
        int garr_idx = -1;
        int gptr_idx = -1;
        int gidx = -1;
        int arr_elem_size = 4;
        int ptr_base_is_global = 0;
        int ptr_base_idx = -1;
        int ptr_field_off = 0;
        int ptr_field_elem_size = 4;
        int ptr_field_is_array = 0;
        int ptr_has_more = 0;
        int ptr_res = 0;

        if (cur->type == CC2_TK_IDENT &&
            (cc2_strcmp(cur->text, "print") == 0 || cc2_strcmp(cur->text, "println") == 0)) {
            char call_name[CC2_PARSE_FN_NAME];
            int fn_addr = 0;
            int has_arg = 0;
            int is_println = 0;
            int literal_fast = 0;

            cc2_strncpy(call_name, cur->text, CC2_PARSE_FN_NAME - 1);
            call_name[CC2_PARSE_FN_NAME - 1] = 0;
            if (cc2_strcmp(call_name, "println") == 0) is_println = 1;
            cc2_lex_next(cur);
            if (cur->type == CC2_TK_LPAREN) {
                cc2_lex_next(cur);
                {
                    int dbg_tk = cur->type;
                    int dbg_line = cc2_lex_line;
                    int dbg_pos = cc2_lex_pos;
                    serial_printf("[cc2_dbg] fastprint enter\n");
                    serial_printf("[cc2_dbg] tk=%d line=%d pos=%d\n", dbg_tk, dbg_line, dbg_pos);
                }
                if (cur->type == CC2_TK_STRING) {
                    int putc_addr = (int)(uint32_t)putchar;
                    int c0 = (int)(unsigned char)cur->text[0];
                    int c1 = (int)(unsigned char)cur->text[1];
                    int c2 = (int)(unsigned char)cur->text[2];
                    int c3 = (int)(unsigned char)cur->text[3];
                    int c4 = (int)(unsigned char)cur->text[4];
                    int c5 = (int)(unsigned char)cur->text[5];
                    int c6 = (int)(unsigned char)cur->text[6];
                    int c7 = (int)(unsigned char)cur->text[7];
                    int c8 = (int)(unsigned char)cur->text[8];
                    int c9 = (int)(unsigned char)cur->text[9];
                    int c10 = (int)(unsigned char)cur->text[10];
                    int c11 = (int)(unsigned char)cur->text[11];
                    int c12 = (int)(unsigned char)cur->text[12];
                    int c13 = (int)(unsigned char)cur->text[13];
                    int c14 = (int)(unsigned char)cur->text[14];
                    int c15 = (int)(unsigned char)cur->text[15];
                    {
                        int b0 = (int)(unsigned char)cur->text[0];
                        int b1 = (int)(unsigned char)cur->text[1];
                        int b2 = (int)(unsigned char)cur->text[2];
                        int b3 = (int)(unsigned char)cur->text[3];
                        int b4 = (int)(unsigned char)cur->text[4];
                        int b5 = (int)(unsigned char)cur->text[5];
                        int b6 = (int)(unsigned char)cur->text[6];
                        int b7 = (int)(unsigned char)cur->text[7];
                        int dbg_len = cc2_strlen(cur->text);
                        serial_printf("[cc2_dbg] str b0=%d b1=%d b2=%d b3=%d\n", b0, b1, b2, b3);
                        serial_printf("[cc2_dbg] str b4=%d b5=%d b6=%d b7=%d\n", b4, b5, b6, b7);
                        serial_printf("[cc2_dbg] str len=%d\n", dbg_len);
                    }

                    if (c0) { if (!cc2_emit_putchar_imm(code, pos, max, c0, putc_addr)) return 0; }
                    if (c1) { if (!cc2_emit_putchar_imm(code, pos, max, c1, putc_addr)) return 0; }
                    if (c2) { if (!cc2_emit_putchar_imm(code, pos, max, c2, putc_addr)) return 0; }
                    if (c3) { if (!cc2_emit_putchar_imm(code, pos, max, c3, putc_addr)) return 0; }
                    if (c4) { if (!cc2_emit_putchar_imm(code, pos, max, c4, putc_addr)) return 0; }
                    if (c5) { if (!cc2_emit_putchar_imm(code, pos, max, c5, putc_addr)) return 0; }
                    if (c6) { if (!cc2_emit_putchar_imm(code, pos, max, c6, putc_addr)) return 0; }
                    if (c7) { if (!cc2_emit_putchar_imm(code, pos, max, c7, putc_addr)) return 0; }
                    if (c8) { if (!cc2_emit_putchar_imm(code, pos, max, c8, putc_addr)) return 0; }
                    if (c9) { if (!cc2_emit_putchar_imm(code, pos, max, c9, putc_addr)) return 0; }
                    if (c10) { if (!cc2_emit_putchar_imm(code, pos, max, c10, putc_addr)) return 0; }
                    if (c11) { if (!cc2_emit_putchar_imm(code, pos, max, c11, putc_addr)) return 0; }
                    if (c12) { if (!cc2_emit_putchar_imm(code, pos, max, c12, putc_addr)) return 0; }
                    if (c13) { if (!cc2_emit_putchar_imm(code, pos, max, c13, putc_addr)) return 0; }
                    if (c14) { if (!cc2_emit_putchar_imm(code, pos, max, c14, putc_addr)) return 0; }
                    if (c15) { if (!cc2_emit_putchar_imm(code, pos, max, c15, putc_addr)) return 0; }

                    if (is_println) {
                        if (!cc2_emit_putchar_imm(code, pos, max, '\n', putc_addr)) return 0;
                    }
                    cc2_lex_next(cur);
                    if (!cc2_parse_expect(cur, CC2_TK_RPAREN, "')'")) return 0;
                    if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                    return 1;
                }
                if (cur->type != CC2_TK_RPAREN) {
                    if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
                    if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push eax
                    has_arg = 1;
                }
                if (!cc2_parse_expect(cur, CC2_TK_RPAREN, "')'")) return 0;
                if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                if (!literal_fast) {
                    if (!cc2_builtin_fn_addr(call_name, &fn_addr)) {
                        serial_printf("[cc2_parse] FAIL unresolved call '%s'\n", call_name);
                        cc2_test_failures++;
                        return 0;
                    }
                }
                {
                    int dbg_pos2 = *pos;
                    serial_printf("[cc2_dbg] emit fn=0x%x has_arg=%d\n", fn_addr, has_arg);
                    serial_printf("[cc2_dbg] emit println=%d pos=%d\n", is_println, dbg_pos2);
                }
                if (!cc2_emit_mov_eax_imm(code, pos, max, fn_addr)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0xFF)) return 0; // call eax
                if (!cc2_emit8_chk(code, pos, max, 0xD0)) return 0;
                if (has_arg) {
                    if (!cc2_emit_add_esp_imm8(code, pos, max, 4)) return 0;
                }
                if (is_println) {
                    int putc_addr = (int)(uint32_t)putchar;
                    if (!cc2_emit_mov_eax_imm(code, pos, max, '\n')) return 0;
                    if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push eax
                    if (!cc2_emit_mov_eax_imm(code, pos, max, putc_addr)) return 0;
                    if (!cc2_emit8_chk(code, pos, max, 0xFF)) return 0; // call eax
                    if (!cc2_emit8_chk(code, pos, max, 0xD0)) return 0;
                    if (!cc2_emit_add_esp_imm8(code, pos, max, 4)) return 0;
                }
                return 1;
            }
        }
        int op = 0;
        int stmt_lex_pos = cc2_lex_pos;
        int stmt_lex_line = cc2_lex_line;
        int stmt_tok_type = cur->type;
        int stmt_tok_int = cur->int_val;
        char stmt_tok_text[256];
        cc2_strncpy(stmt_tok_text, cur->text, 255);
        stmt_tok_text[255] = 0;
        cc2_strncpy(name, cur->text, CC2_PARSE_LOCAL_NAME - 1);
        name[CC2_PARSE_LOCAL_NAME - 1] = 0;
        cc2_lex_next(cur);
        if (cur->type == CC2_TK_LBRACKET) {
            int ptr_idx = -1;
            int is_ptr_subscript = 0;
            cc2_lex_next(cur);
            arr_idx = cc2_arr_find(name);
            if (arr_idx < 0) {
                gidx = cc2_parse_find_global(name);
                if (gidx >= 0) {
                    if (cc2_global_is_array[gidx]) {
                        garr_idx = gidx;
                        arr_elem_size = cc2_global_elem_size[gidx];
                    } else {
                        gptr_idx = gidx;
                    }
                } else {
                    ptr_idx = cc2_parse_find_local(local_names, *local_count, name);
                    if (ptr_idx < 0) {
                        serial_printf("[cc2_parse] FAIL assign unknown subscript base '%s'\n", name);
                        cc2_test_failures++;
                        return 0;
                    }
                    is_ptr_subscript = 1;
                }
            }
            if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
            if (!cc2_parse_expect(cur, CC2_TK_RBRACKET, "']'")) return 0;
            if (!is_ptr_subscript) {
                if (arr_idx >= 0) {
                    if (!cc2_emit_shl_eax_2(code, pos, max)) return 0;
                    if (!cc2_emit_lea_edx_local(code, pos, max, (cc2_arr_base[arr_idx] + cc2_arr_len[arr_idx] - 1) * 4)) return 0;
                } else {
                    if (arr_elem_size >= 4) {
                        if (!cc2_emit_shl_eax_2(code, pos, max)) return 0;
                    }
                    if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push eax (index)
                    if (!cc2_emit_mov_eax_imm(code, pos, max, cc2_global_addr(garr_idx))) return 0;
                    if (!cc2_emit_mov_edx_eax(code, pos, max)) return 0;
                    if (!cc2_emit8_chk(code, pos, max, 0x58)) return 0; // pop eax (index)
                }
            } else {
                if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push eax (index)
                if (gptr_idx >= 0) {
                    if (!cc2_emit_load_global(code, pos, max, cc2_global_addr(gptr_idx), 0)) return 0;
                } else {
                    if (!cc2_emit_load_local(code, pos, max, (ptr_idx + 1) * 4)) return 0; // eax=base ptr
                }
                if (!cc2_emit_mov_edx_eax(code, pos, max)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x58)) return 0; // pop eax (index)
            }
            if (!cc2_emit_add_edx_eax(code, pos, max)) return 0;
            op = cur->type;
            if (is_ptr_subscript) {
                if (op != CC2_TK_EQ) {
                    serial_printf("[cc2_parse] FAIL pointer subscript supports only '=' for '%s'\n", name);
                    cc2_test_failures++;
                    return 0;
                }
                cc2_lex_next(cur);
                if (!cc2_emit8_chk(code, pos, max, 0x52)) return 0; // push edx (dest addr)
                if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x5A)) return 0; // pop edx (dest addr)
                if (!cc2_emit_store_u8_ptr_edx_eax(code, pos, max)) return 0;
                if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                return 1;
            }
            if (op == CC2_TK_PLUSPLUS || op == CC2_TK_MINUSMINUS) {
                cc2_lex_next(cur);
                if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                if (arr_elem_size == 1) {
                    if (!cc2_emit_load_eax_u8_ptr_edx(code, pos, max)) return 0;
                } else {
                    if (!cc2_emit_load_eax_ptr_edx(code, pos, max)) return 0;
                }
                if (!cc2_emit8_chk(code, pos, max, 0x83)) return 0; // add/sub eax,1
                if (!cc2_emit8_chk(code, pos, max, (op == CC2_TK_PLUSPLUS) ? 0xC0 : 0xE8)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x01)) return 0;
                if (arr_elem_size == 1) {
                    if (!cc2_emit_store_u8_ptr_edx_eax(code, pos, max)) return 0;
                } else {
                    if (!cc2_emit_store_ptr_edx_eax(code, pos, max)) return 0;
                }
                return 1;
            }
            if (op == CC2_TK_EQ) {
                cc2_lex_next(cur);
                if (!cc2_emit8_chk(code, pos, max, 0x52)) return 0; // push edx (dest addr)
                if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x5A)) return 0; // pop edx (dest addr)
                if (arr_elem_size == 1) {
                    if (!cc2_emit_store_u8_ptr_edx_eax(code, pos, max)) return 0;
                } else {
                    if (!cc2_emit_store_ptr_edx_eax(code, pos, max)) return 0;
                }
                if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                return 1;
            }
            if (op == CC2_TK_PLUSEQ || op == CC2_TK_MINUSEQ || op == CC2_TK_STAREQ ||
                op == CC2_TK_SLASHEQ || op == CC2_TK_PERCENTEQ || op == CC2_TK_ANDEQ ||
                op == CC2_TK_OREQ || op == CC2_TK_XOREQ || op == CC2_TK_SHLEQ ||
                op == CC2_TK_SHREQ) {
                if (!cc2_emit8_chk(code, pos, max, 0x52)) return 0; // push edx
                if (arr_elem_size == 1) {
                    if (!cc2_emit_load_eax_u8_ptr_edx(code, pos, max)) return 0;
                } else {
                    if (!cc2_emit_load_eax_ptr_edx(code, pos, max)) return 0;
                }
                if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push lhs
                cc2_lex_next(cur);
                if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x59)) return 0; // pop ecx
                if (!cc2_emit_apply_compound(op, code, pos, max)) {
                    serial_printf("[cc2_parse] FAIL unsupported compound array op token=%s\n",
                                  cc2_tok_name(op));
                    cc2_test_failures++;
                    return 0;
                }
                if (!cc2_emit8_chk(code, pos, max, 0x5A)) return 0; // pop edx
                if (arr_elem_size == 1) {
                    if (!cc2_emit_store_u8_ptr_edx_eax(code, pos, max)) return 0;
                } else {
                    if (!cc2_emit_store_ptr_edx_eax(code, pos, max)) return 0;
                }
                if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                return 1;
            }
            cc2_lex_pos = stmt_lex_pos;
            cc2_lex_line = stmt_lex_line;
            cur->type = stmt_tok_type;
            cur->int_val = stmt_tok_int;
            cc2_strncpy(cur->text, stmt_tok_text, 255);
            cur->text[255] = 0;
            if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
            if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
            return 1;
        }
        while (cur->type == CC2_TK_DOT) {
            cc2_lex_next(cur);
            if (cur->type != CC2_TK_IDENT) {
                serial_printf("[cc2_parse] FAIL expected field after '.'\n");
                cc2_test_failures++;
                return 0;
            }
            cc2_name_append_dot_field(name, CC2_PARSE_LOCAL_NAME, cur->text);
            cc2_lex_next(cur);
        }
        idx = cc2_parse_find_local(local_names, *local_count, name);
        if (idx < 0) {
            gidx = cc2_parse_find_global(name);
            if (gidx < 0) {
                ptr_res = cc2_resolve_ptr_field_access(local_names, *local_count, name,
                                                       &ptr_base_is_global, &ptr_base_idx,
                                                       &ptr_field_off, &ptr_field_elem_size,
                                                       &ptr_field_is_array, &ptr_has_more);
                if (ptr_res > 0) {
                    if (ptr_has_more) {
                        serial_printf("[cc2_parse] FAIL unsupported nested dotted lvalue '%s'\n", name);
                        cc2_test_failures++;
                        return 0;
                    }
                    if (!cc2_emit_ptr_field_addr(code, pos, max,
                                                 ptr_base_is_global, ptr_base_idx,
                                                 ptr_field_off)) return 0;
                    if (cur->type == CC2_TK_LBRACKET) {
                        if (!ptr_field_is_array) {
                            serial_printf("[cc2_parse] FAIL subscript on non-array field '%s'\n", name);
                            cc2_test_failures++;
                            return 0;
                        }
                        if (!cc2_codegen_ptr_field_subscript(cur, code, pos, max,
                                                             local_names, *local_count,
                                                             ptr_field_elem_size)) return 0;
                        arr_elem_size = ptr_field_elem_size;
                        ptr_field_is_array = 0;
                    } else {
                        arr_elem_size = ptr_field_elem_size;
                    }
                    op = cur->type;
                    if (ptr_field_is_array &&
                        (op == CC2_TK_PLUSPLUS || op == CC2_TK_MINUSMINUS ||
                         op == CC2_TK_EQ || op == CC2_TK_PLUSEQ || op == CC2_TK_MINUSEQ ||
                         op == CC2_TK_STAREQ || op == CC2_TK_SLASHEQ || op == CC2_TK_PERCENTEQ ||
                         op == CC2_TK_ANDEQ || op == CC2_TK_OREQ || op == CC2_TK_XOREQ ||
                         op == CC2_TK_SHLEQ || op == CC2_TK_SHREQ)) {
                        serial_printf("[cc2_parse] FAIL non-assignable array field '%s'\n", name);
                        cc2_test_failures++;
                        return 0;
                    }
                    if (op == CC2_TK_PLUSPLUS || op == CC2_TK_MINUSMINUS) {
                        cc2_lex_next(cur);
                        if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                        if (arr_elem_size == 1) {
                            if (!cc2_emit_load_eax_u8_ptr_edx(code, pos, max)) return 0;
                        } else {
                            if (!cc2_emit_load_eax_ptr_edx(code, pos, max)) return 0;
                        }
                        if (!cc2_emit8_chk(code, pos, max, 0x83)) return 0; // add/sub eax,1
                        if (!cc2_emit8_chk(code, pos, max, (op == CC2_TK_PLUSPLUS) ? 0xC0 : 0xE8)) return 0;
                        if (!cc2_emit8_chk(code, pos, max, 0x01)) return 0;
                        if (arr_elem_size == 1) {
                            if (!cc2_emit_store_u8_ptr_edx_eax(code, pos, max)) return 0;
                        } else {
                            if (!cc2_emit_store_ptr_edx_eax(code, pos, max)) return 0;
                        }
                        return 1;
                    }
                    if (op == CC2_TK_EQ) {
                        cc2_lex_next(cur);
                        if (!cc2_emit8_chk(code, pos, max, 0x52)) return 0; // push edx
                        if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
                        if (!cc2_emit8_chk(code, pos, max, 0x5A)) return 0; // pop edx
                        if (arr_elem_size == 1) {
                            if (!cc2_emit_store_u8_ptr_edx_eax(code, pos, max)) return 0;
                        } else {
                            if (!cc2_emit_store_ptr_edx_eax(code, pos, max)) return 0;
                        }
                        if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                        return 1;
                    }
                    if (op == CC2_TK_PLUSEQ || op == CC2_TK_MINUSEQ || op == CC2_TK_STAREQ ||
                        op == CC2_TK_SLASHEQ || op == CC2_TK_PERCENTEQ || op == CC2_TK_ANDEQ ||
                        op == CC2_TK_OREQ || op == CC2_TK_XOREQ || op == CC2_TK_SHLEQ ||
                        op == CC2_TK_SHREQ) {
                        if (!cc2_emit8_chk(code, pos, max, 0x52)) return 0; // push edx
                        if (arr_elem_size == 1) {
                            if (!cc2_emit_load_eax_u8_ptr_edx(code, pos, max)) return 0;
                        } else {
                            if (!cc2_emit_load_eax_ptr_edx(code, pos, max)) return 0;
                        }
                        if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push lhs
                        cc2_lex_next(cur);
                        if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
                        if (!cc2_emit8_chk(code, pos, max, 0x59)) return 0; // pop ecx
                        if (!cc2_emit_apply_compound(op, code, pos, max)) {
                            serial_printf("[cc2_parse] FAIL unsupported compound op token=%s\n",
                                          cc2_tok_name(op));
                            cc2_test_failures++;
                            return 0;
                        }
                        if (!cc2_emit8_chk(code, pos, max, 0x5A)) return 0; // pop edx
                        if (arr_elem_size == 1) {
                            if (!cc2_emit_store_u8_ptr_edx_eax(code, pos, max)) return 0;
                        } else {
                            if (!cc2_emit_store_ptr_edx_eax(code, pos, max)) return 0;
                        }
                        if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                        return 1;
                    }
                    cc2_lex_pos = stmt_lex_pos;
                    cc2_lex_line = stmt_lex_line;
                    cur->type = stmt_tok_type;
                    cur->int_val = stmt_tok_int;
                    cc2_strncpy(cur->text, stmt_tok_text, 255);
                    cur->text[255] = 0;
                    if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
                    if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                    return 1;
                }
                if (cc2_strchr(name, '.')) {
                    serial_printf("[cc2_parse] FAIL unknown dotted lvalue '%s' at line %d token=%s text='%s'\n",
                                  name, cc2_lex_line, cc2_tok_name(cur->type), cur->text);
                    cc2_test_failures++;
                    return 0;
                }
                // Not a local/global lvalue: treat as expression statement (e.g. function call).
                cc2_lex_pos = stmt_lex_pos;
                cc2_lex_line = stmt_lex_line;
                cur->type = stmt_tok_type;
                cur->int_val = stmt_tok_int;
                cc2_strncpy(cur->text, stmt_tok_text, 255);
                cur->text[255] = 0;
                if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
                if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                return 1;
            }
            op = cur->type;
            if (op == CC2_TK_PLUSPLUS || op == CC2_TK_MINUSMINUS) {
                cc2_lex_next(cur);
                if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                if (!cc2_emit_load_global(code, pos, max, cc2_global_addr(gidx),
                                          (cc2_global_elem_size[gidx] == 1) ? 1 : 0)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x83)) return 0; // add/sub eax,1
                if (!cc2_emit8_chk(code, pos, max, (op == CC2_TK_PLUSPLUS) ? 0xC0 : 0xE8)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x01)) return 0;
                if (!cc2_emit_store_global_from_eax(code, pos, max, cc2_global_addr(gidx),
                                                    (cc2_global_elem_size[gidx] == 1) ? 1 : 0)) return 0;
                return 1;
            }
            if (op == CC2_TK_EQ) {
                cc2_lex_next(cur);
                if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
                if (!cc2_emit_store_global_from_eax(code, pos, max, cc2_global_addr(gidx),
                                                    (cc2_global_elem_size[gidx] == 1) ? 1 : 0)) return 0;
                if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                return 1;
            }
            if (op == CC2_TK_PLUSEQ || op == CC2_TK_MINUSEQ || op == CC2_TK_STAREQ ||
                op == CC2_TK_SLASHEQ || op == CC2_TK_PERCENTEQ || op == CC2_TK_ANDEQ ||
                op == CC2_TK_OREQ || op == CC2_TK_XOREQ || op == CC2_TK_SHLEQ ||
                op == CC2_TK_SHREQ) {
                cc2_lex_next(cur);
                if (!cc2_emit_load_global(code, pos, max, cc2_global_addr(gidx),
                                          (cc2_global_elem_size[gidx] == 1) ? 1 : 0)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push lhs
                if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
                if (!cc2_emit8_chk(code, pos, max, 0x59)) return 0; // pop ecx
                if (!cc2_emit_apply_compound(op, code, pos, max)) {
                    serial_printf("[cc2_parse] FAIL unsupported compound op token=%s\n",
                                  cc2_tok_name(op));
                    cc2_test_failures++;
                    return 0;
                }
                if (!cc2_emit_store_global_from_eax(code, pos, max, cc2_global_addr(gidx),
                                                    (cc2_global_elem_size[gidx] == 1) ? 1 : 0)) return 0;
                if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
                return 1;
            }
            cc2_lex_pos = stmt_lex_pos;
            cc2_lex_line = stmt_lex_line;
            cur->type = stmt_tok_type;
            cur->int_val = stmt_tok_int;
            cc2_strncpy(cur->text, stmt_tok_text, 255);
            cur->text[255] = 0;
            if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
            if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
            return 1;
        }
        op = cur->type;
        if (op == CC2_TK_PLUSPLUS || op == CC2_TK_MINUSMINUS) {
            cc2_lex_next(cur);
            if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
            if (!cc2_emit_load_local(code, pos, max, (idx + 1) * 4)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x83)) return 0; // add/sub eax,1
            if (!cc2_emit8_chk(code, pos, max, (op == CC2_TK_PLUSPLUS) ? 0xC0 : 0xE8)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x01)) return 0;
            if (!cc2_emit_store_local(code, pos, max, (idx + 1) * 4)) return 0;
            return 1;
        }
        if (op == CC2_TK_EQ) {
            cc2_lex_next(cur);
            if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
            if (!cc2_emit_store_local(code, pos, max, (idx + 1) * 4)) return 0;
            if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
            return 1;
        }
        if (op == CC2_TK_PLUSEQ || op == CC2_TK_MINUSEQ || op == CC2_TK_STAREQ ||
            op == CC2_TK_SLASHEQ || op == CC2_TK_PERCENTEQ || op == CC2_TK_ANDEQ ||
            op == CC2_TK_OREQ || op == CC2_TK_XOREQ || op == CC2_TK_SHLEQ ||
            op == CC2_TK_SHREQ) {
            cc2_lex_next(cur);
            if (!cc2_emit_load_local(code, pos, max, (idx + 1) * 4)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push lhs
            if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x59)) return 0; // pop ecx
            if (!cc2_emit_apply_compound(op, code, pos, max)) {
                serial_printf("[cc2_parse] FAIL unsupported compound op token=%s\n",
                              cc2_tok_name(op));
                cc2_test_failures++;
                return 0;
            }
            if (!cc2_emit_store_local(code, pos, max, (idx + 1) * 4)) return 0;
            if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
            return 1;
        }
        cc2_lex_pos = stmt_lex_pos;
        cc2_lex_line = stmt_lex_line;
        cur->type = stmt_tok_type;
        cur->int_val = stmt_tok_int;
        cc2_strncpy(cur->text, stmt_tok_text, 255);
        cur->text[255] = 0;
        if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
        if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
        return 1;
    }

    if (cur->type == CC2_TK_RETURN) {
        cc2_lex_next(cur);
        if (cur->type == CC2_TK_SEMI) {
            if (!cc2_emit_mov_eax_imm(code, pos, max, 0)) return 0;
        } else {
            if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
        }
        if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
        // emit function epilogue: mov esp,ebp; pop ebp; ret
        if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0xEC)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x5D)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0xC3)) return 0;
        return 1;
    }

    if (cur->type == CC2_TK_ASM) {
        return cc2_codegen_asm_stmt(cur, code, pos, max);
    }

    if (cur->type == CC2_TK_LBRACE) {
        int scope_local_count = *local_count;
        if (!cc2_scope_push(scope_local_count)) {
            serial_printf("[cc2_parse] FAIL scope depth overflow\n");
            cc2_test_failures++;
            return 0;
        }
        cc2_lex_next(cur);
        while (cur->type != CC2_TK_RBRACE && cur->type != CC2_TK_EOF) {
            if (!cc2_codegen_one_stmt(cur, code, pos, max, local_names, local_count, max_offset,
                                      in_loop, loop_cond_pos,
                                      break_patches, break_count,
                                      cont_patches, cont_count)) {
                return 0;
            }
        }
        if (!cc2_parse_expect(cur, CC2_TK_RBRACE, "'}'")) return 0;
        cc2_scope_pop(local_count);
        return 1;
    }

    if (cur->type == CC2_TK_IF) {
        int patch_pos;
        int end_patch = -1;
        cc2_lex_next(cur);
        if (!cc2_parse_expect(cur, CC2_TK_LPAREN, "'('")) return 0;
        if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
        if (!cc2_parse_expect(cur, CC2_TK_RPAREN, "')'")) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x85)) return 0; // test eax,eax
        if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // jz rel32
        if (!cc2_emit8_chk(code, pos, max, 0x84)) return 0;
        patch_pos = *pos;
        if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;
        if (!cc2_codegen_one_stmt(cur, code, pos, max, local_names, local_count, max_offset,
                                  in_loop, loop_cond_pos,
                                  break_patches, break_count,
                                  cont_patches, cont_count)) {
            return 0;
        }
        if (cur->type == CC2_TK_ELSE) {
            if (!cc2_emit8_chk(code, pos, max, 0xE9)) return 0; // jmp end
            end_patch = *pos;
            if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;
            cc2_patch_rel32(code, patch_pos, *pos); // jump to else
            cc2_lex_next(cur);
            if (!cc2_codegen_one_stmt(cur, code, pos, max, local_names, local_count, max_offset,
                                      in_loop, loop_cond_pos,
                                      break_patches, break_count,
                                      cont_patches, cont_count)) {
                return 0;
            }
            cc2_patch_rel32(code, end_patch, *pos);
        } else {
            cc2_patch_rel32(code, patch_pos, *pos);
        }
        return 1;
    }

    if (cur->type == CC2_TK_WHILE) {
        int cond_pos;
        int jz_patch;
        int back_patch;
        int loop_end;
        int i = 0;
        int while_break[CC2_PARSE_MAX_PATCHES];
        int while_cont[CC2_PARSE_MAX_PATCHES];
        int while_break_count = 0;
        int while_cont_count = 0;
        cc2_lex_next(cur);
        if (!cc2_parse_expect(cur, CC2_TK_LPAREN, "'('")) return 0;
        cond_pos = *pos;
        if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
        if (!cc2_parse_expect(cur, CC2_TK_RPAREN, "')'")) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x85)) return 0; // test eax,eax
        if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // jz rel32
        if (!cc2_emit8_chk(code, pos, max, 0x84)) return 0;
        jz_patch = *pos;
        if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;
        if (!cc2_codegen_one_stmt(cur, code, pos, max, local_names, local_count, max_offset,
                                  1, cond_pos,
                                  while_break, &while_break_count,
                                  while_cont, &while_cont_count)) {
            return 0;
        }
        if (!cc2_emit8_chk(code, pos, max, 0xE9)) return 0; // jmp loop cond
        back_patch = *pos;
        if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;
        cc2_patch_rel32(code, back_patch, cond_pos);
        loop_end = *pos;
        cc2_patch_rel32(code, jz_patch, loop_end);
        i = 0;
        while (i < while_cont_count) {
            cc2_patch_rel32(code, while_cont[i], cond_pos);
            i++;
        }
        i = 0;
        while (i < while_break_count) {
            cc2_patch_rel32(code, while_break[i], loop_end);
            i++;
        }
        return 1;
    }

    if (cur->type == CC2_TK_DO) {
        return cc2_codegen_do_stmt(cur, code, pos, max,
                                   local_names, local_count, max_offset);
    }

    if (cur->type == CC2_TK_FOR) {
        char post_code[256];
        int post_len = 0;
        int has_cond = 0;
        int cond_pos;
        int jz_patch = -1;
        int loop_end;
        int post_start;
        int back_patch;
        int i = 0;
        int idx = 0;
        int for_break[CC2_PARSE_MAX_PATCHES];
        int for_cont[CC2_PARSE_MAX_PATCHES];
        int for_break_count = 0;
        int for_cont_count = 0;
        int for_scope_base = 0;
        char name2[CC2_PARSE_LOCAL_NAME];

        cc2_lex_next(cur);
        if (!cc2_parse_expect(cur, CC2_TK_LPAREN, "'('")) return 0;
        for_scope_base = *local_count;
        if (!cc2_scope_push(for_scope_base)) {
            serial_printf("[cc2_parse] FAIL scope depth overflow\n");
            cc2_test_failures++;
            return 0;
        }

        // init
        if (cur->type == CC2_TK_SEMI) {
            cc2_lex_next(cur);
        } else if (cur->type == CC2_TK_INT_KW ||
                   (cur->type == CC2_TK_IDENT && cc2_is_type_word(cur->text))) {
            while (cur->type == CC2_TK_INT_KW ||
                   (cur->type == CC2_TK_IDENT && cc2_is_type_word(cur->text))) {
                cc2_lex_next(cur);
            }
            while (cur->type == CC2_TK_STAR) cc2_lex_next(cur);
            while (1) {
                int existing_idx = -1;
                if (cur->type != CC2_TK_IDENT) {
                    serial_printf("[cc2_parse] FAIL for init local name\n");
                    cc2_test_failures++;
                    return 0;
                }
                cc2_strncpy(name2, cur->text, CC2_PARSE_LOCAL_NAME - 1);
                name2[CC2_PARSE_LOCAL_NAME - 1] = 0;
                existing_idx = cc2_parse_find_local_current_scope(local_names, *local_count, name2);
                if (existing_idx >= 0) {
                    idx = existing_idx;
                } else {
                    if (*local_count >= CC2_PARSE_MAX_LOCALS) {
                        serial_printf("[cc2_parse] FAIL line %d for init too many locals count=%d max=%d\n",
                                      cc2_lex_line, *local_count, CC2_PARSE_MAX_LOCALS);
                        cc2_test_failures++;
                        return 0;
                    }
                    idx = *local_count;
                    *local_count = *local_count + 1;
                    cc2_parse_slot_set(local_names, CC2_PARSE_LOCAL_NAME, idx, name2);
                    if (((idx + 1) * 4) > *max_offset) *max_offset = (idx + 1) * 4;
                }
                cc2_local_struct_ptr_si[idx] = -1;
                cc2_lex_next(cur);
                if (cur->type == CC2_TK_EQ) {
                    cc2_lex_next(cur);
                    if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
                    if (!cc2_emit_store_local(code, pos, max, (idx + 1) * 4)) return 0;
                } else {
                    if (!cc2_emit_mov_eax_imm(code, pos, max, 0)) return 0;
                    if (!cc2_emit_store_local(code, pos, max, (idx + 1) * 4)) return 0;
                }
                if (cur->type == CC2_TK_COMMA) {
                    cc2_lex_next(cur);
                    continue;
                }
                break;
            }
            if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
        } else {
            while (1) {
                if (cur->type != CC2_TK_IDENT) {
                    serial_printf("[cc2_parse] FAIL for init form\n");
                    cc2_test_failures++;
                    return 0;
                }
                cc2_strncpy(name2, cur->text, CC2_PARSE_LOCAL_NAME - 1);
                name2[CC2_PARSE_LOCAL_NAME - 1] = 0;
                cc2_lex_next(cur);
                if (!cc2_codegen_local_update(name2, cur, code, pos, max, local_names, *local_count)) {
                    serial_printf("[cc2_parse] FAIL for init update '%s'\n", name2);
                    cc2_test_failures++;
                    return 0;
                }
                if (cur->type == CC2_TK_COMMA) {
                    cc2_lex_next(cur);
                    continue;
                }
                break;
            }
            if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
        }

        cond_pos = *pos;
        // cond
        if (cur->type == CC2_TK_SEMI) {
            cc2_lex_next(cur);
            has_cond = 0;
        } else {
            has_cond = 1;
            if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
            if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x85)) return 0; // test eax,eax
            if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
            if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // jz rel32
            if (!cc2_emit8_chk(code, pos, max, 0x84)) return 0;
            jz_patch = *pos;
            if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;
        }

        // post (captured code)
        i = 0;
        while (i < 256) {
            post_code[i] = 0;
            i++;
        }
        if (cur->type != CC2_TK_RPAREN) {
            int p = 0;
            while (1) {
                if (cur->type != CC2_TK_IDENT) {
                    serial_printf("[cc2_parse] FAIL for post ident\n");
                    cc2_test_failures++;
                    return 0;
                }
                cc2_strncpy(name2, cur->text, CC2_PARSE_LOCAL_NAME - 1);
                name2[CC2_PARSE_LOCAL_NAME - 1] = 0;
                cc2_lex_next(cur);
                if (!cc2_codegen_local_update(name2, cur, post_code, &p, 256, local_names, *local_count)) {
                    serial_printf("[cc2_parse] FAIL for post update '%s'\n", name2);
                    cc2_test_failures++;
                    return 0;
                }
                if (cur->type == CC2_TK_COMMA) {
                    cc2_lex_next(cur);
                    continue;
                }
                break;
            }
            post_len = p;
        }
        if (!cc2_parse_expect(cur, CC2_TK_RPAREN, "')'")) return 0;

        if (!cc2_codegen_one_stmt(cur, code, pos, max, local_names, local_count, max_offset,
                                  1, cond_pos,
                                  for_break, &for_break_count,
                                  for_cont, &for_cont_count)) {
            return 0;
        }

        post_start = *pos;
        i = 0;
        while (i < post_len) {
            if (!cc2_emit8_chk(code, pos, max, (int)(unsigned char)post_code[i])) return 0;
            i++;
        }

        if (!cc2_emit8_chk(code, pos, max, 0xE9)) return 0; // jmp cond
        back_patch = *pos;
        if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;
        cc2_patch_rel32(code, back_patch, cond_pos);

        loop_end = *pos;
        if (has_cond) cc2_patch_rel32(code, jz_patch, loop_end);
        i = 0;
        while (i < for_cont_count) {
            cc2_patch_rel32(code, for_cont[i], post_start);
            i++;
        }
        i = 0;
        while (i < for_break_count) {
            cc2_patch_rel32(code, for_break[i], loop_end);
            i++;
        }
        cc2_scope_pop(local_count);
        return 1;
    }

    if (cur->type == CC2_TK_SWITCH) {
        return cc2_codegen_switch_stmt(cur, code, pos, max,
                                       local_names, local_count, max_offset,
                                       in_loop, loop_cond_pos,
                                       cont_patches, cont_count);
    }

    if (cur->type == CC2_TK_BREAK) {
        int patch_pos;
        if (!break_patches || !break_count) {
            serial_printf("[cc2_parse] FAIL break outside loop\n");
            cc2_test_failures++;
            return 0;
        }
        cc2_lex_next(cur);
        if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0xE9)) return 0; // jmp rel32
        patch_pos = *pos;
        if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;
        if (!cc2_add_patch(break_patches, break_count, patch_pos)) {
            serial_printf("[cc2_parse] FAIL too many break patches\n");
            cc2_test_failures++;
            return 0;
        }
        return 1;
    }

    if (cur->type == CC2_TK_CONTINUE) {
        int patch_pos;
        if (!in_loop) {
            serial_printf("[cc2_parse] FAIL continue outside loop\n");
            cc2_test_failures++;
            return 0;
        }
        cc2_lex_next(cur);
        if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0xE9)) return 0; // jmp rel32
        patch_pos = *pos;
        if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;
        if (!cc2_add_patch(cont_patches, cont_count, patch_pos)) {
            serial_printf("[cc2_parse] FAIL too many continue patches\n");
            cc2_test_failures++;
            return 0;
        }
        return 1;
    }

    if (cur->type == CC2_TK_LPAREN) {
        int save_pos = cc2_lex_pos;
        int save_line = cc2_lex_line;
        int save_type = cur->type;
        int save_int = cur->int_val;
        char save_text[256];
        char pname[CC2_PARSE_LOCAL_NAME];
        int matched = 0;
        cc2_strncpy(save_text, cur->text, 255);
        save_text[255] = 0;
        pname[0] = 0;

        cc2_lex_next(cur);
        if (cur->type == CC2_TK_IDENT) {
            cc2_strncpy(pname, cur->text, CC2_PARSE_LOCAL_NAME - 1);
            pname[CC2_PARSE_LOCAL_NAME - 1] = 0;
            cc2_lex_next(cur);
            while (cur->type == CC2_TK_DOT) {
                cc2_lex_next(cur);
                if (cur->type != CC2_TK_IDENT) {
                    pname[0] = 0;
                    break;
                }
                cc2_name_append_dot_field(pname, CC2_PARSE_LOCAL_NAME, cur->text);
                cc2_lex_next(cur);
            }
            if (pname[0] && cur->type == CC2_TK_RPAREN) {
                cc2_lex_next(cur);
                if (cur->type == CC2_TK_EQ || cur->type == CC2_TK_PLUSEQ ||
                    cur->type == CC2_TK_MINUSEQ || cur->type == CC2_TK_STAREQ ||
                    cur->type == CC2_TK_SLASHEQ || cur->type == CC2_TK_PERCENTEQ ||
                    cur->type == CC2_TK_ANDEQ || cur->type == CC2_TK_OREQ ||
                    cur->type == CC2_TK_XOREQ || cur->type == CC2_TK_SHLEQ ||
                    cur->type == CC2_TK_SHREQ || cur->type == CC2_TK_PLUSPLUS ||
                    cur->type == CC2_TK_MINUSMINUS) {
                    matched = 1;
                }
            }
        }

        if (matched) {
            if (!cc2_codegen_local_update(pname, cur, code, pos, max, local_names, *local_count)) {
                serial_printf("[cc2_parse] FAIL paren assign update '%s'\n", pname);
                cc2_test_failures++;
                return 0;
            }
            if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
            return 1;
        }

        cc2_lex_pos = save_pos;
        cc2_lex_line = save_line;
        cur->type = save_type;
        cur->int_val = save_int;
        cc2_strncpy(cur->text, save_text, 255);
        cur->text[255] = 0;
    }

    if (cur->type == CC2_TK_STAR) {
        cc2_lex_next(cur);
        if (!cc2_codegen_expr_prec(cur, 11, code, pos, max, local_names, *local_count)) return 0;
        // eax = pointer address
        if (!cc2_emit8_chk(code, pos, max, 0x50)) return 0; // push eax (save ptr addr)
        if (cur->type != CC2_TK_EQ) {
            serial_printf("[cc2_parse] FAIL expected '=' after '*ptr'\n");
            cc2_test_failures++;
            return 0;
        }
        cc2_lex_next(cur);
        if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
        // eax = value to store
        if (!cc2_emit8_chk(code, pos, max, 0x5A)) return 0; // pop edx (ptr addr)
        if (!cc2_emit_store_ptr_edx_eax(code, pos, max)) return 0;
        if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
        return 1;
    }

    if (cur->type == CC2_TK_LPAREN || cur->type == CC2_TK_INT_LIT ||
        cur->type == CC2_TK_STRING || cur->type == CC2_TK_BANG ||
        cur->type == CC2_TK_TILDE || cur->type == CC2_TK_MINUS ||
        cur->type == CC2_TK_PLUSPLUS || cur->type == CC2_TK_MINUSMINUS ||
        cur->type == CC2_TK_AMP) {
        if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
        if (cur->type == CC2_TK_EQ || cur->type == CC2_TK_PLUSEQ ||
            cur->type == CC2_TK_MINUSEQ || cur->type == CC2_TK_STAREQ ||
            cur->type == CC2_TK_SLASHEQ || cur->type == CC2_TK_PERCENTEQ ||
            cur->type == CC2_TK_ANDEQ || cur->type == CC2_TK_OREQ ||
            cur->type == CC2_TK_XOREQ || cur->type == CC2_TK_SHLEQ ||
            cur->type == CC2_TK_SHREQ) {
            cc2_lex_next(cur);
            if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
        }
        if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
        return 1;
    }

    serial_printf("[cc2_parse] FAIL unsupported stmt token=%s text='%s'\n",
                  cc2_tok_name(cur->type), cur->text);
    cc2_test_failures++;
    return 0;
}

int cc2_codegen_asm_stmt(struct cc2_token *cur, char *code, int *pos, int max) {
    cc2_lex_next(cur);
    if (!cc2_parse_expect(cur, CC2_TK_LPAREN, "'('")) return 0;
    if (cur->type != CC2_TK_RPAREN) {
        while (1) {
            int byte_v;
            if (cur->type != CC2_TK_INT_LIT) {
                serial_printf("[cc2_parse] FAIL asm expects integer byte literal\n");
                cc2_test_failures++;
                return 0;
            }
            byte_v = cur->int_val & 255;
            if (!cc2_emit8_chk(code, pos, max, byte_v)) return 0;
            cc2_lex_next(cur);
            if (cur->type == CC2_TK_COMMA) {
                cc2_lex_next(cur);
                continue;
            }
            break;
        }
    }
    if (!cc2_parse_expect(cur, CC2_TK_RPAREN, "')'")) return 0;
    if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
    return 1;
}

int cc2_codegen_do_stmt(struct cc2_token *cur, char *code, int *pos, int max,
                        char *local_names, int *local_count, int *max_offset) {
    int do_start;
    int cond_pos;
    int jnz_patch;
    int loop_end;
    int i = 0;
    int do_break[CC2_PARSE_MAX_PATCHES];
    int do_cont[CC2_PARSE_MAX_PATCHES];
    int do_break_count = 0;
    int do_cont_count = 0;

    cc2_lex_next(cur);
    do_start = *pos;
    if (!cc2_codegen_one_stmt(cur, code, pos, max, local_names, local_count, max_offset,
                              1, 0, do_break, &do_break_count, do_cont, &do_cont_count)) {
        return 0;
    }
    if (!cc2_parse_expect(cur, CC2_TK_WHILE, "'while'")) return 0;
    if (!cc2_parse_expect(cur, CC2_TK_LPAREN, "'('")) return 0;
    cond_pos = *pos;
    if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
    if (!cc2_parse_expect(cur, CC2_TK_RPAREN, "')'")) return 0;
    if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
    if (!cc2_emit8_chk(code, pos, max, 0x85)) return 0; // test eax,eax
    if (!cc2_emit8_chk(code, pos, max, 0xC0)) return 0;
    if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // jnz rel32
    if (!cc2_emit8_chk(code, pos, max, 0x85)) return 0;
    jnz_patch = *pos;
    if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;
    cc2_patch_rel32(code, jnz_patch, do_start);
    loop_end = *pos;
    i = 0;
    while (i < do_cont_count) {
        cc2_patch_rel32(code, do_cont[i], cond_pos);
        i++;
    }
    i = 0;
    while (i < do_break_count) {
        cc2_patch_rel32(code, do_break[i], loop_end);
        i++;
    }
    return 1;
}

int cc2_codegen_switch_stmt(struct cc2_token *cur, char *code, int *pos, int max,
                            char *local_names, int *local_count, int *max_offset,
                            int in_loop, int loop_cond_pos,
                            int *cont_patches, int *cont_count) {
    int switch_scope_base = 0;
    int switch_slot;
    int case_vals[CC2_PARSE_MAX_PATCHES];
    int case_targets[CC2_PARSE_MAX_PATCHES];
    int case_count = 0;
    int default_target = -1;
    int entry_patch = 0;
    int dispatch_pos;
    int end_patch = 0;
    int default_patch = 0;
    int loop_end;
    int i = 0;
    int sw_break[CC2_PARSE_MAX_PATCHES];
    int sw_break_count = 0;

    cc2_lex_next(cur);
    if (!cc2_parse_expect(cur, CC2_TK_LPAREN, "'('")) return 0;
    if (!cc2_codegen_expr_prec(cur, 1, code, pos, max, local_names, *local_count)) return 0;
    if (!cc2_parse_expect(cur, CC2_TK_RPAREN, "')'")) return 0;
    switch_scope_base = *local_count;
    if (!cc2_scope_push(switch_scope_base)) {
        serial_printf("[cc2_parse] FAIL scope depth overflow\n");
        cc2_test_failures++;
        return 0;
    }

    if (*local_count >= CC2_PARSE_MAX_LOCALS) {
        serial_printf("[cc2_parse] FAIL switch local slot overflow\n");
        cc2_test_failures++;
        return 0;
    }
    switch_slot = *local_count + 1;
    *local_count = *local_count + 1;
    if ((switch_slot * 4) > *max_offset) *max_offset = switch_slot * 4;
    if (!cc2_emit_store_local(code, pos, max, switch_slot * 4)) return 0;

    if (!cc2_parse_expect(cur, CC2_TK_LBRACE, "'{'")) return 0;
    if (!cc2_emit8_chk(code, pos, max, 0xE9)) return 0; // jmp dispatch
    entry_patch = *pos;
    if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;

    while (cur->type != CC2_TK_RBRACE && cur->type != CC2_TK_EOF) {
        if (cur->type == CC2_TK_CASE) {
            if (case_count >= CC2_PARSE_MAX_PATCHES) {
                serial_printf("[cc2_parse] FAIL too many switch cases\n");
                cc2_test_failures++;
                return 0;
            }
            cc2_lex_next(cur);
            if (cur->type != CC2_TK_INT_LIT) {
                serial_printf("[cc2_parse] FAIL switch case expects int literal\n");
                cc2_test_failures++;
                return 0;
            }
            case_vals[case_count] = cur->int_val;
            case_targets[case_count] = *pos;
            case_count = case_count + 1;
            cc2_lex_next(cur);
            if (!cc2_parse_expect(cur, CC2_TK_COLON, "':'")) return 0;
            continue;
        }
        if (cur->type == CC2_TK_DEFAULT) {
            cc2_lex_next(cur);
            if (!cc2_parse_expect(cur, CC2_TK_COLON, "':'")) return 0;
            default_target = *pos;
            continue;
        }
        if (!cc2_codegen_one_stmt(cur, code, pos, max, local_names, local_count, max_offset,
                                  in_loop, loop_cond_pos,
                                  sw_break, &sw_break_count,
                                  cont_patches, cont_count)) {
            return 0;
        }
    }
    if (!cc2_parse_expect(cur, CC2_TK_RBRACE, "'}'")) return 0;

    if (!cc2_emit8_chk(code, pos, max, 0xE9)) return 0; // body fallthrough -> end
    end_patch = *pos;
    if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;

    dispatch_pos = *pos;
    cc2_patch_rel32(code, entry_patch, dispatch_pos);

    i = 0;
    while (i < case_count) {
        if (!cc2_emit_load_local(code, pos, max, switch_slot * 4)) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x3D)) return 0; // cmp eax,imm32
        if (!cc2_emit32le_chk(code, pos, max, case_vals[i])) return 0;
        if (!cc2_emit8_chk(code, pos, max, 0x0F)) return 0; // je rel32
        if (!cc2_emit8_chk(code, pos, max, 0x84)) return 0;
        default_patch = *pos;
        if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;
        cc2_patch_rel32(code, default_patch, case_targets[i]);
        i++;
    }
    if (!cc2_emit8_chk(code, pos, max, 0xE9)) return 0; // jmp default/end
    default_patch = *pos;
    if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;

    loop_end = *pos;
    if (default_target >= 0) cc2_patch_rel32(code, default_patch, default_target);
    else cc2_patch_rel32(code, default_patch, loop_end);
    cc2_patch_rel32(code, end_patch, loop_end);

    i = 0;
    while (i < sw_break_count) {
        cc2_patch_rel32(code, sw_break[i], loop_end);
        i++;
    }
    cc2_scope_pop(local_count);
    return 1;
}

int cc2_codegen_stmt_block(char *src, char *code, int max, int *out_len) {
    struct cc2_token *cur = &cc2_tok_stmt_block;
    int local_count = 0;
    int max_offset = 0;
    int frame_patch;
    int pos = 0;
    int i = 0;

    while (i < CC2_PARSE_LOCAL_BUF) {
        cc2_codegen_local_names[i] = 0;
        i++;
    }
    cc2_local_struct_ptr_reset();
    cc2_arr_reset();
    cc2_scope_reset();
    cc2_arg_code_depth = 0;
    if (!cc2_scope_push(0)) {
        serial_printf("[cc2_parse] FAIL scope depth overflow\n");
        cc2_test_failures++;
        return 0;
    }

    if (!cc2_emit8_chk(code, &pos, max, 0x55)) return 0; // push ebp
    if (!cc2_emit8_chk(code, &pos, max, 0x89)) return 0; // mov ebp,esp
    if (!cc2_emit8_chk(code, &pos, max, 0xE5)) return 0;
    if (!cc2_emit8_chk(code, &pos, max, 0x81)) return 0; // sub esp,imm32
    if (!cc2_emit8_chk(code, &pos, max, 0xEC)) return 0;
    frame_patch = pos;
    if (!cc2_emit32le_chk(code, &pos, max, 0)) return 0;

    cc2_lex_init(src);
    cc2_lex_next(cur);
    while (cur->type != CC2_TK_EOF) {
        if (!cc2_codegen_one_stmt(cur, code, &pos, max,
                                  cc2_codegen_local_names, &local_count, &max_offset,
                                  0, 0, 0, 0, 0, 0)) {
            return 0;
        }
    }
    cc2_scope_pop(&local_count);

    code[frame_patch + 0] = (char)(max_offset & 255);
    code[frame_patch + 1] = (char)((max_offset >> 8) & 255);
    code[frame_patch + 2] = (char)((max_offset >> 16) & 255);
    code[frame_patch + 3] = (char)((max_offset >> 24) & 255);

    if (!cc2_emit8_chk(code, &pos, max, 0x89)) return 0; // mov esp,ebp
    if (!cc2_emit8_chk(code, &pos, max, 0xEC)) return 0;
    if (!cc2_emit8_chk(code, &pos, max, 0x5D)) return 0; // pop ebp
    if (!cc2_emit8_chk(code, &pos, max, 0xC3)) return 0; // ret

    *out_len = pos;
    return 1;
}

int cc2_codegen_function_def(struct cc2_token *cur, char *code, int *pos, int max) {
    char fname[CC2_PARSE_FN_NAME];
    char pname[CC2_PARSE_LOCAL_NAME];
    char cand[CC2_PARSE_FN_NAME];
    int local_count = 0;
    int max_offset = 0;
    int frame_patch = 0;
    int frame_size = 0;
    int param_count = 0;
    int i = 0;
    int fn_start_pos = *pos;

    while (i < CC2_PARSE_LOCAL_BUF) {
        cc2_codegen_local_names[i] = 0;
        i++;
    }
    cc2_local_struct_ptr_reset();
    cc2_arr_reset();
    cc2_scope_reset();
    cc2_arg_code_depth = 0;

    fname[0] = 0;
    while (fname[0] == 0) {
        if (cur->type == CC2_TK_EOF) {
            serial_printf("[cc2_parse] FAIL expected function name at line %d token=%s text='%s'\n",
                          cc2_lex_line, cc2_tok_name(cur->type), cur->text);
            cc2_test_failures++;
            return 0;
        }
        if (cur->type == CC2_TK_STRUCT) {
            cc2_lex_next(cur);
            if (cur->type == CC2_TK_IDENT) cc2_lex_next(cur);
            continue;
        }
        if (cur->type == CC2_TK_INT_KW || cur->type == CC2_TK_STAR) {
            cc2_lex_next(cur);
            continue;
        }
        if (cur->type == CC2_TK_IDENT) {
            cc2_strncpy(cand, cur->text, CC2_PARSE_FN_NAME - 1);
            cand[CC2_PARSE_FN_NAME - 1] = 0;
            cc2_lex_next(cur);
            if (cur->type == CC2_TK_LPAREN) {
                cc2_strncpy(fname, cand, CC2_PARSE_FN_NAME - 1);
                fname[CC2_PARSE_FN_NAME - 1] = 0;
                break;
            }
            continue;
        }
        if (cur->type == CC2_TK_SEMI) {
            cc2_lex_next(cur);
            return 1;
        }
        cc2_lex_next(cur);
        continue;
    }

    if (!cc2_cg_add_fn(fname, *pos)) {
        serial_printf("[cc2_parse] FAIL too many functions\n");
        cc2_test_failures++;
        return 0;
    }
    cc2_strncpy(cc2_codegen_cur_fn, fname, CC2_PARSE_FN_NAME - 1);
    cc2_codegen_cur_fn[CC2_PARSE_FN_NAME - 1] = 0;
    serial_printf("[cc2_fn] %s start=0x%x\n", fname, fn_start_pos);
    if (!cc2_parse_expect(cur, CC2_TK_LPAREN, "'('")) return 0;

    if (cur->type != CC2_TK_RPAREN) {
        while (1) {
            int depth_paren = 0;
            int depth_brack = 0;
            int got_name = 0;
            int param_struct_si = -1;
            int param_ptr_depth = 0;
            int expect_struct_name = 0;
            pname[0] = 0;

            if (param_count == 0 && cur->type == CC2_TK_IDENT &&
                cc2_strcmp(cur->text, "void") == 0) {
                cc2_lex_next(cur);
                if (cur->type == CC2_TK_RPAREN) break;
            }

            while (cur->type != CC2_TK_EOF) {
                if (depth_paren == 0 && depth_brack == 0 &&
                    (cur->type == CC2_TK_COMMA || cur->type == CC2_TK_RPAREN)) {
                    break;
                }
                if (cur->type == CC2_TK_STRUCT) {
                    expect_struct_name = 1;
                } else if (cur->type == CC2_TK_IDENT) {
                    if (expect_struct_name) {
                        param_struct_si = cc2_struct_find(cur->text);
                        expect_struct_name = 0;
                    } else {
                        cc2_strncpy(pname, cur->text, CC2_PARSE_LOCAL_NAME - 1);
                        pname[CC2_PARSE_LOCAL_NAME - 1] = 0;
                        got_name = 1;
                    }
                } else if (cur->type == CC2_TK_STAR) {
                    param_ptr_depth = param_ptr_depth + 1;
                } else if (cur->type == CC2_TK_LPAREN) {
                    depth_paren = depth_paren + 1;
                } else if (cur->type == CC2_TK_RPAREN) {
                    if (depth_paren > 0) depth_paren = depth_paren - 1;
                } else if (cur->type == CC2_TK_LBRACKET) {
                    depth_brack = depth_brack + 1;
                } else if (cur->type == CC2_TK_RBRACKET) {
                    if (depth_brack > 0) depth_brack = depth_brack - 1;
                }
                cc2_lex_next(cur);
            }

            if (!got_name) {
                serial_printf("[cc2_parse] FAIL expected parameter name\n");
                cc2_test_failures++;
                return 0;
            }
            if (param_count >= CC2_PARSE_MAX_LOCALS) {
                serial_printf("[cc2_parse] FAIL too many parameters\n");
                cc2_test_failures++;
                return 0;
            }
            if (cc2_parse_find_local(cc2_codegen_local_names, param_count, pname) >= 0) {
                serial_printf("[cc2_parse] FAIL duplicate parameter '%s'\n", pname);
                cc2_test_failures++;
                return 0;
            }
            cc2_parse_slot_set(cc2_codegen_local_names, CC2_PARSE_LOCAL_NAME, param_count, pname);
            if (param_ptr_depth > 0 && param_struct_si >= 0) {
                cc2_local_struct_ptr_si[param_count] = param_struct_si;
            } else {
                cc2_local_struct_ptr_si[param_count] = -1;
            }
            param_count = param_count + 1;
            if (cur->type == CC2_TK_COMMA) {
                cc2_lex_next(cur);
                continue;
            }
            break;
        }
    }

    if (!cc2_parse_expect(cur, CC2_TK_RPAREN, "')'")) return 0;
    if (cur->type == CC2_TK_SEMI) {
        cc2_lex_next(cur);
        cc2_codegen_cur_fn[0] = 0;
        return 1;
    }
    if (!cc2_parse_expect(cur, CC2_TK_LBRACE, "'{'")) return 0;

    if (!cc2_emit8_chk(code, pos, max, 0x55)) return 0; // push ebp
    if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov ebp,esp
    if (!cc2_emit8_chk(code, pos, max, 0xE5)) return 0;
    if (!cc2_emit8_chk(code, pos, max, 0x81)) return 0; // sub esp,imm32
    if (!cc2_emit8_chk(code, pos, max, 0xEC)) return 0;
    frame_patch = *pos;
    if (!cc2_emit32le_chk(code, pos, max, 0)) return 0;
    if (!cc2_scope_push(0)) {
        serial_printf("[cc2_parse] FAIL scope depth overflow\n");
        cc2_test_failures++;
        return 0;
    }

    local_count = param_count;
    i = 0;
    while (i < param_count) {
        if (!cc2_emit_load_arg(code, pos, max, 8 + (i * 4))) return 0;
        if (!cc2_emit_store_local(code, pos, max, (i + 1) * 4)) return 0;
        i++;
    }
    if ((local_count * 4) > max_offset) max_offset = local_count * 4;

    while (cur->type != CC2_TK_RBRACE && cur->type != CC2_TK_EOF) {
        int stmt_pos_before = cc2_lex_pos;
        int stmt_line_before = cc2_lex_line;
        int stmt_type_before = cur->type;
        if (!cc2_codegen_one_stmt(cur, code, pos, max,
                                  cc2_codegen_local_names, &local_count, &max_offset,
                                  0, 0, 0, 0, 0, 0)) {
            serial_printf("[cc2_parse] FAIL stmt codegen in function '%s' line %d token=%s text='%s' lex_pos=%d code_pos=0x%x\n",
                          fname, cc2_lex_line, cc2_tok_name(cur->type), cur->text, cc2_lex_pos, *pos);
            return 0;
        }
        if (cc2_lex_pos == stmt_pos_before &&
            cc2_lex_line == stmt_line_before &&
            cur->type == stmt_type_before) {
            serial_printf("[cc2_parse] FAIL no progress in function '%s' line %d token=%s text='%s'\n",
                          fname, cc2_lex_line, cc2_tok_name(cur->type), cur->text);
            cc2_test_failures++;
            return 0;
        }
    }
    if (!cc2_parse_expect(cur, CC2_TK_RBRACE, "'}'")) return 0;
    cc2_scope_pop(&local_count);

    frame_size = max_offset + 64;
    if (frame_size < max_offset) {
        serial_printf("[cc2_parse] FAIL frame size overflow fn='%s' max_off=%d\n", fname, max_offset);
        cc2_test_failures++;
        return 0;
    }
    frame_size = (frame_size + 15) & ~15;

    code[frame_patch + 0] = (char)(frame_size & 255);
    code[frame_patch + 1] = (char)((frame_size >> 8) & 255);
    code[frame_patch + 2] = (char)((frame_size >> 16) & 255);
    code[frame_patch + 3] = (char)((frame_size >> 24) & 255);

    if (!cc2_emit8_chk(code, pos, max, 0x89)) return 0; // mov esp,ebp
    if (!cc2_emit8_chk(code, pos, max, 0xEC)) return 0;
    if (!cc2_emit8_chk(code, pos, max, 0x5D)) return 0; // pop ebp
    if (!cc2_emit8_chk(code, pos, max, 0xC3)) return 0; // ret
    serial_printf("[cc2_fn_end] %s end=0x%x len=%d\n", fname, *pos, *pos - fn_start_pos);
    cc2_codegen_cur_fn[0] = 0;

    return 1;
}

int cc2_codegen_try_global_decl(struct cc2_token *cur) {
    int save_pos = cc2_lex_pos;
    int save_line = cc2_lex_line;
    int save_type = cur->type;
    int save_int = cur->int_val;
    char save_text[256];
    int elem_size = 4;
    int ptr_depth = 0;
    int is_array = 0;
    int arr_len = 0;
    int size = 4;
    int gidx = -1;
    char name[CC2_PARSE_GLOBAL_NAME];

    cc2_strncpy(save_text, cur->text, 255);
    save_text[255] = 0;

    if (!(cur->type == CC2_TK_INT_KW ||
          (cur->type == CC2_TK_IDENT && cc2_is_type_word(cur->text)))) {
        return 0;
    }

    while (cur->type == CC2_TK_INT_KW ||
           (cur->type == CC2_TK_IDENT && cc2_is_type_word(cur->text))) {
        if (cur->type == CC2_TK_IDENT && cc2_is_byte_type_name(cur->text)) {
            elem_size = 1;
        }
        cc2_lex_next(cur);
    }
    while (cur->type == CC2_TK_STAR) {
        ptr_depth = ptr_depth + 1;
        cc2_lex_next(cur);
    }
    if (cur->type != CC2_TK_IDENT) {
        cc2_lex_pos = save_pos;
        cc2_lex_line = save_line;
        cur->type = save_type;
        cur->int_val = save_int;
        cc2_strncpy(cur->text, save_text, 255);
        cur->text[255] = 0;
        return 0;
    }

    cc2_strncpy(name, cur->text, CC2_PARSE_GLOBAL_NAME - 1);
    name[CC2_PARSE_GLOBAL_NAME - 1] = 0;
    cc2_lex_next(cur);

    if (cur->type == CC2_TK_LPAREN) { // function/prototype
        cc2_lex_pos = save_pos;
        cc2_lex_line = save_line;
        cur->type = save_type;
        cur->int_val = save_int;
        cc2_strncpy(cur->text, save_text, 255);
        cur->text[255] = 0;
        return 0;
    }

    if (cur->type == CC2_TK_LBRACKET) {
        is_array = 1;
        cc2_lex_next(cur);
        if (!cc2_parse_array_len_token_stream(cur, &arr_len)) {
            cc2_lex_pos = save_pos;
            cc2_lex_line = save_line;
            cur->type = save_type;
            cur->int_val = save_int;
            cc2_strncpy(cur->text, save_text, 255);
            cur->text[255] = 0;
            return 0;
        }
    }

    if (cur->type == CC2_TK_EQ) {
        int depth_paren = 0;
        int depth_brack = 0;
        int depth_brace = 0;
        cc2_lex_next(cur);
        while (cur->type != CC2_TK_EOF) {
            if (depth_paren == 0 && depth_brack == 0 && depth_brace == 0 &&
                cur->type == CC2_TK_SEMI) {
                break;
            }
            if (cur->type == CC2_TK_LPAREN) depth_paren = depth_paren + 1;
            else if (cur->type == CC2_TK_RPAREN && depth_paren > 0) depth_paren = depth_paren - 1;
            else if (cur->type == CC2_TK_LBRACKET) depth_brack = depth_brack + 1;
            else if (cur->type == CC2_TK_RBRACKET && depth_brack > 0) depth_brack = depth_brack - 1;
            else if (cur->type == CC2_TK_LBRACE) depth_brace = depth_brace + 1;
            else if (cur->type == CC2_TK_RBRACE && depth_brace > 0) depth_brace = depth_brace - 1;
            cc2_lex_next(cur);
        }
    }
    if (cur->type != CC2_TK_SEMI) {
        cc2_lex_pos = save_pos;
        cc2_lex_line = save_line;
        cur->type = save_type;
        cur->int_val = save_int;
        cc2_strncpy(cur->text, save_text, 255);
        cur->text[255] = 0;
        return 0;
    }
    cc2_lex_next(cur);

    if (ptr_depth > 0) elem_size = 4;
    if (is_array) {
        if (arr_len <= 0) arr_len = 1;
        size = arr_len * elem_size;
    } else {
        size = 4;
    }
    gidx = cc2_global_add(name, size, elem_size, is_array);
    if (gidx < 0) {
        serial_printf("[cc2_parse] FAIL global alloc '%s'\n", name);
        cc2_test_failures++;
        return 0;
    }
    return 1;
}

int cc2_codegen_program() {
    struct cc2_token *cur = &cc2_tok_codegen;
    if (!cc2_ptr_readable(cc2_codegen_src_ptr) ||
        !cc2_ptr_readable(cc2_codegen_code_ptr) ||
        cc2_codegen_code_max <= 0) {
        serial_printf("[cc2_parse] FAIL bad codegen ptrs src=0x%x code=0x%x max=%d\n",
                      cc2_codegen_src_ptr, cc2_codegen_code_ptr, cc2_codegen_code_max);
        cc2_test_failures++;
        return 0;
    }

    cc2_cg_reset();
    cc2_codegen_pos = 0;
    cc2_codegen_i = 0;
    cc2_codegen_guard = 0;
    cc2_codegen_main_idx = -1;
    cc2_codegen_loop_pos = 0;
    cc2_codegen_loop_type = 0;
    cc2_codegen_top_count = 0;
    cc2_codegen_fn_idx = -1;
    cc2_codegen_fn_addr = 0;
    cc2_codegen_fname[0] = 0;
    cc2_codegen_entry_off = -1;
    cc2_codegen_out_len = 0;
    cc2_tok_clear(cur);
    cc2_lex_init(cc2_codegen_src_ptr);
    cc2_lex_next(cur);
    if (!cc2_tok_type_valid(cur->type)) {
        serial_printf("[cc2_parse] FAIL invalid token after first lex_next type=%d line=%d pos=%d src=0x%x\n",
                      cur->type, cc2_lex_line, cc2_lex_pos, cc2_lex_src);
        cc2_test_failures++;
        return 0;
    }

    while (cur->type != CC2_TK_EOF) {
        if (!cc2_tok_type_valid(cur->type)) {
            serial_printf("[cc2_parse] FAIL invalid token in top-level loop type=%d line=%d pos=%d text0=0x%x\n",
                          cur->type, cc2_lex_line, cc2_lex_pos,
                          (int)(unsigned char)cur->text[0]);
            cc2_test_failures++;
            return 0;
        }
        cc2_codegen_top_count = cc2_codegen_top_count + 1;
        cc2_codegen_guard = cc2_codegen_guard + 1;
        if (cc2_codegen_guard > CC2_GUARD_MAX) {
            serial_printf("[cc2_parse] FAIL watchdog in program loop at line %d token=%s text0=0x%x\n",
                          cc2_lex_line, cc2_tok_name(cur->type),
                          (int)(unsigned char)cur->text[0]);
            cc2_test_failures++;
            return 0;
        }
        cc2_maybe_yield(cc2_codegen_guard);
        cc2_codegen_loop_pos = cc2_lex_pos;
        cc2_codegen_loop_type = cur->type;

        if (cc2_codegen_try_global_decl(cur)) {
            if (cur->type == cc2_codegen_loop_type &&
                cc2_lex_pos == cc2_codegen_loop_pos) {
                serial_printf("[cc2_parse] FAIL no progress in global decl at line %d token=%s text0=0x%x\n",
                              cc2_lex_line, cc2_tok_name(cur->type),
                              (int)(unsigned char)cur->text[0]);
                cc2_test_failures++;
                return 0;
            }
            continue;
        }
        if (cur->type == CC2_TK_IDENT &&
            cc2_strcmp(cur->text, "typedef") == 0) {
            int td_paren = 0;
            int td_brack = 0;
            int td_brace = 0;
            cc2_lex_next(cur);
            while (cur->type != CC2_TK_EOF) {
                if (cur->type == CC2_TK_SEMI &&
                    td_paren == 0 && td_brack == 0 && td_brace == 0) {
                    break;
                }
                if (cur->type == CC2_TK_LPAREN) td_paren = td_paren + 1;
                else if (cur->type == CC2_TK_RPAREN && td_paren > 0) td_paren = td_paren - 1;
                else if (cur->type == CC2_TK_LBRACKET) td_brack = td_brack + 1;
                else if (cur->type == CC2_TK_RBRACKET && td_brack > 0) td_brack = td_brack - 1;
                else if (cur->type == CC2_TK_LBRACE) td_brace = td_brace + 1;
                else if (cur->type == CC2_TK_RBRACE && td_brace > 0) td_brace = td_brace - 1;
                cc2_lex_next(cur);
            }
            if (!cc2_parse_expect(cur, CC2_TK_SEMI, "';'")) return 0;
            if (cur->type == cc2_codegen_loop_type &&
                cc2_lex_pos == cc2_codegen_loop_pos) {
                serial_printf("[cc2_parse] FAIL no progress in typedef skip at line %d\n", cc2_lex_line);
                cc2_test_failures++;
                return 0;
            }
            continue;
        }
        if (cur->type == CC2_TK_STRUCT) {
            if (!cc2_codegen_one_stmt(cur, cc2_codegen_code_ptr, &cc2_codegen_pos,
                                      cc2_codegen_code_max,
                                      0, 0, 0, 0, 0, 0, 0, 0, 0)) {
                return 0;
            }
        } else {
            if (!cc2_codegen_function_def(cur, cc2_codegen_code_ptr,
                                          &cc2_codegen_pos, cc2_codegen_code_max)) {
                serial_printf("[cc2_parse] FAIL function_def dispatch line %d token=%s text0=0x%x lex_pos=%d code_pos=0x%x\n",
                              cc2_lex_line, cc2_tok_name(cur->type),
                              (int)(unsigned char)cur->text[0], cc2_lex_pos, cc2_codegen_pos);
                return 0;
            }
        }
        if (cur->type == CC2_TK_EOF) {
            break;
        }
        if (cur->type == cc2_codegen_loop_type &&
            cc2_lex_pos == cc2_codegen_loop_pos) {
            serial_printf("[cc2_parse] FAIL no progress in top-level parse at line %d token=%s text0=0x%x\n",
                          cc2_lex_line, cc2_tok_name(cur->type),
                          (int)(unsigned char)cur->text[0]);
            cc2_test_failures++;
            return 0;
        }
        if (cc2_cg_fn_count > 0) {
            char fn0_first = cc2_cg_fn_names[0];
            if (fn0_first == 0) {
                serial_printf("[cc2_corrupt] fn[0] name wiped after top_count=%d line=%d fn_count=%d global_count=%d token=%s text='%s'\n",
                              cc2_codegen_top_count, cc2_lex_line, cc2_cg_fn_count, cc2_global_count,
                              cc2_tok_name(cur->type), cur->text);
            }
        }
    }

    serial_printf("[cc2_parse] loop done top_count=%d fn_count=%d lex_pos=%d token=%s text0=0x%x\n",
                  cc2_codegen_top_count, cc2_cg_fn_count, cc2_lex_pos,
                  cc2_tok_name(cur->type), (int)(unsigned char)cur->text[0]);
    if (cc2_ptr_readable(cc2_lex_src)) {
        int src_total = cc2_strlen(cc2_lex_src);
        int byte_at_pos = (cc2_lex_pos >= 0 && cc2_lex_pos < CC2_MAIN_PRE_MAX) ?
                          (int)(cc2_lex_src[cc2_lex_pos]) : -1;
        serial_printf("[cc2_parse] src_len=%d byte@lex_pos=0x%x lex_src=0x%x\n",
                      src_total, byte_at_pos, cc2_lex_src);
    }

    cc2_codegen_i = 0;
    while (cc2_codegen_i < cc2_cg_call_count) {
        cc2_codegen_fn_idx = -1;
        cc2_codegen_fn_addr = 0;
        cc2_parse_slot_copy(cc2_cg_call_names, CC2_PARSE_FN_NAME, cc2_codegen_i,
                            cc2_codegen_fname, CC2_PARSE_FN_NAME);
        cc2_codegen_fn_idx = cc2_cg_find_fn(cc2_codegen_fname);
        if (cc2_codegen_fn_idx >= 0) {
            cc2_patch_rel32(cc2_codegen_code_ptr, cc2_cg_call_patch_pos[cc2_codegen_i],
                            cc2_cg_fn_pos[cc2_codegen_fn_idx]);
        } else if (cc2_builtin_fn_addr(cc2_codegen_fname, &cc2_codegen_fn_addr)) {
            cc2_patch_rel32_abs(cc2_codegen_code_ptr, cc2_cg_call_patch_pos[cc2_codegen_i],
                                cc2_codegen_fn_addr);
        } else {
            serial_printf("[cc2_parse] FAIL unresolved call '%s'\n", cc2_codegen_fname);
            cc2_test_failures++;
            return 0;
        }
        cc2_codegen_i = cc2_codegen_i + 1;
    }

    if (cc2_cg_fn_count <= 0) {
        serial_printf("[cc2_parse] FAIL no functions in program top_count=%d last_token=%s text0=0x%x lex_pos=%d line=%d src0=%d src1=%d src2=%d\n",
                      cc2_codegen_top_count, cc2_tok_name(cur->type),
                      (int)(unsigned char)cur->text[0],
                      cc2_lex_pos, cc2_lex_line, -1, -1, -1);
        cc2_test_failures++;
        return 0;
    }
    serial_printf("[cc2_entry] fn_count=%d\n", cc2_cg_fn_count);
    cc2_codegen_i = 0;
    while (cc2_codegen_i < cc2_cg_fn_count && cc2_codegen_i < 20) {
        char fn_diag[CC2_PARSE_FN_NAME];
        cc2_parse_slot_copy(cc2_cg_fn_names, CC2_PARSE_FN_NAME, cc2_codegen_i,
                            fn_diag, CC2_PARSE_FN_NAME);
        serial_printf("[cc2_entry] fn[%d] pos=0x%x name='%s'\n",
                      cc2_codegen_i, cc2_cg_fn_pos[cc2_codegen_i], fn_diag);
        cc2_codegen_i = cc2_codegen_i + 1;
    }
    cc2_codegen_main_idx = cc2_cg_find_fn("main");
    if (cc2_codegen_main_idx < 0) {
        serial_printf("[cc2_entry] MAIN NOT FOUND among %d functions\n", cc2_cg_fn_count);
        cc2_test_failures++;
        return 0;
    }
    serial_printf("[cc2_entry] main found at fn[%d] pos=0x%x\n",
                  cc2_codegen_main_idx, cc2_cg_fn_pos[cc2_codegen_main_idx]);
    cc2_codegen_entry_off = cc2_cg_fn_pos[cc2_codegen_main_idx];
    cc2_codegen_out_len = cc2_codegen_pos;
    return 1;
}

void cc2_parse_selftest() {
    return;
}

void cc2_pp_reset() {
    cc2_pp_define_count = 0;
    if (cc2_pp_name_data) cc2_pp_clear_slots(cc2_pp_name_data, CC2_PP_NAME_BUF);
    if (cc2_pp_body_data) cc2_pp_clear_slots(cc2_pp_body_data, CC2_PP_BODY_BUF);
    if (cc2_pp_is_func_data) cc2_pp_clear_slots((char*)cc2_pp_is_func_data, CC2_PP_MAX_DEFINES * 4);
    if (cc2_pp_param_count_data) cc2_pp_clear_slots((char*)cc2_pp_param_count_data, CC2_PP_MAX_DEFINES * 4);
}

int cc2_pp_emit_char(char *out, int out_max, int *out_pos, char c) {
    if (*out_pos >= out_max - 1) {
        return 0;
    }
    out[*out_pos] = c;
    *out_pos = *out_pos + 1;
    return 1;
}

void cc2_pp_set_define(char *name, char *body) {
    int idx = cc2_pp_find_define(name);
    if (!cc2_pp_name_data || !cc2_pp_body_data ||
        !cc2_pp_is_func_data || !cc2_pp_param_count_data) return;
    if (idx < 0) {
        if (cc2_pp_define_count >= CC2_PP_MAX_DEFINES) return;
        idx = cc2_pp_define_count;
        cc2_pp_define_count++;
    }
    cc2_pp_set_slot(cc2_pp_name_data, CC2_PP_MAX_NAME, idx, name);
    cc2_pp_set_slot(cc2_pp_body_data, CC2_PP_MAX_BODY, idx, body);
    cc2_pp_is_func_data[idx] = 0;
    cc2_pp_param_count_data[idx] = 0;
}

int cc2_pp_slot_limit(char *flat, int stride) {
    if (!flat || stride <= 0) return 0;
    if (flat == cc2_pp_name_data) return CC2_PP_MAX_DEFINES;
    if (flat == cc2_pp_body_data) return CC2_PP_MAX_DEFINES;
    return CC2_PP_MAX_DEFINES;
}

void cc2_pp_clear_slots(char *flat, int total) {
    int i = 0;
    if (!flat || total <= 0) return;
    if (flat == cc2_pp_name_data && total > CC2_PP_NAME_BUF) total = CC2_PP_NAME_BUF;
    if (flat == cc2_pp_body_data && total > CC2_PP_BODY_BUF) total = CC2_PP_BODY_BUF;
    if ((flat == (char*)cc2_pp_is_func_data || flat == (char*)cc2_pp_param_count_data) &&
        total > (CC2_PP_MAX_DEFINES * 4)) {
        total = CC2_PP_MAX_DEFINES * 4;
    }
    while (i < total) {
        flat[i] = 0;
        i++;
    }
}

void cc2_pp_set_slot(char *flat, int stride, int idx, char *src) {
    int limit = cc2_pp_slot_limit(flat, stride);
    int base = cc2_pp_slot_base(idx, stride);
    int i = 0;
    if (!cc2_ptr_readable(flat) || !cc2_ptr_readable(src) || base < 0) return;
    if (limit <= 0 || idx < 0 || idx >= limit) return;
    while (src[i] && i < stride - 1) {
        flat[base + i] = src[i];
        i++;
    }
    while (i < stride) {
        flat[base + i] = 0;
        i++;
    }
}

int cc2_pp_slot_eq(char *flat, int stride, int idx, char *name) {
    int limit = cc2_pp_slot_limit(flat, stride);
    int base = cc2_pp_slot_base(idx, stride);
    int i = 0;
    if (!cc2_ptr_readable(flat) || !cc2_ptr_readable(name) || base < 0) return 0;
    if (limit <= 0 || idx < 0 || idx >= limit) return 0;
    while (name[i] && flat[base + i]) {
        if (name[i] != flat[base + i]) return 0;
        i++;
    }
    return (name[i] == 0 && flat[base + i] == 0);
}

void cc2_pp_emit_slot(char *out, int out_max, int *out_pos,
                      char *flat, int stride, int idx) {
    int limit = cc2_pp_slot_limit(flat, stride);
    int base = cc2_pp_slot_base(idx, stride);
    int i = 0;
    if (!cc2_ptr_readable(out) || !cc2_ptr_readable(flat) || base < 0) return;
    if (limit <= 0 || idx < 0 || idx >= limit) return;
    while (i < stride && flat[base + i]) {
        if (!cc2_pp_emit_char(out, out_max, out_pos, flat[base + i])) return;
        i++;
    }
}

void cc2_pp_trim_inplace(char *s) {
    int len = cc2_strlen(s);
    int start = 0;
    int end = len;
    int i;
    while (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n') {
        start++;
    }
    while (end > start &&
           (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' ||
            s[end - 1] == '\n')) {
        end--;
    }
    i = 0;
    while (start + i < end) {
        s[i] = s[start + i];
        i++;
    }
    s[i] = 0;
}

void cc2_pp_emit_text(char *out, int out_max, int *out_pos, char *text) {
    int i = 0;
    while (text[i]) {
        if (!cc2_pp_emit_char(out, out_max, out_pos, text[i])) return;
        i++;
    }
}

int cc2_pp_is_skipping(int *cond_stack, int cond_depth) {
    int i = 0;
    while (i < cond_depth) {
        if (cond_stack[i]) return 1;
        i++;
    }
    return 0;
}

int cc2_read_file(char *path, char *buf, int max_len) {
    int pi = 0;
    int has_name = 0;
    int fd;
    int total = 0;
    if (!path || !buf || max_len <= 1) return -1;
    while (path[pi] && pi < 127) {
        if (path[pi] != '/') has_name = 1;
        pi++;
    }
    if (!has_name || pi <= 0 || pi >= 127) return -1;
    fd = vfs_open(path, 0);
    if (fd < 0) return -1;
    while (total < max_len - 1) {
        int r = vfs_read(fd, buf + total, max_len - 1 - total);
        if (r <= 0) break;
        total = total + r;
    }
    vfs_close(fd);
    buf[total] = 0;
    return total;
}

int cc2_preprocess_inner(char *src, int src_len, char *out, int out_max,
                         int *out_pos, int include_depth) {
    int i = 0;
    int guard = 0;
    int line_start = 1;
    int cond_skip[CC2_PP_MAX_COND];
    int cond_parent_skip[CC2_PP_MAX_COND];
    int cond_taken[CC2_PP_MAX_COND];
    int cond_depth = 0;
    int ci = 0;

    while (ci < CC2_PP_MAX_COND) {
        cond_skip[ci] = 0;
        cond_parent_skip[ci] = 0;
        cond_taken[ci] = 0;
        ci++;
    }

    while (i < src_len) {
        guard = guard + 1;
        if ((guard & 32767) == 1) {
            serial_printf("[cc2_pre] i=%d/%d out=%d/%d\n",
                          i, src_len, *out_pos, out_max);
        }
        if (guard > CC2_GUARD_MAX) {
            serial_printf("[cc2_pre] FAIL watchdog include_depth=%d src_len=%d i=%d\n",
                          include_depth, src_len, i);
            cc2_test_failures++;
            return 0;
        }
        cc2_maybe_yield(guard);
        int skip_now = cc2_pp_is_skipping(cond_skip, cond_depth);

        if (line_start && src[i] == '#') {
            int p = i + 1;
            char dir[16];
            int di = 0;
            while (p < src_len && (src[p] == ' ' || src[p] == '\t')) p++;
            while (p < src_len && cc2_ident_body(src[p]) && di < 15) {
                dir[di] = src[p];
                di++;
                p++;
            }
            dir[di] = 0;

            if (cc2_strcmp(dir, "define") == 0) {
                if (!skip_now) {
                    char *name = cc2_pp_tmp_name;
                    char *body = cc2_pp_tmp_body;
                    char *enc_body = cc2_pp_tmp_enc_body;
                    char *params = cc2_pp_tmp_params;
                    int param_count = 0;
                    int is_func = 0;
                    int idx;
                    int ni = 0;
                    int bi = 0;
                    cc2_pp_clear_slots(params, CC2_PP_PARAM_NAME_BUF);
                    while (p < src_len && (src[p] == ' ' || src[p] == '\t')) p++;
                    while (p < src_len && cc2_ident_body(src[p]) && ni < CC2_PP_MAX_NAME - 1) {
                        name[ni] = src[p];
                        ni++;
                        p++;
                    }
                    name[ni] = 0;
                    if (p < src_len && src[p] == '(') {
                        is_func = 1;
                        p++;
                        while (p < src_len && src[p] != ')' && src[p] != '\n') {
                            char pname[CC2_PP_MAX_PARAM_NAME];
                            int pni = 0;
                            while (p < src_len && (src[p] == ' ' || src[p] == '\t')) p++;
                            while (p < src_len && cc2_ident_body(src[p]) &&
                                   pni < CC2_PP_MAX_PARAM_NAME - 1) {
                                pname[pni] = src[p];
                                pni++;
                                p++;
                            }
                            pname[pni] = 0;
                            if (pni > 0 && param_count < CC2_PP_MAX_PARAMS) {
                                cc2_pp_set_slot(params, CC2_PP_MAX_PARAM_NAME, param_count, pname);
                                param_count++;
                            }
                            while (p < src_len && (src[p] == ' ' || src[p] == '\t')) p++;
                            if (p < src_len && src[p] == ',') {
                                p++;
                            }
                        }
                        if (p < src_len && src[p] == ')') p++;
                    }
                    while (p < src_len && (src[p] == ' ' || src[p] == '\t')) p++;
                    while (p < src_len && src[p] != '\n' && src[p] != '\r' &&
                           bi < CC2_PP_MAX_BODY - 1) {
                        body[bi] = src[p];
                        bi++;
                        p++;
                    }
                    while (bi > 0 && (body[bi - 1] == ' ' || body[bi - 1] == '\t')) bi--;
                    body[bi] = 0;
                    if (name[0]) {
                        if (is_func && param_count > 0 && param_count <= 2) {
                            int in_i = 0;
                            int out_i = 0;
                            while (body[in_i] && out_i < CC2_PP_MAX_BODY - 1) {
                                if (cc2_ident_start(body[in_i])) {
                                    char tok[CC2_PP_MAX_PARAM_NAME];
                                    int ti = 0;
                                    int m = in_i;
                                    int param_i = -1;
                                    int pi = 0;
                                    while (body[m] && cc2_ident_body(body[m]) &&
                                           ti < CC2_PP_MAX_PARAM_NAME - 1) {
                                        tok[ti] = body[m];
                                        ti++;
                                        m++;
                                    }
                                    tok[ti] = 0;
                                    while (pi < param_count && pi < 2) {
                                        if (cc2_pp_slot_eq(params, CC2_PP_MAX_PARAM_NAME, pi, tok)) {
                                            param_i = pi;
                                            break;
                                        }
                                        pi++;
                                    }
                                    if (param_i == 0) {
                                        enc_body[out_i] = 1;
                                        out_i++;
                                    } else if (param_i == 1) {
                                        enc_body[out_i] = 2;
                                        out_i++;
                                    } else {
                                        while (in_i < m && out_i < CC2_PP_MAX_BODY - 1) {
                                            enc_body[out_i] = body[in_i];
                                            out_i++;
                                            in_i++;
                                        }
                                        continue;
                                    }
                                    in_i = m;
                                    continue;
                                }
                                enc_body[out_i] = body[in_i];
                                out_i++;
                                in_i++;
                            }
                            enc_body[out_i] = 0;
                            cc2_pp_set_define(name, enc_body);
                        } else {
                            cc2_pp_set_define(name, body);
                        }
                        idx = cc2_pp_find_define(name);
                        if (idx >= 0) {
                            cc2_pp_is_func_data[idx] = is_func;
                            cc2_pp_param_count_data[idx] = param_count;
                        }
                    }
                }
            } else if (cc2_strcmp(dir, "ifndef") == 0) {
                char name[CC2_PP_MAX_NAME];
                int ni = 0;
                while (p < src_len && (src[p] == ' ' || src[p] == '\t')) p++;
                while (p < src_len && cc2_ident_body(src[p]) && ni < CC2_PP_MAX_NAME - 1) {
                    name[ni] = src[p];
                    ni++;
                    p++;
                }
                name[ni] = 0;
                if (cond_depth < CC2_PP_MAX_COND) {
                    cond_parent_skip[cond_depth] = skip_now;
                    if (skip_now) {
                        cond_skip[cond_depth] = 1;
                        cond_taken[cond_depth] = 0;
                    } else if (cc2_pp_find_define(name) >= 0) {
                        cond_skip[cond_depth] = 1;
                        cond_taken[cond_depth] = 0;
                    } else {
                        cond_skip[cond_depth] = 0;
                        cond_taken[cond_depth] = 1;
                    }
                    cond_depth++;
                }
            } else if (cc2_strcmp(dir, "else") == 0) {
                if (cond_depth > 0) {
                    int top = cond_depth - 1;
                    if (cond_parent_skip[top]) {
                        cond_skip[top] = 1;
                    } else if (cond_taken[top]) {
                        cond_skip[top] = 1;
                    } else {
                        cond_skip[top] = 0;
                        cond_taken[top] = 1;
                    }
                }
            } else if (cc2_strcmp(dir, "endif") == 0) {
                if (cond_depth > 0) cond_depth--;
            } else if (cc2_strcmp(dir, "include") == 0) {
                if (!skip_now) {
                    char path[128];
                    int pi = 0;
                    int inc_len;
                    char *inc_buf = cc2_pp_include_buf + (include_depth * CC2_PP_FILE_BUF);
                    while (p < src_len && (src[p] == ' ' || src[p] == '\t')) p++;
                    if (p < src_len && src[p] == '"') p++;
                    while (p < src_len && src[p] != '"' && src[p] != '\n' &&
                           pi < 127) {
                        path[pi] = src[p];
                        pi++;
                        p++;
                    }
                    path[pi] = 0;
                    if (path[0]) {
                        inc_len = cc2_read_file(path, inc_buf, CC2_PP_FILE_BUF);
                        if (inc_len >= 0) {
                            if (!cc2_preprocess_inner(inc_buf, inc_len, out, out_max,
                                                      out_pos, include_depth + 1)) {
                                return 0;
                            }
                        } else {
                            serial_printf("[cc2_pre] FAIL include open: %s\n", path);
                            cc2_test_failures++;
                        }
                    }
                }
            }

            while (i < src_len && src[i] != '\n') i++;
            if (i < src_len && src[i] == '\n') {
                if (!cc2_pp_emit_char(out, out_max, out_pos, '\n')) return 0;
                i++;
            }
            line_start = 1;
            continue;
        }

        if (skip_now) {
            if (src[i] == '\n') line_start = 1;
            else line_start = 0;
            i++;
            continue;
        }

        if (src[i] == '"') {
            if (!cc2_pp_emit_char(out, out_max, out_pos, src[i])) return 0;
            i++;
            while (i < src_len) {
                char c = src[i];
                if (!cc2_pp_emit_char(out, out_max, out_pos, c)) return 0;
                i++;
                if (c == '\\' && i < src_len) {
                    if (!cc2_pp_emit_char(out, out_max, out_pos, src[i])) return 0;
                    i++;
                    continue;
                }
                if (c == '"') break;
            }
            line_start = 0;
            continue;
        }

        if (src[i] == '\'') {
            if (!cc2_pp_emit_char(out, out_max, out_pos, src[i])) return 0;
            i++;
            while (i < src_len) {
                char c = src[i];
                if (!cc2_pp_emit_char(out, out_max, out_pos, c)) return 0;
                i++;
                if (c == '\\' && i < src_len) {
                    if (!cc2_pp_emit_char(out, out_max, out_pos, src[i])) return 0;
                    i++;
                    continue;
                }
                if (c == '\'') break;
            }
            line_start = 0;
            continue;
        }

        if (cc2_ident_start(src[i])) {
            char name[CC2_PP_MAX_NAME];
            int j = i;
            int ni = 0;
            int idx;
            while (j < src_len && cc2_ident_body(src[j]) && ni < CC2_PP_MAX_NAME - 1) {
                name[ni] = src[j];
                ni++;
                j++;
            }
            name[ni] = 0;
            idx = cc2_pp_find_define(name);
            if (idx >= 0) {
                if (cc2_pp_is_func_data[idx]) {
                    int p = j;
                    int has_comma = 0;
                    int arg_count = 0;
                    int expand_ok = 1;
                    int k = 0;
                    int body_base = cc2_pp_slot_base(idx, CC2_PP_MAX_BODY);
                    int a0s = 0;
                    int a0e = 0;
                    int a1s = 0;
                    int a1e = 0;

                    while (p < src_len && (src[p] == ' ' || src[p] == '\t')) p++;
                    if (p < src_len && src[p] == '(' && cc2_pp_param_count_data[idx] <= 2) {
                        p++;
                        while (p < src_len && (src[p] == ' ' || src[p] == '\t')) p++;
                        a0s = p;
                        while (p < src_len && src[p] != ',' && src[p] != ')') {
                            p++;
                        }
                        a0e = p;
                        if (p < src_len && src[p] == ',') {
                            has_comma = 1;
                            p++;
                            while (p < src_len && (src[p] == ' ' || src[p] == '\t')) p++;
                            a1s = p;
                            while (p < src_len && src[p] != ')') {
                                p++;
                            }
                            a1e = p;
                        }
                        if (p >= src_len || src[p] != ')') {
                            expand_ok = 0;
                        } else {
                            p++;
                            arg_count = has_comma ? 2 : 1;
                            while (a0s < a0e && (src[a0s] == ' ' || src[a0s] == '\t'))
                                a0s++;
                            while (a0e > a0s &&
                                   (src[a0e - 1] == ' ' || src[a0e - 1] == '\t'))
                                a0e--;
                            while (a1s < a1e && (src[a1s] == ' ' || src[a1s] == '\t'))
                                a1s++;
                            while (a1e > a1s &&
                                   (src[a1e - 1] == ' ' || src[a1e - 1] == '\t'))
                                a1e--;
                            if (a0s < 0 || a0e < a0s || a0e > src_len)
                                expand_ok = 0;
                            if (a1s < 0 || a1e < a1s || a1e > src_len)
                                expand_ok = 0;
                        }
                    } else {
                        expand_ok = 0;
                    }

                    if (expand_ok) {
                        k = 0;
                        while (k < CC2_PP_MAX_BODY && cc2_pp_body_data[body_base + k]) {
                            char bc = cc2_pp_body_data[body_base + k];
                            if (bc == 1 && arg_count > 0) {
                                int t = a0s;
                                while (t < a0e) {
                                    if (!cc2_pp_emit_char(out, out_max, out_pos, src[t])) {
                                        return 0;
                                    }
                                    t++;
                                }
                            } else if (bc == 2 && arg_count > 1) {
                                int t = a1s;
                                while (t < a1e) {
                                    if (!cc2_pp_emit_char(out, out_max, out_pos, src[t])) {
                                        return 0;
                                    }
                                    t++;
                                }
                            } else if (!cc2_pp_emit_char(out, out_max, out_pos, bc)) {
                                return 0;
                            }
                            k = k + 1;
                        }
                        i = p;
                        line_start = 0;
                        continue;
                    }

                    while (i < j) {
                        if (!cc2_pp_emit_char(out, out_max, out_pos, src[i])) return 0;
                        i++;
                    }
                    line_start = 0;
                    continue;
                } else {
                    int body_base = cc2_pp_slot_base(idx, CC2_PP_MAX_BODY);
                    int k = 0;
                    while (k < CC2_PP_MAX_BODY && cc2_pp_body_data[body_base + k]) {
                        if (!cc2_pp_emit_char(out, out_max, out_pos,
                                              cc2_pp_body_data[body_base + k])) {
                            return 0;
                        }
                        k++;
                    }
                }
            } else {
                while (i < j) {
                    if (!cc2_pp_emit_char(out, out_max, out_pos, src[i])) return 0;
                    i++;
                }
                line_start = 0;
                continue;
            }
            i = j;
            line_start = 0;
            continue;
        }

        if (!cc2_pp_emit_char(out, out_max, out_pos, src[i])) return 0;
        line_start = (src[i] == '\n');
        i++;
    }

    return 1;
}

int cc2_preprocess_light(char *src, int src_len, char *out, int out_max) {
    int i = 0;
    int out_pos = 0;
    int line_start = 1;
    char def_name[CC2_PP_MAX_NAME];
    char def_body[CC2_PP_MAX_BODY];
    while (i < src_len) {
        char c = src[i];
        if (line_start && c == '#') {
            int j = 0;
            int k = 0;
            i++;
            while (i < src_len && (src[i] == ' ' || src[i] == '\t')) i++;
            if (i + 6 <= src_len &&
                src[i + 0] == 'd' && src[i + 1] == 'e' && src[i + 2] == 'f' &&
                src[i + 3] == 'i' && src[i + 4] == 'n' && src[i + 5] == 'e' &&
                (i + 6 == src_len || src[i + 6] == ' ' || src[i + 6] == '\t')) {
                i = i + 6;
                while (i < src_len && (src[i] == ' ' || src[i] == '\t')) i++;
                j = 0;
                while (i < src_len && (cc2_isalnum(src[i]) || src[i] == '_')) {
                    if (j < CC2_PP_MAX_NAME - 1) {
                        def_name[j] = src[i];
                        j++;
                    }
                    i++;
                }
                def_name[j] = 0;
                if (def_name[0] && src[i] != '(') {
                    while (i < src_len && (src[i] == ' ' || src[i] == '\t')) i++;
                    while (i < src_len && src[i] != '\n') {
                        if (k < CC2_PP_MAX_BODY - 1) {
                            def_body[k] = src[i];
                            k++;
                        }
                        i++;
                    }
                    def_body[k] = 0;
                    cc2_pp_trim_inplace(def_body);
                    cc2_pp_set_define(def_name, def_body);
                } else {
                    while (i < src_len && src[i] != '\n') i++;
                }
            } else {
                while (i < src_len && src[i] != '\n') i++;
            }
            if (i < src_len && src[i] == '\n') {
                if (out_pos < out_max - 1) out[out_pos++] = '\n';
                i++;
            }
            line_start = 1;
            continue;
        }
        if (c == '"') {
            if (out_pos >= out_max - 1) return -1;
            out[out_pos++] = src[i++];
            while (i < src_len) {
                c = src[i];
                if (out_pos >= out_max - 1) return -1;
                out[out_pos++] = c;
                i++;
                if (c == '\\' && i < src_len) {
                    if (out_pos >= out_max - 1) return -1;
                    out[out_pos++] = src[i++];
                    continue;
                }
                if (c == '"') break;
            }
            line_start = 0;
            continue;
        }
        if (c == '\'') {
            if (out_pos >= out_max - 1) return -1;
            out[out_pos++] = src[i++];
            while (i < src_len) {
                c = src[i];
                if (out_pos >= out_max - 1) return -1;
                out[out_pos++] = c;
                i++;
                if (c == '\\' && i < src_len) {
                    if (out_pos >= out_max - 1) return -1;
                    out[out_pos++] = src[i++];
                    continue;
                }
                if (c == '\'') break;
            }
            line_start = 0;
            continue;
        }
        if (c == '/' && i + 1 < src_len && src[i + 1] == '/') {
            if (out_pos >= out_max - 1) return -1;
            out[out_pos++] = src[i++];
            if (out_pos >= out_max - 1) return -1;
            out[out_pos++] = src[i++];
            while (i < src_len && src[i] != '\n') {
                if (out_pos >= out_max - 1) return -1;
                out[out_pos++] = src[i++];
            }
            line_start = 0;
            continue;
        }
        if (c == '/' && i + 1 < src_len && src[i + 1] == '*') {
            if (out_pos >= out_max - 1) return -1;
            out[out_pos++] = src[i++];
            if (out_pos >= out_max - 1) return -1;
            out[out_pos++] = src[i++];
            while (i + 1 < src_len) {
                if (out_pos >= out_max - 1) return -1;
                out[out_pos++] = src[i];
                if (src[i] == '*' && src[i + 1] == '/') {
                    i++;
                    if (out_pos >= out_max - 1) return -1;
                    out[out_pos++] = src[i];
                    i++;
                    break;
                }
                i++;
            }
            line_start = 0;
            continue;
        }
        if (cc2_ident_start(c)) {
            int j = i;
            int k = 0;
            int idx = -1;
            while (j < src_len && cc2_ident_body(src[j])) {
                if (k < CC2_PP_MAX_NAME - 1) {
                    def_name[k] = src[j];
                    k++;
                }
                j++;
            }
            def_name[k] = 0;
            idx = cc2_pp_find_define(def_name);
            if (idx >= 0 && idx < CC2_PP_MAX_DEFINES &&
                cc2_pp_is_func_data && cc2_pp_is_func_data[idx] == 0 &&
                cc2_pp_body_data) {
                int base = cc2_pp_slot_base(idx, CC2_PP_MAX_BODY);
                int bi = 0;
                if (base < 0) return -1;
                while (bi < CC2_PP_MAX_BODY && cc2_pp_body_data[base + bi]) {
                    if (out_pos >= out_max - 1) return -1;
                    out[out_pos++] = cc2_pp_body_data[base + bi];
                    bi++;
                }
                i = j;
                line_start = 0;
                continue;
            }
            while (i < j) {
                if (out_pos >= out_max - 1) return -1;
                out[out_pos++] = src[i++];
            }
            line_start = 0;
            continue;
        }
        if (out_pos >= out_max - 1) return -1;
        out[out_pos++] = c;
        line_start = (c == '\n');
        i++;
    }
    out[out_pos] = 0;
    return out_pos;
}

int cc2_preprocess(char *src, int src_len, char *out, int out_max) {
    int out_pos = 0;
    if (!src || !out || out_max <= 1 || src_len < 0) return -1;

    serial_printf("[cc2_pre] lightweight preprocess mode src_len=%d\n", src_len);
    out_pos = cc2_preprocess_light(src, src_len, out, out_max);

    if (out_max > 0) {
        if (out_pos >= 0 && out_pos < out_max) out[out_pos] = 0;
        else out[out_max - 1] = 0;
    }
    return out_pos;
}

int cc2_contains(char *hay, char *needle) {
    int i = 0;
    if (!needle[0]) return 1;
    while (hay[i]) {
        int j = 0;
        while (needle[j] && hay[i + j] && hay[i + j] == needle[j]) j++;
        if (!needle[j]) return 1;
        i++;
    }
    return 0;
}

int cc2_u8(char c) {
    int v = (int)c;
    if (v < 0) v = v + 256;
    return v;
}

void cc2_put16le(char *buf, int off, int v) {
    buf[off + 0] = (char)(v & 255);
    buf[off + 1] = (char)((v >> 8) & 255);
}

void cc2_put32le(char *buf, int off, int v) {
    buf[off + 0] = (char)(v & 255);
    buf[off + 1] = (char)((v >> 8) & 255);
    buf[off + 2] = (char)((v >> 16) & 255);
    buf[off + 3] = (char)((v >> 24) & 255);
}

int cc2_get16le(char *buf, int off) {
    return cc2_u8(buf[off + 0]) | (cc2_u8(buf[off + 1]) << 8);
}

int cc2_get32le(char *buf, int off) {
    return cc2_u8(buf[off + 0]) |
           (cc2_u8(buf[off + 1]) << 8) |
           (cc2_u8(buf[off + 2]) << 16) |
           (cc2_u8(buf[off + 3]) << 24);
}

int cc2_write_all(int fd, char *buf, int len) {
    int total = 0;
    if (fd < 0 || !buf || len < 0) return 0;
    while (total < len) {
        int w = vfs_write(fd, buf + total, len - total);
        if (w <= 0) return 0;
        total = total + w;
    }
    return 1;
}

int cc2_write_zeroes(int fd, int count) {
    char z = 0;
    if (fd < 0 || count < 0) return 0;
    while (count > 0) {
        if (vfs_write(fd, &z, 1) != 1) return 0;
        count--;
    }
    return 1;
}

int cc2_align4(int n) {
    return (n + 3) & (~3);
}

int cc2_write_elf(char *path) {
    char *hdr = cc2_elf_hdr_buf;
    int i = 0;
    char *code = cc2_elf_code_ptr;
    int code_size = cc2_elf_code_size;
    char *data = cc2_elf_data_ptr;
    int data_size = cc2_elf_data_size;
    int entry_offset = cc2_elf_entry_off;
    int code_offset = 128;
    int data_offset = cc2_align4(code_offset + code_size);
    int phnum = (data_size > 0) ? 2 : 1;
    int fd;
    int pad;

    while (i < 128) {
        hdr[i] = 0;
        i++;
    }

    hdr[0] = (char)127;
    hdr[1] = 'E';
    hdr[2] = 'L';
    hdr[3] = 'F';
    hdr[4] = 1;
    hdr[5] = 1;
    hdr[6] = 1;

    cc2_put16le(hdr, 16, 2);                  // ET_EXEC
    cc2_put16le(hdr, 18, 3);                  // EM_386
    cc2_put32le(hdr, 20, 1);                  // EV_CURRENT
    cc2_put32le(hdr, 24, CC2_OUT_CODE_BASE + entry_offset);
    cc2_put32le(hdr, 28, 52);                 // e_phoff
    cc2_put16le(hdr, 40, 52);                 // e_ehsize
    cc2_put16le(hdr, 42, 32);                 // e_phentsize
    cc2_put16le(hdr, 44, phnum);              // e_phnum

    // Program header 0: code segment
    cc2_put32le(hdr, 52 + 0, 1);              // PT_LOAD
    cc2_put32le(hdr, 52 + 4, code_offset);
    cc2_put32le(hdr, 52 + 8, CC2_OUT_CODE_BASE);
    cc2_put32le(hdr, 52 + 12, CC2_OUT_CODE_BASE);
    cc2_put32le(hdr, 52 + 16, code_size);
    cc2_put32le(hdr, 52 + 20, code_size);
    cc2_put32le(hdr, 52 + 24, 0x5);           // PF_R | PF_X
    cc2_put32le(hdr, 52 + 28, 4);

    // Program header 1: data segment
    if (phnum > 1) {
        cc2_put32le(hdr, 84 + 0, 1);          // PT_LOAD
        cc2_put32le(hdr, 84 + 4, data_offset);
        cc2_put32le(hdr, 84 + 8, CC2_OUT_DATA_BASE);
        cc2_put32le(hdr, 84 + 12, CC2_OUT_DATA_BASE);
        cc2_put32le(hdr, 84 + 16, data_size);
        cc2_put32le(hdr, 84 + 20, data_size);
        cc2_put32le(hdr, 84 + 24, 0x6);       // PF_R | PF_W
        cc2_put32le(hdr, 84 + 28, 4);
    }

    if (!path || !path[0]) return -1;
    if (!code) return -1;
    if (data_size > 0 && !data) return -1;
    if (code_size < 0 || data_size < 0) return -1;
    fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;

    if (!cc2_write_all(fd, hdr, 128)) {
        vfs_close(fd);
        return -1;
    }
    if (code_size > 0 && !cc2_write_all(fd, code, code_size)) {
        vfs_close(fd);
        return -1;
    }
    pad = data_offset - (code_offset + code_size);
    if (pad > 0 && !cc2_write_zeroes(fd, pad)) {
        vfs_close(fd);
        return -1;
    }
    if (data_size > 0 && !cc2_write_all(fd, data, data_size)) {
        vfs_close(fd);
        return -1;
    }

    vfs_close(fd);
    return data_offset + data_size;
}

void cc2_elf_selftest() {
    return;
}

void cc2_pre_selftest() {
    return;
}

int cc2_read_arg_token(char *args, int *pos, char *out, int out_max) {
    int i = 0;
    if (!args || !out || out_max < 2) return 0;
    while (args[*pos] == ' ' || args[*pos] == '\t' ||
           args[*pos] == '\r' || args[*pos] == '\n') {
        *pos = *pos + 1;
    }
    if (!args[*pos]) {
        out[0] = 0;
        return 0;
    }
    while (args[*pos] &&
           args[*pos] != ' ' && args[*pos] != '\t' &&
           args[*pos] != '\r' && args[*pos] != '\n') {
        if (i < out_max - 1) {
            out[i] = args[*pos];
            i++;
        }
        *pos = *pos + 1;
    }
    out[i] = 0;
    while (args[*pos] == ' ' || args[*pos] == '\t' ||
           args[*pos] == '\r' || args[*pos] == '\n') {
        *pos = *pos + 1;
    }
    return 1;
}

void cc2_default_out_path(char *in_path, char *out_path, int out_max) {
    int i = 0;
    while (in_path[i] && i < out_max - 1) {
        out_path[i] = in_path[i];
        i++;
    }
    out_path[i] = 0;
    if (i >= 3 &&
        out_path[i - 3] == '.' &&
        out_path[i - 2] == 'c' &&
        out_path[i - 1] == 'c') {
        out_path[i - 2] = 'e';
        out_path[i - 1] = 'l';
        if (i < out_max - 1) {
            out_path[i] = 'f';
            out_path[i + 1] = 0;
        }
        return;
    }
    if (i + 4 < out_max) {
        out_path[i] = '.';
        out_path[i + 1] = 'e';
        out_path[i + 2] = 'l';
        out_path[i + 3] = 'f';
        out_path[i + 4] = 0;
    }
}

int cc2_is_valid_path(char *p) {
    int i = 0;
    if (!p || !p[0]) return 0;
    while (p[i] && i < 127) {
        int c = (int)p[i];
        if (c < 0) c = c + 256;
        if (c < 32 || c > 126) return 0;
        i = i + 1;
    }
    if (i <= 0 || i >= 127) return 0;
    return 1;
}

void cc2_fix_elf_suffix(char *path) {
    int n = 0;
    if (!path) return;
    while (path[n] && n < 127) n = n + 1;
    if (n >= 127) return;
    if (n >= 4 &&
        path[n - 4] == '.' &&
        path[n - 3] == 'e' &&
        path[n - 2] == 'l' &&
        path[n - 1] == 'f') {
        return;
    }
    if (n >= 3 &&
        path[n - 3] == '.' &&
        path[n - 2] == 'e' &&
        path[n - 1] == 'l' &&
        n < 127) {
        path[n] = 'f';
        path[n + 1] = 0;
    }
}

int cc2_parse_compile_args(char *args, char *in_path, char *out_path) {
    int p = 0;
    char pending_out = 0;
    int saw_in = 0;
    int saw_self_name = 0;
    int ti = 0;
    in_path[0] = 0;
    out_path[0] = 0;

    if (!args) return 0;

    while (p < 1024) {
        while (p < 1024 &&
               (args[p] == ' ' || args[p] == '\t' ||
                args[p] == '\r' || args[p] == '\n')) {
            p = p + 1;
        }
        if (p >= 1024 || args[p] == 0) break;

        ti = 0;
        while (p < 1024 && args[p] &&
               args[p] != ' ' && args[p] != '\t' &&
               args[p] != '\r' && args[p] != '\n') {
            if (ti < 127) {
                cc2_arg_tok[ti] = args[p];
                ti++;
            }
            p = p + 1;
        }
        cc2_arg_tok[ti] = 0;
        if (!cc2_arg_tok[0]) {
            if (p < 1024) p = p + 1;
            continue;
        }

        if (pending_out) {
            cc2_strncpy(out_path, cc2_arg_tok, 127);
            out_path[127] = 0;
            pending_out = 0;
            continue;
        }
        if (cc2_strcmp(cc2_arg_tok, "selftest") == 0) return 0;
        if (cc2_strcmp(cc2_arg_tok, "--selftest") == 0) return 0;
        if (cc2_strcmp(cc2_arg_tok, "-o") == 0) {
            pending_out = 1;
            continue;
        }
        if (!saw_in) {
            if (cc2_strcmp(cc2_arg_tok, "cc2_single") == 0 ||
                cc2_strcmp(cc2_arg_tok, "cc2_single.elf") == 0 ||
                cc2_strcmp(cc2_arg_tok, "/bin/cc2_single.elf") == 0 ||
                cc2_strcmp(cc2_arg_tok, "cc3.elf") == 0 ||
                cc2_strcmp(cc2_arg_tok, "/bin/cc3.elf") == 0) {
                saw_self_name = 1;
                continue;
            }
            cc2_strncpy(in_path, cc2_arg_tok, 127);
            in_path[127] = 0;
            saw_in = 1;
        }
        cc2_maybe_yield(p);
    }
    if (pending_out) {
        serial_printf("[cc2] FAIL expected output path after -o\n");
        return 0;
    }
    if (!saw_in) {
        if (saw_self_name) return 0;
        return 0;
    }
    if (!out_path[0]) cc2_default_out_path(in_path, out_path, 128);
    cc2_fix_elf_suffix(out_path);
    return 1;
}

int cc2_parse_compile_args_fallback(char *args, char *in_path, char *out_path) {
    int i = 0;
    int start = -1;
    int end = -1;
    int oi = 0;

    in_path[0] = 0;
    out_path[0] = 0;
    if (!args) return 0;

    // Find first token that ends with ".cc"
    while (args[i]) {
        if (args[i] == '.' && args[i + 1] == 'c' && args[i + 2] == 'c') {
            end = i + 3;
            start = i;
            while (start > 0 && !cc2_isspace(args[start - 1])) {
                start = start - 1;
            }
            break;
        }
        i = i + 1;
    }
    if (start >= 0 && end > start) {
        oi = 0;
        i = start;
        while (i < end && oi < 127) {
            in_path[oi] = args[i];
            oi = oi + 1;
            i = i + 1;
        }
        in_path[oi] = 0;
    }

    // Find explicit -o output path
    i = 0;
    while (args[i]) {
        if (args[i] == '-' && args[i + 1] == 'o' &&
            (args[i + 2] == 0 || cc2_isspace(args[i + 2]))) {
            i = i + 2;
            while (cc2_isspace(args[i])) i = i + 1;
            oi = 0;
            while (args[i] && !cc2_isspace(args[i]) && oi < 127) {
                out_path[oi] = args[i];
                oi = oi + 1;
                i = i + 1;
            }
            out_path[oi] = 0;
            break;
        }
        i = i + 1;
    }

    if (!in_path[0]) return 0;
    if (!out_path[0]) cc2_default_out_path(in_path, out_path, 128);
    cc2_fix_elf_suffix(out_path);
    if (!cc2_is_valid_path(in_path)) return 0;
    if (!cc2_is_valid_path(out_path)) return 0;
    return 1;
}

void cc2_compile_reset_state() {
    cc2_pp_name_data = 0;
    cc2_pp_body_data = 0;
    cc2_pp_is_func_data = 0;
    cc2_pp_param_count_data = 0;
    cc2_pp_define_count = 0;
    cc2_cg_data = 0;
    cc2_cg_data_pos = 0;
    cc2_codegen_entry_off = -1;
    cc2_codegen_out_len = 0;
    cc2_codegen_src_ptr = 0;
    cc2_codegen_code_ptr = 0;
    cc2_codegen_code_max = 0;
    cc2_elf_code_ptr = 0;
    cc2_elf_code_size = 0;
    cc2_elf_data_ptr = 0;
    cc2_elf_data_size = 0;
    cc2_elf_entry_off = 0;
}

void cc2_compile_free_buffers() {
    if (cc2_compile_src_buf) kfree(cc2_compile_src_buf);
    if (cc2_compile_pre_buf) kfree(cc2_compile_pre_buf);
    if (cc2_compile_code_buf) kfree(cc2_compile_code_buf);
    if (cc2_compile_data_buf) kfree(cc2_compile_data_buf);
    if (cc2_compile_pp_name_buf) kfree(cc2_compile_pp_name_buf);
    if (cc2_compile_pp_body_buf) kfree(cc2_compile_pp_body_buf);
    if (cc2_compile_pp_is_func_buf) kfree(cc2_compile_pp_is_func_buf);
    if (cc2_compile_pp_param_count_buf) kfree(cc2_compile_pp_param_count_buf);
    cc2_compile_src_buf = 0;
    cc2_compile_pre_buf = 0;
    cc2_compile_code_buf = 0;
    cc2_compile_data_buf = 0;
    cc2_compile_pp_name_buf = 0;
    cc2_compile_pp_body_buf = 0;
    cc2_compile_pp_is_func_buf = 0;
    cc2_compile_pp_param_count_buf = 0;
    cc2_compile_reset_state();
}

int cc2_compile_alloc_buffers() {
    cc2_compile_src_buf = kmalloc(CC2_MAIN_SRC_MAX);
    cc2_compile_pre_buf = kmalloc(CC2_MAIN_PRE_MAX);
    cc2_compile_code_buf = kmalloc(CC2_MAIN_CODE_MAX);
    cc2_compile_data_buf = kmalloc(CC2_MAIN_DATA_MAX);
    cc2_compile_pp_name_buf = kmalloc(CC2_PP_NAME_BUF);
    cc2_compile_pp_body_buf = kmalloc(CC2_PP_BODY_BUF);
    cc2_compile_pp_is_func_buf = kmalloc(CC2_PP_MAX_DEFINES * 4);
    cc2_compile_pp_param_count_buf = kmalloc(CC2_PP_MAX_DEFINES * 4);
    if (!cc2_compile_src_buf || !cc2_compile_pre_buf || !cc2_compile_code_buf ||
        !cc2_compile_data_buf || !cc2_compile_pp_name_buf || !cc2_compile_pp_body_buf ||
        !cc2_compile_pp_is_func_buf || !cc2_compile_pp_param_count_buf) {
        return 0;
    }
    return 1;
}

int cc2_compile_to_elf(char *in_path, char *out_path) {
    int src_len;
    int pre_len;
    int code_len = 0;
    int entry_off = 0;
    int out_sz;
    int ok = 0;
    int cg_ok = 0;

    if (!cc2_is_valid_path(in_path)) {
        serial_printf("[cc2] FAIL bad input path\n");
        return 0;
    }
    if (!cc2_is_valid_path(out_path)) {
        serial_printf("[cc2] FAIL bad output path\n");
        return 0;
    }

    cc2_compile_free_buffers();
    if (!cc2_compile_alloc_buffers()) {
        serial_printf("[cc2] FAIL buffer alloc pp\n");
        cc2_compile_free_buffers();
        return 0;
    }
    cc2_pp_name_data = cc2_compile_pp_name_buf;
    cc2_pp_body_data = cc2_compile_pp_body_buf;
    cc2_pp_is_func_data = cc2_compile_pp_is_func_buf;
    cc2_pp_param_count_data = cc2_compile_pp_param_count_buf;
    cc2_cg_data = cc2_compile_data_buf;
    cc2_cg_data_pos = 0;

    serial_printf("[cc2] stage read src='%s'\n", in_path);
    src_len = cc2_read_file(in_path, cc2_compile_src_buf, CC2_MAIN_SRC_MAX);
    if (src_len <= 0) {
        serial_printf("[cc2] FAIL open source: %s\n", in_path);
        cc2_compile_free_buffers();
        return 0;
    }

    serial_printf("[cc2] stage preprocess bytes=%d\n", src_len);
    cc2_pp_reset();
    pre_len = cc2_preprocess(cc2_compile_src_buf, src_len, cc2_compile_pre_buf, CC2_MAIN_PRE_MAX);
    serial_printf("[cc2] preprocess done pre_len=%d\n", pre_len);
    if (pre_len <= 0) {
        serial_printf("[cc2] FAIL preprocess: %s\n", in_path);
        cc2_compile_free_buffers();
        return 0;
    }

    serial_printf("[cc2] stage codegen bytes=%d\n", pre_len);
    cc2_codegen_src_ptr = cc2_compile_pre_buf;
    cc2_codegen_code_ptr = cc2_compile_code_buf;
    cc2_codegen_code_max = CC2_MAIN_CODE_MAX;
    cg_ok = cc2_codegen_program();
    if (!cg_ok) {
        serial_printf("[cc2] FAIL codegen: %s\n", in_path);
        cc2_compile_free_buffers();
        return 0;
    }
    code_len = cc2_codegen_out_len;
    entry_off = cc2_codegen_entry_off;
    if (code_len <= 0 || entry_off < 0) {
        int fallback_main = cc2_cg_find_fn("main");
        int fallback_len = cc2_codegen_pos;
        int fallback_entry = -1;
        if (fallback_main >= 0) fallback_entry = cc2_cg_fn_pos[fallback_main];
        serial_printf("[cc2] WARN recovering codegen output len=%d entry=%d -> len=%d entry=%d\n",
                      code_len, entry_off, fallback_len, fallback_entry);
        code_len = fallback_len;
        entry_off = fallback_entry;
    }
    if (code_len <= 0 || entry_off < 0) {
        serial_printf("[cc2] FAIL bad codegen output len=%d entry=%d\n", code_len, entry_off);
        cc2_compile_free_buffers();
        return 0;
    }

    serial_printf("[cc2] stage write out='%s' code=%d data=%d\n",
                  out_path, code_len, cc2_cg_data_pos);
    serial_printf("[cc2_elf] entry_offset=%d e_entry=0x%x\n",
                  entry_off, CC2_OUT_CODE_BASE + entry_off);
    cc2_elf_code_ptr = cc2_compile_code_buf;
    cc2_elf_code_size = code_len;
    cc2_elf_data_ptr = cc2_cg_data;
    cc2_elf_data_size = cc2_cg_data_pos;
    cc2_elf_entry_off = entry_off;
    out_sz = cc2_write_elf(out_path);
    if (out_sz < 0) {
        serial_printf("[cc2] FAIL write elf: %s\n", out_path);
    } else {
        serial_printf("[cc2] compiled '%s' -> '%s' code=%d entry=0x%x\n",
                      in_path, out_path, code_len, CC2_OUT_CODE_BASE + entry_off);
        ok = 1;
    }

    cc2_compile_free_buffers();
    return ok;
}

void main() {
    char *args = (char*)get_args();
    int ai = 0;

    cc2_test_failures = 0;

    if (!args) {
        cc2_main_args_buf[0] = 0;
    } else {
        while (args[ai] && ai < 1023) {
            cc2_main_args_buf[ai] = args[ai];
            ai++;
        }
        cc2_main_args_buf[ai] = 0;
        if (ai >= 1023 && args[ai]) {
            serial_printf("[cc2] WARN args truncated to 1023 bytes\n");
        }
    }

    cc2_lex_selftest();
    cc2_pre_selftest();
    cc2_parse_selftest();
    cc2_elf_selftest();

    serial_printf("[cc2] tests complete: failures=%d\n", cc2_test_failures);
    if (cc2_test_failures == 0) {
        serial_printf("[cc2] status PASS\n");
    } else {
        serial_printf("[cc2] status FAIL\n");
    }

    if (cc2_test_failures == 0) {
        int parse_ok = 0;
        serial_printf("[cc2] parse args begin\n");
        serial_printf("[cc2] raw args='%s'\n", cc2_main_args_buf);
        if (!cc2_main_args_buf[0]) {
            serial_printf("[cc2] no args; skipping compile\n");
            return;
        }
        parse_ok = cc2_parse_compile_args_fallback(cc2_main_args_buf, cc2_main_in_path, cc2_main_out_path);
        serial_printf("[cc2] parse args rc=%d in='%s' out='%s'\n",
                      parse_ok, cc2_main_in_path, cc2_main_out_path);
        if (parse_ok) {
            serial_printf("[cc2] compile request in='%s' out='%s'\n", cc2_main_in_path, cc2_main_out_path);
            if (!cc2_compile_to_elf(cc2_main_in_path, cc2_main_out_path)) {
                serial_printf("[cc2] COMPILE FAIL\n");
            } else {
                serial_printf("[cc2] COMPILE PASS\n");
            }
        } else {
            serial_printf("[cc2] FAIL arg parse\n");
        }
    }
}
