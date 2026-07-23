#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CUPID_LINUX_SYS_EXIT 1
#define CUPID_LINUX_SYS_READ 3
#define CUPID_LINUX_SYS_WRITE 4
#define CUPID_LINUX_SYS_OPEN 5
#define CUPID_LINUX_SYS_CLOSE 6
#define CUPID_LINUX_SYS_LSEEK 19
#define CUPID_LINUX_SYS_BRK 45
#define CUPID_LINUX_SYS_GETCWD 183

#define CUPID_LINUX_EINTR 4
#define CUPID_LINUX_EIO 5
#define CUPID_LINUX_EBADF 9
#define CUPID_LINUX_ENOMEM 12
#define CUPID_LINUX_EINVAL 22
#define CUPID_LINUX_EOVERFLOW 75

#define CUPID_LINUX_O_RDONLY 0
#define CUPID_LINUX_O_WRONLY 1
#define CUPID_LINUX_O_CREAT 64
#define CUPID_LINUX_O_TRUNC 512
#define CUPID_LINUX_O_APPEND 1024

#define CUPID_LINUX_SEEK_SET 0
#define CUPID_LINUX_SEEK_CUR 1

#define CUPID_RUNTIME_UINT_MAX 4294967295u
#define CUPID_RUNTIME_INT_MAX 2147483647
#define CUPID_RUNTIME_IO_CHUNK 2147479552u
#define CUPID_RUNTIME_HEAP_ALIGNMENT 16u

int cupid_linux_syscall1(int number, unsigned int first);
int cupid_linux_syscall2(int number, unsigned int first,
                         unsigned int second);
int cupid_linux_syscall3(int number, unsigned int first,
                         unsigned int second, unsigned int third);

struct _IO_FILE {
  int descriptor;
  int error;
  int owned;
};

typedef struct cupid_heap_block cupid_heap_block_t;

struct cupid_heap_block {
  size_t size;
  unsigned int available;
  cupid_heap_block_t *previous;
  cupid_heap_block_t *next;
};

static int cupid_runtime_errno;
static FILE cupid_runtime_stdout = {1, 0, 0};
static FILE cupid_runtime_stderr = {2, 0, 0};
static cupid_heap_block_t *cupid_heap_first;
static cupid_heap_block_t *cupid_heap_last;
static unsigned int cupid_heap_end;

FILE *stdout = &cupid_runtime_stdout;
FILE *stderr = &cupid_runtime_stderr;

int *__errno_location(void) {
  return &cupid_runtime_errno;
}

static int cupid_runtime_syscall_failed(int result) {
  return result < 0 && result >= -4095 ? 1 : 0;
}

static int cupid_runtime_syscall_error(int result) {
  int value = -result;
  errno = value;
  return value;
}

void *memcpy(void *destination, const void *source, size_t bytes) {
  unsigned char *target = (unsigned char *)destination;
  const unsigned char *input = (const unsigned char *)source;
  size_t index;
  for (index = 0u; index < bytes; index++) {
    target[index] = input[index];
  }
  return destination;
}

void *memset(void *destination, int value, size_t bytes) {
  unsigned char *target = (unsigned char *)destination;
  unsigned char byte = (unsigned char)value;
  size_t index;
  for (index = 0u; index < bytes; index++) {
    target[index] = byte;
  }
  return destination;
}

size_t strlen(const char *text) {
  size_t size = 0u;
  while (text[size] != '\0') {
    size++;
  }
  return size;
}

int strcmp(const char *left, const char *right) {
  size_t index = 0u;
  while (left[index] != '\0' && left[index] == right[index]) {
    index++;
  }
  return (int)(unsigned char)left[index] -
         (int)(unsigned char)right[index];
}

int strncmp(const char *left, const char *right, size_t count) {
  size_t index = 0u;
  while (index < count && left[index] != '\0' &&
         left[index] == right[index]) {
    index++;
  }
  if (index == count) {
    return 0;
  }
  return (int)(unsigned char)left[index] -
         (int)(unsigned char)right[index];
}

char *strchr(const char *text, int character) {
  char wanted = (char)character;
  size_t index = 0u;
  for (;;) {
    if (text[index] == wanted) {
      return (char *)(text + index);
    }
    if (text[index] == '\0') {
      return (char *)0;
    }
    index++;
  }
}

