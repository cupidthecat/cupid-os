# Bound private CupidC statement depth

- Status: Accepted
- Date: 2026-07-26

## Context

The in-kernel CupidC parser stores 128 tagged loop-or-switch controls. ADR
0078 left a 129-switch negative test out because parser recovery exhausted
the terminal task's 4 MiB stack. The intended depth error appeared first, but
the parser then reached heap corruption and a kernel panic.

The checked CupidC compiler did not optimize automatic storage shared by
branches in `cc_parse_statement`. Its native frame was 86,504 bytes because
token values and declaration scratch from unrelated statement forms occupied
one frame. Every nested switch kept another copy alive. Reducing the
128-control language capacity would have hidden that implementation problem.

Control depth alone is not enough. Nested `if` statements and blocks recurse
without adding a loop-or-switch frame, so they need their own fail-closed
limit.

## Decision

Keep the 128-control capacity. Reject a 129th active loop or switch with
`control nesting too deep` before parsing its body. Use one checked helper to
open controls and one helper to patch breaks and restore the prior depth.

Make `cc_parse_statement` a small recursive dispatcher. Move token-heavy
simple statements to a nonrecursive helper, and move `do`, `switch`, and the
token-heavy parts of `for` into helpers whose scratch storage is not live
while a nested statement is parsed.

Track active statement-parser calls separately. Accept 1,024 active calls and
reject the next one with `statement nesting too deep` before recursing. Reset
the statement count when compiler state is initialized, when a REPL
evaluation starts, and when failed evaluation restores the committed state.

The 1,024 limit comes from the generated code, not from a smaller language
target. CupidDis measures a four-byte `cc_parse_statement` frame and a
1,056-byte repeatable `cc_parse_if` frame. The accepted chain also reaches one
64,452-byte simple-statement leaf. Its complete frame payload is about 1.1
MiB before normal call overhead, below the terminal task's 4 MiB stack. The
repeatable switch frame is 4,204 bytes, so 128 switch levels use about 526 KiB
of dispatcher and switch frame payload before the same one-time leaf.

## Rejected alternatives

Lowering the control limit was rejected because the existing 128 entries are
a deliberate language capacity. The parser must fit that capacity.

Increasing the terminal stack was rejected because it would leave recursive
parsing unbounded and would make an ordinary compiler input consume more
kernel memory.

Keeping only the control-depth check was rejected because nested statements
that are not loops or switches would still rely on the native stack to stop
them.

Rewriting active programs to avoid nesting was rejected because source
requirements drive CupidC. Programs should not be shaped around a compiler
frame accident.

## Evidence

The first real guest red test built 128 nested switches and invoked them
through `repl_eval`. The old 86,504-byte recursive frame reached heap
corruption at `0x01c00000` and panicked before it could publish the positive
result.

After the dispatcher split and control guard, a second guest red test reported
`control=1 overflow=1 recovery=1 statement=1 statement-overflow=0
statement-recovery=1`. This isolated the missing general statement limit:
128 controls passed, the 129th failed, the next evaluation succeeded, and the
unguarded statement overflow was still accepted.

The checked CupidC wrapper compiles both changed private compiler objects.
CupidDis measures the recursive frames described above. The final guest
contract builds the nested sources at runtime and checks 128 controls, a
129th-control failure, 1,024 active statement calls, a 1,025th-call failure,
and a successful evaluation after each rejection.

## Consequences

The private parser fails before unsafe recursion instead of relying on heap
canaries or a kernel panic. A rejected REPL input does not poison the next
evaluation.

The large simple-statement frame still exists, but it is not live across a
nested statement call. Further compiler optimization can reduce that frame
without changing these language limits.

This changes private JIT and AOT parser safety. It does not transfer a build
transform, remove a host dependency, or close the broader Cupid-mode work.
