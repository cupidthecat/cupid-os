 [org 0x7c00]
[bits 16]

; ═══════════════════════════════════════════════════════════════════════
;  STAGE 1 — Boot Sector (512 bytes)
;  Loads stage 2 from sectors 2-5 to 0x7E00, then jumps to it.
;  With [org 0x7c00], stage 2 labels at offset 512+ resolve to 0x7E00+
;  which is exactly where we load them. BOOT_DRIVE in stage 1 memory
;  remains accessible since the boot sector stays in RAM at 0x7C00.
; ═══════════════════════════════════════════════════════════════════════

STAGE2_SECTORS     equ 4       ; Stage 2 = 4 sectors (2KB)
KERNEL_START_SECT  equ 6       ; Kernel starts at CHS sector 6 (1-indexed)

start:
    mov [BOOT_DRIVE], dl        ; Save boot drive from BIOS
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x9000

    ; Load stage 2 (4 sectors from sector 2) to 0x7E00
    mov ax, 0x07E0              ; Segment = 0x7E00 >> 4
    mov es, ax
    xor bx, bx                 ; Offset 0
    mov ah, 0x02                ; BIOS read sectors
    mov al, STAGE2_SECTORS
    mov ch, 0                   ; Cylinder 0
    mov cl, 2                   ; Start at CHS sector 2
    mov dh, 0                   ; Head 0
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .s1err

    ; Jump to stage 2 (label resolves to 0x7E00 due to org math)
    jmp 0x0000:stage2_entry

.s1err:
    mov ah, 0x0e
    mov al, '!'
    int 0x10
    jmp $

BOOT_DRIVE db 0

; ── Pad to 510 bytes + boot signature ──
times 510-($-$$) db 0
dw 0xAA55


; ═══════════════════════════════════════════════════════════════════════
;  STAGE 2 — Loaded at 0x7E00 (= 0x7C00 + 512)
;  A20 → unreal mode → load kernel to 1MB → VBE → protected mode → go
; ═══════════════════════════════════════════════════════════════════════

KERNEL_OFFSET      equ 0x100000  ; Kernel destination (1MB)
TEMP_SEGMENT       equ 0x1000    ; Temp buffer segment
TEMP_LINEAR        equ 0x10000   ; Temp buffer linear address
SECTORS_PER_CHUNK  equ 127       ; Max sectors per BIOS read (< 64KB)

stage2_entry:
    ; Re-establish segments (far jump set CS=0)
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; ── Enable A20 line ─────────────────────────────────────────────
    in al, 0x92
    or al, 2
    out 0x92, al

    ; ── Enter unreal mode ───────────────────────────────────────────
    ; Briefly enable PM just to load DS/ES with 4GB-limit descriptors,
    ; then return to real mode. CS is never changed, so [bits 16] is
    ; correct throughout — no far jump needed.
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or al, 1
    mov cr0, eax              ; PE=1: protected mode (but CS still real-mode)

    mov bx, 0x10              ; GDT data selector (4GB limit)
    mov ds, bx
    mov es, bx

    and al, 0xFE
    mov cr0, eax              ; PE=0: back to real mode

    ; Restore segment bases to 0 — cached 4GB limit stays in descriptor
    xor ax, ax
    mov ds, ax
    mov es, ax
    sti

    ; ── Load kernel above 1MB (chunked CHS reads) ──────────────────
    mov byte [cur_cyl], 0
    mov byte [cur_head], 0
    mov byte [cur_sect], KERNEL_START_SECT
    mov byte [cur_count], 18 - KERNEL_START_SECT + 1  ; 13 sectors left in first track
    mov dword [dest_high], KERNEL_OFFSET
    mov word [sectors_left], 2560   ; Up to 1.25MB of kernel

.read_loop:
    cmp word [sectors_left], 0
    je .load_done

    ; How many sectors to read this iteration
    movzx ax, byte [cur_count]
    cmp ax, [sectors_left]
    jbe .count_ok
    mov ax, [sectors_left]
