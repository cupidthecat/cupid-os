# ADR 0273: Update private derived floating lvalues

## Status

Accepted on 2026-08-13.

## Context

Private CupidC already kept `float` and `double` types on pointer
dereferences, indexed elements, and record fields. It also supported prefix
and postfix `++` and `--` on direct variables. The update parser still
rejected those typed derived lvalues, which left an artificial gap between
ordinary floating assignment and floating increment or decrement.

A derived designator can call a function or advance an index while finding
its destination. C requires that work to happen once. A postfix update must
also return the exact old payload after storing the replacement. Rebuilding
the old value with inverse arithmetic would change negative zero and could
change a NaN payload.

The expression parser, standalone statement shortcuts, and `for` increment
path each had their own update entry. The `for` fallback also rewound only one
token, which was not enough to retry a call expression or a derived lvalue.

## Decision

Keep transient lvalue identity in two forms. A direct lvalue names its symbol.
A derived lvalue keeps its evaluated address in EAX and its typed value in
XMM0. Computed rvalues, casts, calls, `sizeof`, address-of, incomplete array
rows, and other nonmodifiable results clear both forms before another update
can use them.

Prefix parsing accepts the complete primary designator instead of only an
identifier. Prefix and postfix parsing both use the same lvalue identity left
by the primary expression, including a direct variable carried through one or
more grouping parentheses. For a derived `float` or `double` update, the
emitter pushes the address once, copies XMM0 to XMM2 for a postfix result,
forms exact integer one in EAX, converts it to the operand width in XMM1, and
emits the matching SSE addition or subtraction. It then restores the saved
address, stores the new value through that address, and restores XMM2 to XMM0
for postfix. Prefix leaves the stored value in XMM0.

Standalone indexed and member updates use the same typed helper. A lexer
checkpoint now saves and restores the cursor, line, current token, buffered
lookahead, and lookahead state when the `for` initializer or increment needs
the general expression parser. This lets a call initializer and a derived
floating update increment cross the fallback without token loss.

Keep the existing feature-13 source as the broad in-kernel JIT exercise. Add
a smaller active source for the external-process check. That program isolates
pointer and indexed-record updates, so the guest can compile it with `ccc`,
load the resulting ELF with `exec`, and test the same lowering without nesting
the private JIT inside a process created by that JIT.

## Evidence

Private AOT and JIT contracts cover pointer dereference, fixed-array index,
direct member, pointer member, and indexed-record member updates. They cover
prefix and postfix forms in both directions, grouped direct variables, a
side-effecting designator that must run once, binary32 negative zero, a
binary64 NaN payload, standalone statements, and a `for` increment. Negative
cases reject an indirect integer update, a computed rvalue, `sizeof`,
address-of, and an incomplete array row, then compile a valid expression in
the same parser state.

The private call-ABI suite passes 141 tests in 21.199 seconds. Six focused
feature-13 and AOT contracts pass in 0.841 seconds, and all 75 frontier runtime
contracts pass in 2.702 seconds. The guest first requires
`[feature13-indirect-update] PASS score=41 once=3 zero=0x80000000`. It then
compiles `/bin/feature13_derived_aot.cc`, loads the external ELF, requires
`[feature13-derived-aot] PASS score=41 once=2 zero=0x80000000`, and observes
that same PID exit cleanly. Both failure markers stop the run immediately.
Ruff reports no findings in the changed Python contracts, and the scoped
whitespace check passes.

The source-current four-vCPU RTL8139 run passed the complete frontier in 820.7
seconds. The guest loaded the dedicated ELF as PID 4, printed the required AOT
marker, and printed `[PROCESS] PID 4 "/feature13_derived_aot" exiting` before
the runner continued through graphics, audio, USB, network, browser, and
process-lifecycle checks.

Review found that the ISO prerequisite contract read only the first physical
line of a continued Make rule. The dependency was already present. The test now
joins continued lines before checking the rule; full frontier and guest results
are recorded in the bootstrap log.

## Rejected alternatives

Evaluating the designator again for the store was rejected because a call or
index side effect could select a different object.

Reconstructing a postfix result by applying the inverse operation was rejected
because floating arithmetic is not reversible and cannot promise the old raw
payload.

Adding separate pointer, index, and member update bodies was rejected because
all three have the same typed lvalue contract. The earlier direct-variable
implementation had already shown how duplicated parser paths drift.

Keeping the one-token `for` rewind was rejected because a complete expression
can consume the current token and buffered lookahead before the fallback
begins.

Using all of `feature13_double.cc` as the external AOT smoke was rejected
after a real guest run. That source calls `repl_eval` as part of its JIT
coverage, so executing its AOT image creates a nested private-JIT path and
traps independently of derived floating updates. Removing that coverage from
the feature source would make the existing test weaker. The dedicated AOT
program keeps the broad JIT exercise intact and gives external ELF execution
a narrow, deterministic contract.

## Consequences

Private JIT and AOT programs can update scalar `float` and `double` objects
through a supported pointer, fixed-array index, or record-field designator.
Each destination is evaluated once, prefix returns the stored value, and
postfix returns the original payload.

Indirect integer updates, floating pointer depth greater than one,
pointer-to-array types, assignment through a pointer-valued floating field,
and SIMD updates remain outside this boundary. No build owner or host-tool
dependency changes, and no `.c` source rename is due.
