; fs_syscalls.asm — Demonstrate expanded CupidASM VFS/process bindings
; Run: as demos/fs_syscalls.asm

section .data
    msg_hdr   db "CupidASM VFS/syscall demo", 10, 0
    msg_pid   db "pid=", 0
    msg_ok    db " read-back OK", 10, 0
    msg_fail  db " read-back FAIL", 10, 0
    msg_err   db "open/write/read error", 10, 0
    file_path db "/tmp/asm_sys_demo.txt", 0
    payload   db "ASM file write/read path", 0
    newline   db 10, 0
    buf       resb 64

section .text

main:
    push msg_hdr
    call print
    add  esp, 4

    ; Print current PID
    push msg_pid
    call print
    add  esp, 4
    call getpid
    push eax
    call print_int
    add  esp, 4
    push newline
    call print
    add  esp, 4

    ; Best-effort cleanup old file
    push file_path
    call vfs_unlink
    add  esp, 4

    ; fd = vfs_open(path, O_CREAT|O_TRUNC|O_WRONLY)
    mov  eax, O_CREAT
    add  eax, O_TRUNC
    add  eax, O_WRONLY
    push eax
    push file_path
    call vfs_open
    add  esp, 8
    mov  ebx, eax
    cmp  ebx, 0
    jl   .io_error

    ; write(fd, payload, strlen(payload))
    push payload
    call strlen
    add  esp, 4
    push eax
    push payload
    push ebx
    call vfs_write
    add  esp, 12

    push ebx
    call vfs_close
    add  esp, 4

    ; reopen read-only
    push O_RDONLY
    push file_path
    call vfs_open
    add  esp, 8
    mov  ebx, eax
    cmp  ebx, 0
    jl   .io_error

    ; read(fd, buf, 63)
    push 63
    push buf
    push ebx
    call vfs_read
    add  esp, 12

    push ebx
    call vfs_close
    add  esp, 4

    ; strcmp(buf, payload)
    push payload
    push buf
    call strcmp
    add  esp, 8
    cmp  eax, 0
    jne  .not_equal

    push msg_ok
    call print
    add  esp, 4
    ret

.not_equal:
    push msg_fail
    call print
    add  esp, 4
    ret

.io_error:
    push msg_err
    call print
    add  esp, 4
    ret
