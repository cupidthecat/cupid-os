[BITS 32]

extern main

global _start:function
global cupid_linux_syscall0:function
global cupid_linux_syscall1:function
global cupid_linux_syscall2:function
global cupid_linux_syscall3:function
global cupid_linux_syscall4:function
global cupid_linux_syscall5:function

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

cupid_linux_syscall0:
 mov eax, [esp + 4]
 int 0x80
 ret

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

cupid_linux_syscall4:
 push ebx
 push esi
 mov eax, [esp + 12]
 mov ebx, [esp + 16]
 mov ecx, [esp + 20]
 mov edx, [esp + 24]
 mov esi, [esp + 28]
 int 0x80
 pop esi
 pop ebx
 ret

cupid_linux_syscall5:
 push ebx
 push esi
 push edi
 mov eax, [esp + 16]
 mov ebx, [esp + 20]
 mov ecx, [esp + 24]
 mov edx, [esp + 28]
 mov esi, [esp + 32]
 mov edi, [esp + 36]
 int 0x80
 pop edi
 pop esi
 pop ebx
 ret
