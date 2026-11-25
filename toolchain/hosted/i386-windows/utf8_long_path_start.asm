[BITS 32]
extern __imp_GetFullPathNameW
global cupid_windows_get_full_path_name_wide:function
section .text
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
