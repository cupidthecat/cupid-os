[BITS 32]

extern __imp_GetFinalPathNameByHandleW
global cupid_windows_get_final_path_name_by_handle_wide:function

section .text

cupid_windows_get_final_path_name_by_handle_wide:
 push ebp
 mov ebp, esp
 push dword [ebp + 20]
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_GetFinalPathNameByHandleW]
 mov esp, ebp
 pop ebp
 ret
