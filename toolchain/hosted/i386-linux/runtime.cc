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

typedef __builtin_va_list cupid_va_list;

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

void *memmove(void *destination, const void *source, size_t bytes) {
  unsigned char *target = (unsigned char *)destination;
  const unsigned char *input = (const unsigned char *)source;
  unsigned int target_address = (unsigned int)target;
  unsigned int input_address = (unsigned int)input;
  size_t index;
  if (target_address <= input_address ||
      target_address - input_address >= bytes) {
    for (index = 0u; index < bytes; index++) {
      target[index] = input[index];
    }
  } else {
    index = bytes;
    while (index != 0u) {
      index--;
      target[index] = input[index];
    }
  }
  return destination;
}

int memcmp(const void *left, const void *right, size_t bytes) {
  const unsigned char *left_bytes = (const unsigned char *)left;
  const unsigned char *right_bytes = (const unsigned char *)right;
  size_t index;
  for (index = 0u; index < bytes; index++) {
    if (left_bytes[index] != right_bytes[index]) {
      return (int)left_bytes[index] - (int)right_bytes[index];
    }
  }
  return 0;
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

char *strstr(const char *text, const char *needle) {
  size_t needle_size = strlen(needle);
  size_t index = 0u;
  if (needle_size == 0u) {
    return (char *)text;
  }
  while (text[index] != '\0') {
    if (text[index] == needle[0] &&
        strncmp(text + index, needle, needle_size) == 0) {
      return (char *)(text + index);
    }
    index++;
  }
  return (char *)0;
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

typedef struct cupid_format_sink cupid_format_sink_t;

struct cupid_format_sink {
  FILE *stream;
  char *buffer;
  size_t capacity;
  size_t stored;
  int total;
  int buffer_mode;
};

static int cupid_format_write(cupid_format_sink_t *sink, const char *text,
                              size_t size) {
  size_t copied = 0u;
  if (size > (size_t)(CUPID_RUNTIME_INT_MAX - sink->total)) {
    errno = CUPID_LINUX_EOVERFLOW;
    return 0;
  }
  if (sink->buffer_mode == 0) {
    if (size != 0u && fwrite(text, 1u, size, sink->stream) != size) {
      return 0;
    }
  } else if (sink->capacity != 0u) {
    size_t available = sink->capacity - 1u - sink->stored;
    copied = size < available ? size : available;
    if (copied != 0u) {
      (void)memcpy(sink->buffer + sink->stored, text, copied);
      sink->stored += copied;
    }
    sink->buffer[sink->stored] = '\0';
  }
  sink->total += (int)size;
  return 1;
}

static int cupid_format_character(cupid_format_sink_t *sink, char value) {
  return cupid_format_write(sink, &value, 1u);
}

static int cupid_format_padding(cupid_format_sink_t *sink, char value,
                                size_t count) {
  size_t index;
  for (index = 0u; index < count; index++) {
    if (!cupid_format_character(sink, value)) {
      return 0;
    }
  }
  return 1;
}

static int cupid_format_number(cupid_format_sink_t *sink,
                               unsigned long long value,
                               unsigned int base,
                               int uppercase, int negative, size_t width,
                               int zero_pad) {
  char digits[32];
  size_t size = 0u;
  size_t padding;
  const char *alphabet =
      uppercase != 0 ? "0123456789ABCDEF" : "0123456789abcdef";
  do {
    digits[size] =
        alphabet[(unsigned int)(value % (unsigned long long)base)];
    size++;
    value /= (unsigned long long)base;
  } while (value != 0ULL);
  padding = width > size + (negative != 0 ? 1u : 0u)
                ? width - size - (negative != 0 ? 1u : 0u)
                : 0u;
  if (zero_pad == 0 &&
      !cupid_format_padding(sink, ' ', padding)) {
    return 0;
  }
  if (negative != 0 &&
      !cupid_format_character(sink, '-')) {
    return 0;
  }
  if (zero_pad != 0 &&
      !cupid_format_padding(sink, '0', padding)) {
    return 0;
  }
  while (size != 0u) {
    size--;
    if (!cupid_format_character(sink, digits[size])) {
      return 0;
    }
  }
  return 1;
}

static int cupid_vformat(cupid_format_sink_t *sink, const char *format,
                         cupid_va_list arguments) {
  size_t index = 0u;
  int result = -1;
  if (format == (const char *)0) {
    errno = CUPID_LINUX_EINVAL;
    return -1;
  }
  if (sink->buffer_mode == 0 &&
      cupid_stdio_bad_stream(sink->stream)) {
    return -1;
  }
  while (format[index] != '\0') {
    size_t start = index;
    size_t width = 0u;
    size_t precision = 0u;
    int zero_pad = 0;
    int precision_set = 0;
    int long_value = 0;
    char specifier;
    while (format[index] != '\0' && format[index] != '%') {
      index++;
    }
    if (index != start &&
        !cupid_format_write(sink, format + start, index - start)) {
      goto done;
    }
    if (format[index] == '\0') {
      break;
    }
    index++;
    if (format[index] == '%') {
      if (!cupid_format_character(sink, '%')) {
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
    if (format[index] == '.') {
      index++;
      if (format[index] == '*') {
        int requested_precision =
            __builtin_va_arg(arguments, int);
        index++;
        if (requested_precision >= 0) {
          precision = (size_t)requested_precision;
          precision_set = 1;
        }
      } else {
        precision_set = 1;
        while (format[index] >= '0' && format[index] <= '9') {
          unsigned int digit =
              (unsigned int)(format[index] - '0');
          if (precision >
                  (size_t)CUPID_RUNTIME_INT_MAX / 10u ||
              precision * 10u >
                  (size_t)CUPID_RUNTIME_INT_MAX - digit) {
            errno = CUPID_LINUX_EOVERFLOW;
            goto done;
          }
          precision = precision * 10u + digit;
          index++;
        }
      }
    }
    if (format[index] == 'l') {
      long_value = 1;
      index++;
      if (format[index] == 'l') {
        long_value = 2;
        index++;
      }
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
      if (long_value != 0) {
        errno = CUPID_LINUX_EINVAL;
        goto done;
      }
      if (text == (const char *)0) {
        text = "(null)";
      }
      if (precision_set != 0) {
        size = 0u;
        while (size < precision && text[size] != '\0') {
          size++;
        }
      } else {
        size = strlen(text);
      }
      padding = width > size ? width - size : 0u;
      if (!cupid_format_padding(sink, ' ', padding) ||
          !cupid_format_write(sink, text, size)) {
        goto done;
      }
    } else if (specifier == 'c') {
      int value = __builtin_va_arg(arguments, int);
      if (long_value != 0 || precision_set != 0) {
        errno = CUPID_LINUX_EINVAL;
        goto done;
      }
      if (width > 1u &&
          !cupid_format_padding(sink, ' ', width - 1u)) {
        goto done;
      }
      if (!cupid_format_character(sink, (char)value)) {
        goto done;
      }
    } else if (specifier == 'd' || specifier == 'i') {
      long long value;
      unsigned long long magnitude;
      int negative;
      if (precision_set != 0) {
        errno = CUPID_LINUX_EINVAL;
        goto done;
      }
      if (long_value == 2) {
        value = __builtin_va_arg(arguments, long long);
      } else if (long_value == 1) {
        value = (long long)__builtin_va_arg(arguments, long);
      } else {
        value = (long long)__builtin_va_arg(arguments, int);
      }
      negative = value < 0LL ? 1 : 0;
      magnitude = negative != 0
                      ? 0ULL - (unsigned long long)value
                      : (unsigned long long)value;
      if (!cupid_format_number(sink, magnitude, 10u, 0, negative,
                               width, zero_pad)) {
        goto done;
      }
    } else if (specifier == 'u' || specifier == 'x' ||
               specifier == 'X') {
      unsigned long long value;
      unsigned int base = specifier == 'u' ? 10u : 16u;
      if (precision_set != 0) {
        errno = CUPID_LINUX_EINVAL;
        goto done;
      }
      if (long_value == 2) {
        value = __builtin_va_arg(arguments, unsigned long long);
      } else if (long_value == 1) {
        value =
            (unsigned long long)__builtin_va_arg(arguments, unsigned long);
      } else {
        value =
            (unsigned long long)__builtin_va_arg(arguments, unsigned int);
      }
      if (!cupid_format_number(sink, value, base,
                               specifier == 'X' ? 1 : 0, 0, width,
                               zero_pad)) {
        goto done;
      }
    } else {
      errno = CUPID_LINUX_EINVAL;
      goto done;
    }
  }
  result = sink->total;

done:
  return result;
}

int fprintf(FILE *stream, const char *format, ...) {
  cupid_va_list arguments;
  cupid_format_sink_t sink;
  int result;
  sink.stream = stream;
  sink.buffer = (char *)0;
  sink.capacity = 0u;
  sink.stored = 0u;
  sink.total = 0;
  sink.buffer_mode = 0;
  __builtin_va_start(arguments, format);
  result = cupid_vformat(&sink, format, arguments);
  __builtin_va_end(arguments);
  return result;
}

int printf(const char *format, ...) {
  cupid_va_list arguments;
  cupid_format_sink_t sink;
  int result;
  sink.stream = stdout;
  sink.buffer = (char *)0;
  sink.capacity = 0u;
  sink.stored = 0u;
  sink.total = 0;
  sink.buffer_mode = 0;
  __builtin_va_start(arguments, format);
  result = cupid_vformat(&sink, format, arguments);
  __builtin_va_end(arguments);
  return result;
}

int fputc(int character, FILE *stream) {
  unsigned char byte = (unsigned char)character;
  return fwrite(&byte, 1u, 1u, stream) == 1u ? (int)byte : EOF;
}

int fputs(const char *text, FILE *stream) {
  size_t size;
  if (text == (const char *)0) {
    errno = CUPID_LINUX_EINVAL;
    if (stream != (FILE *)0) {
      stream->error = 1;
    }
    return EOF;
  }
  if (cupid_stdio_bad_stream(stream)) {
    return EOF;
  }
  size = strlen(text);
  return size == 0u || fwrite(text, 1u, size, stream) == size ? 0 : EOF;
}

int puts(const char *text) {
  cupid_format_sink_t sink;
  if (text == (const char *)0) {
    errno = CUPID_LINUX_EINVAL;
    return -1;
  }
  sink.stream = stdout;
  sink.buffer = (char *)0;
  sink.capacity = 0u;
  sink.stored = 0u;
  sink.total = 0;
  sink.buffer_mode = 0;
  if (!cupid_format_write(&sink, text, strlen(text)) ||
      !cupid_format_character(&sink, '\n')) {
    return -1;
  }
  return sink.total;
}

int snprintf(char *destination, size_t capacity, const char *format, ...) {
  cupid_va_list arguments;
  cupid_format_sink_t sink;
  int result;
  if (capacity != 0u && destination == (char *)0) {
    errno = CUPID_LINUX_EINVAL;
    return -1;
  }
  sink.stream = (FILE *)0;
  sink.buffer = destination;
  sink.capacity = capacity;
  sink.stored = 0u;
  sink.total = 0;
  sink.buffer_mode = 1;
  if (capacity != 0u) {
    destination[0] = '\0';
  }
  __builtin_va_start(arguments, format);
  result = cupid_vformat(&sink, format, arguments);
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
