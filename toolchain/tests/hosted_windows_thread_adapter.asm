[BITS 32]

extern __imp_CreateThread

global cupid_windows_observer_thread_open:function
global cupid_windows_observer_thread_entry:function
global cupid_windows_observer_thread_finish:function
global cupid_windows_observer_thread_finished:function

section .text

; The descriptor contains a cdecl function and its argument. The caller owns
; it through completion and join. Only the Windows entry uses callee cleanup.
cupid_windows_observer_thread_open:
 push ebp
 mov ebp, esp
 mov eax, [ebp + 8]
 test eax, eax
 jz .invalid
 cmp dword [eax], 0
 je .invalid
 push dword 0
 push dword 0
 push eax
 push dword cupid_windows_observer_thread_entry
 push dword 131072
 push dword 0
 call dword [__imp_CreateThread]
 mov esp, ebp
 pop ebp
 ret
.invalid:
 xor eax, eax
 mov esp, ebp
 pop ebp
 ret

cupid_windows_observer_thread_entry:
 push ebp
 mov ebp, esp
 mov eax, [ebp + 8]
 push dword [eax + 4]
 call dword [eax]
 add esp, 4
 mov esp, ebp
 pop ebp
 ret 4

; XCHG with memory supplies a release fence. The getter's ordinary x86 load
; is an acquire operation across the external cdecl call boundary.
cupid_windows_observer_thread_finish:
 mov ecx, [esp + 4]
 test ecx, ecx
 jz .done
 mov eax, 1
 xchg dword [ecx], eax
.done:
 ret

cupid_windows_observer_thread_finished:
 mov eax, [esp + 4]
 test eax, eax
 jz .done
 mov eax, [eax]
.done:
 ret
