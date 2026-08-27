# CupidC Language Reference

CupidC is a C-like language for cupid-os. Its JIT compiler emits x86 machine code.

The language accepts the common C and HolyC spellings used by the shipped
programs: `U0/U8/U16/U32/I8/I16/I32`,
`U64/I64`, `float`, `double`, `float4`, `double2`, `long`, `short`,
`signed`, `unsigned`, `extern`, `inline`, `register`, `restrict`, labels,
`goto`, and `__attribute__((...))` decorations. The private compiler treats
attributes as compatibility syntax. The shared bootstrap compiler assigns
meaning to its documented entity attributes and fails closed on unknown ones.

## Private typedef declarators and structures

The private JIT, AOT, and persistent REPL parser accepts both ordinary forms:

```c
typedef struct Tagged {
    int value;
    struct Tagged *next;
} Tagged, TaggedAlias, *TaggedPointer;

typedef struct {
    int value;
} Anonymous;

typedef int Words[4];
```

The alias keeps the structure identity used by `.` and `->`. That identity also
survives an alias of an alias and a structure-pointer alias. Tagged and
anonymous bodies use the same field layout and reject an incomplete structure
stored by value. A declaration can publish several value and pointer aliases,
with independent pointer depth for each name. The private table remains limited
to sixteen aliases.

One-dimensional fixed-array aliases retain their element type and positive
count through alias chains. Automatic, global, block-static, record-field,
class-field, and persistent REPL objects receive the complete storage. A
function or method parameter declared with an array alias decays to an element
pointer, while `sizeof` on the type or an object keeps the complete array size.
An array member keeps that complete size and the structure identity of its
elements through direct or pointer access. Indexed reads and assignments can
continue to an element member from either a named record or a record-array
element. Unsized and multidimensional aliases, pointers to array aliases, a
second array declarator, array returns, array casts, and `new` with an array
alias report a focused error. [ADR 0220](../docs/adr/0220-support-private-fixed-array-typedefs.md)
records this array boundary.

A direct file-scope function-pointer typedef also retains its result, fixed
parameters, record identities, prototype state, and variadic boundary. A free
function or Cupid class method parameter declared with that alias carries the
signature in JIT, AOT, and persistent REPL source, including a later REPL unit.
A declaration-initialized automatic object carries it too, with an independent
copy for each comma declarator. A file object declared directly with the alias
keeps the same signature. It can start as `NULL`, receive a compatible callback
through plain assignment, call it indirectly, and be cleared. Indirect calls
use the direct cdecl conversions and 4-, 8-, and 16-byte slots, with SIMD
results returned through XMM0. This covers Doom's `vpatchclipfunc_t` storage
shape and the active ISO callback whose `uint8_t` entry length is converted to
the declared `uint32_t` parameter. The private table holds sixteen typedefs,
and a callback signature may have at most 32 parameters. Each declaration may
introduce only one function-pointer alias. A structure or class field declared
directly with one of these typedefs or a raw function-pointer declarator retains
the signature. Checked stores, copies into named callbacks, null checks, null
clearing, and direct calls work through direct members, nested records, and
indexed record arrays. A typedef-backed fixed callback array field carries the
same signature through indexing. Raw callback array fields remain unsupported.
Callback alias chains and arbitrary computed callback expressions do not retain
this metadata. Direct structure and array
callback results are rejected; record-pointer results retain their record
identity. A rejected source or REPL unit restores the typedef table with the
prior symbols, patches, control state, code, and data.
[ADR 0303](../docs/adr/0303-retain-typedef-callback-signatures-in-private-cupidc.md)
records the free-function parameter boundary.
[ADR 0306](../docs/adr/0306-retain-global-typedef-callback-signatures-in-private-cupidc.md)
records global callback storage and checked assignment.
[ADR 0310](../docs/adr/0310-retain-automatic-callback-typedef-signatures-in-private-cupidc.md)
records automatic objects and Cupid class method parameters.
[ADR 0313](../docs/adr/0313-initialize-private-cupidc-global-callbacks-from-functions.md)
records static initialization from a compatible defined or later-defined
function. The private JIT and fixed-address AOT writers place that address in
the global data slot before execution. A direct `&function` address uses the
same signature check and address patch as the plain designator, including when
parentheses surround the expression or function name. Automatic callback
initialization and checked assignment keep the same callable metadata. A named
raw callback file object and a direct raw callback parameter on a free function retain the same parsed
signature without requiring a typedef. The file object uses the existing
initialized-data, assignment, call, and null rules. The parameter uses the
existing cdecl conversion and arity checks. The private pool accepts 32
distinct raw parameter signatures and recovers after rejecting the next one.
[ADR 0315](../docs/adr/0315-retain-raw-callback-signatures-in-private-cupidc.md)
records the raw boundary. [ADR 0319](../docs/adr/0319-retain-explicit-function-addresses-in-private-callbacks.md)
records direct explicit function addresses.
[ADR 0321](../docs/adr/0321-retain-typedef-callback-signatures-on-private-record-fields.md)
records typedef-backed record and class fields. Runtime initialization and
assignment accept `&(function)` and nested grouping; ADR 0324 records that
boundary. Raw callback field declarators retain the parsed signature. A direct
postfix call through a raw or typedef-backed callback field applies typed fixed
and variadic cdecl conversion, evaluates its designator once, and preserves
represented scalar, floating, pointer, or SIMD results. ADR 0325 records this
boundary. ADR 0328 records typedef-backed fixed callback array fields.

A retained callback signature may contain callback parameters. For each such
parameter, a raw declarator records a raw child-signature handle and a callback
typedef records its typedef handle. CupidC follows either kind recursively when
it compares signatures. Nested callback results, fixed parameters,
record-pointer identities, and variadic boundaries must agree. Initialization,
assignment, arguments, calls, and conditionals keep the existing compatibility
rule for unprototyped callbacks. A declaration or definition match also
requires the same prototype state. Raw and typedef spellings are compatible
when the applicable structure matches.

The recursive check applies when a callback initializes or is assigned to a
signature-bearing object, crosses a higher-order fixed parameter, is passed by
an indirect callback call, appears in a conditional, or participates in a
function declaration, definition, or later-definition check. The nested value
still uses one four-byte i386 cdecl slot. Signature nesting is limited to 16
levels. The backing pool holds 33 raw entries; the active kernel callback uses
one, leaving 32 for source declarations. Failed depth,
capacity, or compatibility checks restore the child handles with the source or
REPL transaction before a later compile runs.

