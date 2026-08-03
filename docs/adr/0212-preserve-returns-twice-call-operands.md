# ADR 0212: Preserve operands across returns-twice calls

## Status

Accepted on 2026-08-01.

## Context

`dg_setjmp` can appear inside an ordinary C expression. In
`int result = dg_setjmp(env)`, CupidC keeps the address of `result` on its
Linear IR value stack while it calls the function. A later `dg_longjmp`
resumes after that call, so the pending address must still be available for
the assignment.

The ordinary direct-call path left live operands on the machine stack. That
works when a call returns once, but the old dglibc jump frame saved ESP at
function entry. Restoring that value put the stack four bytes below the state
expected after `ret`. The long-jump arguments then occupied the caller's live
operand slots. In the assignment above, the resumed store used the jump value
as an address and could loop or fault before the self-test marker appeared.

GNU C marks this control-flow boundary with `returns_twice`. CupidC already
carried declaration attributes and analyzed the abstract stack depth at every
instruction, but it did not represent this attribute or use the depth record
to protect a suspended call expression.

## Decision

Represent `returns_twice` and `__returns_twice__`, with an optional empty
parenthesized form, as a function declaration attribute. Compatible
file-scope redeclarations merge the property onto the canonical binding.
Objects, records, members, and attribute arguments fail with the normal
attribute diagnostic.

Treat the property as a call-site control-flow rule, not as a function-body
code-generation option. A marked function must remain a direct call target.
Linear IR and static initializer emission reject conversion of its designator
to a function pointer because the current pointer type does not carry the
attribute.

Before each supported call, use the validated Linear IR stack depth to count
the operands below its arguments. Copy those live words into a frame region
owned by that call instruction. After the call and its alignment padding have
been reclaimed, restore the words before publishing the call result. Each
live-prefix call receives a separate region. A call with no live prefix
allocates no spill region and may repeat in a loop.

Seed one multi-source control-flow traversal with the continuation after every
marked call. The traversal visits each reachable instruction and edge at most
once, so the check is linear in the function's IR graph. Reject a live-prefix
call if that traversal reaches it. This covers a call reached again through a
loop and a later call reached after an earlier checkpoint. Branch-exclusive
live-prefix calls remain valid and keep separate spill regions.

Limit this boundary to direct calls with four-byte cdecl arguments. The result
may be void or any nonaggregate type, including a wide integer or floating
value. Aggregate, wide-integer, and wider-than-four-byte floating arguments,
and aggregate results, fail with a specific unsupported diagnostic. An
unmarked call keeps its existing frame and byte sequence.

Correct the represented dglibc jump template at the same compiler boundary.
`dg_setjmp` now saves `ESP + 4`, the stack value that an ordinary `ret` would
leave behind, and stores the return address separately. The corrected template
requires `returns_twice` on `dg_setjmp` and `noreturn` on `dg_longjmp`.

Retain the exact dglibc compatibility template with its unannotated
declarations and 27-byte setjmp body. Compiler head can therefore reproduce
the current checked-seed production object. The compatibility source is not
reinterpreted as the corrected sequence.

## Evidence

The declaration contract accepts both GNU spellings, both redeclaration
orders, and the optional empty parentheses. It rejects arguments and invalid
object, record, and member placements.

The base call contract emits the same ELF32 object twice. It covers a marked
call with no live prefix, an initializer with a hidden lvalue, arithmetic with
one live operand, and two branch-exclusive live-prefix sites. The object has
334 text bytes, fingerprint `FCE5B12C`, five `R_386_PC32` relocations, and
exact spill and restore sequences.

Separate fixtures cover attribute propagation through a later file-scope
declaration, a block declaration, and an attributed definition. Call-shape
fixtures cover zero and multiple arguments, void, `long long`, and `double`
results, and loop repetition without a live prefix. A 64-branch switch fixture
covers mutually exclusive live-prefix calls that converge on one shared tail.
Decoder checks require all 64 calls and 64 restores, with each restore matched
to a frame store. Negative cases reject live-prefix self-reentry, a later
live-prefix call reached from an earlier checkpoint, automatic and static
function-pointer conversions, aggregate, wide-integer, and `double` arguments,
an aggregate result, and constrained output. A constrained failure is followed
by byte-identical same-job recovery.

A decoder-driven i386 execution oracle models the emitted caller's first and
second returns with transfer values zero and seven. It checks call alignment,
restored expression state, zero-to-one normalization, final results, and
preserved stack, frame, and register state. This is modeled execution of the
emitted caller, not a guest run of active dglibc.

The dglibc assembly contract decodes both forms. The compatibility form keeps
its 27-byte setjmp body. The corrected form has a 31-byte setjmp body with
`LEA ECX, [ESP + 4]`; both share the 38-byte longjmp body and have no
relocations. Changed template text, missing or mismatched declarations,
missing `returns_twice`, output exhaustion, rollback, and recovery are
covered.

The repository's checked-seed test separately compiles each unchanged Doom
compatibility root twice and checks the existing locks. The focused run for
this decision exercised the exact-profile test: it compiles the same roots with
native source-head CupidC and Cupid-built source-head CupidC, requires their
outputs to match, and checks the same locks. The exact-profile test passed, so
the compiler change has not altered a production object before seed promotion.

ADR 0213 promotes this compiler boundary into the checked seed. Its focused
carriage test emits the same 500-byte returns-twice object twice and rejects a
marked-function pointer conversion without replacing an existing output.

## Rejected alternatives

Rewriting the dglibc self-test so the assignment address was not live across
`dg_setjmp` was rejected. The source exposed a real control-flow requirement;
moving the expression would hide the compiler defect.

One function-wide scratch region was rejected. Each live-prefix call has a
region sized from its own validated depth. The separate control-flow check
rejects any continuation that could revisit such a site.

Saving the old entry ESP and compensating only in `dg_longjmp` was rejected.
The jump buffer is supposed to describe the caller state after a normal
return. Recording that state directly keeps the frame contract local to
`dg_setjmp` and matches the resumed instruction's cleanup rules.

## Consequences

Compiler head preserves represented call expressions across the supported
direct returns-twice boundary. The decoder-driven oracle covers the modeled
second return, but it is not guest runtime proof. The checked seed now carries
the compiler boundary. Active dglibc still uses the compatibility form, so
active-source migration and guest runtime proof remain open.

Function pointers do not carry this declaration property, so CupidC rejects
conversion of a marked function to a pointer. Live-prefix reentry, aggregate
and other outgoing-area arguments, and aggregate results remain explicit
unsupported cases. General non-local-jump lifetime rules, signal masks, and
host libc compatibility are outside this boundary.

No normal build owner or host dependency changes here. `TempleOS/` remains
untouched reference material.