static int cupid_heap_size(size_t bytes, size_t *aligned_out) {
  size_t requested = bytes == 0u ? 1u : bytes;
  if (requested > CUPID_RUNTIME_UINT_MAX -
                      (CUPID_RUNTIME_HEAP_ALIGNMENT - 1u)) {
    errno = CUPID_LINUX_ENOMEM;
    return 0;
  }
  *aligned_out =
      (requested + CUPID_RUNTIME_HEAP_ALIGNMENT - 1u) &
      ~(CUPID_RUNTIME_HEAP_ALIGNMENT - 1u);
  return 1;
}

static int cupid_heap_initialize(void) {
  unsigned int current;
  unsigned int aligned;
  int result;
  if (cupid_heap_end != 0u) {
    return 1;
  }
  result = cupid_linux_syscall1(CUPID_LINUX_SYS_BRK, 0u);
  if (result == 0 || cupid_runtime_syscall_failed(result)) {
    errno = CUPID_LINUX_ENOMEM;
    return 0;
  }
  current = (unsigned int)result;
  if (current > CUPID_RUNTIME_UINT_MAX -
                    (CUPID_RUNTIME_HEAP_ALIGNMENT - 1u)) {
    errno = CUPID_LINUX_ENOMEM;
    return 0;
  }
  aligned =
      (current + CUPID_RUNTIME_HEAP_ALIGNMENT - 1u) &
      ~(CUPID_RUNTIME_HEAP_ALIGNMENT - 1u);
  if (aligned != current) {
    result = cupid_linux_syscall1(CUPID_LINUX_SYS_BRK, aligned);
    if ((unsigned int)result != aligned) {
      errno = CUPID_LINUX_ENOMEM;
      return 0;
    }
  }
  cupid_heap_end = aligned;
  return 1;
}

static void cupid_heap_split(cupid_heap_block_t *block, size_t size) {
  cupid_heap_block_t *remainder;
  size_t required = size + sizeof(cupid_heap_block_t) +
                    CUPID_RUNTIME_HEAP_ALIGNMENT;
  if (block->size < required) {
    return;
  }
  remainder =
      (cupid_heap_block_t *)((unsigned char *)(block + 1) + size);
  remainder->size =
      block->size - size - sizeof(cupid_heap_block_t);
  remainder->available = 1u;
  remainder->previous = block;
  remainder->next = block->next;
  if (remainder->next != (cupid_heap_block_t *)0) {
    remainder->next->previous = remainder;
  } else {
    cupid_heap_last = remainder;
  }
  block->next = remainder;
  block->size = size;
}

static void cupid_heap_join_next(cupid_heap_block_t *block) {
  cupid_heap_block_t *next = block->next;
  if (next == (cupid_heap_block_t *)0 || next->available == 0u) {
    return;
  }
  block->size += sizeof(cupid_heap_block_t) + next->size;
  block->next = next->next;
  if (block->next != (cupid_heap_block_t *)0) {
    block->next->previous = block;
  } else {
    cupid_heap_last = block;
  }
}

void *malloc(size_t bytes) {
  size_t size;
  cupid_heap_block_t *block;
  unsigned int address;
  unsigned int end;
  int result;
  if (!cupid_heap_size(bytes, &size)) {
    return (void *)0;
  }
  block = cupid_heap_first;
  while (block != (cupid_heap_block_t *)0) {
    if (block->available != 0u && block->size >= size) {
      cupid_heap_split(block, size);
      block->available = 0u;
      return (void *)(block + 1);
    }
    block = block->next;
  }
  if (!cupid_heap_initialize()) {
    return (void *)0;
  }
  address = cupid_heap_end;
  if (size > CUPID_RUNTIME_UINT_MAX - address -
                 (size_t)sizeof(cupid_heap_block_t)) {
    errno = CUPID_LINUX_ENOMEM;
    return (void *)0;
  }
  end = address + (unsigned int)sizeof(cupid_heap_block_t) +
        (unsigned int)size;
  result = cupid_linux_syscall1(CUPID_LINUX_SYS_BRK, end);
  if ((unsigned int)result != end) {
    errno = CUPID_LINUX_ENOMEM;
    return (void *)0;
  }
  cupid_heap_end = end;
  block = (cupid_heap_block_t *)address;
  block->size = size;
  block->available = 0u;
  block->previous = cupid_heap_last;
  block->next = (cupid_heap_block_t *)0;
  if (cupid_heap_last != (cupid_heap_block_t *)0) {
    cupid_heap_last->next = block;
  } else {
    cupid_heap_first = block;
  }
  cupid_heap_last = block;
  return (void *)(block + 1);
}