The active private binding
`void (*p_icon_set_drawer)(int, void (*)(int, int))` exposes
`set_icon_drawer` from `kernel/lang/cupidc.cc`. That production source remains
built by checked-seed hosted CupidC. Retaining its nested signature in the
private parser does not transfer the compiler source to in-OS CupidC.

A reviewed native kernel binding may publish a complete
`cc_function_pointer_signature_t`. Fixed calls then use the same scalar and
floating conversion, cdecl slot, cleanup, arity, variadic promotion, and result
rules as retained direct or indirect functions. Console, string, port, and all
50 `libm` bindings and `set_icon_drawer` form the reviewed set. The icon setter
publishes a retained `void(int, int)` child handle, so its second argument must
match the nested result, parameters, record identities, prototype state, and
variadic boundary. Other bindings keep their previous source-width arguments
through the legacy result-only path. ADR 0332 records fixed native signature
publication, and ADR 0333 records the nested binding.

A raw callback scalar with static storage may be declared at file scope, as a
block-static object, or as a persistent REPL global. It may start as null or a
compatible defined or later-defined function. A raw callback array is also
available in those three storage contexts. The array has one fixed dimension,
or `[]` infers the count from a nonempty braced initializer. Each entry accepts
a compatible plain or explicitly addressed function, including one defined
later, or a null pointer. Private CupidC writes known addresses directly,
records absolute data patches for later functions, and leaves uninitialized
fixed-array entries at zero. Indexed assignment and calls preserve the
signature and evaluate the index once.

The active `kernel/doom/src/f_wipe.cc` declaration has six entries arranged as
three callbacks per wipe. Its three call sites select `wipeno*3`,
`wipeno*3+1`, and `wipeno*3+2`. Fixed-size automatic raw callback arrays use
cleared four-byte frame slots and retain their signature through brace
initialization, indexed stores, copies, and calls. Unsized automatic arrays,
raw callback array parameters, raw callback arrays in structures or classes,
and multidimensional raw callback arrays remain unsupported. A failed initializer
restores its data, patches, signature entries, and symbols with the rest of the
source or REPL transaction. [ADR 0330](../docs/adr/0330-support-data-backed-raw-callback-arrays.md)
records this boundary. Conditional field values, alias chains, aggregate
results, and raw method parameters remain open.

The source-current private callback ABI module passes all 318 tests in 60.519
seconds, and the full GUI module passes all 128 tests in 0.955 seconds. A
private four-vCPU frontier boot records all four CPUs online,
`[feature14-callback-raw-array] PASS modes=2 phases=3 calls=12 stored=1 persistent=1`,
`[feature14-callback-nested] PASS outer=1 inner=1 value=43`,
`PASS feature14_simd`, and clean in-OS CupidC JIT completion. Its 157,520-byte
log has SHA-256
`b34a68aebdfecaeeb347c1ff4764cbe609a6ed2f154557a15133a601101585c6`.
The broader frontier changes 109,518 framebuffer pixels and captures
32,701,862 AC97 frames and 76,710 PC-speaker frames.
The standalone CupidC seeds do not contain this private parser. The active Doom
source remains in the production Doom cohort built by checked-seed hosted
CupidC. Supporting its shape in the private compiler does not transfer that
cohort to in-OS CupidC, promote a seed, or remove a remaining bootstrap
dependency.

The preceding poisoned-host OS build checkpoint passed in 684.260 seconds and
accepted all fourteen exact policy artifacts. A private four-vCPU `max`/e1000
smoke of that checkpoint passed in 64.601 seconds. It printed the direct,
named, and typedef callback markers in order, including
`[feature14-callback-typedef] PASS float4=4 calls=1`, before
`PASS feature14_simd` and `[cupidc] JIT execution complete`. No reject marker
appeared, and the source image was unchanged. The 33,219-byte log has SHA-256
`e39a1905002c2baa483c65eb6e763f4f62907c22f8954873dbb20f4ba5a53e93`.
The pre-documentation artifact gate later passed in 651.3 seconds, accepted all
fourteen exact paths, and measured `kernel/kernel.bin` at 9,225,092 bytes.

The integrated fully poisoned build first reached the exact-size gate with
three rebuilt kernel outputs. The artifact group passed all 46 tests in 4.160
seconds, with four expected Windows skips. After the pass-one ELF, final ELF,
and raw kernel policy rows were updated, the repeated build passed in 874.531
seconds with all fourteen artifacts accepted, existing FAT contents preserved,
and `hello.iso` staged. Its 9,251,100-byte raw kernel has SHA-256
`4014b1b2acf34be4dd7483fb8aa9e8a8b0e76eea771c83669571cbf7b66fe0e3`.

The source-head artifact contract passes against all sixteen exact artifacts.
The current 3,382-byte policy has SHA-256
`7f9c1f49d1543112bf6984def1e4ecba6df4fb7a55d2481a85deb7370cf4bfc2`
and covers 38,120,452 bytes across those paths.

| Source-head artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 9,601,052 | `2c2b16f018c018ecd55c96868428d9427213aca4344c67b44e2241f05a054463` |
| `kernel/kernel.elf` | 9,732,124 | `342d57566d853ee9a894ec5c6d4e0eafbc6169019950c768f648873ce797fee6` |
| `kernel/kernel.bin` | 9,505,572 | `1d12e0ddc98bcc66fe34157357a80f4a4f6916f0f75b921b5e56eaa792de1977` |
| `cupidos.img` | 209,715,200 | `112a9764bc9c99382d06dabcbcf2cb6e28498d0076ccc1aec787f159b46a8bc3` |

Those output identities come from the final source-head normal build. Its
431-input linked-image scan passed local-target and code-anchor validation.
Staging `hello`, `ls`, `cat`, and `catfix.txt` produced a 209,715,200-byte
runtime derivative with SHA-256
`816f219305dd0b406d2077913a7f1def08a88efd9591239d0effe44b385cbf10`.
Private copies of that derivative passed the `hello`, `ls`, and hostile-fixture
`cat` QEMU smokes. The published clean image was then restored with SHA-256
`112a9764bc9c99382d06dabcbcf2cb6e28498d0076ccc1aec787f159b46a8bc3`.

