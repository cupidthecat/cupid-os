; context_switch.asm - Low-level context switch for CupidOS scheduler
;
; void context_switch(process_t *old_proc, process_t *new_proc);
;
; Saves all callee-saved registers (EBX, ESI, EDI, EBP, EFLAGS) and
; the SSE/x87 state (via FXSAVE) into the old PCB, then loads the new
; PCB's ESP + SSE/x87 state and returns through context_switch_resume.
;
; Bakes the following PCB offsets (C-side _Static_asserts in process.c
; lock these):
;   PCB_ESP_OFFSET      = 32   (offsetof(process_t, context.esp))
;   PCB_EIP_OFFSET      = 40   (offsetof(process_t, context.eip))
;   PCB_FP_STATE_OFFSET = 80   (offsetof(process_t, fp_state))
;
; The fp_state field is __attribute__((aligned(16))), which FXSAVE /
; FXRSTOR require for their memory operand.

[BITS 32]
section .text

global context_switch
global context_switch_resume

%define PCB_ESP_OFFSET      32
%define PCB_EIP_OFFSET      40
%define PCB_FP_STATE_OFFSET 80

; void context_switch(process_t *old_proc, process_t *new_proc);
;   [esp+4] = old_proc
;   [esp+8] = new_proc
;   [esp+0] = return address
context_switch:
    ; Grab arguments BEFORE we push anything
    mov eax, [esp + 4]        ; eax = old_proc
    mov edx, [esp + 8]        ; edx = new_proc

    ; Save callee-saved registers onto current stack
    push ebp
    push edi
    push esi
    push ebx
    pushfd                    ; save EFLAGS

    ; FXSAVE current SSE/x87 state into old_proc->fp_state
    ; (PCB field is __attribute__((aligned(16))); address is 16-byte
    ; aligned by construction.)
    fxsave [eax + PCB_FP_STATE_OFFSET]

    ; Save current ESP into old_proc->context.esp
    mov [eax + PCB_ESP_OFFSET], esp

    ; Switch to new task's stack
    mov esp, [edx + PCB_ESP_OFFSET]

    ; Restore new task's SSE/x87 state
    fxrstor [edx + PCB_FP_STATE_OFFSET]

    ; Jump to new task's EIP.  For a resumed process that is
    ; context_switch_resume (we seeded it below before switching).
    ; For a brand-new process, it's the entry point with
    ; process_exit_trampoline as the return address on its stack.
    jmp dword [edx + PCB_EIP_OFFSET]

; Resume point
; When a previously-saved process is rescheduled, its saved EIP points
; here.  The stack is exactly as it was when context_switch() ran its
; own pushes above (EFLAGS / EBX / ESI / EDI / EBP).
context_switch_resume:
    and dword [esp], ~(1 << 8) ; clear TF - never restore single-step mode
    popfd                      ; restore EFLAGS (includes IF -> sti)
    cld                        ; clear DF - C ABI requires it clear on function entry
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret                        ; return to whoever called context_switch()
