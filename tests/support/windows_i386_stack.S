.text
.globl _native_execute
_native_execute:
  pushl %ebp
  movl %esp, %ebp
  pushl %ebx
  pushl %esi
  pushl %edi
  movl 8(%ebp), %eax
  movl 12(%ebp), %esp
  andl $-16, %esp
  fninit
  call *%eax
  leal -12(%ebp), %esp
  popl %edi
  popl %esi
  popl %ebx
  popl %ebp
  ret