| Runtime smoke | Log bytes | Log SHA-256 |
| --- | ---: | --- |
| `hello` | 28,807 | `6543cecfb005f2b775541e279201ee6af4bac4af74bed81a27918f5711392c82` |
| `ls` | 30,158 | `03c01d39b2320be1cc5ec2f92b33d6ffbc62acf50de74f4464ac4fc73f55ea27` |
| `cat` against the hostile fixture | 63,987 | `ca5f6622e317e8ade54370d428271e8f64221b9afd3bf3f238d04760ebdec57a` |

Both active v2 seeds carry the same source snapshot. ADR 0318 records the
preceding linked-image
promotion, ADR 0323 records the preceding code-anchor promotion, ADR 0336
records the parent v1 pair, and ADR 0353 records the active paired v2 promotion.

The preceding source-head checkpoint had a 9,504,760-byte raw kernel with
SHA-256
`346063e08f2fef5550e2a40b8edb1ea7cbe2f242a3084f1137a9552ed7baf84a`
and a 209,715,200-byte disk image with SHA-256
`aa9bc411d48625837b511b32444019f0aa555a48fc5aaa2400c9259dd8607333`.
That image passed its private four-vCPU E1000 frontier smoke.

The integrated strong full private frontier smoke passed in 883.513 seconds
with e1000, four `max` vCPUs, SMP and frontier checks, and the private USB
fixture. The expected direct-call, named-callback, typedef-callback,
global-callback, automatic-callback, and overall feature14 PASS markers each
appeared once and in order. The feature run then printed a clean JIT completion.
The 161,418-byte log has SHA-256
`bc30f5083b96a36362bec5975c0a88437c4f23515de329328bb03d8f6c3e9326`.
The source image was unchanged at SHA-256
`31b25b6881419b1bb8a04b2b3765323b21c5706ac114af1a07b514dcdcd07ea3`.

Field arrays require a positive count and a checked count-by-stride product.
Each padding step, field addition, and final record alignment must fit the
signed parser range. Persistent REPL rollback restores complete structure
records as well as the table count, so a failed definition cannot fill an
older forward tag. [ADR 0219](../docs/adr/0219-support-private-tagged-struct-typedefs.md)
records the typedef, layout, and rollback rules.

The private compiler also supports direct member addresses. In
`&record.field`, it keeps the record object's base address and adds the field
offset. In `&pointer->field`, it loads the pointed-to record first. Neither
form reads the field value while forming its address. Missing members report
`unknown struct field`.

## Private integer constants and allocation limits

Decimal and hexadecimal integer literals cover the represented `uint32_t`
range. A larger value reports `integer literal overflow`, and `0x` or `0xu`
reports `expected hexadecimal digits`. The optional `u` or `U` suffix counts
toward the 95-character token limit and is consumed as part of a rejected
overlong token.

Constant integer `+`, `-`, `*`, `/`, and unary `-` check signed overflow before
performing the operation. Division by zero has a separate diagnostic. If
either operand is unsigned, the operation wraps modulo `2^32`, and enum
constants retain that unsigned state. An explicit `INT_MAX` enumerator is
valid; an implicit successor reports `enum value overflow`.

Fixed array products and record layout may not exceed `0x7ffffffc`. Automatic
objects share one cumulative frame reservation capped at `0x7ffffff0`, leaving
room for the function's final 16-byte frame alignment. Global and persistent
REPL records and enums check capacity before bytes and addresses are committed.
A failed REPL declaration does not escape its transaction.

## Private unsigned 32-bit values

Private JIT, AOT, and persistent REPL source keeps `unsigned int`, `uint32_t`,
and `U32` distinct from signed `int`. The unsigned type survives aliases,
parameters and returns, arrays, record fields, pointers, dereference, indexing,
assignment, and calls. If either represented four-byte integer operand is
unsigned, the usual arithmetic result is unsigned. A conditional expression
applies that rule to both arms instead of inheriting the final arm's type, and
`sizeof` has the unsigned `size_t` result required by the i386 ABI.

Comparisons use unsigned conditions. Division and remainder use unsigned
division, and right shift is logical. The same rules cover `/=`, `%=`, and
`>>=` while evaluating the destination once. Shift assignment takes its
signedness from the promoted left operand, not the count. Unary plus, minus,
and complement retain the unsigned result type, as do unsigned enum constants.

Conversion from unsigned 32-bit values to `float` or `double` is exact before
the destination width rounds. Values on both sides of `0x80000000` work in
casts, initialization, assignment, arguments, ordinary or method returns,
results, and mixed arithmetic. The 40 kernel bindings declared with
`uint32_t`, `size_t`, or `swap_handle_t` results publish the same unsigned
lane; narrower unsigned results keep integer promotion. Scalar `float` and
`double` values in C's defined interval convert back to unsigned 32-bit values
through casts, initialization, plain assignment, fixed arguments, and
returns. The implementation splits at 2^31 so the upper half does not enter a
signed truncation outside its range.
[ADR 0221](../docs/adr/0221-preserve-private-unsigned-int-semantics.md)
records the original runtime rules. [ADR 0249](../docs/adr/0249-complete-private-unsigned-word-conversions-and-remainder-assignment.md)
records `%=` and floating input.

## In-kernel floating rules

The private JIT and AOT compiler keeps scalar `float` and `double` values in
XMM registers. Unary signs preserve the floating width, and unary minus
changes only the IEEE sign bit. The six equality and relational operators
accept matching widths or widen a mixed pair to `double`. They return a
normalized `int`.

Floating comparisons follow C's unordered rules. Signed zero compares equal
to positive zero. NaN makes only `!=` true. Pointers, aggregates, function
pointers, and SIMD vectors are not accepted as floating arithmetic operands.

Scalar floating truth works in unary `!`, conditional selection, `if`, and
all three loop forms. Both signed zero encodings are false. Finite nonzero
values, infinities, and NaNs are true. Void expressions, structures by value,
and SIMD vectors are rejected as truth operands.

Pre-registered kernel calls retain their declared result type in later
expressions. Only bindings whose functions return no value have type `void`.

Scalar `float` and `double` lvalues accept prefix and postfix `++` and `--`.
Prefix returns the updated value; postfix returns the exact old payload after
storing the update. Direct locals, parameters, and globals work alongside
pointer dereferences, array elements, and scalar fields reached through direct,
pointer, or indexed-record members. The destination expression runs once.
Whole arrays, aggregates, function pointers, and SIMD vectors are rejected.

Fixed `float` and `double` arrays are supported with one, two, or three
dimensions as globals, locals, block statics, and persistent REPL
declarations. Their declared type and remaining row stride survive each
subscript. Leaf access uses the matching scalar SSE width, while
`sizeof(array[index])` reports the full remaining row without evaluating the
index. Bounds must be positive, and every dimension product is checked before
storage is reserved.