void *calloc(size_t count, size_t bytes) {
  size_t size;
  void *allocation;
  if (bytes != 0u && count > CUPID_RUNTIME_UINT_MAX / bytes) {
    errno = CUPID_LINUX_ENOMEM;
    return (void *)0;
  }
  size = count * bytes;
  allocation = malloc(size);
  if (allocation != (void *)0) {
    (void)memset(allocation, 0, size);
  }
  return allocation;
}

void free(void *allocation) {
  cupid_heap_block_t *block;
  cupid_heap_block_t *previous;
  unsigned int address;
  int result;
  if (allocation == (void *)0) {
    return;
  }
  block = ((cupid_heap_block_t *)allocation) - 1;
  block->available = 1u;
  cupid_heap_join_next(block);
  previous = block->previous;
  if (previous != (cupid_heap_block_t *)0 &&
      previous->available != 0u) {
    cupid_heap_join_next(previous);
    block = previous;
  }
  if (block->next != (cupid_heap_block_t *)0) {
    return;
  }
  previous = block->previous;
  address = (unsigned int)block;
  result = cupid_linux_syscall1(CUPID_LINUX_SYS_BRK, address);
  if ((unsigned int)result != address) {
    return;
  }
  cupid_heap_end = address;
  cupid_heap_last = previous;
  if (cupid_heap_last != (cupid_heap_block_t *)0) {
    cupid_heap_last->next = (cupid_heap_block_t *)0;
  } else {
    cupid_heap_first = (cupid_heap_block_t *)0;
  }
}

void *realloc(void *allocation, size_t bytes) {
  cupid_heap_block_t *block;
  size_t size;
  size_t copy_size;
  void *replacement;
  if (allocation == (void *)0) {
    return malloc(bytes);
  }
  if (bytes == 0u) {
    free(allocation);
    return (void *)0;
  }
  if (!cupid_heap_size(bytes, &size)) {
    return (void *)0;
  }
  block = ((cupid_heap_block_t *)allocation) - 1;
  if (block->size >= size) {
    cupid_heap_split(block, size);
    return allocation;
  }
  if (block->next != (cupid_heap_block_t *)0 &&
      block->next->available != 0u &&
      block->size + sizeof(cupid_heap_block_t) +
              block->next->size >=
          size) {
    cupid_heap_join_next(block);
    cupid_heap_split(block, size);
    block->available = 0u;
    return allocation;
  }
  replacement = malloc(bytes);
  if (replacement == (void *)0) {
    return (void *)0;
  }
  copy_size = block->size < bytes ? block->size : bytes;
  (void)memcpy(replacement, allocation, copy_size);
  free(allocation);
  return replacement;
}

static int cupid_stdio_bad_stream(FILE *stream) {
  if (stream == (FILE *)0 || stream->descriptor < 0) {
    errno = CUPID_LINUX_EBADF;
    if (stream != (FILE *)0) {
      stream->error = 1;
    }
    return 1;
  }
  return 0;
}

static FILE *cupid_stdio_open(const char *path, int flags) {
  int descriptor;
  FILE *stream;
  descriptor = cupid_linux_syscall3(
      CUPID_LINUX_SYS_OPEN, (unsigned int)path, (unsigned int)flags, 438u);
  if (cupid_runtime_syscall_failed(descriptor)) {
    (void)cupid_runtime_syscall_error(descriptor);
    return (FILE *)0;
  }
  stream = (FILE *)malloc(sizeof(FILE));
  if (stream == (FILE *)0) {
    (void)cupid_linux_syscall1(CUPID_LINUX_SYS_CLOSE,
                               (unsigned int)descriptor);
    return (FILE *)0;
  }
  stream->descriptor = descriptor;
  stream->error = 0;
  stream->owned = 1;
  return stream;
}

