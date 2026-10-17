# ADR 0220: Preserve private typedef declarator shapes

## Status

Accepted on 2026-08-03.

## Context

Private CupidC kept one base type for each `typedef`. It accepted a single
value or pointer alias, but it did not preserve the shape of an array
declarator. Ordinary C declarations such as these therefore fell outside the
private compiler:

```c
typedef struct Pair { int left; int right; } Pair, *PairPointer;
typedef int Words[4];
```

Treating `Words` as `int` would compile the spelling while allocating the
wrong object. Dropping the declaration list or replacing the alias with a raw
array at each use would make the source depend on a compiler limitation.

The private parser also needs one consistent answer for automatic, global,
block-static, record-field, class-field, and persistent REPL storage. Function
and method parameters need C array-parameter decay, while `sizeof` must keep
the complete array size for an object or type name.

## Decision

Parse every comma-separated typedef declarator and retain its own pointer
depth. Each successful alias consumes one of the existing sixteen typedef
slots. A trailing comma, a missing name, or a declaration that would overflow
the table fails before an incomplete alias is published.

Retain one positive fixed-array count with an alias. The alias can name an
integer, character, or complete record element type. Alias chains keep the
same count and element identity. Automatic, global, block-static, structure,
class, and persistent REPL declarations allocate the complete checked object.
`sizeof` reports that complete size, including character arrays. A function or
method parameter declared with an array alias decays to an element pointer.

Member access retains both the complete array-object size and the structure
identity of a record element. One lvalue traversal serves a named record, a
record pointer, and a record selected from an array. After it indexes an array
field, it may continue to a record member for a plain or compound assignment.
Parsing a tagged structure body resets declarator shape before the enclosing
typedef is published, so the final field cannot leak its array count into the
structure alias.

The private representation remains deliberately one-dimensional. Unsized and
multidimensional array aliases, pointers to array aliases, another array
declarator after an array alias, array returns, array casts, and `new` with an
array alias receive focused diagnostics. The compiler checks incomplete record
elements, nonpositive counts, multiplication overflow, and storage capacity
before committing state. A rejected declaration can be followed by a valid
request in the same compiler job.

## Evidence

Execution contracts cover value and pointer aliases in either declarator
order, multiple anonymous and tagged record aliases, automatic and global
arrays, block-static alias chains, record and class fields, parameter decay,
method parameter decay, indexing, complete `sizeof`, and persistent REPL use
across several units. Record-field coverage checks the complete member size,
record-element `sizeof`, reads, and assignments through `.` and `->`. It also
continues through an indexed record element when the outer object is a record
array or a pointer subscript.

Negative contracts cover typedef-table exhaustion, a trailing comma, an
unsized array, a zero count, count-by-stride overflow, an incomplete record
element, a second dimension, a pointer to an array alias, an additional array
declarator, function and method returns, casts, and `new`. Each diagnostic is
checked for recovery with later valid source. The exact test and build results
are recorded in `docs/bootstrap/LOG.md`.

## Rejected alternatives

Expanding an array alias into a raw array at every use site was rejected. That
would weaken ordinary source to fit the private compiler and would still leave
parameter decay and `sizeof` inconsistent.

Storing only the alias base type was rejected because it silently changes
layout. Array count is part of the declared type shape.

Accepting unsupported compound declarators as a scalar was rejected. A clear
diagnostic is safer than producing an object with the wrong ABI.

## Consequences

Private JIT, AOT, and persistent REPL source can use ordinary declaration lists
and one-dimensional fixed-array typedefs without losing storage or parameter
semantics. The existing sixteen-alias capacity is unchanged. Function-pointer
typedefs, multidimensional array aliases, pointers to arrays, and broader C
declarator composition remain open.

No build owner moves, no host dependency is removed, and `TempleOS/` remains
untouched reference material.