Depth-one `float *` and `double *` values retain their pointee type through
declarations, address expressions, returns, function and method array
parameters, dereference, subscripting, assignment, and arithmetic compound
assignment. Direct pointer `++` and `--` advance by four or eight bytes.
Structure and class objects, their arrays, and their pointers may contain
scalar floating fields and one-dimensional fixed floating field arrays. Deeper
floating pointers, pointer-to-array types, and assignment through a
pointer-valued floating field subscript remain unsupported. Scalar floating
updates through the represented dereference, index, and member paths are
supported.

Matching `float4` or `double2` values support direct `+`, `-`, `*`, and `/`.
Fixed arrays of either vector type may have one, two, or three dimensions as
globals, locals, block statics, and persistent REPL declarations. Each vector
leaf occupies 16 bytes. Declared rank is independent of byte stride, so a unit
inner extent still needs its final subscript. Indexed plain assignment and
`+=`, `-=`, `*=`, and `/=` use unaligned-safe packed moves and preserve lane
access. Row and vector `sizeof` keep their complete sizes without evaluating an
index. Bounds and allocation sizes are checked before storage is reserved.
Modifiable direct vectors and fully indexed leaves also support prefix and
postfix `++` and `--`. Global and block-static direct vectors receive the same
zero initialization as other static objects, and persistent REPL vectors keep
their stored state. Prefix returns the new vector. Postfix returns the exact
old 128-bit payload. Each subscript is evaluated once. Const qualification is
retained through typedef aliases. A const direct vector or fixed-array leaf
remains readable. Plain and arithmetic compound assignment, plus prefix and
postfix `++` and `--`, are rejected before a store.
Every direct operation keeps the written left value in the machine destination.
MIN and MAX intrinsics keep the second input for NaN and equal signed-zero
cases. A both-NaN ADD or MUL may carry either input payload, depending on the
processor or emulator. Incomplete rows are rejected rather than treated as
untyped pointers. SIMD pointers, record fields, `new`, array parameters, row
values, lane updates, and computed vector updates remain unsupported. ADR 0257
records the multidimensional array boundary, ADR 0294 records whole-vector
updates, and ADR 0299 records fixed SIMD calls.

Direct functions and methods with parsed fixed parameter types convert each
represented integer, `char`, `float`, or `double` argument to its declared
cdecl slot type. A fixed `float4` or `double2` parameter takes one complete
16-byte stack slot, is passed by value, and returns through XMM0. Vector slots
are packed at four-byte granularity and use unaligned-safe moves, so this ABI
does not promise 16-byte call-site alignment. Arguments evaluate from left to
right, and callers reclaim the exact outgoing width.

Represented pointer categories and integer null forms can fill a pointer slot.
A represented object pointer can fill a fixed `int` or
`unsigned int` slot as one unchanged i386 word. Narrow and floating
destinations remain rejected, and the existing represented pointer-category
rule is unchanged. A parsed variadic tail widens `float` to `double` and promotes
`char` to `int`. Its fixed prefix may contain vectors, but a SIMD tail value is
rejected. Unprototyped and signature-erased function-pointer SIMD calls also
fail explicitly. A named block-local function pointer with an explicit
prototype is signature-bearing. A free-function or Cupid class method parameter
declared with a direct file-scope function-pointer typedef is signature-bearing
too. A declaration-initialized automatic object and a file object have the same
rule. Their scalar, floating, pointer, or SIMD arguments use the declared fixed
slots, their variadic tails receive default promotions, and their results keep
the declared type. Grouped zero, `((void *)0)`, a compatible defined or
later-defined function designator, and the direct address of that function may
initialize a callback file object.
Checked plain assignment stores a compatible callback or clears it to null.
A structure or class field declared through the typedef or a raw
function-pointer declarator keeps the same signature for checked stores, named
copies, null checks, clearing, and direct postfix calls. Nested record members
and indexed record-array members use the same path and evaluate their designator
once. Raw callback scalars and one-dimensional arrays in data-backed static
storage keep the declared slots too. Fixed-size automatic raw callback arrays
use cleared local frame storage and keep the same signature. Unsized automatic
arrays, callback array parameters, raw array fields, multidimensional raw arrays, empty `()`,
callback alias chains, and `void *` forms remain
unsupported or retain source-width slots.
Direct structure and array callback results
are rejected; record-pointer results retain their record identity. Kernel
bindings and other calls without fixed parameter metadata do the same.
When a plain function designator or direct `&function` address initializes a
named local, fills a typed callback parameter, or is assigned to a
signature-bearing destination, its
result, record identity, fixed parameters, and variadic
boundary must match. The same check applies when copying another named local callback. A function
defined later receives an address fixup, and a prescan-only signature must
match its definition. A compatible conditional retains every named candidate
and checks each arm. A represented integer constant expression that evaluates
to zero is a valid null initializer. This includes unary signs, integer casts,
arithmetic, character zero, and `sizeof(int) - 4`. A conditional keeps that
proof only when every required arm remains constant. Other scalar, mutable
enum-storage, or object values are rejected unless an explicit `void *` cast
erases the selected value's source type. Null conditional arms are neutral;
every possible non-null object pointer must be cast. Failed functions and
methods restore emitted state, patches, signatures, labels, and control
nesting. A failed source also restores touched prototypes, definitions, kernel
bindings, and a reused `__start`. The implicit thunk is typed `void(void)`.
Character operands
undergo integer promotion in integer arithmetic and use the integer conversion
path for floating arithmetic and explicit casts.
Pointer-producing expressions reset subscript metadata before publishing a
known stride, so a later pointer result cannot reuse stride state from an
earlier array expression.

Decimal `float` and `double` literals are converted from an exact integer
ratio and rounded once to nearest with ties to even. An `f` or `F` suffix
selects binary32 before rounding. Decimal subnormals, finite limits, infinity,
and signed zero are represented. Numeric tokens may contain up to 95
characters including the suffix. A longer token or an exponent without digits
receives a focused diagnostic, and later parser recovery does not replace the
first public error. Hexadecimal floating and `long double` literals are not
implemented in the in-kernel compiler.

Neighboring ordinary string tokens form one null-terminated data object. Each
token may contain at most 1,023 decoded bytes. The combined string can use the
remaining private data section and works in automatic expressions, file-scope
pointer initializers, and persistent REPL declarations. CupidC reports an
overlong token or data-section exhaustion instead of silently shortening the
value. Wide strings are not supported.

