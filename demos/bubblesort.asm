; bubblesort.asm - Bubble sort on an integer array
; Sorts 8 integers in-place and prints them before and after.
; Run: as demos/bubblesort.asm

section .data
    arr     dd 64, 25, 12, 22, 11, 90, 33, 7
    len     dd 8
    msg_before db "Before: ", 0
    msg_after  db "After:  ", 0
    space   db " ", 0
    newline db 10, 0

section .text

; print_array - prints len dwords starting at arr
print_array:
    push ebp
    mov  ebp, esp
    push esi
    push ecx

    mov  esi, arr
    mov  ecx, [len]

.pa_loop:
    cmp  ecx, 0
    je   .pa_done

    push ecx
    push esi
    mov  eax, [esi]
    push eax
    call print_int
    add  esp, 4
    push space
    call print
    add  esp, 4
    pop  esi
    pop  ecx

    add  esi, 4
    dec  ecx
    jmp  .pa_loop

.pa_done:
    push newline
    call print
    add  esp, 4

    pop  ecx
    pop  esi
    pop  ebp
    ret

main:
    ; Print "Before: " and the unsorted array
    push msg_before
    call print
    add  esp, 4
    call print_array

    ; Bubble sort
    mov  ecx, [len]        ; n = 8

.outer:
    dec  ecx               ; n-1 passes
    cmp  ecx, 0
    jle  .sorted

    mov  esi, 0            ; i = 0
    mov  edx, ecx          ; inner limit
    push edx               ; save outer limit

.inner:
    cmp  esi, edx
    jge  .next_pass

    ; Load arr[i] and arr[i+1]
    mov  eax, esi
    shl  eax, 2            ; eax = i * 4
    add  eax, arr          ; eax = &arr[i]

    mov  ebx, [eax]        ; ebx = arr[i]
    mov  edi, [eax+4]      ; edi = arr[i+1]

    cmp  ebx, edi
    jle  .no_swap

    ; Swap
    mov  [eax], edi
    mov  [eax+4], ebx

.no_swap:
    inc  esi
    jmp  .inner

.next_pass:
    pop  ecx               ; restore outer counter
    jmp  .outer

.sorted:
    ; Print "After:  " and the sorted array
    push msg_after
    call print
    add  esp, 4
    call print_array

    ret
