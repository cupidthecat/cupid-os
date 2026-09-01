# Lower structured selection through CupidC IR

## Context

The shared CupidC frontend already publishes typed `if` statements, but the hosted IR accepted only a declaration prefix followed by one return or expression statement. The remaining blocker in `dis_signed_bits` from `toolchain/cupiddis.c` was its complete statement structure:

```c
static ctool_i32 dis_signed_bits(ctool_u32 value) {
  if (value <= 0x7fffffffu) {
    return (ctool_i32)value;
  }
  if (value == 0x80000000u) {
    return (-2147483647 - 1);
  }
  return -(ctool_i32)((~value) + 1u);
}
```

Earlier increments covered every expression in this helper. Rewriting it as one expression would hide the control flow that active source needs and would weaken the toolchain roadmap.

## Decision

`ctool_c_lower_ir` now lowers statements recursively. The supported statement set is return, expression, empty expression, compound without declarations, and `if` with optional `else`. The outer function body keeps its existing direct declaration prefix, so local ownership and fixed stack slots do not change.

An `if` condition must produce a represented four-byte integer value. Lowering emits `BRANCH_ZERO`, then lowers the true arm. An `else` arm receives a `JUMP` around it only when the true arm can reach the next statement. Both targets stay function-relative, as required by the existing public IR contract.

Each recursive call reports whether execution can continue past the statement. A return closes its path. An `if` without `else` can fall through, while an `if` with `else` can fall through when either arm can. A statement sequence stops emitting instructions when its current path cannot continue. This prevents an unreachable trailing branch from naming the one-past-end instruction. It also lets one function contain several direct return instructions without adding a shared return block or a new IR instruction. A void path that reaches the end receives the existing implicit return. Possible fallthrough from a nonvoid function remains unsupported.

Reachability does not skip validation. Each unreachable statement is lowered through a count-only validation context that inherits the current function and visible local state but has no instruction buffer. The temporary context is rolled back after each statement. Malformed references still receive the invalid-unit diagnostic, and valid syntax outside the supported statement set still receives its public feature diagnostic.

The parser nesting limit also bounds statement recursion. Frozen statement references and condition types are checked before publication. Loops, `switch`, labels, `goto`, and declarations inside nested compounds remain outside this slice.

## Consequences and evidence

Focused `if` and `if` with `else` fixtures lower to 18 exact instructions. They pin branch targets, the jump around a falling true arm, discarded expression values, and multiple returns.

The contract composes a fixture from the complete unchanged `dis_signed_bits` helper. It lowers to 27 exact instructions with an abstract stack depth of two. The two false branches land at instruction offsets 9 and 19. All three source returns stay direct.

The deterministic ELF32 object contains one 143-byte local function and 71 decoded instructions. Its branch targets are byte offsets 53 and 111. It has two symbols including the null symbol, no relocations, and repeated emission is byte-identical.

A focused edge fixture returns before an unreachable `if`, then defines a separate void function whose `if` may fall through. The unreachable function lowers to two instructions. The void function lowers to five instructions, with its false branch landing on the implicit return. Their exact object contains 38 text bytes, three symbols, no relocations, and one branch target at byte offset 25. A nonvoid function with the same falling path receives the unsupported-statement diagnostic.

A copied reachable `if` node and a separate unreachable `if` node with invalid condition references both receive the invalid-unit diagnostic. A declaration after an unconditional return receives the unsupported-statement diagnostic instead of being mistaken for malformed local ownership. A `long long` condition receives the unsupported-type diagnostic. A `while` statement receives the unsupported-statement diagnostic. Both IR and object failures leave the frozen frontend unit unchanged, rewind temporary allocations, and publish no partial output.

This is hosted bootstrap evidence. The host C compiler still produces the normal root and user C objects. The private in-kernel compiler remains the embedded runtime JIT and AOT path. No production artifact, build owner, host dependency, boot path, or runtime ABI changed.

Issue #25 remains open. Nested declarations, loops, `switch`, labels, `goto`, broader types and addresses, production integration, and staged self-hosting still remain.