## Hosted floating-width rules

The shared self-hosting compiler carries non-atomic `float` and `double`
values through objects, calls, variadic reads, and returns. Explicit casts and
assignment conversion work in either direction between those two widths.
Mixed `float` and `double` arithmetic uses `double`, as do conditional arms
with one value of each width. Matching floating arms keep their width, and
the condition may be a represented integer or pointer.

A runtime conditional may mix either floating width with any represented
signed or unsigned integer through 64 bits, or with a compatible enum. CupidC
applies the usual arithmetic conversion only to the selected integer arm. The
result is `float` when the floating arm is `float` and `double` when it is
`double`.

`+=`, `-=`, `*=`, and `/=` compute at the common floating width, then convert
the stored result back to the left operand's type. The left designator is
evaluated once. A source `float` passed through an ellipsis or a function type
without a prototype is promoted to `double`.

The hosted path accepts decimal floating constants. Source-head CupidC forms
each `float` or `double` ratio in a private 1536-bit integer workspace, then
rounds once at the requested IEEE width with ties going to even. The path
covers subnormals, finite limits, infinity, signed underflow zero, and extreme
exponents. A complete token may contain 95 characters. Every represented signed
or unsigned integer through 64 bits converts to `float` or `double` through a
cast, initialization, plain assignment, return, or fixed argument. Runtime
`+`, `-`, `*`, `/`, and all six comparisons apply the same usual arithmetic
conversions. Inputs through four bytes use SSE; a wide input uses x87 `FILD`
with the unsigned 2^64 correction before a binary32 or binary64 store. Runtime
`float` and `double` values convert to represented unsigned four-byte results
across C's defined range. Conversion from any represented floating width to
`_Bool` follows C scalar truth rules. Only `!=` is true when either operand of
a floating comparison is NaN.

The four arithmetic compound operators accept mixed integer and floating
operands in either lvalue direction. The operation uses the usual `float`,
`double`, or `long double` common type, then converts the result back to the
declared left type. The destination is evaluated once, and the expression has
the stored left type and value. Represented integer bit fields follow the same
conversion order. Atomic mixed compound assignment remains unsupported.

Non-atomic `long double` values use twelve-byte objects with x87 80-bit memory
transport. Bounded finite normal decimal `L` tokens round an exact ratio to a
64-bit explicit significand with ties to even. The emitter writes that
significand and the positive token's biased exponent as three snapshot words;
unary minus supplies the sign. Automatic values use frame snapshots.
Static-duration scalars, fixed arrays, and complete records may contain
long-double leaves. Implicit initialization zeros the complete object. An
explicit leaf accepts a represented integer constant expression or a bounded
decimal `L` literal with parentheses and unary signs. Each leaf has ten exact
x87 value bytes and two
zero padding bytes, and it uses `.bss`, `.data`, or `.rodata` according to its
payload and qualifiers. Atomic leaves fail recursively without following
pointers. Casts among `float`, `double`, and `long double`, unary plus and
minus, and `+`, `-`, `*`, and `/` work for represented values.
Direct and indirect fixed, variadic, and unprototyped arguments occupy twelve
cdecl bytes. Functions return the value in x87 `ST0`, and direct or indirect
callers store it in a twelve-byte snapshot. `va_arg(long double)` copies
twelve bytes and leaves the cursor at the following four-byte slot. Matching
long-double operands and mixed `float` or `double` inputs support all six
comparisons. The balanced `FUCOMIP` path preserves C unordered behavior, so
only `!=` is true when either input is NaN. Unary `!`, `&&`, `||`, the
controlling operand of `?:`, the conditions of `if`, `while`, `do`, and `for`,
and conversion to `_Bool` accept non-atomic `float`, `double`, and automatic
`long double`. Both signed zeros are false; finite nonzero values, subnormals,
infinities, and NaNs are true. Runtime casts, assignments, arguments, and
returns convert between `long double` and signed or unsigned 8, 16, 32, and
64-bit integers. The unsigned 64-bit correction uses 64-bit x87 precision
without changing the caller's rounding mode and restores the complete control
word. Runtime `+`, `-`, `*`, `/`, all six comparisons, and conditional selection
apply the usual arithmetic conversions between `long double` and every
represented value integer or enum. Only the selected conditional arm is
evaluated and converted. Static initializer conversion covers `_Bool`, plain
`char`, each signed or unsigned
i386 integer width, and an enum whose compatible integer type has the
represented target layout. Integer input packs exactly into the 64-bit x87
significand. For integer destinations other than `_Bool`, long-double input
discards fractional bits toward zero before its range is checked. A value in
`(-1, 0)` therefore becomes unsigned zero. `_Bool` tests the original floating
value: both signed zeros become false, and every represented finite nonzero
value becomes true. In particular, `-0.5L` becomes true for `_Bool` but zero
for an unsigned integer, because numeric truncation does not precede the
Boolean truth test. Static long-double truth, all six comparisons,
short-circuit logic, and conditional selection use the target representation.
Finite binary32 and binary64 values widen exactly to x87, and represented
finite long-double values narrow with round-to-nearest, ties-to-even packing.
Binary32 and binary64 infinities keep their sign when widened. NaNs widen to
one canonical quiet x87 payload. Canonical x87 infinities and NaNs narrow to
the corresponding target infinity or quiet NaN. These expressions become
static data and add no runtime IR.

Static long-double addition, subtraction, multiplication, and division use
integer-only 128-bit intermediates. Results round once to the i386 x87
explicit significand with nearest-even rounding and gradual underflow.
Overflow and invalid operations use the canonical special payloads. The
folded result stays in the initializer forest and emits no runtime instruction.

