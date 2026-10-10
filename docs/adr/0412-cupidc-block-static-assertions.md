# CupidC block-scope static assertions

- Status: Accepted
- Date: 2026-10-03

## Context

Issue #25 includes C11 static assertions. Shared CupidC already checked them
at file and record scope, but rejected `_Static_assert` as a function block
item. Moving a check to file scope would lose access to local typedefs, enum
constants, and object layout. A check after `return` must still be diagnosed.

## Decision

Use the existing target integer constant evaluator for assertions in function
compounds. Both strict C11 and Cupid mode accept them without GNU extensions.
Lexical lookup keeps local and parameter shadowing, scope expiry, and the
activation point of names introduced by enum type definitions inside a query.

Publish each successful assertion as a declaration statement. Its binding
slice owns any newly introduced enumerators; an ordinary assertion has an
empty slice at the current lexical cursor. Existing Linear IR validates that
ownership and emits no assertion instruction, frame slot, or ELF symbol.
Unevaluated operands and literal scratch keep the existing query rewind.

An assertion remains a declaration. It cannot replace the statement following
an `if`, loop, label, or case label. The separate `for` initializer assertion
boundary remains deferred. The existing integer-constant-expression grammar
and configured syntax, arena, output, and diagnostic limits still apply.

A statement-free result was rejected because an assertion may introduce enum
names that subsequent source uses. Reusing declaration ownership avoids a new
AST or IR kind and keeps that lexical event visible to the validators.

## Evidence

The focused frontend and object selectors pass on Windows and Linux. Frontend
cases cover strict C11 and Cupid mode, local typedefs and arrays, parameters,
nested enum shadowing and restoration, enum definitions inside type and
expression queries, an assertion-only body, and checks after `return`.
Unevaluated calls and arithmetic retain no runtime operand expressions.

Failures cover false assertions, concatenated messages with exact source
location, nonconstant parameters and shadowing objects, expired enums,
parameter conflicts, malformed messages and missing semicolons, selected
arithmetic faults, invalid statement placement, occupied syntax limits, and
bounded diagnostic storage. Failure leaves the tape, arena, prior result and
anchor intact; the same job recovers.

The object contract emits assertion-bearing and equivalent assertion-free
functions to identical ELF32 bytes, repeats emission without changing the
frozen unit, and requires no relocation or enumerator symbol. It covers an
assertion-created enum after `return` as well as an assertion-only function.
The public Cupid-built driver accepts the positive fixture, matches native
output, and rejects a false assertion without replacing a prior object.

The initial Windows frontend and IR selection passes 184 tests. A fresh Linux
Clang selection passes those 184 and three object/driver/frontier checks. The
final Windows selection passes all 300 methods outside the dedicated static
fixed-point check in 409.878 seconds; that separate check passes in 992.407
seconds. CupidC, CupidASM, CupidDis, CupidLD and CupidObj reproduce their
objects and executable bytes across successive Cupid-built generations.
This test does not cover CupidBuild. OS evidence belongs in the bootstrap log.
The complete final Linux selection, including the same static fixed-point
method, passes all 301 tests with default GCC in 1,312.953 seconds.

The normal Windows build passes all sixteen exact artifact checks with host
compiler, assembler, linker and object utilities forbidden. A strict private
four-CPU QEMU boot passes runtime checks and completes `ls` in the Terminal.
The in-image manual adds 340 raw-kernel bytes; only that exact size-policy row
changes. The final active-source audit passes. This is a Windows clean-root
OS qualification; it does not establish a paired Linux OS build.

## Inventory repair and limitations

The initial selection exposed stale source inventories and frontier locks.
The refreshed contracts retain exact AST and object locks. The generic header
sweep now records a single preprocessing diagnostic as well as a parser
failure, so it visits all 175 selected headers. It pins 171 successes and four
explicit failures, including the Windows UTF-8 bridge under the generic Linux
header profile. No header is removed to make that check pass.

The default GCC object-contract build initially reports `maybe-uninitialized`
in the existing floating-update IR implementation. Move the old-value check
immediately after its successful postfix stack pop. Prefix updates never
inspect that value, and failed pops still return their error. The strict GCC
compile now passes without relaxing warnings. A fixture containing all four
update forms at both floating widths emits the same complete ELF bytes before
and after the guard change. Existing signed-zero, NaN, indirect-lvalue and
transactional IR contracts pass. The exact hosted IR source locks are refreshed.

This is compiler-head work. It changes neither the installed checked seeds
nor normal transform ownership, and it does not add assertion support to the
private in-kernel compiler. Seed promotion and the broader #25 acceptance
remain separate work. TempleOS stays read-only and outside every inventory.
