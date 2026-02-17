; context_switch.asm - Low-level context switch for CupidOS scheduler
;
; void context_switch(uint32_t *old_esp, uint32_t new_esp, uint32_t new_eip);
;
; Saves all callee-saved registers (EBX, ESI, EDI, EBP) and the flags
; onto the current stack, stores the resulting ESP into *old_esp,
; then switches to new_esp and jumps to new_eip.
;
; When a saved process is later resumed (via new_eip pointing to
; .resume below), the saved registers are popped and the function
; returns normally to its caller - as if context_switch() just returned.

[BITS 32]
section .text

global context_switch
global context_switch_resume

context_switch:
    ; On entry (cdecl, 4 bytes each):
    ;   [esp+12] = new_eip
    ;   [esp+8]  = new_esp
    ;   [esp+4]  = old_esp (pointer to uint32_t)
    ;   [esp+0]  = return address (caller)

    ; Save callee-saved registers onto current stack
    push ebp
    push edi
    push esi
    push ebx
    pushfd                    ; save EFLAGS

    ; Grab all three arguments BEFORE we touch esp
    mov eax, [esp + 24]       ; eax = old_esp pointer  (5 pushes * 4 + ret = 24)
    mov ecx, [esp + 28]       ; ecx = new_esp          (24 + 4)
    mov edx, [esp + 32]       ; edx = new_eip          (24 + 8)

    ; Store current esp into *old_esp
    mov [eax], esp

    ; Switch to the new process's stack and jump
    mov esp, ecx
    jmp edx

; Resume point
; When a previously-saved process is rescheduled, new_eip points here.
; The new_esp points to the stack where we pushed EBX/ESI/EDI/EBP/EFLAGS.
context_switch_resume:
    popfd                     ; restore EFLAGS (includes IF → sti)
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret                       ; return to whoever called context_switch()