Ordinary non-atomic `float` and `double` lvalues support prefix and postfix
increment and decrement. Atomic floating compound assignment, atomic floating
updates, `long double` increment and decrement, decimal long-double
subnormals, long-double decimals beyond
the bounded ratio parser, and SIMD remain unsupported.
[ADR 0349](../docs/adr/0349-parse-exact-hexadecimal-floating-constants.md)
records exact source-head hexadecimal conversion.
[ADR 0229](../docs/adr/0229-emit-exact-decimal-long-double-literals.md)
records the literal representation. ADR 0250 records runtime unsigned
four-byte conversion, ADR 0251 records static long-double data, and ADR 0253
records runtime conversion between `long double` and every signed or
unsigned i386 integer width. ADR 0254 records static initializer conversion,
ADR 0255 records static controls and finite width conversion, and ADR 0256
records canonical x87 payloads and special-value conversion.
ADR 0288 records runtime integer and long-double usual conversions. ADR 0289
records wide integer conversion and usual arithmetic with `float` and
`double`. [ADR 0293](../docs/adr/0293-round-hosted-decimal-literals-exactly.md)
records exact decimal `float` and `double` literals.
[ADR 0296](../docs/adr/0296-support-mixed-floating-compound-assignments.md)
records mixed arithmetic compound assignment. ADR 0312 first carried both
capabilities. The preceding linked-image cohort carried them under ADR 0318,
and the active paired v2 cohort retains them under ADR 0353.
ADR 0258 records the preceding checked seed. ADR 0260 records static
long-double arithmetic, ADR 0263 records ordinary hosted floating updates, ADR
0265 records their checked-seed carriage, and ADR 0273 records private derived
floating updates.
The checked native Windows seed carried both features through an earlier
2026-08-13 poisoned-host checkpoint. Its first invocation stopped at
the 602.5-second command limit; the resumed build finished in 968.5 seconds,
for 1,571.0 seconds of cumulative work. At that checkpoint, the 2,560-byte boot
image had
SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`.
The 9,056,612-byte pass-one ELF has SHA-256
`e2f63b5cd9c4e2769b9d6bc893ab5cf778951b97aec954ece6cbac0cc429e92a`,
the 9,179,492-byte final ELF has SHA-256
`1bc06263dbf9849e6d2c594b6fb4be2a3f3b673c91f69d23a2d2e639b1f64776`,
and the 8,962,776-byte raw kernel has SHA-256
`3170aa71eafa656b1f6e23c918f1f472860f513c9c5cd0376d7d4f5f8a7d891c`.
Its exact-size prerequisite accepted all nine artifacts before publishing the
209,715,200-byte image with SHA-256
`3b5dd6523a90d6ed0543a6ab2464892f3289b876654f9869f88db0901940b91e`.
A four-vCPU RTL8139 frontier passed from this image in 820.7 seconds. Private
CupidC emitted the broad indirect-update marker, compiled and loaded the
dedicated external ELF as PID 4, emitted
`[feature13-derived-aot] PASS score=41 once=2 zero=0x80000000`, and reported
that same PID exiting. The full SMP, framebuffer, audio, USB detach/replug, and
survival checks passed. The completed dual-NIC checkpoint immediately before
this rebuild used image
SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.
The earlier 1,057.969-second build and definitive four-vCPU E1000 and RTL8139
boot frontiers remain pre-freeze evidence. Those boots passed with exits 0 in
794.034 and 758.667 seconds.
The guarded 2026-08-14 poisoned-host normal build finished in 674.693
seconds. Its
2,560-byte boot image keeps the same SHA-256. The 9,211,340-byte pass-one ELF
has SHA-256
`2a6f5deafb580b30254483179d6dade9ed4ed7b17b39f9368137b1ff14932263`,
the 9,334,220-byte final ELF has SHA-256
`bc855462c1f8f42e34d94a974443f7c6e565d60b1913e3b6f33b3e6e375f3ed6`,
and the 9,114,084-byte raw kernel has SHA-256
`8b5d73e74538ce11c1fb074f88b3852d690038aa5cb3a8de3ce222e9df88cade`.
The published 209,715,200-byte image has SHA-256
`813c9b0c78f795c1ac9fcff59b9c4111a958a07eb1e3943dc7af60c536521110`.
A private four-vCPU `/bin/ls.cc` JIT boot passed from that image in 49.257
seconds.
The in-kernel compiler has a separate, broader floating and SIMD
implementation.

## Checked-seed returns-twice calls

Checked-seed CupidC recognizes GNU `returns_twice` and
`__returns_twice__` on file-scope function declarations. Compatible
redeclared prototypes keep the property on one canonical function binding. A
marked function must remain a direct call target. CupidC rejects conversion of
its designator to a function pointer because the pointer type does not carry
the attribute.

At a direct call to a marked function, the i386 emitter saves every live
four-byte Linear IR operand below the call arguments in frame slots owned by
that call instruction. It restores those words after cdecl cleanup and then
publishes the call result. This keeps a pending assignment address or
arithmetic operand intact when a later non-local jump resumes at the call.

Supported calls use four-byte cdecl arguments and may return void or any
nonaggregate type. Aggregate, wide-integer, and wider-than-four-byte floating
arguments and aggregate results fail with specific diagnostics. Each
live-prefix site owns its spill region, but it must not be reachable from any
returns-twice continuation. A marked call with no live prefix may repeat in a
loop.

A decoder-driven i386 oracle models first and second returns with transfer
values zero and seven. The guest self-test separately executes active
`dg_longjmp` and `dg_exit`, so the hosted oracle is no longer the only runtime
evidence. Active dglibc uses the corrected 31-byte `ESP + 4` form. Outgoing-area
arguments and aggregate results remain unsupported. ADR 0213 records
checked-seed carriage, and ADR 0214 records active adoption.

## Hosted static initializer references

A block-static pointer may be initialized with the address of another
block-static object. The object keeps its local ELF symbol, so the pointer
initializer receives the same absolute relocation used for file objects.

Within a static initializer, an earlier file-scope or block-static non-atomic
`const` integer with a direct integer initializer can be reused as a constant
value. This works inside scalar, array, and structure initializers. It does
not turn mutable objects, automatic objects, atomics, indirect initializers,
or non-integer objects into constant expressions. This is a narrow Cupid C
extension rather than an ISO C integer constant expression. It preserves the
unchanged address-table form used by the Toolchain object contract.

## Hosted null pointers and external arrays

The self-hosting compiler accepts an integer null pointer constant and the
common `((void *)0)` spelling when either is converted to a represented object
pointer. It also accepts computed zero constant expressions such as
`(void *)(1 - 1)`. The frontend marks the constant proof on the conversion,
and IR rejects missing or misplaced proof as well as an ordinary runtime
`void *`. The typed path requires an unqualified `void` referent and keeps the
same four-byte zero representation.

An external array declaration may omit its bound when the element type is
complete:

```c
extern const struct Entry entries[];
```

The array designator can decay to a pointer, be indexed with the target
element size, and continue through member access. The array remains
incomplete, so this support does not provide its storage size or turn it into
an array value.

## String Escape Sequences

CupidC supports the following escape sequences in string and character literals:

| Escape | Meaning | ASCII Value |
|--------|---------|-------------|
| `\n` | Newline (LF) | 10 |
| `\t` | Tab | 9 |
| `\r` | Carriage return (CR) | 13 |
| `\b` | Backspace | 8 |
| `\\` | Backslash | 92 |
| `\'` | Single quote | 39 |
| `\"` | Double quote | 34 |
| `\0` | Null terminator | 0 |
| `\xNN` | Hexadecimal byte (00-FF) | Variable |

