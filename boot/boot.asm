 [org 0x7c00]
[bits 16]

; Constants
; Kernel is loaded at linear address 0x10000 (ES=0x1000, BX=offset)
KERNEL_SEGMENT equ 0x1000
KERNEL_OFFSET  equ 0x10000   ; Linear address for protected mode jump

; Main bootloader entry point
start:
    ; Save boot drive (BIOS passes it in DL)
    mov [BOOT_DRIVE], dl

    ; Set up segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00

    ; Print boot message
    mov si, MSG_BOOT
    call print_string

    ; Load kernel
    call load_kernel
    
    ; Switch to protected mode
    call switch_to_pm
    
    jmp $

; Load kernel from disk (in chunks, one track at a time for max compatibility)
; Floppy geometry: 18 sectors/track, 2 heads/cylinder
; Need to read 125 sectors total starting from C:0 H:0 S:2
; Kernel is loaded at ES:BX where ES=0x1000 → linear address 0x10000+BX
load_kernel:
    mov si, MSG_LOAD_KERNEL
    call print_string

    ; Set ES to kernel segment for all BIOS disk reads
    mov ax, KERNEL_SEGMENT
    mov es, ax

    ; Chunk 1: rest of track 0, head 0 (sectors 2-18 = 17 sectors) [total: 17]
    mov bx, 0x0000        ; ES:BX = 0x1000:0x0000 = linear 0x10000
    mov dl, [BOOT_DRIVE]
    mov ah, 0x02
    mov al, 17
    mov ch, 0             ; Cylinder 0
    mov dh, 0             ; Head 0
    mov cl, 2             ; Sector 2
    int 0x13
    jc disk_error

    ; Chunk 2: full track 0, head 1 (sectors 1-18 = 18 sectors) [total: 35]
    mov bx, (17 * 512)    ; ES:BX = 0x1000:0x2200 = linear 0x12200
    mov dl, [BOOT_DRIVE]
    mov ah, 0x02
    mov al, 18
    mov ch, 0             ; Cylinder 0
    mov dh, 1             ; Head 1
    mov cl, 1             ; Sector 1
    int 0x13
    jc disk_error

    ; Chunk 3: full track 1, head 0 (sectors 1-18 = 18 sectors) [total: 53]
    mov bx, (35 * 512)    ; ES:BX = 0x1000:0x4600 = linear 0x14600
    mov dl, [BOOT_DRIVE]
    mov ah, 0x02
    mov al, 18
    mov ch, 1             ; Cylinder 1
    mov dh, 0             ; Head 0
    mov cl, 1             ; Sector 1
    int 0x13
    jc disk_error

    ; Chunk 4: full track 1, head 1 (sectors 1-18 = 18 sectors) [total: 71]
    mov bx, (53 * 512)    ; ES:BX = 0x1000:0x6A00 = linear 0x16A00
    mov dl, [BOOT_DRIVE]
    mov ah, 0x02
    mov al, 18
    mov ch, 1             ; Cylinder 1
    mov dh, 1             ; Head 1
    mov cl, 1             ; Sector 1
    int 0x13
    jc disk_error

    ; Chunk 5: full track 2, head 0 (sectors 1-18 = 18 sectors) [total: 89]
    mov bx, (71 * 512)    ; ES:BX = 0x1000:0x8E00 = linear 0x18E00
    mov dl, [BOOT_DRIVE]
    mov ah, 0x02
    mov al, 18
    mov ch, 2             ; Cylinder 2
    mov dh, 0             ; Head 0
    mov cl, 1             ; Sector 1
    int 0x13
    jc disk_error

    ; Chunk 6: full track 2, head 1 (sectors 1-18 = 18 sectors) [total: 107]
    mov bx, (89 * 512)    ; ES:BX = 0x1000:0xB200 = linear 0x1B200
    mov dl, [BOOT_DRIVE]
    mov ah, 0x02
    mov al, 18
    mov ch, 2             ; Cylinder 2
    mov dh, 1             ; Head 1
    mov cl, 1             ; Sector 1
    int 0x13
    jc disk_error

    ; Chunk 7: full track 3, head 0 (sectors 1-18 = 18 sectors) [total: 125]
    mov bx, (107 * 512)   ; ES:BX = 0x1000:0xD600 = linear 0x1D600
    mov dl, [BOOT_DRIVE]
    mov ah, 0x02
    mov al, 18
    mov ch, 3             ; Cylinder 3
    mov dh, 0             ; Head 0
    mov cl, 1             ; Sector 1
    int 0x13
    jc disk_error

    ; Restore ES to 0 for print_string and other real-mode code
    xor ax, ax
    mov es, ax

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
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000  ; Ensure stack pointer is set
    mov ebp, esp       ; Set base pointer to stack top
    
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