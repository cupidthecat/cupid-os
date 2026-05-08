/*
 * cupidscript_streams.h - Stream and file descriptor system for CupidScript
 *
 * Provides a file descriptor table with support for terminal I/O,
 * in-memory buffers (pipes), and VFS file access.
 */
#ifndef CUPIDSCRIPT_STREAMS_H
#define CUPIDSCRIPT_STREAMS_H

#include "types.h"

/* Forward declaration */
typedef struct script_context script_context_t;

/* Constants */
#define MAX_FDS   16
#define CS_STDIN  0
#define CS_STDOUT 1
#define CS_STDERR 2

/* File descriptor types */
typedef enum {
    FD_CLOSED,
    FD_BUFFER,      /* In-memory buffer (pipes, command substitution) */
    FD_FILE,        /* VFS file */
    FD_TERMINAL     /* Terminal input/output */
} fd_type_t;

/* File descriptor */
typedef struct {
    fd_type_t type;
    union {
        struct {
            char  *data;
            size_t size;
            size_t read_pos;
            size_t capacity;
            bool   owner;
        } buffer;
        struct {
            int vfs_fd;
        } file;
        struct {
            void (*output_fn)(const char *);
        } terminal;
    };
} file_descriptor_t;

/* File descriptor table */
typedef struct {
    file_descriptor_t fds[MAX_FDS];
    int next_fd;
} fd_table_t;

/* Pipeline / redirection structures */

#define MAX_PIPELINE_CMDS 8

typedef struct {
    char *filename;     /* Target file (NULL for fd-to-fd) */
    int   source_fd;    /* Which fd to redirect (1=stdout, 2=stderr) */
    int   target_fd;    /* Destination fd (-1 for file, else fd number) */
    bool  append;       /* >> vs > */
} redirection_t;

#define MAX_REDIRECTIONS 4

typedef struct {
    char          command[256];
    redirection_t redirections[MAX_REDIRECTIONS];
    int           redir_count;
} pipeline_command_t;

typedef struct {
    pipeline_command_t commands[MAX_PIPELINE_CMDS];
    int  command_count;
    bool background;    /* True if ends with & */
} pipeline_t;

/* Public API */

/* Initialize fd_table with default stdin/stdout/stderr */
void fd_table_init(fd_table_t *table, script_context_t *ctx);

/* Read from fd */
int fd_read(fd_table_t *table, int fd, char *buf, size_t len);

/* Write to fd */
int fd_write(fd_table_t *table, int fd, const char *buf, size_t len);

/* Close and free resources */
void fd_close(fd_table_t *table, int fd);

/* Duplicate fd (for redirection: 2>&1) */
int fd_dup(fd_table_t *table, int oldfd, int newfd);

/* Create pipe (returns two fds: read end and write end) */
int fd_create_pipe(fd_table_t *table, int *read_fd, int *write_fd);

/* Create in-memory buffer */
int fd_create_buffer(fd_table_t *table, size_t capacity);

/* Get buffer contents as a null-terminated string (caller must free) */
char *fd_get_buffer_contents(fd_table_t *table, int fd);

/* Open a VFS file and return an fd */
int fd_open_file(fd_table_t *table, const char *filename, int flags);

#endif /* CUPIDSCRIPT_STREAMS_H */
