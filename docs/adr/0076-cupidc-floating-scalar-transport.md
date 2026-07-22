# Carry floating scalar values through hosted CupidC

- Status: Accepted
- Date: 2026-07-22

## Context

The active-source audit finds 339 `float` spellings and 392 `double` spellings across 31 distinct files. The shared CupidC type graph already described both types, but runtime expressions stopped before Linear IR. That left ordinary object access, calls, returns, and variadic `double` values outside the hosted bootstrap path.

The first useful boundary is transport, not arithmetic. Ordinary object movement must copy the stored target bytes, while calls and returns must follow the i386 C ABI without pretending that conversion, comparison, or computation exists. The unchanged `js_push_num` body in `bin/browser/js_interp.cc` provides one active assignment requirement. Focused contracts cover the wider ABI surface without claiming ownership of that translation unit.

## Decision

The shared frontend accepts same-kind `float` or `double` values in automatic initialization, plain assignment, fixed prototyped arguments, and return expressions. Compatible qualified and aligned wrappers retain the underlying floating kind. Mixed integer and floating assignment, conversion between `float` and `double`, and `long double` remain unsupported with focused diagnostics.

Linear IR treats each supported floating value as one semantic stack entry. A `float` entry carries its raw four-byte representation. A `double` entry names an emitter-owned eight-byte snapshot. `LOAD`, `STORE`, `STORE_VALUE`, `DISCARD`, fixed direct or indirect calls, call results, parameters, `RETURN_VALUE`, and non-atomic `VARIADIC_ARGUMENT` accept these represented types. The handle is an internal transport value, not a pointer exposed to Cupid C source.

Transport does not make a floating value a supported truth operand. Linear IR uses a separate integer-or-pointer predicate for logical operands, the condition of `?:`, rejection of unsupported `?:` result types, and statement conditions. The emitter applies the same rule to logical-not and branch metadata. A malformed frozen unit therefore cannot turn the raw `float` word or the private `double` snapshot handle into a Boolean test.

The i386 emitter uses byte copies for ordinary floating loads, stores, and argument setup. A `float` argument occupies one four-byte cdecl slot. A `double` argument occupies eight adjacent bytes. Fixed direct and indirect calls use those widths for parameter offsets, outgoing storage, sixteen-byte call-site alignment, and caller cleanup. A variadic caller can pass a value that is already `double`, and `va_arg(double)` copies eight bytes into a fresh snapshot before advancing the cursor by eight. The x87 return bridge is a separate ABI operation, not a byte-copy claim.

The i386 floating return boundary uses x87 register `ST0`. A callee loads a returned `float` or `double` from its represented value with `FLD`. After call cleanup, a caller uses `FSTP` to place a `float` result in a four-byte semantic stack slot or a `double` result in a private eight-byte frame snapshot. This keeps x87 state at the ABI edge and prevents the next Linear IR instruction from depending on a live x87 stack entry. Floating arithmetic and comparison do not use this bridge.

An ellipsis or unprototyped argument that starts as `float` still needs the C default promotion to `double`, so it remains unsupported. `va_arg(float)` is invalid C and receives a diagnostic that directs the source to request `double`. Atomic floating access, floating conditional expressions and conditions, unary and binary operators, value-producing floating conversions, explicit static floating initializers, `long double`, SIMD values, and over-aligned object emission remain outside this decision. Implicit zero initialization and casts to `void` use existing paths.

Malformed metadata and storage limits retain the existing transactional behavior. A failed lowering or emission publishes no partial result, and the same job can recover. Floating mutations at both the IR and object boundaries cover the truth-operation rule.

## Rejected alternatives

Returning floating payloads in EAX or EDX:EAX would reuse the integer machinery but violate the i386 C ABI. It would also hide interoperability bugs until a host-built caller or callee crossed the boundary.

Representing `double` as two public Linear IR values would expose target word order to a target-neutral interface and make one C value look like two operands. The existing private snapshot model keeps the semantic stack accurate.

Leaving call results live in `ST0` until their eventual use would couple unrelated IR instructions to x87 stack depth. The immediate `FSTP` gives each result ordinary value lifetime and makes nested calls deterministic.

## Consequences and evidence

The frontend proof covers four floating initializers, seven assignments, four fixed calls, one variadic call, `va_arg(double)`, and four returns across nine functions. Negative cases retain precise boundaries for `double` to `float`, `float` to integer, `long double`, default `float` promotion, `va_arg(float)`, and positive- or negative-zero floating conditions.

The IR proof checks seven functions, the exact type of every floating instruction and call argument slice, repeat lowering, constrained allocation, and an unchanged active-source guard for the `js_push_num` declaration and assignment.

The deterministic ELF32 proof contains 39 functions, 4,121 text bytes with fingerprint `438A39AB`, 24 BSS bytes, 44 symbols including the null symbol, and 36 relocations. It covers automatic, file, block-static, pointer-derived, indexed, and ordinary-member objects; fixed direct and indirect calls; direct and indirect variadic calls; direct and indirect unprototyped calls; direct, indirect, and nested returns; one and two successive variadic reads; a copied cursor; and discarded `double`. Its decoder-driven oracle checks sixteen-byte ESP alignment before every executed direct or indirect `CALL`. It models `FLD` and `FSTP` as bit copies and round-trips positive zero, negative zero, infinity, and one quiet NaN pattern for each width. This is not a native x87 execution proof. Constrained output, floating truth metadata, and storage failures leave no partial object, and recovery reproduces the same bytes.

The verified hosted suites contain 52 frontend tests, 38 IR tests, and 32 object tests. This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler and every normal Cupid OS C object. No production artifact, boot path, host dependency, or source-ownership count changes.

Issue #25 remains open for floating computation and conversion, the remaining C and ABI surface, production integration, staged self-hosting, and the fixed-point bootstrap.
