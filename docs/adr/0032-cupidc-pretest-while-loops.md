# Lower pre-test while loops through CupidC IR

## Context

The shared CupidC frontend already publishes typed `while` statements, but the hosted IR stopped at that statement kind. The unchanged blocker in `syscall_sleep_ms` from `kernel/core/syscall.c` is a complete helper:

```c
static void syscall_sleep_ms(uint32_t ms) {
  uint32_t start = timer_get_uptime_ms();
  while ((timer_get_uptime_ms() - start) < ms) {
    process_yield();
  }
}
```

Earlier increments covered its local initializer, direct calls, subtraction, unsigned comparison, expression statement, and implicit void return. Rewriting the helper to avoid its loop would hide an active C requirement and would not advance the shared compiler.

## Decision

`ctool_c_lower_ir` now accepts a pre-test `while` statement whose condition produces a represented four-byte integer value. Lowering records the instruction offset before condition evaluation, emits `BRANCH_ZERO` to the loop exit, lowers the body, and emits `JUMP` back to the recorded condition offset when the body can continue. The condition therefore runs before every possible iteration.

The loop exit is patched to the next function-relative instruction offset. If the surrounding function reaches its end, the existing void-fallthrough rule places the implicit return at that offset. A body that cannot continue needs no backward jump. The loop still reports that execution may continue because its condition can be false before the first iteration. This is conservative and does not try to prove that a constant condition is always true.

The frozen loop record must use its dedicated condition and body fields. Its direct body cannot be a declaration statement, matching the existing nested-declaration boundary. The same recursion limit used by compound and selection statements also bounds loop nesting.

This decision does not add loop-control targets. `break`, `continue`, post-test `do`, `for`, `switch`, labels, `goto`, and declarations inside nested compounds remain unsupported by the hosted IR.

## Consequences and evidence

The contract copies the complete unchanged `syscall_sleep_ms` helper after checking it against active source. It lowers to 14 exact instructions with an abstract stack depth of two. Condition evaluation starts at instruction 3. `BRANCH_ZERO` at instruction 10 exits to instruction 13, the body call is instruction 11, and instruction 12 jumps back to instruction 3. The implicit void return occupies instruction 13.

A focused terminal-body fixture lowers to five exact instructions. Its false branch lands on the following implicit return, while its true path returns from the loop body. The slice contains no `JUMP`, which pins the no-backward-edge path.

The deterministic ELF32 object contains one 82-byte local function and 39 decoded instructions. Its conditional exit lands at byte offset 80, and its backward jump lands at byte offset 20. Three `R_386_PC32` relocations with addend `-4` name `timer_get_uptime_ms` at offsets 11 and 21 and `process_yield` at offset 71. The object has four symbols, repeats byte for byte, and leaves the frozen frontend unit unchanged.

A `long long` condition receives `CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE` at both public seams. A post-test `do` statement remains the unsupported-statement fixture. Failed lowering rewinds temporary allocations and publishes no partial IR or object.

This is hosted bootstrap evidence. The private in-kernel compiler still produces every normal OS C object. No production artifact, build owner, host dependency, boot path, or runtime ABI changed.

Issue #25 remains open. Other loop forms, loop-control statements, nested declarations, broader types and addresses, production integration, and staged self-hosting still remain.
