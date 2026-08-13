[BITS 32]

extern cupid_windows_runtime_start
extern __imp_CloseHandle
extern __imp_CreateFileA
extern __imp_ExitProcess
extern __imp_GetCommandLineA
extern __imp_GetCurrentDirectoryA
extern __imp_GetLastError
extern __imp_GetStdHandle
extern __imp_ReadFile
extern __imp_SetFilePointer
extern __imp_VirtualAlloc
extern __imp_VirtualFree
extern __imp_WriteFile

global _start
global cupid_windows_close_handle
global cupid_windows_create_file
global cupid_windows_get_current_directory
global cupid_windows_get_last_error
global cupid_windows_get_std_handle
global cupid_windows_read_file
global cupid_windows_set_file_pointer
global cupid_windows_virtual_alloc
global cupid_windows_virtual_free
global cupid_windows_write_file

section .text

_start:
 cld
 and esp, 0xfffffff0
 call dword [__imp_GetCommandLineA]
 sub esp, 12
 push eax
 call cupid_windows_runtime_start
 add esp, 16
 sub esp, 12
 push eax
 call dword [__imp_ExitProcess]
 hlt

cupid_windows_close_handle:
 push ebp
 mov ebp, esp
 push dword [ebp + 8]
 call dword [__imp_CloseHandle]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_create_file:
 push ebp
 mov ebp, esp
 push dword [ebp + 32]
 push dword [ebp + 28]
 push dword [ebp + 24]
 push dword [ebp + 20]
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_CreateFileA]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_get_current_directory:
 push ebp
 mov ebp, esp
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_GetCurrentDirectoryA]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_get_last_error:
 call dword [__imp_GetLastError]
 ret

cupid_windows_get_std_handle:
 push ebp
 mov ebp, esp
 push dword [ebp + 8]
 call dword [__imp_GetStdHandle]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_read_file:
 push ebp
 mov ebp, esp
 push dword [ebp + 24]
 push dword [ebp + 20]
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_ReadFile]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_set_file_pointer:
 push ebp
 mov ebp, esp
 push dword [ebp + 20]
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_SetFilePointer]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_virtual_alloc:
 push ebp
 mov ebp, esp
 push dword [ebp + 20]
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_VirtualAlloc]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_virtual_free:
 push ebp
 mov ebp, esp
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_VirtualFree]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_write_file:
 push ebp
 mov ebp, esp
 push dword [ebp + 24]
 push dword [ebp + 20]
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_WriteFile]
 mov esp, ebp
 pop ebp
 ret