.count_ok:
    cmp ax, SECTORS_PER_CHUNK
    jbe .chunk_ok
    mov ax, SECTORS_PER_CHUNK
.chunk_ok:
    test ax, ax
    jz .advance_track

    push ax                     ; Save sector count for later

    ; Read sectors to temp buffer at TEMP_SEGMENT:0
    mov es, word [temp_seg_val]
    xor bx, bx
    mov dl, [BOOT_DRIVE]
    mov dh, [cur_head]
    mov ch, [cur_cyl]
    mov cl, [cur_sect]
    mov ah, 0x02
    int 0x13
    jc disk_error

    xor bx, bx
    mov es, bx

    pop cx                      ; CX = sectors read

    ; Copy chunk from temp buffer to dest_high (32-bit unreal addressing)
    push cx
    movzx ecx, cx
    shl ecx, 7                  ; dword count = sectors × 128
    mov esi, TEMP_LINEAR
    mov edi, [dest_high]

.copy_loop:
    a32 mov eax, [ds:esi]
    a32 mov [ds:edi], eax
    add esi, 4
    add edi, 4
    dec ecx
    jnz .copy_loop

    pop cx

    ; Advance bookkeeping
    movzx eax, cx
    shl eax, 9                  ; bytes = sectors × 512
    add [dest_high], eax
    sub [sectors_left], cx
    add [cur_sect], cl
    sub [cur_count], cl

    cmp byte [cur_count], 0
    jg .read_loop

.advance_track:
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

.load_done:
    xor ax, ax
    mov es, ax

    ; ── VBE 640×480 32bpp via Bochs/QEMU I/O ports ─────────────────
    mov dx, 0x01CE
    mov ax, 4               ; INDEX_ENABLE — disable first
    out dx, ax
    inc dx
    xor ax, ax
    out dx, ax
    dec dx
    mov ax, 1               ; INDEX_XRES
    out dx, ax
    inc dx
    mov ax, 640
    out dx, ax
    dec dx
    mov ax, 2               ; INDEX_YRES
    out dx, ax
    inc dx
    mov ax, 480
    out dx, ax
    dec dx
    mov ax, 3               ; INDEX_BPP
    out dx, ax
    inc dx
    mov ax, 32
    out dx, ax
    dec dx
    mov ax, 4               ; INDEX_ENABLE — enable with LFB
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

    ; ── Switch to protected mode ────────────────────────────────────
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1
    mov cr0, eax

    jmp CODE_SEG:init_pm

; ── Stage 2 data ────────────────────────────────────────────────────
cur_cyl      db 0
cur_head     db 0
cur_sect     db 0
cur_count    db 0
dest_high    dd 0
sectors_left dw 0
temp_seg_val dw TEMP_SEGMENT

disk_error:
    mov ah, 0x0e
    mov al, '!'
    int 0x10
    jmp $

; ── 32-bit protected mode entry ─────────────────────────────────────
[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x880000           ; Boot stack at 8MB
    mov ebp, esp

    ; Quick sanity: write 'P' to VGA text buffer (visible briefly)
    mov byte [0xb8000], 'P'
    mov byte [0xb8001], 0x0f

    ; Jump to kernel at 1MB
    jmp CODE_SEG:KERNEL_OFFSET

; ── GDT ─────────────────────────────────────────────────────────────
[bits 16]
gdt_start:
    dq 0                        ; Null descriptor
gdt_code:
    dw 0xffff                   ; Limit (low)
    dw 0                        ; Base (low)
    db 0                        ; Base (mid)
    db 10011010b                ; Access: present, ring0, code, readable
    db 11001111b                ; Granularity: 4KB, 32-bit, limit (high)
    db 0                        ; Base (high)
gdt_data:
    dw 0xffff
    dw 0
    db 0
    db 10010010b                ; Access: present, ring0, data, writable
    db 11001111b
    db 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; ── Pad stage 2 to exactly STAGE2_SECTORS × 512 bytes ──────────────
times (512 + STAGE2_SECTORS * 512) - ($-$$) db 0