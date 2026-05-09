/* dglibc_compat.h - included via -include into every DOOM source file.
 * Aliases standard libc names to dglibc shim symbols.
 * CupidOS DOOM port.
 */
#ifndef DGLIBC_COMPAT_H
#define DGLIBC_COMPAT_H

/* Pull in our base types first (provides NULL, uint32_t, size_t, bool, etc.) */
#include "types.h"
#include "dglibc.h"

/* va_list / va_arg - GCC always provides these as builtins            */
typedef __builtin_va_list va_list;
#define va_start(ap, last)  __builtin_va_start(ap, last)
#define va_end(ap)          __builtin_va_end(ap)
#define va_arg(ap, type)    __builtin_va_arg(ap, type)
#define va_copy(dst, src)   __builtin_va_copy(dst, src)

/* stdint / stddef aliases (GCC builtins)                              */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef char               int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;
/* ptrdiff_t, intptr_t, uintptr_t */
typedef int                ptrdiff_t;
typedef int                intptr_t;
typedef unsigned int       uintptr_t;
/* ssize_t */
typedef int                ssize_t;
/* off_t */
typedef int32_t            off_t;

/* PRId32, PRIu32 etc. - DOOM/chocolate uses these rarely */
#define PRId32 "d"
#define PRIu32 "u"
#define PRId64 "lld"
#define PRIu64 "llu"

/* Boolean - kernel/types.h already defines bool/true/false under C   */
/* Guard against re-definition if types.h already did it.             */
/* types.h defines bool as enum under !C++ - that's fine. No extra needed. */

/* Common constants                                                    */
#ifndef M_PI
#define M_PI    3.14159265358979323846
#endif
#ifndef M_E
#define M_E     2.71828182845904523536
#endif
#ifndef INT_MAX
#define INT_MAX 0x7FFFFFFF
#endif
#ifndef INT_MIN
#define INT_MIN ((int)0x80000000)
#endif
#ifndef UINT_MAX
#define UINT_MAX 0xFFFFFFFFU
#endif
#ifndef INT16_MAX
#define INT16_MAX 0x7FFF
#endif
#ifndef INT16_MIN
#define INT16_MIN ((int16_t)0x8000)
#endif
#ifndef LONG_MAX
#define LONG_MAX 0x7FFFFFFFL
#endif
#ifndef LONG_MIN
#define LONG_MIN ((long)0x80000000L)
#endif
#ifndef SHRT_MAX
#define SHRT_MAX 0x7FFF
#endif
#ifndef SIZE_MAX
#define SIZE_MAX 0xFFFFFFFFU
#endif

/* SEEK_* constants                                                    */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* errno stub - DOOM tests errno in some file-open error paths         */
/* Provide a per-TU static so multiple TUs don't conflict at link time */
static int errno = 0;
#define ENOENT  2
#define EACCES 13
#define EEXIST 17
#define EINVAL 22
#define EISDIR 21
#define EBADF   9
#define ENOMEM 12
#define ENOSPC 28

/* Heap                                                                */
#define malloc(n)        dg_malloc((uint32_t)(n))
#define calloc(n,sz)     dg_calloc((uint32_t)(n), (uint32_t)(sz))
#define realloc(p,n)     dg_realloc((p), (uint32_t)(n))
#define free(p)          dg_free(p)
#define strdup(s)        dg_strdup(s)
#define getenv(n)        dg_getenv(n)
#define exit(c)          dg_exit(c)
#define abort()          dg_abort()
#define qsort(b,n,s,c)  dg_qsort((b),(uint32_t)(n),(uint32_t)(s),(c))
#define time(t)          dg_time(t)

/* printf family                                                       */
#define snprintf(s,n,...)   dg_snprintf((s),(uint32_t)(n),__VA_ARGS__)
#define vsnprintf(s,n,f,v)  dg_vsnprintf((s),(uint32_t)(n),(f),(v))
#define sprintf(s,...)      dg_sprintf((s),__VA_ARGS__)
#define printf(...)         dg_printf(__VA_ARGS__)
#define fprintf(f,...)      dg_fprintf((f),__VA_ARGS__)

/* stdio                                                               */
#define FILE      DG_FILE
#define stdin     dg_stdin
#define stdout    dg_stdout
#define stderr    dg_stderr
#define fopen     dg_fopen
#define fclose    dg_fclose
#define fread(p,sz,n,f)  dg_fread((p),(uint32_t)(sz),(uint32_t)(n),(f))
#define fwrite(p,sz,n,f) dg_fwrite((p),(uint32_t)(sz),(uint32_t)(n),(f))
#define fseek(f,o,w)     dg_fseek((f),(int32_t)(o),(w))
#define ftell     dg_ftell
#define feof      dg_feof
#define clearerr  dg_clearerr
#define fgets     dg_fgets
#define fgetc     dg_fgetc
#define fputc     dg_fputc
#define fputs(s,f)  dg_fputc('\0', (f))   /* stub: no fputs in dglibc yet */
#define fflush(f)   0                      /* no-op */
#define perror(s)   dg_printf("error: %s\n", (s))

