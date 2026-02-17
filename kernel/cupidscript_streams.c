#include "cupidscript_streams.h"
#include "cupidscript.h"
#include "memory.h"
#include "string.h"
#include "vfs.h"

void fd_table_init(fd_table_t *table, script_context_t *ctx) {
    // Clear all fds
    for (int i = 0; i < MAX_FDS; i++) {
        table->fds[i].type = FD_CLOSED;
    }
    table->next_fd = 3;  // 0, 1, 2 are reserved

    // Setup stdin (terminal input)
    table->fds[CS_STDIN].type = FD_TERMINAL;
    table->fds[CS_STDIN].terminal.output_fn = NULL;

    // Setup stdout (terminal output)
    table->fds[CS_STDOUT].type = FD_TERMINAL;
    table->fds[CS_STDOUT].terminal.output_fn = ctx->print_fn;

    // Setup stderr (terminal output)
    table->fds[CS_STDERR].type = FD_TERMINAL;
    table->fds[CS_STDERR].terminal.output_fn = ctx->print_fn;
}

int fd_read(fd_table_t *table, int fd, char *buf, size_t len) {
    if (fd < 0 || fd >= MAX_FDS || table->fds[fd].type == FD_CLOSED) {
        return -1;
    }

    file_descriptor_t *fdesc = &table->fds[fd];

    switch (fdesc->type) {
        case FD_BUFFER: {
            size_t available = fdesc->buffer.size - fdesc->buffer.read_pos;
            size_t to_read = (len < available) ? len : available;
            if (to_read > 0) {
                memcpy(buf, fdesc->buffer.data + fdesc->buffer.read_pos, to_read);
                fdesc->buffer.read_pos += to_read;
            }
            return (int)to_read;
        }
        case FD_FILE:
            return vfs_read(fdesc->file.vfs_fd, buf, len);
        case FD_TERMINAL:
            // For now, terminal input is not implemented via this system
            return 0;
        default:
            return -1;
    }
}

int fd_write(fd_table_t *table, int fd, const char *buf, size_t len) {
    if (fd < 0 || fd >= MAX_FDS || table->fds[fd].type == FD_CLOSED) {
        return -1;
    }

    file_descriptor_t *fdesc = &table->fds[fd];

    switch (fdesc->type) {
        case FD_BUFFER: {
            char *old_data = fdesc->buffer.data;
            size_t needed = fdesc->buffer.size + len;
            
            // Expand buffer if necessary
            if (needed > fdesc->buffer.capacity) {
                size_t new_capacity = fdesc->buffer.capacity * 2;
                if (new_capacity < needed) new_capacity = needed;
                
                char *new_data = kmalloc(new_capacity);
                if (!new_data) return -1;
                
                if (fdesc->buffer.data) {
                    memcpy(new_data, fdesc->buffer.data, fdesc->buffer.size);
                    if (fdesc->buffer.owner) {
                        kfree(fdesc->buffer.data);
                    }
                }
                fdesc->buffer.data = new_data;
                fdesc->buffer.capacity = new_capacity;
                fdesc->buffer.owner = true;

                /* If buffer moved, repoint peer FDs sharing the same pipe. */
                for (int i = 0; i < MAX_FDS; i++) {
                    if (i == fd) continue;
                    if (table->fds[i].type == FD_BUFFER &&
                        table->fds[i].buffer.data == old_data) {
                        table->fds[i].buffer.data = new_data;
                        table->fds[i].buffer.capacity = new_capacity;
                    }
                }
            }
            
            // Append data
            memcpy(fdesc->buffer.data + fdesc->buffer.size, buf, len);
            fdesc->buffer.size += len;

            /* Keep shared-pipe size in sync for all FDs using same buffer. */
            for (int i = 0; i < MAX_FDS; i++) {
                if (i == fd) continue;
                if (table->fds[i].type == FD_BUFFER &&
                    table->fds[i].buffer.data == fdesc->buffer.data) {
                    table->fds[i].buffer.size = fdesc->buffer.size;
                }
            }
            return (int)len;
        }
        case FD_FILE:
            return vfs_write(fdesc->file.vfs_fd, buf, len);
        case FD_TERMINAL:
            if (fdesc->terminal.output_fn) {
                // Create null-terminated string for output
                char *temp = kmalloc(len + 1);
                if (temp) {
                    memcpy(temp, buf, len);
                    temp[len] = '\0';
                    fdesc->terminal.output_fn(temp);
                    kfree(temp);
                }
            }
            return (int)len;
        default:
            return -1;
    }
}

