[bits 16]           ; We're working in 16-bit mode
[org 0x7c00]        ; BIOS loads us at 0x7C00

boot:
    ; Save boot drive number FIRST - BIOS provides it in DL
    mov [boot_drive], dl

    ; Initialize segment registers
    cli             ; Disable interrupts
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00  ; Set up stack
    sti             ; Enable interrupts

    ; Print initial message
    mov si, msg_start
    call print_string

    ; Reset disk system
    xor ah, ah      ; Function 0 - Reset disk
    mov dl, 0x00    ; Drive 0 = floppy
    int 0x13
    jc disk_error

    ; Print loading message
    mov si, msg_loading
    call print_string

    ; Load kernel from disk
    mov bx, 0x1000  ; Load kernel to 0x1000
    mov dh, 5       ; Load 5 sectors
    mov dl, 0x00    ; Use floppy drive
    call disk_load

    ; Print success message
    mov si, msg_loaded
    call print_string

    ; After loading kernel, prepare for protected mode
    cli                     ; Disable interrupts
    lgdt [gdt_descriptor]   ; Load GDT
    
    ; Enable protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; Far jump to 32-bit code
    jmp CODE_SEG:init_pm

[bits 32]
init_pm:
    ; Initialize segment registers
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Set up stack
    mov ebp, 0x90000
    mov esp, ebp
    
    ; Jump to kernel
    jmp 0x1000

print_string:
    pusha
    mov ah, 0x0e    ; BIOS teletype output
.loop:
    lodsb           ; Load next character
    or al, al       ; Check if end of string
    jz .done
    int 0x10        ; Print character
    jmp .loop
.done:
    popa
    ret

disk_load:
    pusha           ; Save all registers
    push dx         ; Save DX (sectors to read)
    
    mov ah, 0x02    ; BIOS read sector function
    mov al, dh      ; Read DH sectors
    mov ch, 0       ; Cylinder 0
    mov dh, 0       ; Head 0
    mov cl, 2       ; Start from sector 2
    int 0x13        ; BIOS interrupt
    
    jc disk_error   ; Jump if error (carry flag set)
    
    pop dx          ; Restore DX
    cmp dh, al      ; Compare sectors read
    jne sectors_error
    popa            ; Restore all registers
    ret

disk_error:
    mov si, msg_disk_error
    call print_string
    jmp $

sectors_error:
    mov si, msg_sectors_error
    call print_string
    jmp $

; Data
boot_drive: db 0
msg_start: db 'Starting boot sequence...', 13, 10, 0
msg_loading: db 'Loading kernel...', 13, 10, 0
msg_loaded: db 'Kernel loaded!', 13, 10, 0
msg_disk_error: db 'Disk read error!', 13, 10, 0
msg_sectors_error: db 'Incorrect sectors read!', 13, 10, 0

; GDT
gdt_start:
    ; Null descriptor
    dd 0x0
    dd 0x0
    
    ; Code segment
    dw 0xffff       ; Limit
    dw 0x0          ; Base
    db 0x0          ; Base
    db 10011010b    ; Flags
    db 11001111b    ; Flags + Limit
    db 0x0          ; Base
    
    ; Data segment
    dw 0xffff
    dw 0x0
    db 0x0
    db 10010010b
    db 11001111b
    db 0x0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ 0x08
DATA_SEG equ 0x10

times 510-($-$$) db 0   ; Pad with zeros
dw 0xaa55               ; Boot signature 