FILE *fopen(const char *path, const char *mode) {
  int flags;
  if (path == (const char *)0 || mode == (const char *)0) {
    errno = CUPID_LINUX_EINVAL;
    return (FILE *)0;
  }
  if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) {
    flags = CUPID_LINUX_O_RDONLY;
  } else if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0) {
    flags = CUPID_LINUX_O_WRONLY | CUPID_LINUX_O_CREAT |
            CUPID_LINUX_O_TRUNC;
  } else if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0) {
    flags = CUPID_LINUX_O_WRONLY | CUPID_LINUX_O_CREAT |
            CUPID_LINUX_O_APPEND;
  } else {
    errno = CUPID_LINUX_EINVAL;
    return (FILE *)0;
  }
  return cupid_stdio_open(path, flags);
}

int fclose(FILE *stream) {
  int result;
  if (cupid_stdio_bad_stream(stream)) {
    return -1;
  }
  if (stream->owned == 0) {
    return 0;
  }
  result = cupid_linux_syscall1(CUPID_LINUX_SYS_CLOSE,
                                (unsigned int)stream->descriptor);
  stream->descriptor = -1;
  free(stream);
  if (cupid_runtime_syscall_failed(result)) {
    (void)cupid_runtime_syscall_error(result);
    return -1;
  }
  return 0;
}

int fflush(FILE *stream) {
  if (stream == (FILE *)0) {
    return 0;
  }
  return cupid_stdio_bad_stream(stream) ? -1 : 0;
}

int ferror(FILE *stream) {
  if (stream == (FILE *)0) {
    errno = CUPID_LINUX_EBADF;
    return 1;
  }
  return stream->error;
}

static int cupid_stdio_size(FILE *stream, size_t width, size_t count,
                            size_t *size_out) {
  if (width != 0u && count > CUPID_RUNTIME_UINT_MAX / width) {
    errno = CUPID_LINUX_EOVERFLOW;
    stream->error = 1;
    return 0;
  }
  *size_out = width * count;
  return 1;
}

size_t fread(void *destination, size_t width, size_t count, FILE *stream) {
  unsigned char *bytes = (unsigned char *)destination;
  size_t requested;
  size_t total = 0u;
  if (cupid_stdio_bad_stream(stream)) {
    return 0u;
  }
  if (!cupid_stdio_size(stream, width, count, &requested)) {
    return 0u;
  }
  if (requested == 0u) {
    return 0u;
  }
  if (destination == (void *)0) {
    errno = CUPID_LINUX_EINVAL;
    stream->error = 1;
    return 0u;
  }
  while (total < requested) {
    size_t remaining = requested - total;
    size_t chunk = remaining > CUPID_RUNTIME_IO_CHUNK
                       ? CUPID_RUNTIME_IO_CHUNK
                       : remaining;
    int result = cupid_linux_syscall3(
        CUPID_LINUX_SYS_READ, (unsigned int)stream->descriptor,
        (unsigned int)(bytes + total), (unsigned int)chunk);
    if (cupid_runtime_syscall_failed(result)) {
      if (cupid_runtime_syscall_error(result) == CUPID_LINUX_EINTR) {
        continue;
      }
      stream->error = 1;
      break;
    }
    if (result == 0) {
      break;
    }
    total += (size_t)result;
  }
  return total / width;
}

