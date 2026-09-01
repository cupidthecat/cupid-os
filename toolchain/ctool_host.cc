#include "ctool_host.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define CTOOL_HOST_U32_MAX 4294967295u

static FILE *ctool_host_fopen(const char *path, const char *mode) {
#if defined(_WIN32)
  FILE *file = (FILE *)0;
  if (fopen_s(&file, path, mode) != 0) {
    return (FILE *)0;
  }
  return file;
#else
  return fopen(path, mode);
#endif
}

static void *ctool_host_allocate(void *context, ctool_u32 bytes) {
  (void)context;
  return malloc((size_t)bytes);
}

static void ctool_host_release(void *context, void *allocation,
                               ctool_u32 bytes) {
  (void)context;
  (void)bytes;
  free(allocation);
}

static ctool_status_t ctool_host_native_path(ctool_host_adapter_t *adapter,
                                             ctool_string_t logical_path,
                                             char **native_out) {
  ctool_u32 logical_start = 0u;
  ctool_u32 separator = 0u;
  ctool_u32 size;
  ctool_u32 index;
  char *native;
  if (adapter == (ctool_host_adapter_t *)0 || native_out == (char **)0 ||
      logical_path.data == (const char *)0 || logical_path.size == 0u ||
      logical_path.data[0] != '/') {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *native_out = (char *)0;
  logical_start = 1u;
  if (adapter->root.size != 0u &&
      adapter->root.data[adapter->root.size - 1u] != '/' &&
      adapter->root.data[adapter->root.size - 1u] != '\\' &&
      logical_path.size > 1u) {
    separator = 1u;
  }
  if (adapter->root.size > CTOOL_HOST_U32_MAX - separator ||
      adapter->root.size + separator >
          CTOOL_HOST_U32_MAX - (logical_path.size - logical_start) - 1u) {
    return CTOOL_ERR_OVERFLOW;
  }
  size = adapter->root.size + separator +
         (logical_path.size - logical_start) + 1u;
  native = (char *)malloc((size_t)size);
  if (native == (char *)0) {
    return CTOOL_ERR_NO_MEMORY;
  }
  for (index = 0u; index < adapter->root.size; index++) {
    native[index] = adapter->root.data[index];
  }
  size = adapter->root.size;
  if (separator != 0u) {
    native[size] = '/';
    size++;
  }
  for (index = logical_start; index < logical_path.size; index++) {
    char value = logical_path.data[index];
#if defined(_WIN32)
    if (value == '/') {
      value = '\\';
    }
#endif
    native[size] = value;
    size++;
  }
  native[size] = '\0';
  *native_out = native;
  return CTOOL_OK;
}

static ctool_status_t ctool_host_open_error(void) {
  return errno == ENOENT ? CTOOL_ERR_NOT_FOUND : CTOOL_ERR_IO;
}

static ctool_status_t ctool_host_file_size(void *context,
                                           ctool_string_t logical_path,
                                           ctool_u32 *size_out) {
  ctool_host_adapter_t *adapter = (ctool_host_adapter_t *)context;
  char *native;
  FILE *file;
  long length;
  ctool_status_t status;
  if (size_out == (ctool_u32 *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *size_out = 0u;
  status = ctool_host_native_path(adapter, logical_path, &native);
  if (status != CTOOL_OK) {
    return status;
  }
  file = ctool_host_fopen(native, "rb");
  free(native);
  if (file == (FILE *)0) {
    return ctool_host_open_error();
  }
  if (fseek(file, 0L, SEEK_END) != 0) {
    (void)fclose(file);
    return CTOOL_ERR_IO;
  }
  length = ftell(file);
  if (length < 0L || (unsigned long)length > (unsigned long)CTOOL_HOST_U32_MAX) {
    (void)fclose(file);
    return CTOOL_ERR_LIMIT;
  }
  if (fclose(file) != 0) {
    return CTOOL_ERR_IO;
  }
  *size_out = (ctool_u32)(unsigned long)length;
  return CTOOL_OK;
}

static ctool_status_t ctool_host_read_exact(void *context,
                                            ctool_string_t logical_path,
                                            ctool_u8 *destination,
                                            ctool_u32 size) {
  ctool_host_adapter_t *adapter = (ctool_host_adapter_t *)context;
  char *native;
  FILE *file;
  ctool_u32 total = 0u;
  ctool_status_t status;
  if (destination == (ctool_u8 *)0 && size != 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  status = ctool_host_native_path(adapter, logical_path, &native);
  if (status != CTOOL_OK) {
    return status;
  }
  file = ctool_host_fopen(native, "rb");
  free(native);
  if (file == (FILE *)0) {
    return ctool_host_open_error();
  }
  while (total < size) {
    size_t count = fread(destination + total, 1u, (size_t)(size - total), file);
    if (count == 0u) {
      (void)fclose(file);
      return CTOOL_ERR_IO;
    }
    total += (ctool_u32)count;
  }
  if (fclose(file) != 0) {
    return CTOOL_ERR_IO;
  }
  return CTOOL_OK;
}

static ctool_status_t ctool_host_write_all(void *context,
                                           ctool_string_t logical_path,
                                           ctool_bytes_t contents) {
  ctool_host_adapter_t *adapter = (ctool_host_adapter_t *)context;
  char *native;
  FILE *file;
  ctool_u32 total = 0u;
  ctool_status_t status;
  if (contents.data == (const ctool_u8 *)0 && contents.size != 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  status = ctool_host_native_path(adapter, logical_path, &native);
  if (status != CTOOL_OK) {
    return status;
  }
  file = ctool_host_fopen(native, "wb");
  free(native);
  if (file == (FILE *)0) {
    return CTOOL_ERR_IO;
  }
  while (total < contents.size) {
    size_t count = fwrite(contents.data + total, 1u,
                          (size_t)(contents.size - total), file);
    if (count == 0u) {
      (void)fclose(file);
      return CTOOL_ERR_IO;
    }
    total += (ctool_u32)count;
  }
  if (fclose(file) != 0) {
    return CTOOL_ERR_IO;
  }
  return CTOOL_OK;
}

static ctool_status_t ctool_host_write_text(void *context,
                                            ctool_bytes_t text) {
  FILE *stream = (FILE *)context;
  ctool_u32 total = 0u;
  if (stream == (FILE *)0 ||
      (text.data == (const ctool_u8 *)0 && text.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  while (total < text.size) {
    size_t count =
        fwrite(text.data + total, 1u, (size_t)(text.size - total), stream);
    if (count == 0u) {
      return CTOOL_ERR_IO;
    }
    total += (ctool_u32)count;
  }
  return CTOOL_OK;
}

ctool_status_t ctool_host_adapter_init(ctool_host_adapter_t *adapter,
                                       const char *native_root) {
  ctool_u32 size = 0u;
  if (adapter == (ctool_host_adapter_t *)0 || native_root == (const char *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  while (size != CTOOL_HOST_U32_MAX && native_root[size] != '\0') {
    size++;
  }
  if (size == CTOOL_HOST_U32_MAX) {
    return CTOOL_ERR_LIMIT;
  }
  adapter->root.data = native_root;
  adapter->root.size = size;
  return CTOOL_OK;
}

ctool_job_config_t ctool_host_job_config(ctool_host_adapter_t *adapter,
                                         ctool_limits_t limits) {
  ctool_job_config_t config;
  config.allocator.context = (void *)0;
  config.allocator.allocate = ctool_host_allocate;
  config.allocator.release = ctool_host_release;
  config.files.context = adapter;
  config.files.file_size = ctool_host_file_size;
  config.files.read_exact = ctool_host_read_exact;
  config.files.write_all = ctool_host_write_all;
  config.diagnostics.context = stderr;
  config.diagnostics.write = ctool_host_write_text;
  config.limits = limits;
  return config;
}
