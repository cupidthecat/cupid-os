; include_helper.asm - Support file for include_feature.asm
; Demonstrates symbols imported via %include.

section .data
    inc_msg db "%include demo loaded successfully", 10, 0

section .text

print_include_message:
    push inc_msg
    call print
    add  esp, 4
    ret
