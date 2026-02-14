; syscall_table_demo.asm — Validate syscall table offsets from CupidASM
; Run: as demos/syscall_table_demo.asm

section .data
    msg_hdr  db "SYS table demo via syscall_get_table", 10, 0
    msg_ver  db "version=", 0
    msg_size db " table_size=", 0
    msg_pid  db " pid=", 0
    nl       db 10, 0

section .text

main:
    ; eax = cupid_syscall_table_t*
    call syscall_get_table
    mov  ebx, eax

    ; Print header using direct bound kernel symbol (safe in JIT)
    push msg_hdr
    call print
    add  esp, 4

    ; version = [sys + SYS_VERSION]
    push msg_ver
    call print
    add  esp, 4
    mov  eax, [ebx + SYS_VERSION]
    push eax
    call print_int
    add  esp, 4

    ; table_size = [sys + SYS_TABLE_SIZE]
    push msg_size
    call print
    add  esp, 4
    mov  eax, [ebx + SYS_TABLE_SIZE]
    push eax
    call print_int
    add  esp, 4

    ; pid from direct kernel symbol
    push msg_pid
    call print
    add  esp, 4
    call getpid
    push eax
    call print_int
    add  esp, 4

    ; newline
    push nl
    call print
    add  esp, 4

    ret
