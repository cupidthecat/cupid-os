#ifndef KERNEL_DOOM_DGLIBC_H
#define KERNEL_DOOM_DGLIBC_H

#include "../types.h"

/* Opaque file handle */
typedef struct DG_FILE DG_FILE;

extern DG_FILE *dg_stdin;
extern DG_FILE *dg_stdout;
extern DG_FILE *dg_stderr;

/* heap */
void *dg_malloc(uint32_t n);
void *dg_calloc(uint32_t n, uint32_t sz);
void *dg_realloc(void *p, uint32_t newsz);
void  dg_free(void *p);
char *dg_strdup(const char *s);

/* ctype */
int dg_isspace(int c);
int dg_isdigit(int c);
int dg_isalpha(int c);
int dg_isprint(int c);
int dg_tolower(int c);
int dg_toupper(int c);

/* strings */
int dg_strcasecmp(const char *a, const char *b);
int dg_strncasecmp(const char *a, const char *b, uint32_t n);

/* env / time */
char    *dg_getenv(const char *name);
uint32_t dg_time(void *t);

/* stdio */
DG_FILE *dg_fopen(const char *path, const char *mode);
int      dg_fclose(DG_FILE *f);
uint32_t dg_fread(void *p, uint32_t sz, uint32_t n, DG_FILE *f);
uint32_t dg_fwrite(const void *p, uint32_t sz, uint32_t n, DG_FILE *f);
int      dg_fseek(DG_FILE *f, int32_t off, int whence);
int32_t  dg_ftell(DG_FILE *f);
int      dg_feof(DG_FILE *f);
void     dg_clearerr(DG_FILE *f);
char    *dg_fgets(char *s, int n, DG_FILE *f);
int      dg_fgetc(DG_FILE *f);
int      dg_fputc(int c, DG_FILE *f);
int      dg_fprintf(DG_FILE *f, const char *fmt, ...);
int      dg_printf(const char *fmt, ...);
int      dg_sprintf(char *s, const char *fmt, ...);
int      dg_snprintf(char *s, uint32_t n, const char *fmt, ...);
int      dg_vsnprintf(char *s, uint32_t n, const char *fmt, void *va);

/* exit/abort */
void dg_exit(int code);
void dg_abort(void);

/* qsort */
void dg_qsort(void *base, uint32_t n, uint32_t sz,
              int (*cmp)(const void *, const void *));

/* setjmp/longjmp */
typedef uint32_t dg_jmp_buf[6];
int  dg_setjmp(dg_jmp_buf env);
void dg_longjmp(dg_jmp_buf env, int val);

/* exit envelope (used by doom_main; landing slot for dg_exit longjmp) */
void dg_arm_exit(dg_jmp_buf env);

/* Self-test entry - dispatched from CupidC bin/dglibc_test.cc */
int  dglibc_test_main(void);

#endif /* KERNEL_DOOM_DGLIBC_H */
