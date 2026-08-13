#ifndef CUPID_HOSTED_I386_WINDOWS_H
#define CUPID_HOSTED_I386_WINDOWS_H

#include <cupid_host_abi.h>

typedef unsigned int BOOL;
typedef unsigned int DWORD;
typedef unsigned int HANDLE;
typedef void *LPOVERLAPPED;
typedef void *LPSECURITY_ATTRIBUTES;

#define GENERIC_WRITE 0x40000000u
#define CREATE_NEW 1u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define INVALID_HANDLE_VALUE ((HANDLE)0xffffffffu)
#define MOVEFILE_REPLACE_EXISTING 0x00000001u
#define MOVEFILE_WRITE_THROUGH 0x00000008u

unsigned int cupid_windows_close_handle(unsigned int handle);
unsigned int cupid_windows_create_file(
    const char *path, unsigned int access, unsigned int sharing,
    void *security, unsigned int creation, unsigned int attributes,
    unsigned int template_handle);
unsigned int cupid_windows_delete_file(const char *path);
unsigned int cupid_windows_flush_file_buffers(unsigned int handle);
unsigned int cupid_windows_get_full_path_name(
    const char *path, unsigned int capacity, char *destination,
    char **file_part);
unsigned int cupid_windows_get_last_error(void);
unsigned int cupid_windows_move_file_ex(
    const char *source, const char *destination, unsigned int flags);
unsigned int cupid_windows_write_file(
    unsigned int handle, const void *source, unsigned int bytes,
    unsigned int *written, void *overlapped);

#define CloseHandle cupid_windows_close_handle
#define CreateFileA cupid_windows_create_file
#define DeleteFileA cupid_windows_delete_file
#define FlushFileBuffers cupid_windows_flush_file_buffers
#define GetFullPathNameA cupid_windows_get_full_path_name
#define GetLastError cupid_windows_get_last_error
#define MoveFileExA cupid_windows_move_file_ex
#define WriteFile cupid_windows_write_file

#endif
