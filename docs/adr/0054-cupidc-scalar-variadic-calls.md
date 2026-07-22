# Lower scalar variadic calls through hosted CupidC

## Context

The shared CupidC frontend already understood variadic function types, but it rejected every argument after the named parameters. Linear IR also rejected variadic definitions and calls. The active-source audit records 49 variadic declarations, including `serial_printf` and `klog`. Keeping those interfaces unchanged is part of making the compiler fit Cupid OS rather than trimming Cupid OS to fit the compiler.

The immediate requirement is the caller side of the i386 cdecl contract. A variadic caller applies the default argument promotions, places every argument on the stack, and removes the argument area after the call. The callee still needs a way to traverse unnamed arguments before functions such as `serial_printf` can move to the shared path.

## Decision

For arguments after the named parameter list, the frontend now applies the represented part of the C default argument promotions. Arrays and function designators decay to pointers, lvalues undergo lvalue conversion, and `_Bool`, character types, short types, and narrow enums receive integer promotion. On Cupid's i386 target, each represented promoted integer or pointer occupies one four-byte cdecl slot.

A `float` ellipsis argument stops with a focused diagnostic because C requires promotion to `double`, and the shared runtime value path does not yet represent that conversion. Complete structure, union, `double`, and wider integer arguments may remain valid typed AST expressions, but IR rejects them at its narrower ABI boundary. This keeps frontend semantics separate from target transport without claiming value support that does not exist.

Each public `CALL_DIRECT` or `CALL_INDIRECT` instruction now records its actual argument count. The function type still records the named parameter count. Non-call instructions keep an argument count of zero. Fixed calls require equality between the two counts. Variadic calls require at least the named count and may carry additional represented scalar values.

Linear IR accepts variadic definitions and lowers their named parameters and body normally. It does not invent access to the unnamed arguments. For a variadic call, the lowerer checks named arguments against their declared parameter types. Each ellipsis argument must already be a represented four-byte integer or pointer after frontend conversion. Structure-valued, floating, and wider scalar ellipsis arguments receive a transactional ABI diagnostic.

The i386 emitter uses the actual count for abstract stack effects, argument reversal, padding, caller cleanup, and the saved callee offset of an indirect call. Every supported direct or indirect call still places ESP on a sixteen-byte boundary immediately before `CALL`. The structure-aware call path continues to size named structure arguments from the function type and treats supported ellipsis arguments as four-byte scalar slots. Hidden structure-result handling is unchanged.

ADR 0055 later adds a target `__builtin_va_list` cursor and scalar `va_start`, `va_arg`, `va_copy`, and `va_end` operations for i386 callees. Floating default promotions, non-scalar ellipsis transport, and wider or aggregate variadic reads remain open.

This decision reopens ADR 0017's choice not to duplicate fixed-call arity on each call instruction. That choice remains sound for fixed prototypes, where the function type determines the count. A variadic function type records only its named parameters, so its call instruction must retain the caller-owned actual count. Reconstructing the count in the emitter from AST was rejected because target emission consumes Linear IR, not frontend syntax. Treating every extra argument as a raw word in the frontend was rejected because it would skip C's required conversions and would make narrow values depend on stale high bits.

## Consequences and evidence

The frontend contract passes a signed narrow integer, a pointer lvalue, and a narrow string array through an extra-argument list. It checks lvalue conversion, integer promotion, the final signed `int` type, pointer lvalue conversion, array-to-pointer conversion, and source-ordered call children. A separate `float` case proves the focused default-to-`double` boundary.

The IR contract covers a direct call with three actual arguments, an indirect call with two, and a variadic definition that returns its named parameter. It checks the retained actual counts, named counts, maximum stack depths, and zero argument counts on every non-call instruction. Passing a structure through the ellipsis receives the scalar-only ABI diagnostic without changing the frozen frontend unit.

The object contract emits direct and register-indirect three-argument calls, a named structure argument followed by a scalar ellipsis argument, and a variadic definition. Decoder-driven control-flow analysis checks sixteen-byte ESP alignment. A symbolic stack oracle proves that `0x11223344`, `0x55667788`, and `0x99aabbcc` remain in cdecl order after padding. It follows the indirect callee from `EBP + 8` through its saved stack slot and requires caller cleanup to remove the callee, all three arguments, and padding. The structure oracle pins its `EBP + 8` parameter address, complete copies, zeroed ABI area, direct call, and 32-byte caller cleanup. Repeat emission stays byte-identical.

This is hosted bootstrap evidence. GCC or Clang still builds the shared compiler and the normal Cupid OS C objects. The private in-kernel compiler remains the embedded JIT and AOT path, and no production artifact or host dependency changes here.

Issue #25 remains open. ADR 0055 adds scalar unnamed-argument reads. Floating and wider runtime values, remaining aggregate forms, production integration, staged self-hosting, and the fixed-point bootstrap still remain.

## Extension: eight-byte ellipsis arguments

ADR 0075 extends caller-owned metadata from an actual count to a packed slice of actual post-conversion types. Direct and indirect callers use that slice to place signed or unsigned eight-byte ellipsis arguments in two adjacent cdecl words while retaining the alignment and cleanup rules from this decision.

## Extension: variadic double arguments

ADR 0076 lets a value that is already `double` occupy eight bytes at an ellipsis position. The packed actual type selects that width. A source `float` remains unsupported because C requires the still-unimplemented default promotion to `double`.
