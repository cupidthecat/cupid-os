# Lower counted for loops through CupidC IR

## Context

The shared CupidC frontend already publishes typed C11 `for` statements, but hosted IR lowering stopped at that statement kind. Active browser source contains a loop whose control clauses use the represented four-byte integer subset:

```c
for (i = 0; i < 8; i = i + 1)
```

This loop appears in `url_hash_hex` in `bin/browser/url_hash.cc`. Its initializer and iteration use plain assignment, and its condition uses signed less-than. Those operations already reach deterministic ELF32 emission. The missing part was the loop order and its branches.

## Decision

`ctool_c_lower_ir` accepts a `for` statement with no initializer or an expression initializer. It evaluates that initializer once. It then records the condition target, evaluates an optional represented four-byte integer condition, lowers the body, evaluates an optional discarded iteration expression, and emits a backward `JUMP` to the condition target.

An omitted condition denotes C's nonzero constant and has no false exit. Such a loop cannot fall through while `break` remains unsupported. A present condition emits `BRANCH_ZERO` and may reach the following statement.

When the body cannot reach the iteration expression, lowering emits neither the iteration nor the backward jump. It still checks the iteration in a count-only context, so an unreachable malformed or unsupported expression cannot pass silently. The condition remains before the body and is always reachable on the first test.

Declaration initializers remain unsupported because nested block-binding ownership is not part of this IR slice. `break` and `continue` also remain unsupported. A later `continue` implementation must target the iteration expression when one exists, then reach the condition. A later `break` implementation must target the loop exit.

No new IR instruction is needed. The existing expression, `BRANCH_ZERO`, and `JUMP` instructions preserve the C execution order and keep branch targets relative to one function.

## Consequences and evidence

The IR contract guards the unchanged browser loop header and lowers a focused function with the same control clauses. Its exact instruction stream has 23 instructions, reaches an abstract stack depth of three, exits at instruction 20, and jumps back to instruction 4. Separate fixtures cover an omitted initializer, an omitted iteration, an omitted condition, a terminal body, and an infinite loop that does not fall through.

The deterministic ELF32 object contains one 107-byte local function with 58 decoded instructions. Its false exit lands at byte offset 96, its backward jump lands at byte offset 21, and it has no relocations. Repeated emission is byte-identical and leaves the frozen frontend unit unchanged.

Negative contracts reject a 64-bit condition, a 64-bit iteration that is unreachable after a terminal body, a declaration initializer, `break`, and `continue`. Every rejected IR or object operation preserves the input unit. Object failure also leaves the output empty.

This is hosted bootstrap evidence. The private in-kernel compiler still produces every normal OS C object, so this change transfers no build ownership and does not require an OS boot claim.

Issue #25 remains open. Declaration initializers, declarations inside nested compounds, `break`, `continue`, `switch`, labels, `goto`, broader values and addresses, production integration, and staged self-hosting remain.

## Extension

ADR 0036 adds declaration initializers through the existing automatic-local path. The initializer's bindings become visible before its condition, iteration, and body are lowered. The loop order and branch rules in this decision remain unchanged.
