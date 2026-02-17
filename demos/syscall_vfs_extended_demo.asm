; syscall_vfs_extended_demo.asm - Extended syscall table VFS demo
; Run: as demos/syscall_vfs_extended_demo.asm

section .data
    path_src    db "/tmp/sys_src.txt", 0
    path_copy   db "/tmp/sys_copy.txt", 0
    msg_ok      db "extended SYS VFS calls: OK", 10, 0
    msg_fail    db "extended SYS VFS calls: FAIL", 10, 0
    payload     db "hello from syscall v2", 0

section .text

main:
    ; ebx = syscall table
    call syscall_get_table
    mov  ebx, eax

    ; best-effort cleanup
    mov  eax, [ebx + SYS_VFS_UNLINK]
    push path_src
    call eax
    add  esp, 4

    mov  eax, [ebx + SYS_VFS_UNLINK]
    push path_copy
    call eax
    add  esp, 4

    ; vfs_write_text(path_src, payload)
    mov  eax, [ebx + SYS_VFS_WRITE_TEXT]
    push payload
    push path_src
    call eax
    add  esp, 8
    cmp  eax, 0
    jl   .fail

    ; vfs_copy_file(path_src, path_copy)
    mov  eax, [ebx + SYS_VFS_COPY_FILE]
    push path_copy
    push path_src
    call eax
    add  esp, 8
    cmp  eax, 0
    jl   .fail

    ; print success
    push msg_ok
    call print
    add  esp, 4
    ret

.fail:
    push msg_fail
    call print
    add  esp, 4
    ret
