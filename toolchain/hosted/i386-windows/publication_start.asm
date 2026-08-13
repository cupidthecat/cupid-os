[BITS 32]

extern __imp_DeleteFileA
extern __imp_FlushFileBuffers
extern __imp_GetFullPathNameA
extern __imp_MoveFileExA

global cupid_windows_delete_file
global cupid_windows_flush_file_buffers
global cupid_windows_get_full_path_name
global cupid_windows_move_file_ex

section .text

cupid_windows_delete_file:
 push ebp
 mov ebp, esp
 push dword [ebp + 8]
 call dword [__imp_DeleteFileA]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_flush_file_buffers:
 push ebp
 mov ebp, esp
 push dword [ebp + 8]
 call dword [__imp_FlushFileBuffers]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_get_full_path_name:
 push ebp
 mov ebp, esp
 push dword [ebp + 20]
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_GetFullPathNameA]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_move_file_ex:
 push ebp
 mov ebp, esp
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_MoveFileExA]
 mov esp, ebp
 pop ebp
 ret
