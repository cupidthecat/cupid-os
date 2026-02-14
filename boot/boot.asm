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

start:
    mov [BOOT_DRIVE], dl        ; Save boot drive from BIOS
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x9000

    ; Load stage 2 (4 sectors from LBA 1) to 0x7E00 via EDD
    mov si, .dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .s1err

    ; Jump to stage 2 (label resolves to 0x7E00 due to org math)
    jmp 0x0000:stage2_entry

.dap:
    db 0x10            ; DAP size
    db 0               ; reserved
    dw STAGE2_SECTORS  ; sectors to read
    dw 0x0000          ; buffer offset
    dw 0x07E0          ; buffer segment (-> 0x7E00)
    dq 1               ; LBA start

.s1err:
    mov ah, 0x0e
    mov al, '!'
    int 0x10
    jmp $

BOOT_DRIVE db 0

; ── MBR partition table at byte offset 446 ──
times 446-($-$$) db 0

; Partition entry 1: FAT16, bootable, LBA 4096, 98304 sectors
db 0x80
db 0xFE, 0xFF, 0xFF
db 0x06
db 0xFE, 0xFF, 0xFF
dd 4096
dd 98304

; Partition entries 2-4: empty
times 48 db 0

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

    ; ── Load kernel above 1MB (chunked LBA reads) ──────────────────
    mov dword [dest_high], KERNEL_OFFSET
    mov word [sectors_left], 4091    ; LBA 5 through 4095

.read_loop:
    cmp word [sectors_left], 0
    je .load_done

    ; How many sectors to read this iteration
    mov ax, [sectors_left]
    cmp ax, SECTORS_PER_CHUNK
    jbe .chunk_ok
    mov ax, SECTORS_PER_CHUNK
.chunk_ok:
    test ax, ax
    jz .load_done

    push ax                     ; Save sector count for later

    ; Read sectors to temp buffer at TEMP_SEGMENT:0 via LBA EDD
    mov word [chunk_dap + 2], ax
    mov eax, dword [lba_current]
    mov dword [chunk_dap + 8], eax
    mov eax, dword [lba_current + 4]
    mov dword [chunk_dap + 12], eax
    mov si, chunk_dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
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

    ; Advance LBA counter
    movzx eax, cx
    add dword [lba_current], eax
    adc dword [lba_current + 4], 0
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
lba_current  dq 5
dest_high    dd 0
sectors_left dw 0

chunk_dap:
    db 0x10
    db 0
    dw 0
    dw 0x0000
    dw TEMP_SEGMENT
    dq 0

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