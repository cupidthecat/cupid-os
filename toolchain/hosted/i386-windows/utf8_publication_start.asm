[BITS 32]

extern __imp_DeleteFileW
extern __imp_FlushFileBuffers
extern __imp_GetFullPathNameW
extern __imp_MoveFileExW

global cupid_windows_delete_file_wide:function
global cupid_windows_flush_file_buffers:function
global cupid_windows_get_full_path_name_wide:function
global cupid_windows_move_file_ex_wide:function

section .text

cupid_windows_delete_file_wide:
 push ebp
 mov ebp, esp
 push dword [ebp + 8]
 call dword [__imp_DeleteFileW]
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

cupid_windows_get_full_path_name_wide:
 push ebp
 mov ebp, esp
 push dword [ebp + 20]
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_GetFullPathNameW]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_move_file_ex_wide:
 push ebp
 mov ebp, esp
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_MoveFileExW]
 mov esp, ebp
 pop ebp
 ret
