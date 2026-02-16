; include_feature.asm — %include directive demo
; Run: as demos/include_feature.asm

%include "demos/include_helper.asm"

section .text

main:
    call print_include_message
    ret
