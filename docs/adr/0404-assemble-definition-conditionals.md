# ADR 0404: Assemble definition conditionals

Date: 2026-09-23

Status: Accepted

## Context

The native artifact verifier needs UTF-8 command-line arguments on Windows.
Its startup wrapper needs a selectable wide entry. The installed assembler
cannot parse that selector.
Conditional assembly is a toolchain prerequisite; the verifier draft is saved
separately until this capability has a checked seed release.

## Decision

Support `%ifdef`, `%ifndef`, `%else` and `%endif` in the shared assembler.
Test whether a constant definition exists at that point in the source, including
zero-valued `%define` and request constants. Labels and absolute/EQU symbols do
not count. Names follow the existing case and local-scope rules. Directive names
are case-insensitive.

Retain at most 64 nested conditions. An inactive parent keeps its children
inactive without resolving their names. Skip inactive ordinary lines before
tokenization, so their strings, instructions, definitions and includes have no
effect. Still validate conditional control syntax and nesting in skipped blocks.

Includes share definitions but must balance their own conditional blocks. An
included file cannot close its caller's block. Reject missing or extra operands,
unmatched terminators, repeated `%else`, unterminated blocks and excess nesting.
Failure keeps the existing API's cleared result and the CLI's previous output.

This adds definition tests, not expression-based `%if`, `%elif`, macros or
`%undef`. Installed seed identities and production ownership remain unchanged
until separate staged, promotion and OS acceptance checks pass.

## Evidence

Both native and Cupid-built assemblers pass 23 conditional CLI cases. The new
C API mode covers thirteen rows, including case folding, request-definition kinds,
local scope and cleared failure results. It and all twelve preceding contract
modes pass on both hosts, alongside four include cases. The active 49-method
assembler suite passes on each host with four platform skips.

Independent rehashing verifies the retained checked sources, objects, programs
and reports. The shared assembler and contract objects match across hosts.
Both assemblers reproduce the prior ANSI and tested wide startup objects byte
for byte when the selector preserves declaration order. Staged behavior now
requires a successful selection and a failed unterminated block with preserved
output. Canonical staged proofs, Linux publication and paired OS/runtime
acceptance pass. Independent rehashing covers 1,501 captured files, sixteen
artifacts and 431 link inputs per host. Matching images pass four-CPU
disassembly/shell smokes. Installed seeds remain unchanged pending promotion.

Switching startup alone exposed ANSI filesystem and child-process boundaries
in existing Windows commands. Their Unicode adapter work remains private;
this decision accepts the assembler capability, not that later integration.

The first private implementation omitted initialization of the new stack fields
and crashed. Later tests found unqualified local-name lookup, then unnecessary
local-name resolution under an inactive parent. Each failed candidate is retained;
the final tests cover all three repairs. Moving an import declaration in the
startup selector changed ELF symbol order; keeping its original position restored
object identity. No startup source has yet adopted the selector.

Two additional checked probes found a mismatch between the skipped-line scanner
and the normal lexer: carriage-return whitespace hid a terminator, and an
identifier beginning with `endif` wrongly triggered string tokenization. The
scanner now shares the lexer's identifier classification and whitespace rules.
Both cases are regression fixtures; the incomplete v3 staged and kernel runs
were retired explicitly before acceptance and are retained as superseded evidence.
