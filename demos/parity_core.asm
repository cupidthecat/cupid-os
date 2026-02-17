; parity_core.asm - Core CupidC-parity bindings test for CupidASM
; Run: as demos/parity_core.asm

section .data
    msg_hdr    db "[ASM parity core]", 10, 0
    msg_cwd    db "cwd: ", 0
    msg_mount  db "mount_count=", 0
    msg_proc   db " process_count=", 0
    msg_freq   db " timer_hz=", 0
    msg_cpu    db " cpu_mhz=", 0
    msg_epoch  db " rtc_epoch=", 0
    msg_date   db " date=", 0
    msg_time   db " time=", 0
    nl         db 10, 0

section .text

main:
    push msg_hdr
    call __cc_PrintLine
    add  esp, 4

    push msg_cwd
    call print
    add  esp, 4
    call get_cwd
    push eax
    call print
    add  esp, 4

    push msg_mount
    call print
    add  esp, 4
    call mount_count
    push eax
    call print_int
    add  esp, 4

    push msg_proc
    call print
    add  esp, 4
    call process_count
    push eax
    call print_int
    add  esp, 4

    push msg_freq
    call print
    add  esp, 4
    call timer_get_frequency
    push eax
    call print_int
    add  esp, 4

    push msg_cpu
    call print
    add  esp, 4
    call get_cpu_mhz
    push eax
    call print_int
    add  esp, 4

    push msg_epoch
    call print
    add  esp, 4
    call rtc_epoch
    push eax
    call print_int
    add  esp, 4

    push msg_date
    call print
    add  esp, 4
    call date_short_string
    push eax
    call print
    add  esp, 4

    push msg_time
    call print
    add  esp, 4
    call time_string
    push eax
    call print
    add  esp, 4

    push nl
    call print
    add  esp, 4

    call memstats
    ret
