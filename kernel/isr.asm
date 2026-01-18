[BITS 32]

; Declare external C functions
extern isr_handler
extern irq_handler

; Export symbols
global isr0
global isr1
global load_idt
global isr2
global isr3
global isr4
global isr5
global isr6
global isr7
global isr8
global isr13
global isr14

; Export IRQ symbols
global irq0
global irq1
global irq2
global irq3
global irq4
global irq5
global irq6
global irq7
global irq8
global irq9
global irq10
global irq11
global irq12
global irq13
global irq14
global irq15

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
    
    push esp    ; Push pointer to registers struct as parameter
    
    call isr_handler
    
    add esp, 4  ; Remove pushed parameter
    
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret

; Common IRQ stub that saves processor state
irq_common_stub:
    pusha                   ; Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
    
    push ds
    push es
    push fs
    push gs
    
    mov ax, 0x10           ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push esp               ; Push pointer to stack structure as argument
    
    call irq_handler       ; Call C handler
    
    add esp, 4             ; Remove pushed stack pointer
    
    pop gs                 ; Restore segment registers
    pop fs
    pop es
    pop ds
    
    popa                   ; Restore registers
    add esp, 8             ; Clean up pushed error code and IRQ number
    iret                   ; Return from interrupt
    
; Add all ISR handlers
isr0:
    cli
    push byte 0     ; Push dummy error code
    push byte 0     ; Push interrupt number
    jmp isr_common_stub

isr1:
    cli
    push byte 0
    push byte 1
    jmp isr_common_stub

isr2:
    cli
    push byte 0
    push byte 2
    jmp isr_common_stub

isr3:
    cli
    push byte 0
    push byte 3
    jmp isr_common_stub

isr4:
    cli
    push byte 0
    push byte 4
    jmp isr_common_stub

isr5:
    cli
    push byte 0
    push byte 5
    jmp isr_common_stub

isr6:
    cli
    push byte 0
    push byte 6
    jmp isr_common_stub

isr7:
    cli
    push byte 0
    push byte 7
    jmp isr_common_stub

isr8:
    cli
    ; Note: Double fault pushes an error code
    push byte 8
    jmp isr_common_stub

isr13:
    cli
    ; General protection fault pushes an error code
    push byte 13
    jmp isr_common_stub

isr14:
    cli
    ; Page fault pushes an error code
    push byte 14
    jmp isr_common_stub

; IRQ handlers
irq0:
    cli
    push byte 0
    push byte 32
    jmp irq_common_stub

irq1:
    cli
    push byte 0
    push byte 33
    jmp irq_common_stub

irq2:
    cli
    push byte 0
    push byte 34
    jmp irq_common_stub

irq3:
    cli
    push byte 0
    push byte 35
    jmp irq_common_stub

irq4:
    cli
    push byte 0
    push byte 36
    jmp irq_common_stub

irq5:
    cli
    push byte 0
    push byte 37
    jmp irq_common_stub

irq6:
    cli
    push byte 0
    push byte 38
    jmp irq_common_stub

irq7:
    cli
    push byte 0
    push byte 39
    jmp irq_common_stub

irq8:
    cli
    push byte 0
    push byte 40
    jmp irq_common_stub

irq9:
    cli
    push byte 0
    push byte 41
    jmp irq_common_stub

irq10:
    cli
    push byte 0
    push byte 42
    jmp irq_common_stub

irq11:
    cli
    push byte 0
    push byte 43
    jmp irq_common_stub

irq12:
    cli
    push byte 0
    push byte 44
    jmp irq_common_stub

irq13:
    cli
    push byte 0
    push byte 45
    jmp irq_common_stub

irq14:
    cli
    push byte 0
    push byte 46
    jmp irq_common_stub

irq15:
    cli
    push byte 0
    push byte 47
    jmp irq_common_stub

; Load IDT
load_idt:
    mov eax, [esp + 4]  ; Get pointer to IDT
    lidt [eax]          ; Load IDT
    ret