size_t fwrite(const void *source, size_t width, size_t count, FILE *stream) {
  const unsigned char *bytes = (const unsigned char *)source;
  size_t requested;
  size_t total = 0u;
  if (cupid_stdio_bad_stream(stream)) {
    return 0u;
  }
  if (!cupid_stdio_size(stream, width, count, &requested)) {
    return 0u;
  }
  if (requested == 0u) {
    return 0u;
  }
  if (source == (const void *)0) {
    errno = CUPID_LINUX_EINVAL;
    stream->error = 1;
    return 0u;
  }
  while (total < requested) {
    size_t remaining = requested - total;
    size_t chunk = remaining > CUPID_RUNTIME_IO_CHUNK
                       ? CUPID_RUNTIME_IO_CHUNK
                       : remaining;
    int result = cupid_linux_syscall3(
        CUPID_LINUX_SYS_WRITE, (unsigned int)stream->descriptor,
        (unsigned int)(bytes + total), (unsigned int)chunk);
    if (cupid_runtime_syscall_failed(result)) {
      if (cupid_runtime_syscall_error(result) == CUPID_LINUX_EINTR) {
        continue;
      }
      stream->error = 1;
      break;
    }
    if (result == 0) {
      errno = CUPID_LINUX_EIO;
      stream->error = 1;
      break;
    }
    total += (size_t)result;
  }
  return total / width;
}

int fseek(FILE *stream, long offset, int origin) {
  int result;
  if (cupid_stdio_bad_stream(stream)) {
    return -1;
  }
  if (origin < CUPID_LINUX_SEEK_SET || origin > SEEK_END) {
    errno = CUPID_LINUX_EINVAL;
    stream->error = 1;
    return -1;
  }
  result = cupid_linux_syscall3(
      CUPID_LINUX_SYS_LSEEK, (unsigned int)stream->descriptor,
      (unsigned int)offset, (unsigned int)origin);
  if (cupid_runtime_syscall_failed(result)) {
    (void)cupid_runtime_syscall_error(result);
    stream->error = 1;
    return -1;
  }
  return 0;
}

long ftell(FILE *stream) {
  int result;
  if (cupid_stdio_bad_stream(stream)) {
    return -1L;
  }
  result = cupid_linux_syscall3(
      CUPID_LINUX_SYS_LSEEK, (unsigned int)stream->descriptor, 0u,
      CUPID_LINUX_SEEK_CUR);
  if (cupid_runtime_syscall_failed(result)) {
    (void)cupid_runtime_syscall_error(result);
    stream->error = 1;
    return -1L;
  }
  return (long)result;
}

static int cupid_fprintf_write(FILE *stream, const char *text, size_t size,
                               int *total) {
  if (size > (size_t)(CUPID_RUNTIME_INT_MAX - *total)) {
    errno = CUPID_LINUX_EOVERFLOW;
    return 0;
  }
  if (size != 0u && fwrite(text, 1u, size, stream) != size) {
    return 0;
  }
  *total += (int)size;
  return 1;
}

static int cupid_fprintf_character(FILE *stream, char value, int *total) {
  return cupid_fprintf_write(stream, &value, 1u, total);
}

static int cupid_fprintf_padding(FILE *stream, char value, size_t count,
                                 int *total) {
  size_t index;
  for (index = 0u; index < count; index++) {
    if (!cupid_fprintf_character(stream, value, total)) {
      return 0;
    }
  }
  return 1;
}

static int cupid_fprintf_number(FILE *stream, unsigned int value,
                                unsigned int base, int uppercase,
                                int negative, size_t width, int zero_pad,
                                int *total) {
  char digits[16];
  size_t size = 0u;
  size_t padding;
  const char *alphabet =
      uppercase != 0 ? "0123456789ABCDEF" : "0123456789abcdef";
  do {
    digits[size] = alphabet[value % base];
    size++;
    value /= base;
  } while (value != 0u);
  padding = width > size + (negative != 0 ? 1u : 0u)
                ? width - size - (negative != 0 ? 1u : 0u)
                : 0u;
  if (zero_pad == 0 &&
      !cupid_fprintf_padding(stream, ' ', padding, total)) {
    return 0;
  }
  if (negative != 0 &&
      !cupid_fprintf_character(stream, '-', total)) {
    return 0;
  }
  if (zero_pad != 0 &&
      !cupid_fprintf_padding(stream, '0', padding, total)) {
    return 0;
  }
  while (size != 0u) {
    size--;
    if (!cupid_fprintf_character(stream, digits[size], total)) {
      return 0;
    }
  }
  return 1;
}

