/**
 * vfs_helpers.c — High-level VFS convenience functions
 *
 * Simple read/write-all wrappers around the VFS layer so callers
 * don't need to manually open / read-loop / close.
 */

#include "vfs_helpers.h"
#include "vfs.h"
#include "string.h"

/* vfs_read_all */

int vfs_read_all(const char *path, void *buffer, uint32_t max_size) {
  if (!path || !buffer)
    return VFS_EINVAL;

  /* Check file size first */
  vfs_stat_t st;
  int rc = vfs_stat(path, &st);
  if (rc < 0)
    return rc;

  if (st.size > max_size)
    return VFS_ENOSPC;

  int fd = vfs_open(path, O_RDONLY);
  if (fd < 0)
    return fd;

  uint32_t total = 0;
  while (total < st.size) {
    uint32_t chunk = st.size - total;
    if (chunk > 512)
      chunk = 512;
    int r = vfs_read(fd, (char *)buffer + total, chunk);
    if (r < 0) {
      vfs_close(fd);
      return r;
    }
    if (r == 0)
      break;
    total += (uint32_t)r;
  }

  vfs_close(fd);
  return (int)total;
}

/* vfs_write_all */

int vfs_write_all(const char *path, const void *buffer, uint32_t size) {
  if (!path || (!buffer && size > 0))
    return VFS_EINVAL;

  int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0)
    return fd;

  uint32_t total = 0;
  while (total < size) {
    uint32_t chunk = size - total;
    if (chunk > 512)
      chunk = 512;
    int w = vfs_write(fd, (const char *)buffer + total, chunk);
    if (w < 0) {
      vfs_close(fd);
      return w;
    }
    if (w == 0)
      break;
    total += (uint32_t)w;
  }

  vfs_close(fd);
  return (int)total;
}

/* vfs_read_text */

int vfs_read_text(const char *path, char *buffer, uint32_t max_size) {
  if (!path || !buffer || max_size == 0)
    return VFS_EINVAL;

  /* Reserve one byte for null terminator */
  int r = vfs_read_all(path, buffer, max_size - 1);
  if (r < 0)
    return r;

  buffer[r] = '\0';
  return r;
}

/* vfs_write_text */

int vfs_write_text(const char *path, const char *text) {
  if (!path || !text)
    return VFS_EINVAL;

  uint32_t len = (uint32_t)strlen(text);
  return vfs_write_all(path, text, len);
}

/* vfs_copy_file */

int vfs_copy_file(const char *src, const char *dest) {
  if (!src || !dest)
    return VFS_EINVAL;

  /* Get source file size */
  vfs_stat_t st;
  int rc = vfs_stat(src, &st);
  if (rc < 0)
    return rc;

  int src_fd = vfs_open(src, O_RDONLY);
  if (src_fd < 0)
    return src_fd;

  int dst_fd = vfs_open(dest, O_WRONLY | O_CREAT | O_TRUNC);
  if (dst_fd < 0) {
    vfs_close(src_fd);
    return dst_fd;
  }

  char buf[512];
  uint32_t total = 0;
  while (total < st.size) {
    uint32_t chunk = st.size - total;
    if (chunk > 512)
      chunk = 512;
    int r = vfs_read(src_fd, buf, chunk);
    if (r < 0) {
      vfs_close(src_fd);
      vfs_close(dst_fd);
      return r;
    }
    if (r == 0)
      break;
    int w = vfs_write(dst_fd, buf, (uint32_t)r);
    if (w < 0) {
      vfs_close(src_fd);
      vfs_close(dst_fd);
      return w;
    }
    total += (uint32_t)r;
  }

  vfs_close(src_fd);
  vfs_close(dst_fd);
  return (int)total;
}
