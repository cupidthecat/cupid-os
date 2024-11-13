[BITS 32]

; Export symbols
global isr0
global isr1
global load_idt

; Common ISR stub that saves processor state
isr_common_stub:
    pusha           ; Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
    push ds
    push es
    push fs
    push gs
    
    ; Load kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Call C handler
    extern isr_handler
    call isr_handler
    
    ; Restore registers
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8      ; Clean up pushed error code and ISR number
    iret            ; Return from interrupt

; Divide by zero exception
isr0:
    cli
    push byte 0     ; Push dummy error code
    push byte 0     ; Push interrupt number
    jmp isr_common_stub

; Debug exception
isr1:
    cli
    push byte 0
    push byte 1
    jmp isr_common_stub

; Load IDT
load_idt:
    mov eax, [esp + 4]  ; Get pointer to IDT
    lidt [eax]          ; Load IDT
    ret 