[BITS 32]

extern main

global _start
global cupid_linux_syscall1
global cupid_linux_syscall2
global cupid_linux_syscall3

section .text

_start:
 mov esi, esp
 mov eax, [esi]
 lea edx, [esi + 4]
 and esp, 0xfffffff0
 sub esp, 8
 push edx
 push eax
 call main
 mov ebx, eax
 mov eax, 1
 int 0x80

cupid_linux_syscall1:
 push ebx
 mov eax, [esp + 8]
 mov ebx, [esp + 12]
 int 0x80
 pop ebx
 ret

cupid_linux_syscall2:
 push ebx
 mov eax, [esp + 8]
 mov ebx, [esp + 12]
 mov ecx, [esp + 16]
 int 0x80
 pop ebx
 ret

cupid_linux_syscall3:
 push ebx
 mov eax, [esp + 8]
 mov ebx, [esp + 12]
 mov ecx, [esp + 16]
 mov edx, [esp + 20]
 int 0x80
 pop ebx
 ret
