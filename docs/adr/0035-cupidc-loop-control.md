# Resolve loop control through CupidC IR

## Context

The shared CupidC frontend already publishes typed, targetless `break` and `continue` statements. Hosted IR lowering could not use them, even though active compiler source depends on both forms in ordinary loops. `cir_validate_initializer_ownership` in `toolchain/cupidc_ir.c` contains this unchanged `continue`:

```c
if (initializer->kind != CTOOL_C_INITIALIZER_LIST) {
  continue;
}
```

The same function stops its validation loop with an unchanged `break` after recording an invalid initializer. Rewriting either loop would hide an active language requirement instead of improving CupidC.

The continuation target depends on the loop. A `while` continues at its condition. A `do` continues at its condition after the body. A `for` continues at its iteration expression when one is present, or at its condition otherwise. The `do` condition and `for` iteration targets are not known when the body first emits a jump.

## Decision

IR lowering keeps a private stack of loop frames. Each frame records the first instruction emitted inside the loop, its continuation target when known, and whether reachable `break` or `continue` statements were seen. The innermost frame owns each loop-control statement, which gives nested loops the C nearest-loop rule.

Both statements emit the existing `JUMP` instruction. A `while` continuation and a `for` continuation without an iteration expression receive their target immediately. A `do` continuation and a `for` continuation with an iteration expression receive a private patch tag. Lowering resolves those jumps once the target exists, clears the tag, and publishes only ordinary function-relative branch targets. A `break` is patched to the instruction after its loop.

Loop-control flow also affects reachable code. A reachable `break` lets a loop with no condition reach the following statement. A reachable `continue` can make a `do` condition or `for` iteration reachable even when the body cannot fall through normally. Count-only validation uses a copied context, so a loop-control statement after a terminal return cannot change the reachable frame.

No public instruction or target-specific detail is added. `BRANCH_ZERO` and `JUMP` already carry the needed control flow through the i386 object emitter.

`break` inside `switch` is not part of this decision because hosted IR does not lower `switch` yet. A future switch implementation must distinguish switch exits from loop continuations while preserving the nearest enclosing target.

## Consequences and evidence

The two break functions lower to eight exact instructions. The guarded `for` break reaches the loop exit, while the unconditional `do` break skips its condition. Six continue and nesting functions add 47 exact instructions. Their contracts check `while`, `do`, and both forms of `for` continuation targets, plus nearest-loop binding for nested `break` and `continue`.

The deterministic object contract combines the existing 107-byte browser loop with eight loop-control functions. Those functions add 319 text bytes, for 426 text bytes in total. The object has ten symbols including the null symbol, no relocations, fixed decoded branch targets, and byte-identical repeated emission.

Negative contracts reject a `break` statement with an expression payload and a structurally valid `continue` statement placed outside a loop. IR failure preserves the frontend unit. Object failure also leaves its output empty and rewinds temporary allocations.

This is hosted bootstrap evidence. The private in-kernel compiler still produces every normal OS C object, so the change transfers no build ownership and does not require an OS boot claim.

Issue #25 remains open. Declaration-initialized loops, declarations inside nested compounds, `switch`, labels, `goto`, broader values and addresses, production integration, and staged self-hosting remain.