/* ctype                                                               */
#define isspace(c)   dg_isspace(c)
#define isdigit(c)   dg_isdigit(c)
#define isalpha(c)   dg_isalpha(c)
#define isprint(c)   dg_isprint(c)
#define isalnum(c)   (dg_isalpha(c) || dg_isdigit(c))
#define isupper(c)   ((c) >= 'A' && (c) <= 'Z')
#define islower(c)   ((c) >= 'a' && (c) <= 'z')
#define iscntrl(c)   ((unsigned)(c) < 32u || (c) == 127)
#define isxdigit(c)  (dg_isdigit(c) || ((c)>='a'&&(c)<='f') || ((c)>='A'&&(c)<='F'))
#define ispunct(c)   (isprint(c) && !isalnum(c) && (c) != ' ')
#define tolower(c)   dg_tolower(c)
#define toupper(c)   dg_toupper(c)
#define strcasecmp   dg_strcasecmp
#define strncasecmp  dg_strncasecmp

/* string.h functions - kernel/string.h provides these already but    */
/* doom pulls them through <string.h>. We forward-declare them here   */
/* since our include_stubs/string.h is empty.                         */
/* Provided by kernel/string.h which is in the include path */
/* Forward-declare the ones DOOM uses directly */
extern void  *memcpy(void *dst, const void *src, size_t n);
extern void  *memmove(void *dst, const void *src, size_t n);
extern void  *memset(void *dst, int c, size_t n);
extern int    memcmp(const void *a, const void *b, size_t n);
extern char  *strcpy(char *dst, const char *src);
extern char  *strncpy(char *dst, const char *src, size_t n);
extern char  *strcat(char *dst, const char *src);
extern char  *strncat(char *dst, const char *src, size_t n);
extern int    strcmp(const char *a, const char *b);
extern int    strncmp(const char *a, const char *b, size_t n);
extern size_t strlen(const char *s);
extern char  *strchr(const char *s, int c);
extern char  *strrchr(const char *s, int c);
extern char  *strstr(const char *s, const char *t);
extern char  *strtok(char *s, const char *delim);
extern long   strtol(const char *s, char **end, int base);
extern unsigned long strtoul(const char *s, char **end, int base);
extern double strtod(const char *s, char **end);
extern int    atoi(const char *s);

/* math.h - DOOM uses a few math functions                             */
extern double sqrt(double x);
extern double fabs(double x);
extern double floor(double x);
extern double ceil(double x);
extern double sin(double x);
extern double cos(double x);
extern double atan2(double y, double x);
extern double pow(double x, double y);
extern double log(double x);
/* Integer abs.
 *
 * Cast to (int) first so callers passing an unsigned value (notably
 * angle_t in r_segs.c / r_main.c) get the same "shortest angle"
 * behaviour as the real libc int abs(int): for unsigned x>INT_MAX,
 * (int)x is negative, abs returns its positive equivalent (i.e.
 * 2^32 - x). Without the cast, (x)<0 is always false on unsigned and
 * the macro is a no-op, which makes
 *
 *     abs(rw_normalangle - rw_angle1)
 *
 * mis-clamp to ANG90 whenever the true offset angle is small but the
 * unsigned subtraction wraps - that produced rw_distance ≈ 0 and
 * forced R_ScaleFromGlobalAngle into its 64*FRACUNIT clamp, which in
 * turn made R_RenderSegLoop sample the same texture column for every
 * screen X (a uniform vertical stripe = "gray slab" walls).
 */
#ifndef abs
#define abs(x) (((int)(x)) < 0 ? -((int)(x)) : ((int)(x)))
#endif

/* stdlib.h extras - implemented in doom_libc_stubs.c                 */
extern int           atoi(const char *s);
extern long          atol(const char *s);
extern double        atof(const char *s);
extern long          strtol(const char *s, char **endp, int base);
extern unsigned long strtoul(const char *s, char **endp, int base);
extern double        strtod(const char *s, char **endp);

/* stdio extras */
extern int  puts(const char *s);
extern int  sscanf(const char *s, const char *fmt, ...);
extern int  vfprintf(DG_FILE *f, const char *fmt, va_list va);

/* POSIX file ops */
extern int  remove(const char *path);
extern int  rename(const char *old_path, const char *new_path);
extern int  mkdir(const char *path, unsigned int mode);

/* setjmp                                                              */
#define jmp_buf    dg_jmp_buf
#define setjmp(e)  dg_setjmp(e)
#define longjmp    dg_longjmp

/* assert - DOOM uses assert() in some debug code                     */
#define assert(expr)  ((void)0)

/* Rename main() so DOOM's i_system.c main doesn't clash with kernel  */
/* doomgeneric uses doomgeneric_Create() as entry - no main() in tree */
/* but guard anyway: */
/* #define main doomgeneric_main_unused */

/* Misc POSIX bits DOOM may reference                                  */
#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   64
#define O_TRUNC   512

#endif /* DGLIBC_COMPAT_H */
