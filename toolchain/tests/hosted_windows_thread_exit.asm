[BITS 32]
extern __imp_GetExitCodeThread
extern __imp_WaitForSingleObject
extern __imp_ExitProcess
global cupid_test_get_exit_code_thread:function
global cupid_windows_wait_for_single_object:function
global cupid_test_exit_process:function
section .text
cupid_test_get_exit_code_thread:
 push ebp
 mov ebp, esp
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_GetExitCodeThread]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_wait_for_single_object:
 push ebp
 mov ebp, esp
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_WaitForSingleObject]
 mov esp, ebp
 pop ebp
 ret

; Reference-overlap rejection must not run allocator-based process cleanup
; while deliberately suspended worker calls remain inside the test hooks.
cupid_test_exit_process:
 push ebp
 mov ebp, esp
 push dword [ebp + 8]
 call dword [__imp_ExitProcess]
 mov esp, ebp
 pop ebp
 ret
