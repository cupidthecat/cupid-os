 [org 0x7c00]
[bits 16]

; Constants
KERNEL_OFFSET equ 0x1000

; Main bootloader entry point
start:
    ; Set up segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x9000

    ; Print boot message
    mov si, MSG_BOOT
    call print_string

    ; Load kernel
    call load_kernel
    
    ; Switch to protected mode
    call switch_to_pm
    
    jmp $

; Load kernel from disk
load_kernel:
    mov si, MSG_LOAD_KERNEL
    call print_string

    mov bx, KERNEL_OFFSET   ; Load to this address
    mov dh, 15             ; Load this many sectors
    mov dl, [BOOT_DRIVE]   ; From boot drive
    
    mov ah, 0x02          ; BIOS read function
    mov al, dh            ; Sectors to read
    mov ch, 0             ; Cylinder 0
    mov dh, 0             ; Head 0
    mov cl, 2             ; Start from sector 2
    int 0x13
    
    jc disk_error
    ret

disk_error:
    mov si, MSG_DISK_ERROR
    call print_string
    jmp $

; Print string (SI = string pointer)
print_string:
    pusha
    mov ah, 0x0e
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

; Switch to protected mode
switch_to_pm:
    cli                     ; Disable interrupts
    lgdt [gdt_descriptor]   ; Load GDT
    
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax           ; Set protected mode bit
    
    jmp CODE_SEG:init_pm   ; Far jump to 32-bit code

[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    mov ebp, 0x90000
    mov esp, ebp
    
    ; Print PM message directly to video memory
    mov ebx, 0xb8000
    mov al, 'P'
    mov ah, 0x0f
    mov [ebx], ax
    
    ; Jump to kernel
    jmp CODE_SEG:KERNEL_OFFSET

; GDT
gdt_start:
    dq 0                ; Null descriptor

gdt_code:
    dw 0xffff          ; Limit
    dw 0               ; Base
    db 0               ; Base
    db 10011010b       ; Access
    db 11001111b       ; Granularity
    db 0               ; Base

gdt_data:
    dw 0xffff          ; Limit
    dw 0               ; Base
    db 0               ; Base
    db 10010010b       ; Access
    db 11001111b       ; Granularity
    db 0               ; Base

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; Constants
CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; Messages
MSG_BOOT db 'Booting...', 13, 10, 0
MSG_LOAD_KERNEL db 'Loading kernel...', 13, 10, 0
MSG_DISK_ERROR db 'Disk error!', 13, 10, 0

BOOT_DRIVE db 0

; Padding and magic number
times 510-($-$$) db 0
dw 0xaa55