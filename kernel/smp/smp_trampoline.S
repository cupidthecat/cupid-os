; kernel/smp_trampoline.S — AP real-mode -> protected-mode bootstrap at 0x8000.

BITS 16
ORG 0x8000

ap_start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    lgdt [gdtr]

    mov eax, cr0
    or  al, 1
    mov cr0, eax

    jmp dword 0x08:pm32

times 0x100 - ($ - $$) db 0

; GDT at 0x8100
gdt:
    dq 0x0000000000000000    ; null
    dq 0x00CF9A000000FFFF    ; code32 flat
    dq 0x00CF92000000FFFF    ; data32 flat
gdt_end:

times 0x120 - ($ - $$) db 0

gdtr:
    dw gdt_end - gdt - 1
    dd 0x8100

times 0x128 - ($ - $$) db 0
apic_to_cpu_id:    times 32 db 0xFF

times 0x148 - ($ - $$) db 0
idle_stack_tops:   times 32 dd 0

times 0x1C8 - ($ - $$) db 0
gs_selectors:      times 32 dw 0

times 0x208 - ($ - $$) db 0
ap_entry32_ptr:    dd 0

times 0x210 - ($ - $$) db 0

BITS 32
pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov ss, ax

    ; Read LAPIC ID from MMIO (top 8 bits of 0xFEE00020)
    mov eax, [0xFEE00020]
    shr eax, 24
    ; al = apic_id; zero-extend for index loop
    movzx eax, al

    ; Linear scan apic_to_cpu_id[0..31] for our slot
    xor ecx, ecx
.scan:
    cmp byte [0x8128 + ecx], al
    je  .found
    inc ecx
    cmp ecx, 32
    jb  .scan
.hang:
    hlt
    jmp .hang

.found:
    ; ecx = cpu_id
    ; Load GS selector from gs_selectors[ecx]
    mov ax, [0x81C8 + ecx*2]
    mov gs, ax

    ; Load stack pointer from idle_stack_tops[ecx]
    mov esp, [0x8148 + ecx*4]

    ; Far-call ap_main_c (absolute address stored at 0x8208)
    mov eax, [0x8208]
    call eax

    ; ap_main_c shouldn't return; hang just in case
    jmp .hang

times 4096 - ($ - $$) db 0
