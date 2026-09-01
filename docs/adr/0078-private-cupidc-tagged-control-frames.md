# Tag loop and switch control in the private CupidC emitter

- Status: Accepted
- Date: 2026-07-22

## Context

The private in-kernel CupidC compiler used one depth stack for loops and switches. A switch occupied the top entry but had no continuation target. As a result, `continue` inside a switch nested in a loop could jump to an unset or unrelated address instead of the nearest loop target.

The same switch path saves its selector on the machine stack. A switch-local `break` jumped past the common selector pop, and a `continue` that crossed one or more switches also left their selectors behind. Repeated control jumps could therefore consume the runtime stack even when their branch target was otherwise correct.

## Decision

The private compiler keeps a tagged stack of breakable controls. Each entry is either `CC_CONTROL_LOOP` or `CC_CONTROL_SWITCH`. `while`, `do`, and `for` entries keep their established continuation targets. A switch entry has no continuation target.

`break` selects the innermost control. When that control is a switch, emission removes its saved four-byte selector before jumping to the patched switch exit. A loop `break` needs no selector cleanup.

`continue` scans outward for the nearest loop. Before it jumps, emission removes one saved four-byte selector for every switch crossed by that search. It then uses the loop-specific target: the condition for `while`, the patched condition trampoline for `do`, or the iteration expression for `for`.

The renamed control arrays retain storage for 128 breakable controls and 64 `break` patches per control. This decision does not claim a new fail-closed diagnostic at that depth. Compiler initialization, JIT entry, and REPL restoration all reset the tagged control depth.

This decision does not change the shared hosted frontend or Linear IR control model. It corrects the separate private compiler that owns embedded JIT and AOT execution.

## Rejected alternatives

Treating a switch as a loop would keep the ambiguous target that caused the bug. A switch has an exit for `break`, but it is never a target for `continue`.

Popping only the innermost selector would fail when `continue` crosses nested switches. Counting each crossed switch follows the runtime stack shape directly.

Deferring cleanup to the function epilogue would leave every later call and loop iteration with the wrong stack depth. Control jumps must restore the stack before they reach their target.

## Consequences and evidence

The expanded `/bin/feature25.cc` runtime smoke covers `continue` through switches in `do`, `while`, and `for` loops, including two nested switches and a nested-loop case that must select the inner loop. Sustained loops exercise selector cleanup for both `continue` and switch-local `break`; the old emitter consumes several megabytes of stack on those paths. A bound REPL case proves the exact `continue outside loop` diagnostic, returns failure, and lets the running smoke continue. The accepted smoke reaches `[feature25] PASS do=1 for=1 while=1 stack=1 reject=1 nearest=1`, then completes JIT execution without a panic or exception.

A proposed 129-switch negative printed the intended depth diagnostic, then exhausted the terminal process's 4 MiB stack during parser recovery because the unoptimized recursive statement parser uses a large native frame. Reducing the supported control arrays to fit that incidental frame would weaken CupidC. The unsafe probe was removed, and deep recursive parser hardening remains separate work.

The change affects production kernel compiler objects and the embedded test source, so the normal image build and QEMU runtime smoke are required. It does not transfer a build transform or retire a host dependency. The host C compiler still builds the private compiler into the kernel.

Issue #31 remains open for the rest of the embedded Cupid C corpus. Its broader migration order and dependencies are unchanged by this focused runtime correction.
