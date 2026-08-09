[BITS 32]

extern main
extern __imp_ExitProcess
extern __imp_GetStdHandle
extern __imp_WriteFile

global _start
global cupid_windows_write_stdout

section .text

_start:
 cld
 and esp, 0xfffffff0
 call main
 push eax
 call dword [__imp_ExitProcess]
 hlt

cupid_windows_write_stdout:
 push ebp
 mov ebp, esp
 sub esp, 4
 push dword -11
 call dword [__imp_GetStdHandle]
 push dword 0
 lea ecx, [ebp - 4]
 push ecx
 push dword [ebp + 12]
 push dword [ebp + 8]
 push eax
 call dword [__imp_WriteFile]
 test eax, eax
 jz cupid_windows_write_failed
 mov eax, [ebp - 4]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_write_failed:
 xor eax, eax
 mov esp, ebp
 pop ebp
 ret
