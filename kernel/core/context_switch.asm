; context_switch.asm - Low-level context switch for CupidOS scheduler
;
; void context_switch(process_t *old_proc, process_t *new_proc,
;                     uint32_t resume_eflags);
;
; Saves all callee-saved registers (EBX, ESI, EDI, EBP, EFLAGS) and
; the SSE/x87 state (via FXSAVE) into the old PCB, then loads the new
; PCB's ESP + SSE/x87 state and returns through context_switch_resume.
;
; Bakes the following PCB offsets (C-side _Static_asserts in process.c
; lock these):
; PCB_ESP_OFFSET      = 32   (offsetof(process_t, context.esp))
; PCB_EIP_OFFSET      = 40   (offsetof(process_t, context.eip))
; PCB_EFLAGS_OFFSET   = 44   (offsetof(process_t, context.eflags))
; PCB_FP_STATE_OFFSET = 80   (offsetof(process_t, fp_state))
;
; The fp_state field is __attribute__((aligned(16))), which FXSAVE /
; FXRSTOR require for their memory operand.
;
; The caller enters with interrupts disabled.  Interrupts remain disabled
; until ESP and FPU state belong to the new process; STI's one-instruction
; shadow then covers the final jump to the new EIP.

[BITS 32]
section .text

global context_switch
global context_switch_resume
extern bkl_context_switch_release

%define PCB_ESP_OFFSET      32
%define PCB_EIP_OFFSET      40
%define PCB_EFLAGS_OFFSET   44
%define PCB_FP_STATE_OFFSET 80

; void context_switch(process_t *old_proc, process_t *new_proc,
;                     uint32_t resume_eflags);
; [esp+4] = old_proc
; [esp+8] = new_proc
; [esp+12] = caller EFLAGS captured by the scheduler's BKL acquisition
; [esp+0] = return address
context_switch:
    ; Grab arguments BEFORE we push anything
    mov eax, [esp + 4]        ; eax = old_proc
    mov edx, [esp + 8]        ; edx = new_proc
    mov ecx, [esp + 12]       ; ecx = caller EFLAGS

    ; Save callee-saved registers onto current stack
    push ebp
    push edi
    push esi
    push ebx
    ; Save the caller's pre-BKL flags, not the deliberately-cleared live IF.
    ; A resumed scheduler frame restores exactly the state it entered with.
    push ecx

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

    ; Release the scheduler's sole BKL acquisition only after the new stack and
    ; FP state are active.  Preserve edx across the C seam; the temporary words
    ; are removed before entering the target frame.
    push edx
    call bkl_context_switch_release
    pop edx
    test eax, eax
    jz .handoff_failure

    ; A fresh process starts with IF in context.eflags.  A resumed process uses
    ; the flags captured for its suspended schedule caller.  STI's shadow
    ; covers the target JMP, while an IF-clear frame remains disabled.
    test dword [edx + PCB_EFLAGS_OFFSET], (1 << 9)
    jz .jump_target
    sti

    ; Jump to new task's EIP.  For a resumed process that is
    ; context_switch_resume (we seeded it below before switching).
    ; For a brand-new process, it's the entry point with
    ; process_exit_trampoline as the return address on its stack.
.jump_target:
    jmp dword [edx + PCB_EIP_OFFSET]

.handoff_failure:
    cli
.handoff_halt:
    hlt
    jmp .handoff_halt

; Resume point
; When a previously-saved process is rescheduled, its saved EIP points
; here.  The stack is exactly as it was when context_switch() ran its
; own pushes above (EFLAGS / EBX / ESI / EDI / EBP).
context_switch_resume:
    and dword [esp], ~(1 << 8) ; clear TF - never restore single-step mode
    popfd                      ; restore the suspended schedule caller's flags
    cld                        ; clear DF - C ABI requires it clear on function entry
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret                        ; return to whoever called context_switch()