### Hexadecimal Escapes

The `\xNN` escape sequence allows specifying a byte value in hexadecimal:

```c
char esc = '\x1B';          // ESC character (27)
char *red = "\x1B[31m";     // ANSI red color code
print("\x48\x69");          // Prints "Hi" (0x48='H', 0x69='i')
```

Both uppercase and lowercase hex digits are supported: `\x1B`, `\x1b`, `\xFF`, `\xff`

## Control Flow

### Break Statement

The `break` statement exits the innermost loop or switch statement:

```c
while (1) {
    if (condition1) break;  // Exit loop
    if (condition2) break;  // Multiple breaks supported
}
```

CupidC supports multiple `break` statements per loop. The compiler maintains
an array of up to 32 break patch locations per loop and patches them when the
loop ends.

### Labels and Goto

Simple local labels and `goto` are supported for C compatibility:

```c
void main() {
    int n = 3;
again:
    print_int(n);
    n = n - 1;
    if (n > 0) goto again;
}
```

## Available Functions

### Built-in Functions

#### I/O Functions
- `print(char *s)` - Print a string
- `putchar(char c)` - Print a single character
- `print_int(int n)` - Print an integer
- `print_hex(int n)` - Print integer in hexadecimal
- `getchar()` - Read a character from keyboard

#### Memory Functions
- `kmalloc(int size)` - Allocate memory
- `kfree(void *ptr)` - Free memory
- `memset(void *ptr, int val, int size)` - Set memory bytes
- `memcpy(void *dst, void *src, int size)` - Copy memory

#### String Functions
- `strlen(char *s)` - Get string length
- `strcmp(char *s1, char *s2)` - Compare strings
- `strncmp(char *s1, char *s2, int n)` - Compare first n characters

#### File System Functions
- `vfs_open(char *path, int flags)` - Open a file
- `vfs_close(int fd)` - Close a file
- `vfs_read(int fd, char *buf, int size)` - Read from file
- `vfs_write(int fd, char *buf, int size)` - Write to file
- `vfs_unlink(char *path)` - Delete a file
- `vfs_mkdir(char *path)` - Create a directory
- `resolve_path(char *rel, char *abs)` - Resolve relative path to absolute

#### Program Functions
- `get_args()` - Get command-line arguments as string
- `exit(int code)` - Exit program with code

#### Networking - NIC info
- `net_get_ip()` - Primary NIC IPv4 address (host byte order)
- `net_get_gateway()` - Default gateway IPv4
- `net_get_dns()` - DHCP-assigned DNS server IPv4
- `net_get_mask()` - Subnet mask
- `net_get_mac(uint8_t *out)` - Fills 6-byte MAC into `out`
- `net_link_up()` - 1 if link up, 0 if down
- `net_rx_packets()` / `net_tx_packets()` - Packet counters
- `net_rx_drops()` / `net_tx_errors()` - Error counters

#### Networking - Layer 2/3
- `ip_parse(char *s, uint32_t *out)` - Parse `"A.B.C.D"` into uint32_t
- `ipv4_send(uint32_t dst, uint8_t proto, uint8_t *payload, uint32_t len)` - Raw IPv4 send
- `arp_resolve(uint32_t ip, uint8_t *mac_out)` - 500ms blocking resolve, 0 ok / -1 timeout
- `arp_dump()` - Print ARP cache to serial
- `arp_get_entries(uint32_t *ips, uint8_t (*macs)[6], int max)` - Iterate cache
- `icmp_send_echo(uint32_t dst, uint16_t id, uint16_t seq, uint32_t paylen)` - Send ping
- `icmp_wait_reply(uint32_t src, uint16_t id, uint16_t seq, uint32_t timeout_ms)` - Wait for echo reply
- `udp_send_raw(uint32_t dst, uint16_t src_port, uint16_t dst_port, uint8_t *data, uint32_t len)` - Raw UDP send
- `dns_resolve(char *name, uint32_t *ip_out)` - A-record lookup
- `htons` / `htonl` / `ntohs` / `ntohl` - Byte-order helpers

#### Networking - BSD sockets
- `socket(int type)` - `SOCK_TYPE_TCP=1`, `SOCK_TYPE_UDP=2`. Returns fd.
- `bind(int fd, uint32_t ip, uint16_t port)` - Bind to address:port
- `listen(int fd, int backlog)` - TCP passive listen
- `accept(int fd, uint32_t *peer_ip, uint16_t *peer_port)` - Accept TCP
- `connect(int fd, uint32_t ip, uint16_t port)` - TCP connect / UDP set-default-peer
- `send(int fd, void *buf, uint32_t len)` - Send (routes through TLS if enabled)
- `recv(int fd, void *buf, uint32_t len)` - Receive
- `sendto(int fd, void *buf, uint32_t len, uint32_t ip, uint16_t port)` - UDP sendto
- `recvfrom(int fd, void *buf, uint32_t len, uint32_t *ip, uint16_t *port)` - UDP recvfrom
- `setsockopt(int fd, int level, int optname, void *val, uint32_t vlen)` - Use `level=SOL_TLS=1`, `optname=TLS_ENABLE=1`, `val=hostname`, `vlen=strlen(hostname)` to upgrade a connected TCP socket to TLS 1.3
- `sock_avail(int fd)` - Bytes currently buffered (0 means a `recv` would block); `EBADF` on bad fd
- `sock_state(int fd)` - Returns `tcp_state_t` enum value (`TCPS_*`); `EBADF` on bad fd
- `close(int fd)` - Close socket

```c
// HTTP-over-TLS minimal client
void main() {
    uint32_t ip;
    if (dns_resolve("example.com", &ip) != 0) { print("dns fail\n"); return; }
    int fd = socket(1);                          // SOCK_TYPE_TCP
    if (connect(fd, ip, 443) != 0) { print("connect fail\n"); return; }
    setsockopt(fd, 1, 1, "example.com", 11);     // SOL_TLS, TLS_ENABLE
    char *req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
    send(fd, req, strlen(req));
    char buf[2048];
    int n = recv(fd, buf, 2047);
    if (n > 0) { buf[n] = 0; print(buf); }
    close(fd);
}
```

