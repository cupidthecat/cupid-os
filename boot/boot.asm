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

    ; ── VBE 640×480 32bpp via Bochs/QEMU I/O ports ─────────────────────
    mov dx, 0x01CE
    mov ax, 4           ; INDEX_ENABLE — disable first
    out dx, ax
    inc dx
    xor ax, ax
    out dx, ax
    dec dx
    mov ax, 1           ; INDEX_XRES
    out dx, ax
    inc dx
    mov ax, 640
    out dx, ax
    dec dx
    mov ax, 2           ; INDEX_YRES
    out dx, ax
    inc dx
    mov ax, 480
    out dx, ax
    dec dx
    mov ax, 3           ; INDEX_BPP
    out dx, ax
    inc dx
    mov ax, 32
    out dx, ax
    dec dx
    mov ax, 4           ; INDEX_ENABLE — enable with LFB
    out dx, ax
    inc dx
    mov ax, 0x41
    out dx, ax
    ; Read PCI BAR0 of VGA device (Bus 0 Dev 2 Fn 0) for LFB address
    mov eax, 0x80001010
    mov dx, 0xCF8
    out dx, eax
    mov dx, 0xCFC
    in eax, dx
    and eax, 0xFFFFFFF0
    mov [0x0500], eax
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; Switch to protected mode
    call switch_to_pm
    
    jmp $

; Load kernel from disk using a loop.
; Floppy geometry: 18 sectors/track, 2 heads/cylinder.
; Reads one full track at a time, advancing head/cylinder as needed.
; Splits any read that would cross a 64KB DMA boundary.
; Kernel is loaded starting at linear address 0x10000.
load_kernel:
    ; State: CHS position and linear destination
    mov byte [cur_cyl], 0
    mov byte [cur_head], 0
    mov byte [cur_sect], 2      ; First sector after boot sector
    mov byte [cur_count], 17    ; Remaining sectors in first track
    mov word [dest_seg], 0x1000
    mov word [dest_off], 0x0000
    mov word [sectors_left], 1024 ; Total sectors to load (~512KB max kernel)

.read_loop:
    ; Check if done
    cmp word [sectors_left], 0
    je .done

    ; Determine how many sectors to read this iteration
    movzx ax, byte [cur_count]
    cmp ax, [sectors_left]
    jbe .count_ok
    mov ax, [sectors_left]      ; Don't read more than needed
.count_ok:

    ; Check 64KB DMA boundary: max sectors before crossing
    ; boundary_remain = (0x10000 - dest_off) / 512
    push ax
    mov bx, [dest_off]
    mov cx, 0                   ; If dest_off == 0, full 64KB available
    test bx, bx
    jz .boundary_full
    mov cx, bx
    neg cx                      ; cx = 0x10000 - dest_off (works because 16-bit wrap)
    shr cx, 9                   ; cx = remaining sectors before boundary
    jmp .boundary_check
.boundary_full:
    mov cx, 128                 ; 64KB / 512 = 128, more than enough
.boundary_check:
    pop ax
    cmp ax, cx
    jbe .no_split
    mov ax, cx                  ; Limit to boundary
.no_split:
    ; AX = number of sectors to read this call
    test ax, ax
    jz .advance_segment         ; dest_off is exactly at boundary

    push ax                     ; Save sector count

    ; Set up BIOS int 0x13 read
    mov es, [dest_seg]
    mov bx, [dest_off]
    mov dl, [BOOT_DRIVE]
    mov dh, [cur_head]
    mov ch, [cur_cyl]
    mov cl, [cur_sect]
    mov ah, 0x02
    ; AL already has sector count
    int 0x13
    jc disk_error

    pop cx                      ; CX = sectors just read

    ; Update sectors_left
    sub [sectors_left], cx

    ; Advance destination: dest_off += cx * 512
    shl cx, 9                   ; CX = bytes read
    add [dest_off], cx
    jnc .no_seg_advance         ; If no carry, no segment wrap

.advance_segment:
    ; dest_off wrapped past 0xFFFF, advance segment by 0x1000
    add word [dest_seg], 0x1000
    ; dest_off already wrapped to correct low value

.no_seg_advance:
    ; Advance CHS: cur_sect += sectors_read, update cur_count
    ; Recalculate: we need to know how many were read (cx was bytes, convert back)
    shr cx, 9
    add [cur_sect], cl
    sub [cur_count], cl

    ; If cur_count > 0, more sectors remain in this track
    cmp byte [cur_count], 0
    jg .read_loop

    ; Move to next track: toggle head, if head wraps to 0 increment cylinder
    cmp byte [cur_head], 0
    jne .next_cyl
    mov byte [cur_head], 1
    jmp .reset_track
.next_cyl:
    mov byte [cur_head], 0
    inc byte [cur_cyl]
.reset_track:
    mov byte [cur_sect], 1
    mov byte [cur_count], 18
    jmp .read_loop

.done:
    ; Restore ES to 0 for print_string and other real-mode code
    xor ax, ax
    mov es, ax
    ret

; Loader state variables
cur_cyl     db 0
cur_head    db 0
cur_sect    db 0
cur_count   db 0
dest_seg    dw 0
dest_off    dw 0
sectors_left dw 0

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
    mov esp, 0x190000 ; Boot stack at 1.5MB+ (above BSS)
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
MSG_DISK_ERROR db 'Disk error!', 13, 10, 0

BOOT_DRIVE db 0

; Padding and magic number
times 510-($-$$) db 0
dw 0xaa55