int fprintf(FILE *stream, const char *format, ...) {
  typedef __builtin_va_list cupid_va_list;
  cupid_va_list arguments;
  size_t index = 0u;
  int total = 0;
  int result = -1;
  if (cupid_stdio_bad_stream(stream) ||
      format == (const char *)0) {
    errno = CUPID_LINUX_EINVAL;
    return -1;
  }
  __builtin_va_start(arguments, format);
  while (format[index] != '\0') {
    size_t start = index;
    size_t width = 0u;
    int zero_pad = 0;
    int long_value = 0;
    char specifier;
    while (format[index] != '\0' && format[index] != '%') {
      index++;
    }
    if (index != start &&
        !cupid_fprintf_write(stream, format + start, index - start,
                             &total)) {
      goto done;
    }
    if (format[index] == '\0') {
      break;
    }
    index++;
    if (format[index] == '%') {
      if (!cupid_fprintf_character(stream, '%', &total)) {
        goto done;
      }
      index++;
      continue;
    }
    if (format[index] == '0') {
      zero_pad = 1;
      index++;
    }
    while (format[index] >= '0' && format[index] <= '9') {
      unsigned int digit = (unsigned int)(format[index] - '0');
      if (width > (size_t)CUPID_RUNTIME_INT_MAX / 10u ||
          width * 10u >
              (size_t)CUPID_RUNTIME_INT_MAX - digit) {
        errno = CUPID_LINUX_EOVERFLOW;
        goto done;
      }
      width = width * 10u + digit;
      index++;
    }
    if (format[index] == 'l') {
      long_value = 1;
      index++;
    }
    specifier = format[index];
    if (specifier == '\0') {
      errno = CUPID_LINUX_EINVAL;
      goto done;
    }
    index++;
    if (specifier == 's') {
      const char *text =
          __builtin_va_arg(arguments, const char *);
      size_t size;
      size_t padding;
      if (text == (const char *)0) {
        text = "(null)";
      }
      size = strlen(text);
      padding = width > size ? width - size : 0u;
      if (!cupid_fprintf_padding(stream, ' ', padding, &total) ||
          !cupid_fprintf_write(stream, text, size, &total)) {
        goto done;
      }
    } else if (specifier == 'c') {
      int value = __builtin_va_arg(arguments, int);
      if (width > 1u &&
          !cupid_fprintf_padding(stream, ' ', width - 1u, &total)) {
        goto done;
      }
      if (!cupid_fprintf_character(stream, (char)value, &total)) {
        goto done;
      }
    } else if (specifier == 'd' || specifier == 'i') {
      long value;
      unsigned int magnitude;
      int negative;
      if (long_value != 0) {
        value = __builtin_va_arg(arguments, long);
      } else {
        value = (long)__builtin_va_arg(arguments, int);
      }
      negative = value < 0L ? 1 : 0;
      magnitude = negative != 0
                      ? 0u - (unsigned int)value
                      : (unsigned int)value;
      if (!cupid_fprintf_number(stream, magnitude, 10u, 0, negative,
                                width, zero_pad, &total)) {
        goto done;
      }
    } else if (specifier == 'u' || specifier == 'x' ||
               specifier == 'X') {
      unsigned long value;
      unsigned int base = specifier == 'u' ? 10u : 16u;
      if (long_value != 0) {
        value = __builtin_va_arg(arguments, unsigned long);
      } else {
        value =
            (unsigned long)__builtin_va_arg(arguments, unsigned int);
      }
      if (!cupid_fprintf_number(stream, (unsigned int)value, base,
                                specifier == 'X' ? 1 : 0, 0, width,
                                zero_pad, &total)) {
        goto done;
      }
    } else {
      errno = CUPID_LINUX_EINVAL;
      goto done;
    }
  }
  result = total;

done:
  __builtin_va_end(arguments);
  return result;
}

char *getcwd(char *destination, size_t capacity) {
  int result;
  if (destination == (char *)0 || capacity == 0u) {
    errno = capacity == 0u ? ERANGE : CUPID_LINUX_EINVAL;
    return (char *)0;
  }
  result = cupid_linux_syscall2(
      CUPID_LINUX_SYS_GETCWD, (unsigned int)destination,
      (unsigned int)capacity);
  if (cupid_runtime_syscall_failed(result)) {
    (void)cupid_runtime_syscall_error(result);
    return (char *)0;
  }
  return destination;
}