#### Audio - AC97 driver
- `ac97_init()` - Probe + init AC97 card, returns 0 on success
- `ac97_start()` - Arm DMA
- `ac97_stop()` - Halt + mute
- `ac97_set_master_volume(uint8_t pct)` - 0-100
- `ac97_set_pcm_volume(uint8_t pct)` - 0-100, sets the PCM-out channel attenuation
- `ac97_get_master_volume()` - Returns last-set master percentage (0 if device absent)
- `ac97_get_pcm_volume()` - Returns last-set PCM percentage
- `ac97_tsc_sleep_ms(uint32_t ms)` - TSC busy-wait (IRQ-state independent)
- `ac97_is_present_int()` - Returns 0 or 1
- `ac97_smoke_sine()` - 440 Hz triangle for 2s
- `ac97_smoke_sweep()` - 50-8000 Hz sweep
- `ac97_smoke_pan()` - L↔R panning
- `audiotest_all()` - Sine + sweep + pan + opl in sequence

```c
void main() { ac97_init(); ac97_smoke_sine(); }
```

```c
// Set master volume to 50% (or read current with ac97_get_master_volume).
// Full source: bin/volume.cc.  Run:  volume 50
```

#### Imaging - in-memory codecs
- `png_decode_mem(uint8_t *data, uint32_t len, uint32_t **out_pixels, int *out_w, int *out_h)` - Decode PNG to a fresh XRGB buffer (caller `kfree`s `*out_pixels`); returns 0 on success, negative `PNG_E*` on failure. Non-interlaced 8-bit PNGs only.
- `jpeg_decode_mem(uint8_t *data, uint32_t len, uint32_t **out_pixels, int *out_w, int *out_h)` - Baseline JPEG (SOF0/SOF1, 8-bit, 1- or 3-channel); same buffer convention.
- `kdeflate_raw(uint8_t *src, uint32_t src_len, uint8_t *out, uint32_t out_len)` - RFC 1951 raw DEFLATE; returns produced bytes or negative on error.

```c
// Load a PNG from disk and blit it to the screen.
gfx2d_fullscreen_enter();
uint8_t *bytes; int n = vfs_read_all("/img.png", &bytes);
uint32_t *px; int w, h;
if (png_decode_mem(bytes, n, &px, &w, &h) == 0) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) gfx2d_pixel(x, y, px[y*w + x]);
    gfx2d_flip();
    kfree(px);
}
kfree(bytes);
gfx2d_fullscreen_exit();
```

Raw gfx2d drawing and borrowed graphics-resource pointers require a
fullscreen or retained-window paint scope. The shared render state is
owner-tagged across processes, and process cleanup releases an abandoned
scope before the PID is reused.

#### Audio - MIDI / OPL3 synth
- `midiopl_init(uint8_t *genmidi_lump, uint32_t lump_len)` - Parse Doom GENMIDI patches
- `midiopl_reset()` - Silence all channels, keep patches
- `midiopl_feed(uint8_t *bytes, uint32_t len)` - Stream MIDI bytes
- `midiopl_render(int16_t *out_stereo, uint32_t frames)` - Pull synth output @ 22050 Hz
- `midiopl_set_volume(uint8_t v)` - 0-127
- `opl_smoke()` - OPL3 smoke test

#### Audio - PCM mixer (16 slots, s16 stereo @ 22050 Hz)
- `mixer_init()` - One-time init
- `mixer_play(int slot, int16_t *pcm, uint32_t frames, uint8_t channels, uint8_t loop, uint8_t vol_l, uint8_t vol_r)` - Start playback
- `mixer_stop(int slot)` - Stop
- `mixer_active(int slot)` - 1 if playing
- `mixer_set_volume(int slot, uint8_t l, uint8_t r)`
- `mixer_fill(int16_t *out, uint32_t frames)` - Fill output buffer (called by AC97 IRQ)

## Limitations

- Maximum 64 `break` statements at one loop or switch level
- The private compiler accepts 128 active loop-or-switch control frames and 1,024 active statement calls. It rejects the next entry before further recursion and restores both counters after a failed REPL evaluation.
- Object-like preprocessor macros are supported; function-like macros and general `#if` expressions are not
- A `continue` inside one or more switches targets the nearest enclosing loop. The compiler removes the saved switch selectors before taking that jump.
- Not full hosted GCC C; accepted wide integer spellings still target the
  32-bit flat kernel ABI unless a binding explicitly handles wider data

## Common Patterns

### Parsing Command-Line Arguments

Programs receive arguments as a single string via `get_args()`. To parse multiple space-separated arguments:

```c
// Parse a single token from a string
int parse_token(char *str, int start, char *out, int maxlen) {
    int i = start;

    // Skip leading spaces
    while (str[i] == ' ' || str[i] == '\t') {
        i = i + 1;
    }

    // Check if end of string
    if (str[i] == 0) {
        out[0] = 0;
        return 0;
    }

    // Copy token until space or end
    int j = 0;
    while (str[i] != 0 && str[i] != ' ' && str[i] != '\t' && j < maxlen - 1) {
        out[j] = str[i];
        i = i + 1;
        j = j + 1;
    }
    out[j] = 0;

    return i - start;
}

// Usage example
void main() {
    char *args = (char*)get_args();
    char token[256];
    int pos = 0;

    while (1) {
        int len = parse_token(args, pos, token, 256);
        if (len == 0) break;  // No more tokens

        print("Token: ");
        print(token);
        print("\n");

        pos = pos + len;
    }
}
```

### Error Handling with VFS

```c
int fd = vfs_open(path, 0);
if (fd < 0) {
    print("Error opening file: ");
    print(path);
    print("\n");
    return;
}

// Use file...
vfs_close(fd);
```

## Examples

### Hello World

```c
void main() {
    print("Hello, World!\n");
}
```

### Using ANSI Colors

```c
void main() {
    char *red = "\x1B[31m";
    char *reset = "\x1B[0m";

    print(red);
    print("This is red text");
    print(reset);
    print("\n");
}
```

### Loop with Multiple Breaks

```c
void main() {
    int i = 0;
    while (i < 100) {
        if (i == 10) break;      // Exit at 10
        if (i % 7 == 0) break;   // Or exit at first multiple of 7
        i = i + 1;
    }
    print_int(i);
}
```
