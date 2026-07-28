# ADR 0159: Normalize GNU named assembly operands

## Status

Accepted on 2026-07-28.

## Context

Once CupidC could emit the file-scope wrappers at the start of
`kernel/cpu/libm.c`, the next parser boundary was a function-body statement
that refers to its operands as `%[x]`, `%[out]`, and similar GNU labels.
CupidC's public assembly records and i386 emitter already use compact numeric
placeholders, so carrying a second naming scheme through the rest of the
compiler would have added state without solving a new code-generation
problem.

## Decision

GNU statement assembly accepts an optional `[identifier]` before each input
or output constraint. The labels exist only while the statement is being
parsed. After all outputs and inputs are known, the parser rewrites each
`%[identifier]` reference to its numeric operand index. The public frontend
record, Linear IR, and the i386 emitter keep their existing representation.

The parser rejects empty, duplicate, unterminated, and unresolved labels.
Named operands pass through the same lvalue, register-object, atomic, type,
and constraint checks as numeric operands. A doubled percent sign remains an
escaped percent sign; text such as `%%[name]` is not treated as an operand
reference. Numeric operands continue to work unchanged. Named matching
constraints, such as an input constraint tied to `[out]`, remain unsupported.

## Evidence

The frontend contracts accept named RDRAND and SETC register outputs as well
as named x87 memory operands, then check the canonical numeric templates that
reach the public graph. Negative cases cover malformed names, duplicate
names, unknown references, named matching constraints, const and register
outputs, atomic outputs, rvalue and register inputs, atomic inputs, and
escaped percent text. The Linear IR and object fixtures use the named source
spelling while retaining the same evaluation order, instruction bytes,
relocations, and execution oracle.

The unchanged `kernel/cpu/libm.c` probe now passes the named-operand syntax
and stops at the wider `libm_pow_impl` statement:

```text
/kernel/cpu/libm.c:764:5: error CTB00000F: GNU inline assembly m input template is outside this slice
```

That statement has one memory output and four memory inputs. Representing its
exact x87 operation is separate work.

## Consequences

This change advances compiler head only. The checked seed still describes
revision `c00b3494014ca0a5f41143caa7e713e46b2ad3ec`, and the normal
`libm.c` recipe remains host-owned. No ABI, production object, runtime path,
file suffix, or host-dependency count changes here.