void fd_close(fd_table_t *table, int fd) {
    if (fd < 0 || fd >= MAX_FDS) return;

    file_descriptor_t *fdesc = &table->fds[fd];

    switch (fdesc->type) {
        case FD_BUFFER:
            if (fdesc->buffer.data && fdesc->buffer.owner) {
                kfree(fdesc->buffer.data);
                fdesc->buffer.data = NULL;
            }
            break;
        case FD_FILE:
            vfs_close(fdesc->file.vfs_fd);
            break;
        default:
            break;
    }

    fdesc->type = FD_CLOSED;
}

int fd_dup(fd_table_t *table, int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= MAX_FDS || table->fds[oldfd].type == FD_CLOSED) {
        return -1;
    }
    if (newfd < 0 || newfd >= MAX_FDS) {
        return -1;
    }

    // Close new fd if it's open
    if (table->fds[newfd].type != FD_CLOSED) {
        fd_close(table, newfd);
    }

    // Copy fd (shallow copy for now - both fds point to same resource)
    table->fds[newfd] = table->fds[oldfd];
    if (table->fds[newfd].type == FD_BUFFER) {
        table->fds[newfd].buffer.owner = false;
    }

    return newfd;
}

int fd_create_pipe(fd_table_t *table, int *read_fd, int *write_fd) {
    // Find two free fds
    int rfd = -1, wfd = -1;

    for (int i = 3; i < MAX_FDS; i++) {
        if (table->fds[i].type == FD_CLOSED) {
            if (rfd == -1) {
                rfd = i;
            } else if (wfd == -1) {
                wfd = i;
                break;
            }
        }
    }

    if (rfd == -1 || wfd == -1) {
        return -1;  // No free fds
    }

    // Create shared buffer (4KB default)
    char *pipe_data = kmalloc(4096);
    if (!pipe_data) return -1;

    // Setup read end
    table->fds[rfd].type = FD_BUFFER;
    table->fds[rfd].buffer.data = pipe_data;
    table->fds[rfd].buffer.size = 0;
    table->fds[rfd].buffer.read_pos = 0;
    table->fds[rfd].buffer.capacity = 4096;
    table->fds[rfd].buffer.owner = true;

    // Setup write end (same buffer)
    table->fds[wfd].type = FD_BUFFER;
    table->fds[wfd].buffer.data = pipe_data;
    table->fds[wfd].buffer.size = 0;
    table->fds[wfd].buffer.read_pos = 0;
    table->fds[wfd].buffer.capacity = 4096;
    table->fds[wfd].buffer.owner = false;

    *read_fd = rfd;
    *write_fd = wfd;

    return 0;
}

int fd_create_buffer(fd_table_t *table, size_t capacity) {
    // Find free fd
    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++) {
        if (table->fds[i].type == FD_CLOSED) {
            fd = i;
            break;
        }
    }

    if (fd == -1) return -1;

    // Create buffer
    char *data = kmalloc(capacity);
    if (!data) return -1;

    table->fds[fd].type = FD_BUFFER;
    table->fds[fd].buffer.data = data;
    table->fds[fd].buffer.size = 0;
    table->fds[fd].buffer.read_pos = 0;
    table->fds[fd].buffer.capacity = capacity;
    table->fds[fd].buffer.owner = true;

    return fd;
}

char *fd_get_buffer_contents(fd_table_t *table, int fd) {
    if (fd < 0 || fd >= MAX_FDS) {
        return NULL;
    }

    file_descriptor_t *fdesc = &table->fds[fd];

    if (fdesc->type == FD_BUFFER) {
        /* Allocate new string with null terminator */
        char *result = kmalloc(fdesc->buffer.size + 1);
        if (!result) return NULL;

        memcpy(result, fdesc->buffer.data, fdesc->buffer.size);
        result[fdesc->buffer.size] = '\0';
        return result;
    }

    if (fdesc->type == FD_FILE) {
        /* Read file contents from current offset into a bounded buffer. */
        char *result = kmalloc(1025);
        if (!result) return NULL;

        int total = 0;
        while (total < 1024) {
            int r = fd_read(table, fd, result + total, (size_t)(1024 - total));
            if (r <= 0) break;
            total += r;
        }
        result[total] = '\0';
        return result;
    }

    return NULL;
}

int fd_open_file(fd_table_t *table, const char *filename, int flags) {
    // Find free fd
    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++) {
        if (table->fds[i].type == FD_CLOSED) {
            fd = i;
            break;
        }
    }

    if (fd == -1) return -1;

    // Open VFS file
    int vfs_fd = vfs_open(filename, (uint32_t)flags);
    if (vfs_fd < 0) return -1;

    table->fds[fd].type = FD_FILE;
    table->fds[fd].file.vfs_fd = vfs_fd;

    return fd;
}
