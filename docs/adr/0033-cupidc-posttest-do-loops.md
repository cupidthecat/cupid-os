# Lower post-test do loops through CupidC IR

## Context

The shared CupidC frontend already publishes typed `do` statements, but the hosted IR stopped at that statement kind. Doom's unchanged `D_Display` function in `kernel/doom/src/d_main.c` contains a source-driven requirement:

```c
do
{
    nowtime = I_GetTime ();
    tics = nowtime - wipestart;
    I_Sleep(1);
} while (tics <= 0);
```

Earlier increments covered the local assignments, fixed direct calls, subtraction, signed comparison, and void return used by a focused copy of this loop. The remaining gap was its post-test control flow. Replacing it with a pre-test loop would change the first-iteration behavior.

## Decision

`ctool_c_lower_ir` accepts a post-test `do` statement whose condition produces a represented four-byte integer value. Lowering records the instruction offset before the body, lowers the body once, evaluates the condition, emits `BRANCH_ZERO` to the exit, and emits `JUMP` back to the recorded body offset. The first iteration therefore reaches the body without evaluating the condition.

The loop shares its frozen-record validation with `while`. Its condition and body fields are required, unused statement fields must stay empty, and a direct declaration body remains invalid. Both loop forms use the same condition helper for expression lowering, stack validation, represented-type checking, and the false branch.

The condition helper initializes its output reference to `CTOOL_C_AST_NONE` before lowering. The out-parameter is defined on every return path, and static analysis can verify each caller without depending on the diagnostic helper's return value.

A body that cannot reach its condition emits no condition or backward edge. The condition still passes through a count-only validation context, so unreachable code cannot hide a malformed reference or unsupported type. The `do` statement reports no fallthrough in that case. Otherwise, it reports possible fallthrough because the condition may be false after an iteration.

`break` and `continue` remain unsupported. When `continue` is added, its target in a `do` loop must be the condition rather than the body start. No new IR instruction is needed because `BRANCH_ZERO` and `JUMP` already express the two edges with function-relative targets.

## Consequences and evidence

The object contract guards the exact active Doom loop and builds its focused source from that text. The IR contract lowers the same focused function to 21 exact instructions with a maximum abstract-stack depth of three. Its body begins at instruction 0, its false branch exits to instruction 20, and instruction 19 jumps back to instruction 0.

The deterministic ELF32 object contains one 109-byte local function and 54 decoded instructions. Its false exit lands at byte offset 107, and its backward jump lands at byte offset 6 after the cdecl prologue. Calls to `I_GetTime` and `I_Sleep` use two `R_386_PC32` relocations at offsets 11 and 62 with addend `-4`. Repeated emission is byte-identical and leaves the frozen frontend unit unchanged.

A terminal-body fixture lowers to one direct return with no condition instructions, implicit return, or backward edge. A `long long` condition receives the unsupported-type diagnostic in both ordinary and terminal-body forms, which proves that unreachable condition validation remains active. The object seam rejects the same wide condition transactionally. A `for` statement remains the unsupported-loop boundary.

This is hosted bootstrap evidence. The private in-kernel compiler still produces every normal OS C object. No production artifact, build owner, host dependency, boot path, or runtime ABI changed.

Issue #25 remains open. `for`, `break`, `continue`, `switch`, labels, `goto`, nested declarations, broader values and addresses, production integration, and staged self-hosting remain.
