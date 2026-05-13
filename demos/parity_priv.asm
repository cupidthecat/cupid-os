; parity_priv.asm — exercise newly-added privileged + atomic ops so
; the encoder paths get exercised. Run with `as demos/parity_priv.asm`.
; This program returns immediately after each new mnemonic; the value
; of executing them in JIT mode is *not* meaningful (they touch real
; CPU control state) — the point is that the assembler accepts them
; and emits the right opcode bytes. Use cupiddis on the JIT region to
; verify encodings byte-by-byte.

section .data
gdt_ptr: dw 0
         dd 0
idt_ptr: dw 0
         dd 0
counter: dd 0

section .text
main:
    ; Zero-operand 0F-prefixed system ops.
    ; (These would actually trap or alter state on real hardware; the
    ;  test stops short of executing them in JIT — return before any.)
    jmp .skip_dangerous

    ; lgdt/lidt: encoder paths only.
    lgdt   [gdt_ptr]
    lidt   [idt_ptr]
    sgdt   [gdt_ptr]
    sidt   [idt_ptr]
    invlpg [counter]
    ltr    ax
    str    ax
    sldt   ax
    smsw   ax
    lmsw   ax

    ; Zero-operand privileged.
    cpuid
    rdtsc
    rdmsr
    wrmsr
    wbinvd
    invd
    clts
    sysenter
    sysexit
    syscall
    iretd
    retf

    ; Atomic R/M ops, with and without the lock prefix.
    bswap   eax
    xadd    ebx, eax
    cmpxchg ecx, eax
    lock xadd    [counter], eax
    lock cmpxchg [counter], eax
    lock inc     dword [counter]
    lock add     [counter], 1
.skip_dangerous:
    xor eax, eax
    ret
