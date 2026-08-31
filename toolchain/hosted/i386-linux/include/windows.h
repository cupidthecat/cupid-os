#ifndef CUPID_HOSTED_I386_WINDOWS_H
#define CUPID_HOSTED_I386_WINDOWS_H

#include <cupid_host_abi.h>

typedef unsigned int BOOL;
typedef unsigned int DWORD;
typedef unsigned int HANDLE;
typedef unsigned int SIZE_T;
typedef unsigned short WORD;
typedef void *LPOVERLAPPED;
typedef void *LPPROC_THREAD_ATTRIBUTE_LIST;
typedef void *LPSECURITY_ATTRIBUTES;

typedef struct {
  DWORD nLength;
  void *lpSecurityDescriptor;
  BOOL bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME;

typedef struct {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD dwVolumeSerialNumber;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD nNumberOfLinks;
  DWORD nFileIndexHigh;
  DWORD nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION;

typedef struct {
  DWORD cb;
  char *lpReserved;
  char *lpDesktop;
  char *lpTitle;
  DWORD dwX;
  DWORD dwY;
  DWORD dwXSize;
  DWORD dwYSize;
  DWORD dwXCountChars;
  DWORD dwYCountChars;
  DWORD dwFillAttribute;
  DWORD dwFlags;
  WORD wShowWindow;
  WORD cbReserved2;
  unsigned char *lpReserved2;
  HANDLE hStdInput;
  HANDLE hStdOutput;
  HANDLE hStdError;
} STARTUPINFOA;

typedef struct {
  STARTUPINFOA StartupInfo;
  LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList;
} STARTUPINFOEXA;

typedef struct {
  HANDLE hProcess;
  HANDLE hThread;
  DWORD dwProcessId;
  DWORD dwThreadId;
} PROCESS_INFORMATION;

typedef struct {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  char cFileName[260];
  char cAlternateFileName[14];
} WIN32_FIND_DATAA;

#define GENERIC_WRITE 0x40000000u
#define GENERIC_READ 0x80000000u
#define FILE_READ_ATTRIBUTES 0x00000080u
#define FILE_TRAVERSE 0x00000020u
#define FILE_ADD_FILE 0x00000002u
#define FILE_ADD_SUBDIRECTORY 0x00000004u
#define FILE_DELETE_CHILD 0x00000040u
#define DELETE 0x00010000u
#define SYNCHRONIZE 0x00100000u
#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u
#define FILE_SHARE_DELETE 0x00000004u
#define CREATE_NEW 1u
#define OPEN_EXISTING 3u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define FILE_ATTRIBUTE_DEVICE 0x00000040u
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400u
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000u
#define FILE_FLAG_OPEN_REPARSE_POINT 0x00200000u
#define FILE_BEGIN 0u
#define HANDLE_FLAG_INHERIT 0x00000001u
#define INVALID_FILE_ATTRIBUTES 0xffffffffu
#define INVALID_HANDLE_VALUE ((HANDLE)0xffffffffu)
#define MOVEFILE_REPLACE_EXISTING 0x00000001u
#define MOVEFILE_WRITE_THROUGH 0x00000008u
#define PROCESS_QUERY_LIMITED_INFORMATION 0x00001000u
#define STILL_ACTIVE 259u
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define INFINITE 0xffffffffu
#define STARTF_USESTDHANDLES 0x00000100u
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000u
#define PROC_THREAD_ATTRIBUTE_HANDLE_LIST 0x00020002u
#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_PATH_NOT_FOUND 3u
#define ERROR_ACCESS_DENIED 5u
#define ERROR_NO_MORE_FILES 18u
#define ERROR_SHARING_VIOLATION 32u
#define ERROR_INVALID_PARAMETER 87u

unsigned int cupid_windows_close_handle(unsigned int handle);
unsigned int cupid_windows_create_file(
    const char *path, unsigned int access, unsigned int sharing,
    void *security, unsigned int creation, unsigned int attributes,
    unsigned int template_handle);
unsigned int cupid_windows_delete_file(const char *path);
unsigned int cupid_windows_create_directory(const char *path, void *security);
unsigned int cupid_windows_create_process(
    const char *application, char *command_line, void *process_security,
    void *thread_security, unsigned int inherit_handles,
    unsigned int creation_flags, void *environment,
    const char *current_directory, STARTUPINFOA *startup,
    PROCESS_INFORMATION *process);
void cupid_windows_delete_proc_thread_attribute_list(
    LPPROC_THREAD_ATTRIBUTE_LIST attributes);
unsigned int cupid_windows_flush_file_buffers(unsigned int handle);
unsigned int cupid_windows_find_close(unsigned int handle);
unsigned int cupid_windows_find_first_file(const char *pattern,
                                           WIN32_FIND_DATAA *entry);
unsigned int cupid_windows_find_next_file(unsigned int handle,
                                          WIN32_FIND_DATAA *entry);
unsigned int cupid_windows_get_current_process_id(void);
unsigned int cupid_windows_get_exit_code_process(unsigned int process,
                                                 unsigned int *status);
unsigned int cupid_windows_get_file_attributes(const char *path);
unsigned int cupid_windows_get_file_information(
    unsigned int handle, BY_HANDLE_FILE_INFORMATION *information);
unsigned int cupid_windows_get_full_path_name(
    const char *path, unsigned int capacity, char *destination,
    char **file_part);
unsigned int cupid_windows_get_last_error(void);
unsigned int cupid_windows_initialize_proc_thread_attribute_list(
    LPPROC_THREAD_ATTRIBUTE_LIST attributes, unsigned int count,
    unsigned int flags, SIZE_T *size);
unsigned int cupid_windows_move_file_ex(
    const char *source, const char *destination, unsigned int flags);
long cupid_windows_nt_set_information_file(
    unsigned int file, void *status, void *information,
    unsigned long length, unsigned int information_class);
long cupid_windows_nt_create_file(
    unsigned int *file, unsigned long access, void *attributes,
    void *status, void *allocation_size, unsigned long file_attributes,
    unsigned long sharing, unsigned long disposition,
    unsigned long options, void *extended, unsigned long extended_size);
long cupid_windows_nt_query_directory_file(
    unsigned int file, unsigned int event, void *apc_routine,
    void *apc_context, void *status, void *information,
    unsigned long length, unsigned int information_class,
    unsigned char return_single_entry, void *file_name,
    unsigned char restart_scan);
unsigned int cupid_windows_open_process(unsigned int access,
                                        unsigned int inherit_handle,
                                        unsigned int process_id);
unsigned int cupid_windows_read_file(unsigned int handle, void *destination,
                                     unsigned int bytes,
                                     unsigned int *read_out,
                                     void *overlapped);
unsigned int cupid_windows_remove_directory(const char *path);
unsigned int cupid_windows_set_file_pointer(
    unsigned int handle, int distance, int *distance_high,
    unsigned int method);
unsigned int cupid_windows_set_handle_information(
    unsigned int handle, unsigned int mask, unsigned int flags);
unsigned int cupid_windows_terminate_process(unsigned int process,
                                             unsigned int exit_code);
unsigned int cupid_windows_update_proc_thread_attribute(
    LPPROC_THREAD_ATTRIBUTE_LIST attributes, unsigned int flags,
    unsigned int attribute, void *value, SIZE_T value_size,
    void *previous_value, SIZE_T *return_size);
unsigned int cupid_windows_wait_for_single_object(unsigned int handle,
                                                   unsigned int milliseconds);
unsigned int cupid_windows_write_file(
    unsigned int handle, const void *source, unsigned int bytes,
    unsigned int *written, void *overlapped);

#define CloseHandle cupid_windows_close_handle
#define CreateDirectoryA cupid_windows_create_directory
#define CreateFileA cupid_windows_create_file
#define CreateProcessA cupid_windows_create_process
#define DeleteFileA cupid_windows_delete_file
#define DeleteProcThreadAttributeList cupid_windows_delete_proc_thread_attribute_list
#define FlushFileBuffers cupid_windows_flush_file_buffers
#define FindClose cupid_windows_find_close
#define FindFirstFileA cupid_windows_find_first_file
#define FindNextFileA cupid_windows_find_next_file
#define GetCurrentProcessId cupid_windows_get_current_process_id
#define GetExitCodeProcess cupid_windows_get_exit_code_process
#define GetFileAttributesA cupid_windows_get_file_attributes
#define GetFileInformationByHandle cupid_windows_get_file_information
#define GetFullPathNameA cupid_windows_get_full_path_name
#define GetLastError cupid_windows_get_last_error
#define InitializeProcThreadAttributeList cupid_windows_initialize_proc_thread_attribute_list
#define MoveFileExA cupid_windows_move_file_ex
#define OpenProcess cupid_windows_open_process
#define ReadFile cupid_windows_read_file
#define RemoveDirectoryA cupid_windows_remove_directory
#define SetFilePointer cupid_windows_set_file_pointer
#define SetHandleInformation cupid_windows_set_handle_information
#define TerminateProcess cupid_windows_terminate_process
#define UpdateProcThreadAttribute cupid_windows_update_proc_thread_attribute
#define WaitForSingleObject cupid_windows_wait_for_single_object
#define WriteFile cupid_windows_write_file

#endif
