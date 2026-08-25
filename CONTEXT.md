# Cupid OS

Cupid OS is an operating-system project pursuing a self-hosting toolchain. This glossary fixes the project-specific language used to discuss the system and its bootstrap.

## Language

### System and source

**Cupid OS**:
The operating system produced and developed by this repository.
_Avoid_: CupidOS, cupid-os (except as a repository or package identifier)

**Active source**:
Cupid OS source that participates in a supported build or ships as part of the system.
_Avoid_: all checked-in source

**Supported build root**:
An accepted build entry point whose reachable inputs and outputs are part of Cupid OS delivery or verification. The normal root image build, separate user-program build, and hosted Cupid Toolchain contract build are current supported build roots; arbitrary Make targets are not automatically supported roots.
_Avoid_: every build target, only the default target

**Source cohort**:
A related group of active sources migrated and verified under one tool-ownership and behavior gate.
_Avoid_: directory (a cohort may cross directories), individual file count

**Toolchain job**:
An owned, bounded lifetime for deterministic Cupid Toolchain arena, buffer, logical-path, source, and diagnostic state.
_Avoid_: global compiler state, platform context

**Source-resolved raw control edge**:
A bounded record that binds a raw call or jump at one source instruction offset
to the address CupidASM resolved before encoding. Local rows also retain the
image offset and target code mode. External rows keep the resolved address and
mode without claiming that the target belongs to the image. Register- and
memory-indirect transfers are recorded as unprovable because runtime state owns
their destination.
_Avoid_: decoded instruction boundary, assumed local target

**Platform adapter**:
The narrow allocator, whole-file, and text-output capabilities that connect the shared Cupid Toolchain core to a hosted runtime or the Cupid OS kernel.
_Avoid_: tool backend, giant platform vtable

**Guarded build transaction**:
A hosted CupidBuild operation that freezes its source and checked tool cohort,
uses a private candidate, validates and inspects that candidate, rechecks live
inputs and publication identities, retains its owner-lock snapshot, rechecks
that lock at the final publication boundaries, serializes cooperating
publishers, and
replaces the destination through a pinned parent only after every check
passes. The source-head CupidASM object operation implements this boundary on
Linux and Windows. It parses the frozen five-tool seed contract, including the
host target and provenance, before it runs CupidASM. The five exact manifest
names must be the complete `.elf` or `.exe` membership, all five frozen images
must match the selected static ELF32 or strict PE32 execution profile, and
membership is checked again after both tools run. CupidDis then requires known
instructions, local targets, and relocatable code anchors. CupidBuild is not a
production owner until a promoted checked seed contains it and a normal Make
recipe invokes it.
_Avoid_: command wrapper, unchecked tool launch, production ownership from source presence

**Hosted bootstrap runtime**:
The static i386 C runtime linked into source-head Cupid tool and contract images. It supplies the represented heap, file, memory, string, error, and working-directory interfaces without a host libc. Its string boundary includes binary `memchr`, which CupidBuild uses while validating frozen JSON. The source-head link proof builds CupidBuild beside CupidC, CupidASM, CupidDis, CupidLD, CupidObj, and the runtime contract; this proof does not promote a six-tool seed.
_Avoid_: host libc, production ownership from a source-head link

**External executable arena**:
The permanently reserved identity-mapped range `[0x01C00000, 0x01E00000)` leased exclusively to one ordinary fixed-address ELF process at a time.
_Avoid_: dynamically allocated user memory, CupidC region, CupidASM region

**TempleOS reference tree**:
The checked-in TempleOS source consulted for design understanding but excluded from Cupid OS source, builds, and progress measures.
_Avoid_: vendored Cupid OS source, Cupid OS source

### Languages and tools

**Cupid Toolchain**:
The first-party family of tools that builds and inspects Cupid OS artifacts.
_Avoid_: host toolchain

**Cupid C**:
The C-family language native to Cupid OS.
_Avoid_: CupidC when referring to the language

**CupidC**:
The compiler for C and Cupid C source.
_Avoid_: Cupid C when referring to the compiler

**Linear IR**:
The typed, target-neutral instruction sequence between CupidC's function-body AST and machine-code emission. Its stack entries distinguish object addresses from scalar and structure values. Branch targets stay relative to one function, while parameters, represented automatic objects, compound-literal objects, runtime string literals, block-static objects, linked file objects, and linked function references retain their absolute frontend identities. Machine addresses, section offsets, symbol-table indices, frame offsets, literal symbols, and value snapshot storage remain private to target emission.
_Avoid_: AST, x86 bytecode, machine code

**Wide integer value**:
An eight-byte integer carried through hosted Linear IR as one logical value. The i386 emitter stores its bytes in a private frame snapshot and returns the low word in EAX and the high word in EDX. The current boundary covers constants, matching conditional arms, fixed call results, object access through file, block-static, automatic, pointer, member, and index paths, initialization, plain assignment, all ten compound assignments, prefix and postfix update, declared parameters, named direct or indirect call arguments, signed or unsigned ellipsis and unprototyped call arguments, discard, return, addition, subtraction, multiplication, division, remainder, unary plus, unary minus, bitwise complement, left and signed or unsigned right shifts, AND, OR, XOR, all six comparisons, logical not, short-circuit logical operators, conditional selection, structured scalar conditions, signed or unsigned switch dispatch, conversion to or from represented integer widths, explicit non-atomic `double` to `unsigned long long` conversion, and non-atomic `va_arg` reads. An arithmetic compound assignment may also combine the wide lvalue with `float`, `double`, or `long double`; assignment conversion restores the wide result after the floating computation. A wide integer right operand may likewise feed a floating-lvalue compound assignment. It also covers the standard `signed long long` to `unsigned long long` usual arithmetic conversion and, in GNU mode, promotion of a wide enum to its compatible wide integer type. A switch duplicates the value's snapshot handle and compares the complete eight-byte case value without evaluating the condition again. Mutation evaluates its lvalue address once and returns either the stored snapshot or the reconstructed postfix value. Multiplication combines the low-word product with both cross-word products. Division and remainder use a fixed restoring loop over unsigned magnitudes, then apply the quotient or dividend sign. Each multiplication, division, remainder, or wide variadic-read result receives a fresh snapshot. Shift counts remain represented four-byte integers. Runtime cases that C leaves undefined promise neither a trap nor a result.
_Avoid_: two unrelated 32-bit values, a public IR register pair

**Floating scalar value**:
A non-atomic `float` or `double` carried through hosted Linear IR as one logical value. A `float` keeps its raw four-byte representation. A `double` uses an emitter-owned eight-byte snapshot. Object loads, initialization, plain assignment, discard, fixed arguments and parameters, direct or indirect call results, returns, `double` ellipsis arguments, and non-atomic `va_arg(double)` reads use this path. A static-duration scalar evaluator uses integer-only IEEE binary32 and binary64 arithmetic for unary signs, addition, subtraction, multiplication, division, comparisons, casts, scalar truth, short-circuit logic, and conditional selection. It rounds each result to nearest with ties to even at the C expression width, covers represented signed and unsigned integers through 64 bits, preserves signed zero, and places the final target bits in `.rodata`, `.data`, or `.bss` through the ordinary static object policy. Explicit casts and assignment conversion work in both directions between the two floating widths. Every represented signed or unsigned integer through 64 bits may also convert to either floating width through a cast, initialization, plain assignment, return, or fixed argument. Unary plus and minus and binary addition, subtraction, multiplication, and division work for same-width or mixed-width runtime operands. The four binary operators and all six equality and relational operators also accept every represented value integer or compatible enum opposite `float` or `double`; the usual arithmetic conversion keeps the floating width and produces a normalized signed `int` for a comparison. Their `UCOMISS` or `UCOMISD` emission treats every unordered relation as false except `!=`. Matching floating conditional arms keep their width, and mixed `float` and `double` arms use `double`. A conditional arm may also mix either floating width with any represented value integer or compatible enum. Only the selected integer arm converts. Inputs through four bytes use the SSE conversion sequence, while an eight-byte input uses x87 `FILD` and the unsigned 2^64 correction when needed. The four arithmetic compound assignments accept a floating lvalue with an integer right operand and an integer lvalue with a floating right operand. Usual arithmetic conversion selects the floating computation type, assignment conversion restores the declared left type, and the lvalue is evaluated once. This includes represented integer bit fields. Atomic mixed operands remain unsupported. Prefix and postfix increment and decrement add or subtract exact-width `1.0` and evaluate the lvalue once. A postfix store returns the original raw value or snapshot, which preserves signed-zero and NaN payload bits. Every changed x87 result is stored at its C width before the next IR instruction. Default argument promotion converts an ellipsis or unprototyped source `float` to `double` through x87 and a fresh snapshot. i386 calls place the final value in four or eight cdecl stack bytes. Floating results cross the ABI in x87 `ST0`; after call cleanup, the caller places a `float` in a four-byte semantic stack slot or a `double` in a private eight-byte frame snapshot. Runtime truth testing accepts non-atomic `float` and `double` for unary `!`, `&&`, `||`, the controlling operand of `?:`, and the conditions of `if`, `while`, `do`, and `for`. `UCOMISS` or `UCOMISD` compares the value with positive zero and publishes a normalized 0 or 1. Both signed zeros are false; finite nonzero values, subnormals, infinities, and NaNs are true. Explicit casts and assignment conversion to `_Bool` use the same rule. Decoder-driven oracles check comparison behavior and call alignment; the arithmetic oracle models its supported x87 subset rather than executing native x87.
_Avoid_: general floating-point support, an exposed snapshot pointer

**Long-double value**:
A non-atomic `long double` carried through hosted Linear IR in its twelve-byte
i386 object representation. Automatic objects use frame snapshots. File-scope
and block-static scalars, fixed arrays, and complete records may also contain
long-double leaves. A static leaf accepts implicit zero, a represented integer
constant expression, or a bounded finite normal decimal `L` literal with
parentheses and unary signs. Linear IR keeps the literal's 64-bit explicit
significand and 16-bit x87 sign and exponent separately. The emitter writes the
ten value bytes, clears the two padding bytes, and uses `.bss`, `.data`, or
`.rodata` according to the payload and qualifiers. Recursive validation
rejects atomic leaves without following pointers.

Bounded decimal `L` tokens round an integer ratio to nearest with ties to even.
Static initializer conversion works in both directions between those values
and every represented value integer, including an enum whose compatible
integer type has the represented target layout. Integer input is packed
exactly into the 64-bit x87 significand. Integer destinations other than
`_Bool` truncate toward zero before the signed or unsigned range check, so
`-0.5L` becomes unsigned zero. Integer-valued zero retains a
`CTOOL_C_INITIALIZER_ZERO` record. `_Bool` tests the original floating value:
both signed zeros are false, and every represented finite nonzero value is
true.

The static evaluator also folds long-double truth, all six comparisons,
short-circuit logic, conditional selection, and conversion to or from
binary32 and binary64. One target-only decoder normalizes sign,
classification, exponent, and significand. It accepts canonical signed zero,
subnormal, normal, infinity, and NaN payloads while rejecting x87 pseudo
encodings. Finite widening produces exact x87 payloads, including a binary32
subnormal result from static arithmetic. Infinity keeps its sign, and every
source NaN becomes one canonical quiet x87 NaN. Narrowing rounds finite values
to nearest with ties to even and emits target infinity or canonical quiet NaN
for the special classes. The final values become static initializer records
and add no runtime IR.

Automatic literals use the same metadata in a twelve-byte snapshot and load
it through the shared 80-bit path used for object access, assignment,
floating-width conversion, unary plus and minus, addition, subtraction,
multiplication, and division. All six runtime comparisons accept two
non-atomic long-double values or a long-double value paired with `float` or
`double`. Mixed inputs convert to `long double`. The emitter loads right then
left, uses `FUCOMIP ST0, ST1`, discards the surviving x87 value, and normalizes
a signed `int` result. Signed zeros compare equal, and an unordered input makes
only `!=` true.

Direct and indirect fixed, ellipsis, and unprototyped arguments use three
cdecl words. Functions return a `long double` in x87 `ST0`, and callers store
it in a twelve-byte snapshot. `va_arg(long double)` copies twelve bytes,
advances the cursor by twelve, and leaves the next four-byte argument at the
correct address. Runtime truth and `_Bool` conversion compare automatic values
with x87 zero through a balanced `FLDZ`, `FUCOMIP`, and `FSTP` sequence. Both
signed zeros are false; finite nonzero values, subnormals, infinities, and NaNs
are true. Runtime casts, assignments, arguments, and returns convert between
`long double` and signed or unsigned integers at 8, 16, 32, and 64 bits. The
emitter uses `FILD` for integer input and restores the saved x87 control word
after truncate-mode `FISTP` output. Runtime `+`, `-`, `*`, `/`, all six
comparisons, and conditional selection apply that same integer conversion to
every represented value integer and enum. Only the selected conditional arm
is converted. Each arithmetic compound operator also accepts a long-double
lvalue with an integer right operand or an integer lvalue with a long-double
right operand. The operation uses `long double`, converts the result back to
the left type, and evaluates the destination once. Static-duration addition,
subtraction, multiplication, and
division use target-only 128-bit integer arithmetic and
round once to the x87 significand. Hexadecimal or subnormal long-double
literals and decimals beyond the bounded ratio parser remain outside this
boundary. ADR 0256 records the canonical x87 class and special-width
conversion rules. ADR 0260 records the static arithmetic model.
The static fixture converts `-0.5L` to both `_Bool` and an unsigned integer. The results are one and zero respectively, proving that Boolean truth is checked before numeric truncation. Frozen IR also validates the target type representation. Primitive bases use their canonical target size, signedness, and alignment. An enum, its unwrapped base, and its compatible integer type agree on size, signedness, integer, object, and completeness flags, as well as alignment. A `QUALIFIED` node copies referenced alignment unless it introduces `_Atomic`. An atomic introduction at any layer raises alignment to at least the target atomic alignment. An `ALIGNED` node requires an explicit, nonzero power-of-two alignment and may lower the referenced alignment.
Unsigned 64-bit corrections temporarily select 64-bit x87 precision while
retaining the caller's rounding mode, then restore the saved control word
before the final store or integer truncation.
_Avoid_: host `long double` layout, general long-double support

**Represented double-to-unsigned-wide conversion**:
An explicit non-atomic cast from `double` to `unsigned long long`. Linear IR keeps one typed conversion, and the i386 emitter decomposes the truncated result into exact high and low 32-bit words before publishing a wide-value snapshot. For finite binary64 input, the defined interval is `(-1, 2^64)`. Values for which C leaves the conversion undefined have no promised result.
_Avoid_: implicit assignment, float input, signed wide or enum output, host floating conversion

**Private compiler control frame**:
A tagged loop or switch entry used by the in-kernel CupidC emitter. `break` selects the nearest control frame. `continue` selects the nearest loop and removes the saved selector for each switch crossed on the way. The parser accepts 128 active control frames and fails before entering a 129th.
_Avoid_: loop-only depth stack, silent capacity exhaustion

**Private compiler statement depth**:
The number of active recursive statement-parser calls in the in-kernel CupidC compiler. The parser accepts 1,024 active calls and rejects the next call before it recurses. A failed REPL evaluation restores this count with the other committed parser state.
_Avoid_: relying on the terminal task's native stack as a language limit

**Private CupidC scalar cdecl slot**:
A four-byte or eight-byte argument and parameter slot used by the in-kernel
JIT and AOT compiler. Integers, pointers, function pointers, `float`, and the
implicit method `self` occupy four bytes. A `double` occupies eight bytes with
its low word at the lower address. Calls evaluate arguments from left to right,
then permute complete words into source order before the call. Callees advance
later parameter offsets by the same slot widths, and callers reclaim the full
outgoing area. A direct function or method call with parsed fixed parameter
types converts `int` or `char` to either floating width, converts between the
two floating widths, and truncates a floating value for an `int` or `char`
slot. Represented pointer categories and integer null forms can fill a pointer
slot. A represented object pointer can also fill a fixed `int` or `unsigned
int` slot as one unchanged i386 address word. Narrow and floating destinations
do not receive that rule. The existing represented pointer-category rule is
unchanged. A parsed variadic tail widens
`float` to `double` and promotes `char` to `int`. Indirect calls without
retained fixed parameter metadata and legacy result-only kernel bindings keep
their source width. A reviewed native kernel binding carries the existing
function-pointer signature representation. Its direct call uses fixed
conversion, complete cdecl slots, exact cleanup, arity checks, variadic
promotions, and the declared result channel. The first reviewed cohort contains
console, string, port, and all 50 `libm` bindings, including
`double sqrt(double)`.
A named block-local function-pointer declaration retains its fixed
parameter types, variadic state, and result type. Its indirect call uses the
same conversion and 4-, 8-, or 16-byte slot path as a direct call, enforces
fixed arity, and publishes floating or SIMD results through XMM0. A
file-scope function-pointer typedef retains the same signature in the existing
sixteen-entry typedef table, with at most 32 fixed parameters per signature.
One direct typedef declaration publishes one alias. Direct free-function and
Cupid class method parameters carry that signature. A declaration-initialized
automatic object carries its own copy, including each object in a comma list.
A file object declared directly with the alias also retains the signature. It
may start as grouped zero, the active `((void *)0)` spelling, or a compatible
plain function designator. A defined target is written into initialized data;
a later target receives an absolute data patch during shared symbol resolution.
The object may also accept a checked plain assignment, clear to null, and make a typed indirect call. A non-null
assignment must match the result, record-pointer identities, fixed parameters,
and variadic boundary before the address is stored. A named raw callback file
object keeps the same signature and initialized-data behavior. A plain function
designator or its direct `&function` address may initialize a signature-bearing
file or automatic callback. The explicit address keeps the same compatibility
checks, immediate data write, later data patch, and checked plain assignment as
the designator. Runtime initialization and assignment accept `&(function)` and
nested grouping. A direct raw
callback parameter on a free function retains its result, fixed parameters,
record identities, and variadic boundary through the existing cdecl call path.
Callback-valued parameters are parsed recursively and retain the same result,
parameter, record-identity, prototype, and variadic metadata. For a
`TYPE_FUNC_PTR` parameter, `param_struct_indices` holds the nested signature
handle instead of a record index. The argument remains one four-byte i386 word,
so the retained graph does not change cdecl layout. Raw and direct-typedef
signature graphs compare structurally. Each comparison memoizes handle pairs
across the combined 49-handle domain. Parsing and comparison accept at most 16
nested levels. The backing pool holds 33 raw signatures: the active kernel
callback descriptor occupies one, preserving 32 records for source
declarations. Failed program and REPL transactions restore that pool with the
rest of the parser state. A structure or class field declared directly with a
callback typedef retains that typedef's signature. Checked plain assignment
stores a compatible function or callback value, null clears the field, and a
member read keeps the signature when it is copied into a named callback object.
Nested record and indexed record-array traversal keep the same rule. Raw
function-pointer field declarators retain the same signature. A postfix call
through either field form uses typed fixed and variadic cdecl conversion,
evaluates the member designator once, and preserves represented scalar,
floating, pointer, and SIMD results. A typedef-backed callback array field
keeps the same signature through an indexed store, named copy, and direct
postfix call. The compiler evaluates its index once. A one-dimensional raw
function-pointer array with static storage keeps the same signature at block,
file, and persistent REPL scope. Its bound may be a positive constant or may
be inferred from a nonempty initializer. Omitted fixed elements and an
uninitialized fixed array begin as null. Initializers accept null and
compatible defined or later-defined functions; a later target uses
`CC_PATCH_DATA_ABSOLUTE`. Indexed stores and calls keep the retained signature,
including calls written with an explicit unary `*`. Block-static scalar raw
callbacks use the same initialized-data path. Automatic raw callback arrays,
raw callback array parameters, raw record or class field arrays,
multidimensional raw callback arrays, raw method parameters, conditional field
values, and aggregate results remain outside this boundary. Callback-valued
results, pointer-to-function-pointer `**` declarators, and callback alias
chains remain separate work.
Typedef-backed fixed callback field arrays remain the separate ADR 0328 path.
Direct structure and array results remain rejected. Program and REPL rollback
restore typedef and side-table metadata, provisional signatures, code, data,
and every patch kind. The
promoted standalone CupidC seeds do not contain this private parser or ELF
writer, so their reproof is not callback carriage evidence. ADR 0303 records
free-function parameters, ADR 0306 records global storage and checked
assignment, ADR 0310 records automatic objects and method parameters, and ADR
0313 records initialized-data function-address patches. ADR 0315 records raw
file objects and free-function parameters. ADR 0319 records direct explicit
function addresses, ADR 0321 records typedef-backed callback fields, and ADR
0324 records grouped runtime function addresses. ADR 0325 records raw callback
fields and direct field calls. ADR 0328 records typedef-backed callback field
arrays, and ADR 0330 records data-backed raw callback arrays and block-static
raw callbacks. ADR 0331 records recursive callback-parameter signatures. ADR
0332 records fixed signature publication for reviewed native bindings, and ADR
0333 records nested callback publication for the active icon drawer binding.
Source-head guest runtime proves the existing scalar raw forms with
initialized, parameter, clear, reassignment, and typed-call coverage.
The active nested shape is `p_icon_set_drawer` in `kernel/lang/cupidc.cc`,
which points to `gfx2d_icon_set_custom_drawer` and its callback-valued
`drawer` parameter. The binding retains `void(int, int)` in the shared graph
and publishes its handle in the outer `void(int, callback)` descriptor. Active
Doom source in `kernel/doom/src/f_wipe.cc` supplies
the six-entry raw callback table for the separate array boundary. Checked-seed
hosted CupidC still owns both production translations. The standalone checked
seeds do not contain the private parser, and this capability moves no build
owner or host dependency.
_Avoid_: reversing source evaluation, four bytes for every parameter, erasing a nested callback signature, splitting a double into unrelated arguments

**Represented bit-field assignment**:
A plain Cupid C assignment to a non-atomic integer bit field whose declared storage unit is four bytes and fits inside its record. Linear IR retains the graph member, while i386 emission preserves neighboring bits and returns the value represented by the stored field.
_Avoid_: bit-field address, ordinary member store

**Represented bit-field mutation**:
A compound assignment or prefix or postfix update to a represented bit field. Linear IR evaluates the record address once, applies target-width promotion, and keeps the extracted old value when postfix semantics require it. Partial fields are nonvolatile because their current store path needs a second complete-unit read. A volatile 32-bit field uses one read and one direct store.
_Avoid_: bit-field address, reconstructing a postfix value after truncation

**Represented ordinary bit-field promotion**:
The integer promotion of a narrow `unsigned int` bit-field read in an ordinary expression. On Cupid i386, every value of a field narrower than 32 bits fits signed `int`. The frozen expression and Linear IR keep the direct graph-member reference so this field-specific same-rank conversion cannot be mistaken for a general unsigned-to-signed promotion.
_Avoid_: generic same-rank signedness conversion, bit-field mutation

**Block-static object**:
A block-scope object with static storage duration. It keeps its absolute frontend block-binding identity, receives a local ELF object symbol, and never consumes an automatic frame slot.
_Avoid_: automatic local, file-scope object

**Block extern alias**:
A lexical block declaration that names a canonical linked object. It owns no storage. Uses retain the canonical file-binding identity even when no ordinary file-scope name is visible.
_Avoid_: automatic local, block-static object

**Block function alias**:
A lexical block declaration that names a canonical linked function. It owns no storage or runtime work. Uses keep the type visible at the declaration and retain the canonical function identity even when no ordinary file-scope name is visible.
_Avoid_: nested function, automatic local

**Implicit function alias**:
A block function alias introduced only by the explicit Doom compatibility profile when a bare undeclared identifier is called. It owns the identifier expression that triggered the old-style `extern int name()` declaration and keeps one canonical external function identity across blocks. Calls parsed before a later prototype retain default argument promotions.
_Avoid_: general undeclared identifier support, forward declaration

**Doom compatibility pointer conversion**:
A conversion enabled only by the explicit Doom compatibility profile between one unqualified i386 function pointer and one unqualified i386 data or `void` pointer. CupidC marks the conversion in assignments, automatic initializers, fixed call arguments, returns, and explicit casts. Linear IR verifies both four-byte representations, and i386 emission keeps the bits unchanged.
_Avoid_: general function-pointer compatibility, GNU pointer conversion

**Pointer-preserving static address cast**:
An explicit cast between two non-atomic pointer types around a static string or linked binding address. The initializer keeps the original string bytes, binding identity, and target-byte addend. A cast through an integer is not pointer-preserving and remains outside this term.
_Avoid_: runtime pointer cast, function/data compatibility conversion, integer detour

**External inline definition**:
A file-scope function definition whose external declaration set contains `inline` but does not consist entirely of `inline` declarations without `extern`. A prior or later ordinary declaration and an `extern inline` definition with effective external linkage both provide the external definition. An earlier `static` declaration keeps the function internal, even when its definition is spelled `extern inline`. CupidC records the translation-unit result on the canonical binding while the definition keeps its exact source spelling. Any external-linkage function declared `inline` must have a definition in the same translation unit.
_Avoid_: pure external inline definition, static inline function

**Block typedef**:
A type alias whose name lives in one C block scope. It keeps a stable frontend type identity, shares the ordinary identifier namespace, and owns no runtime storage.
_Avoid_: file typedef, block object

**Private fixed-array typedef**:
A private JIT, AOT, or persistent REPL alias that retains one positive element count as part of its type shape. Automatic, global, block-static, record-field, and class-field objects allocate the complete checked size. A record or class array member retains its complete object size and record-element identity through direct or pointer access, including indexed assignment inside another record array. Function and method parameters decay to an element pointer, while `sizeof` keeps the complete type or object size.
_Avoid_: scalar alias, multidimensional array typedef, pointer to array

**Private unsigned word**:
A four-byte unsigned value retained by the in-kernel compiler through objects, pointers, calls, kernel binding results, enum symbols, unary and conditional expressions, `sizeof`, usual arithmetic conversion, scalar returns, and conversion to or from `float` and `double` within C's defined ranges. Relations, division, remainder, and right shift use unsigned i386 behavior. `/=`, `%=`, and `>>=` keep that rule and evaluate their destination once. The Browser array-length lane uses this type through the ECMAScript maximum length.
_Avoid_: signed int bits, wide integer value, defined out-of-range floating conversion

**Block enumerator**:
An enum constant whose ordinary identifier lives in one C block scope. Its frontend binding keeps the evaluated target value and type but owns no storage, address, symbol, relocation, or runtime declaration work.
_Avoid_: local constant object, file enumerator

**Block enumerator activation**:
The lexical source point where a block enumerator becomes available to ordinary-name lookup. A declaration or function prefix can own the point directly. A type-name definition uses an expression or initializer owner so IR can validate source order independently of runtime control flow.
_Avoid_: runtime evaluation point, local-object lifetime

**Block-scope record tag**:
A `struct` or `union` name whose identity lives in one C block scope. A declaration may leave the type incomplete, a later definition in the same scope may complete it, and a nested tag may hide it until that nested block ends. A tag declared in a function definition's parameter list shares the outer body scope and expires when the definition ends.
_Avoid_: file tag, block object

**Union initializer selection**:
The single direct member chosen by a represented C union initializer list. A positional clause selects the first eligible named member, while a direct member designator selects that member. The initializer forest keeps one edge for the choice. Runtime lowering zeros the complete union before storing the selected member, and static emission writes the member over zero-filled target storage.
_Avoid_: structure member sequence, multiple active members

**Compound-literal object**:
An unnamed object created by a C compound literal. At block scope, one absolute expression identity names the source site's persistent automatic frame slot. Its initializer runs whenever execution reaches the expression, and the expression is an lvalue naming that object. Aggregate list initialization uses a separate emitter-private staging slot, then replaces the persistent object after every initializer read has finished. A narrow string initializer copies immutable literal bytes directly after zeroing the persistent array.
_Avoid_: temporary structure value, hidden block binding

**Runtime string literal**:
The immutable narrow bytes retained by a string expression evaluated inside a function. Linear IR keeps the expression identity, while the i386 emitter owns its local `.rodata` symbol and relocation. An automatic character array initialized from those bytes is a separate destination object.
_Avoid_: host string pointer, automatic string array

**Structure value**:
A complete Cupid C `struct` carried by value through Linear IR. One abstract stack entry represents an emitter-owned snapshot of the target bytes, not an address that aliases the source object.
_Avoid_: aggregate scalar, borrowed object address

**Hosted source frontier**:
The unchanged implementation and contract files that hosted CupidC can
preprocess, parse, lower, and emit as deterministic i386 ELF32 objects. The
current target-profile gate contains 44 strict C11 roots and three GNU-enabled
runtime roots. `HOSTED_I386_LINUX` owns 35 Linux requests,
`HOSTED_I386_WINDOWS` owns six `_WIN32=1` requests,
`FREESTANDING_I386` owns the headerless Windows command probe, and
`HOSTED_I386_KERNEL_BRIDGE` owns the two requests that may include
`/kernel/lang`. The GNU profile owns the Linux runtime, its behavior probe,
and the Windows runtime wrapper. Together they cover the complete 19-source
static Linux tool union, all six Windows-profile roots, the direct Windows
runtime contract, `kernel/lang/as_elf.cc`, and all seventeen Toolchain contract
translation units.
The retired `HOSTED_TOOLCHAIN_64` and `HOSTED_KERNEL_BRIDGE_64` names have no
active roots.

Non-atomic `long double` uses twelve-byte objects for arithmetic,
floating-width conversion, all six matching or mixed floating comparisons,
fixed and unprototyped arguments, variadic calls and reads, function returns,
and direct or indirect call results. Bounded finite normal decimal `L` tokens
produce exact x87 values in automatic expressions and static-duration scalar,
array, or complete-record leaves. Static output preserves signed zero, clears
both padding bytes, and follows the ordinary `.bss`, `.data`, or `.rodata`
policy. Static conversion between those values and every represented integer
width uses target-only packing. Integer outputs other than `_Bool` truncate
toward zero before the range check, while `_Bool` tests the original floating
value and integer-valued zero retains its zero initializer record. Static
truth, comparisons, short-circuit logic, conditional selection, and
conversion to or from binary32 and binary64 use the same target-only value
model. The accepted x87 classes are signed zero, subnormal, normal, infinity,
and NaN. Static `+`, `-`, `*`, and `/` use unsigned 128-bit target arithmetic,
round once to the explicit x87 significand, and produce final initializer data
without runtime IR. Runtime conversion between `long double` and every signed
or unsigned i386 integer width uses `FILD` and `FISTP`; integer output restores
the caller's x87 control word. Hexadecimal or subnormal long-double literals
and decimal ratios beyond the bounded parser remain open. ADR 0256 records the
canonical x87 class rules, and ADR 0260 records the static arithmetic model.

The checked cohort requires byte identity for every newly compiled object and
linked executable. Complete CupidC-emitted closures for CupidC, CupidASM,
CupidDis, CupidLD, and CupidObj link with CupidASM startup and the hosted i386
Linux runtime, then run real behavior checks on Linux or through WSL. Its
initial, frozen, and newly discovered contract inventories must match exactly.
Publication accepts only a dedicated verified `cupidc-contracts` destination,
and every contract run verifies the named artifact, complete cohort,
manifest-bound contract input set, checked seed manifest, and fixed-point
source inventory before execution. Those inputs include the Windows startup
and runtime probe, the Toolchain Makefile, and both Python modules that
construct or verify the cohort, so restored timestamps cannot hide
control-plane drift. Manifest hashing, parsing, and validation use one captured
byte sequence, preventing a concurrent replacement from mixing provenance
from different reads. Link plans reject an unknown object key before the first
compiler process starts. The staged kernel ELF contract names its full
`as_elf`, CupidLD, CupidASM, x86, and ELF32 implementation closure. Compile
resource policy belongs to each contract
plan. Fourteen ordinary plans use the worker pool and a 900-second budget. The
pool drains before `cupidc-object` compiles alone with a 1,800-second budget.
The runtime compile and parallel link pool retain their 360-second limits.
The v2 publication record requires stage three and stage four as the compared
convergence pair. ADR 0282 records these scheduler and publication boundaries.
The final supported gate passed in 4,589.9 seconds. It compared stage-three
and stage-four contract artifacts, ran and published stage four, verified 21
artifacts from 65 inputs, and matched the three native Windows user programs
to the checked seed at both object and executable boundaries. Its 22,591-byte
manifest has SHA-256
`ff193cf81293553706373f5a37d0fedf3dfae0bebcbc608d892a4f40ea3d9629`.
The warmed supported path passed in 12.2 seconds.
_Avoid_: self-hosted toolchain, completed source cohort


**Production crypto cohort**:
All 20 `kernel/crypto` translation units built by checked-seed CupidC in the normal image build. Each deterministic i386 ELF32 object passes the shared validator before publication. The boot gate runs 62 crypto, ASN.1, and X.509 checks, walks the incomplete external CA-array path, and initializes the CSPRNG through its represented CPU assembly.
_Avoid_: compiler-head frontier, partial crypto cohort

**Production CupidC kernel cohort**:
The 156 checked-in normal-build translation units owned by checked-seed CupidC, plus the generated kernel symbol-table translation unit. All 157 sources use `.cc`. The five shared Toolchain roots are also part of the 19-source i386 Linux fixed-point plan; their native GCC and Clang rules select C explicitly with `-x c`. The symbol build freezes the pass-one kernel and five-tool seed, runs checked CupidDis to capture canonical symbol text, and runs checked CupidObj to generate the packed source. Python independently renders the expected bytes, rejects malformed text, missing symbols, output mismatch, or live input drift, and publishes only a regular complete file. The checked compiler wrapper freezes that source and its two-header closure before it validates and publishes the data-only object. It also freezes the exact source-driven closures for the kernel entry, SIMD services, the core string implementation, Nuked OPL3, JPEG decoding, glyph rasterization, libm, FPU state, per-CPU setup, and SMP bring-up. Checked execution uses the shared runner with that caller-owned five-tool capture. The runner verifies the complete live cohort after CupidC returns. Drift detected by that check prevents publication even when compilation succeeded. ADR 0124 records the first 111-root naming transfer, ADR 0126 completes the fixed-point naming boundary, ADR 0129 transfers the in-kernel CupidC lexer, ADR 0135 transfers Nuked OPL3, ADR 0139 transfers JPEG and glyph rasterization, ADR 0167 transfers the FPU and SMP roots, ADR 0176 transfers libm, ADR 0180 transfers the kernel entry and SIMD roots, ADR 0181 transfers the final strict host root, ADR 0224 transfers kernel-symbol source generation to CupidObj, ADR 0246 records the shared invocation boundary, and ADR 0276 adds the checked CupidLD kernel source. Poisoned-host rebuilds and exact recursive header closures cover every recipe. The most recent complete two-pass frontier predates that addition: it covers 155 sources in a 445-file snapshot with SHA-256 `99d03de14f544f6a76d21ed147e62018873f1e2e8dfa2f4459830b69314432c2`, and both 155-object passes are byte-identical at 3,749,796 bytes each. The current 156-source production build passed. The broader two-pass frontier targets 156 sources and 312 checked compilations; its latest rerun exceeded 2,340 seconds without a compiler diagnostic. That timeout is not a complete frontier pass. Input discovery skips hidden paths under the active include roots, which keeps private compiler staging headers out of the repository snapshot during concurrent builds. Final directory promotion retries only short permission-style locks; a persistent lock or any other filesystem error fails without publishing a partial frontier. A data-only relocatable object is valid without `.text` when its sections and symbols pass the remaining ELF checks. The combined graph includes the ISO runtime fixture as an image input. Strong four-vCPU checks cover both supported NICs, all three FPU milestones, the promoted SMP, libm, and string paths, RDRAND, all 62 crypto checks, USB storage, desktop and terminal startup, audio output, TrueType glyph use, an exact baseline JPEG decode, and in-OS CupidC execution. The strict checked-in kernel and driver cohort has no host-compiled root.
_Avoid_: all kernel C, compiler-head frontier, checked seed alone

**Unbootstrapped C census**:
The build audit finds seventeen tracked `.c` files outside `TempleOS/` and none
in a supported transform. It classifies seven historical copies, three
superseded implementations, one dormant runtime draft, five host fixtures, and
one host oracle. Renaming a `bin/*.c` copy would activate it through wildcard
discovery. Renaming a fixture would silently switch it to C++ semantics and
misstate its owner. The audit rejects any active tracked `.c` source owned by
CupidC. It does not infer the reverse claim from `.cc`. A checked compile edge,
the checked Toolchain contract, or an exact runtime-delivery policy entry with
a CupidObj edge supplies independent ownership evidence. The policy also locks
the seventeen residual `.c` paths and four unreachable `.cc` paths, so a host
or inactive source cannot leave the census through a suffix-only rename. The
active evidence rule applies even when an audited tree has no policy file. An
unreferenced `.cc` in a nonproduction audit needs policy, a recorded source
relation, or an explicit Make exclusion. The complete production graph
requires an exact policy entry, while a deliberately partial production view
defers that census. The safe rename set remains empty. ADR 0284 records the
first direction of the gate, and ADR 0291 records the independent provenance
rule.
_Avoid_: pending active-source renames, suffix-only migration, suffix-derived ownership, Cupid-owned host fixtures

**Production Doom cohort**:
The 83 `.cc` Doom and Cupid platform translation units built by checked-seed CupidC in the normal image. Three sources use the exact `DOOM_COMPAT_I386` profile; the sound adapter and 79 Doom-tree sources use `DOOM_TREE_I386`. The checked wrapper freezes the selected source and all 291 `.h` and `.inc` inputs visible through the profiles' 20 include roots. It recursively scans visible `.c` and `.cc` files beneath `kernel/doom` before and after compilation. An always-checked manifest fixes both source memberships and every header hash without changing its timestamp when the content is unchanged. A legacy `.c` file, an unlisted `.cc` file, a missing root, added or removed headers, byte drift, symbolic links, and NTFS junctions fail before object publication. The active dglibc source is 67,155 bytes and produces a 93,332-byte object with SHA-256 `e2496b01c93a7858a0c035b53aea0ad834d95d2be3f7ae49574d1759ebec34d6`. The 69,366-byte closed profile manifest has SHA-256 `47ba35158cac0a7df253a0056235223e62fee24df74701800f88763e588611c2`. The normal Make target passes the checked seed manifest. The wrapper captures one stable profile inventory, derives both the bounded `CUPROF1` snapshot and an independent Python JSON oracle from it, and runs checked CupidObj from the exact frozen seed. It requires byte parity, rechecks the seed, live inputs, candidate, output directory, and existing output under an adjacent no-follow lock, then preserves identical bytes and their timestamp or publishes atomically. CupidObj authors the production bytes; Python retains discovery, native-path checks, freezing, parity, drift detection, locking, and publication. Asset-free runtime checks cover active nonlocal exit, repeated quit and error cleanup, production config helpers with test-only files, native rename and copy boundaries, block-cache failure handling, RamFS limits, FAT collision, read, handle, busy-replacement, and 8.3 behavior, HomeFS ownership, depth, and batched publication, no-WAD recovery, shell survival, and the full stateful four-CPU frontier on e1000 and RTL8139. Gameplay remains a separate IWAD-backed boundary. ADR 0184 records ownership, ADR 0211 the storage bridge, ADR 0214 the shell-session lifecycle, ADR 0242 the CupidObj format boundary, ADR 0243 its seed carriage, and ADR 0244 the normal publisher.
_Avoid_: compiler-head Doom frontier, claiming WAD runtime behavior from asset-free tests, host-built Doom cohort

The fixed asset-free frontier invokes the default Doom search, an explicit
missing-IWAD path, the shell-return marker, and a fresh CupidC-built `ls`.
ADR 0232 records this reproducible recovery gate. It does not change the IWAD
boundary in the production Doom cohort.

**Production generated-install cohort**:
The ramfs program table, homefs document table, and CupidASM demo table. Checked-seed CupidObj generates all three `.cc` sources from Make's ordinal inventories, and checked-seed CupidC compiles them under the fixed kernel profile. Each generation recipe depends on the checked runner, seed manifest, and all five seed images. The compiler wrapper freezes the generated source and complete header union, delegates checked execution to the shared runner, validates each relocatable object, and publishes it atomically. Drift detected by the runner's post-run check rejects the object. `tools/hostbuild.py` remains the parity oracle but no longer owns these production sources. ADR 0204 records the generation transfer, and ADR 0246 records the shared invocation boundary.
_Avoid_: every generated C file, Python-free generation, kernel source cohort

**Production checked-seed tool cohort**:
The root image transforms are owned by CupidASM, CupidObj, CupidLD, and CupidDis. The normal graph verifies and freezes the manifest-bound five-tool seed before each command, then checks the live trust unit again after the command. Checked production kernel, generated-install, and user CupidC calls plus checked user CupidLD links use that same invocation contract. Each wrapper supplies its existing frozen cohort, and the shared runner rejects manifest or image drift detected after the private tool returns. It contains six CupidASM transforms, 192 CupidObj transforms, three CupidLD links, and six CupidDis participations. The extra assembly and link operations build the artifact-size contract, whose policy logic is compiled by CupidC. CupidDis supplies kernel-symbol text, participates with CupidObj in the transactional `kernel.bin` publication, strictly checks the mixed-mode SMP trampoline and bootloader, and guards the ISR and context-switch objects before publication. The two objects use `tools/hostbuild.py assemble-cupidasm-object`. Hostbuild freezes the source and seed, validates a private i386 relocatable with executable bytes, requires complete CupidDis coverage of those bytes, rechecks the live boundaries, and publishes atomically. The final kernel gate remains an independent whole-kernel check. The SMP and bootloader paths share one checked raw-image transaction. It owns output locking, source and seed freezing, drift checks, private candidates, publication-boundary checks, and atomic replacement. Each caller retains its image and map policy. The normal boot rule enters that transaction through `tools/hostbuild.py assemble-bootloader` with the production manifest and `CHECKED_SEED_INPUTS`, so standalone CupidASM overrides cannot bypass the checked closure. All five production assembly recipes now keep the checked-seed prerequisite closure under standalone-tool overrides. The expanded eleven-test transaction suite passed in 1.708 seconds, including direct mismatch and live-output drift checks for both callers. Parent-replacement coverage exposed a POSIX candidate leak when private work lived below the output parent. Private raw-image roots now live directly below the stable repository root. The two caller modules pass all 10 tests on Windows and all 10 through WSL, including parent replacement with no leaked candidate. Hostbuild freezes the selected seed manifest and five artifacts, the 431-entry production manifest and cohort, and the existing output boundary. Checked CupidDis validates that private cohort before checked CupidObj flattens its frozen final ELF. Hostbuild rechecks the live trust inputs and output, then uses parent-relative atomic replacement. Every failure preserves the prior raw kernel. Root `all` has 443 transforms, and every transform has a Cupid participant. The artifact-size verifier builds and runs a private CupidC contract while Python pins the files, launches the checked tools, and checks an independent oracle. The Toolchain `all` verifier applies the same division to the 21-artifact publication, exact 75-file input inventory, 55-file bootstrap closure, and Linux publication seed. The fifth CupidASM transform assembles the ISO spanning fixture from checked-in CupidASM source. Python verifies its exact byte lane and controls publication. The CupidObj total includes the three installation-source generators, kernel-symbol source generator, pristine disk-template author, repository ISO author, and guarded Doom profile-manifest author. Across all three roots, CupidC participates in 250 transforms, CupidObj in 192, CupidASM in nine, CupidLD in nine, and CupidDis in nine. Four transforms run Cupid-built semantic contracts. Python supplies orchestration, safety, parity, and publication checks across all 452 transforms. Windows runs the checked native PE execution seed directly; Linux runs the checked static ELF seed. Contract publication remains rooted in the Linux bootstrap seed. Source head uses checked Linux CupidC for stage-two Windows C objects and the PE execution seed for assembly, inspection, and linking before the native stages converge. Preliminary uncapped Windows and Linux proofs matched stages three and four across the complete artifact and behavior sets. Linux later passed its clean proof, promoted the stage-four seed, and passed a reproof from that seed. Native Windows then passed its clean proof in 1,152.7 seconds, promoted the stage-four PE32 cohort, and passed a 1,130.9-second reproof with all five initial seed comparisons true. The checked user compiler and Toolchain contract publisher create the output directories needed for their artifacts, so empty-directory setup is not a separate graph transform. The compiler pins every POSIX component with no-follow descriptors and every Windows component with parent-relative directory handles until its final resolved-output check. Make applies `$(sort ...)` to every wildcard-discovered output list, so generators and links receive the same order under Windows and Linux host locales. The repository stores its runtime JPEG as a sequential SOF0 frame. Hostbuild freezes its exact bytes and asks checked CupidObj `wrap-jpeg` to validate and wrap the private snapshot under the original source identity. Only after CupidObj succeeds does Python run an independent validator against the same snapshot, require byte parity, recheck the live manifest and input, and publish a regular candidate atomically. A validator disagreement and a failed private oracle copy have distinct diagnostics, and both preserve the old object. FFmpeg, `jpegtran`, `djpeg`, and `cjpeg` do not participate. The first cross-host comparison matched 426 of 430 kernel artifacts and traced all four remaining differences to the old host JPEG conversion. After the replacement, a 607.7-second Linux kernel build and a 341.6-second Windows root build produced all 430 frozen kernel artifacts byte for byte. A fresh normal image then passed a private `/bin/ls.cc` JIT boot in 49.8 seconds. ADR 0190 records the root tool handoff, ADR 0204 records installation-source ownership, ADR 0224 records kernel-symbol source ownership, ADR 0227 records the fixture transfer, ADR 0235 records the JPEG acceptance transfer, ADR 0238 records disk-template production ownership, ADR 0241 records ISO production ownership, ADR 0244 records profile-manifest production ownership, ADR 0245 records publisher-owned output directories, ADR 0246 records the shared invocation boundary, ADR 0271 records trampoline inspection, ADR 0272 records native Windows execution-seed adoption, ADR 0277 records the raw-map transaction, ADR 0280 records the clean Linux stage-four promotion, ADR 0281 records the clean Windows stage-four promotion, ADR 0282 records stage-four contract publication, ADR 0283 records the normal bootloader cutover, ADR 0286 records guarded assembly-object publication, ADR 0295 records the native Windows ABI gate, ADR 0297 records the artifact-size contract, and ADR 0302 records Toolchain manifest verification.
The broad paragraph above preserves earlier checkpoint detail. The clean
Linux candidate passes the 21/5/22 failure/help/success inventory, and the
clean Windows candidate passes 9/5/8; CupidASM and CupidDis changed in each
candidate. The promoted-seed Linux reproof reports all five initial
comparisons false because the new startup function types remain in static ELF
symbol tables and flow into every linked tool. The Windows reproof reports all
five comparisons true because PE linking removes those ELF symbol tables.
Both checked CupidDis images carry executable-relocation,
raw, relocatable, linked local-target, and static ELF code-anchor checks.
Hostbuild selects the raw form for the nine bootloader and four SMP targets
proved by active-source tests, the relocatable form for both active CupidASM
objects, and both linked checks for the two kernel ELFs before flattening. The
artifact-size policy covers
four OS outputs, five Linux seed images, and five Windows seed images.

The source-current three-root graph records 452 transforms, including 443
under root `all`. CupidC participates in 250, CupidObj in 192, CupidASM in nine,
CupidLD in nine, and CupidDis in nine. Four transforms run Cupid-built semantic
contracts. Its assembly ownership contract covers all 32 active assembly
sources, including five Toolchain startup sources, with no ownerless input.
Python participates in all 452 for coordination and safety, but no transform is
Python-only. The Toolchain publisher now gives a checked strict
C11 author its artifact and source facts plus 58 raw stage pairs through
`CUPMAN4`. The pairs cover 17 contract objects, 16 contract executables, 19
bootstrap C objects, one startup object, and five tool images. The author
requires both members to be regular, nonempty, and byte-identical, then hashes
both streams. It derives the 17 object-comparison records from those bytes,
checks the executable pairs against their artifact facts, and derives the
fixed-point summary from its exact inventories. The protocol has no caller
`all_equal` field. Schema
`cupid.toolchain-contracts.v3` is unchanged. Python performs its independent
four-inventory comparison after the Cupid author accepts the request. It still
owns pinned filesystem access, process launch, drift checks, private staging,
and atomic publication. The converged stage-four Linux tools remain the author
producers. Linux runs their static ELF, while Windows runs a validated native
PE built from the same author source, checked Windows runtime, startup, and
exact imports. The executable container changes by host, but the Linux
publication provenance does not. The `CUPMAN2` verifier is also host-selected.
Both checked Python contract launchers resolve `tools` from this checkout
before consulting installed packages. The direct manifest module passes 40
tests in 54.623 seconds, the publisher passes 64 in 12.144 seconds, and the
pinned verifier runner executes 25 tests in 32.773 seconds with three
POSIX-only skips on Windows. ADR 0307 records the paired-evidence boundary, and
ADR 0311 records checkout-local contract imports, and ADR 0322 records native
Windows author execution. The latest complete schema v3
`CUPMAN4` `make -C toolchain all` passed. The Cupid author and
Python agreed on all 58 stage pairs. Every stage-three object and executable
matched its stage-four counterpart, the hosted runtime passed, and live inputs
stayed frozen. The publisher wrote 21 artifacts and a 27,071-byte manifest
with SHA-256
`02408d9d541de1454e2f0888cff501bc755964448d0f177a4162bcebdcaf178b`.
Its final `CUPMAN2` verifier printed
`Cupid Toolchain manifest: ok (21 artifacts)`. The first corrected attempt had
already published a valid cohort when that final read-only verifier found an
unrelated installed `tools` package. The checkout-local launcher rule fixed the
host import boundary. The source-current `make bootstrap-audit` and
`make check-bootstrap-audit` both pass. The generated fixed-point inventory
records failure, help, and success counts of 21/5/22 for Linux and 9/5/8 for
Windows. The extra cases prove source-head static ELF code anchors. The
promoted seeds carry the same inventories, and the normal linked-kernel pass
selects the code-anchor rule before CupidObj flattening.
A pre-final-CTXT build reached the exact-size gate after 668.414 seconds. The
later poisoned-host build passed in 684.260 seconds, and its private four-vCPU
e1000 smoke passed in 64.601 seconds. Those records are preceding checkpoint
history.

At the next preceding source checkpoint, the first post-documentation fully
poisoned `make -j4 all` stopped only at the expected size mismatches after
641.474 seconds. It measured a 9,324,520-byte pass-one ELF and a 9,228,268-byte
raw kernel while the 9,447,400-byte final ELF stayed exact. Only the pass-one
and raw-kernel policy rows changed. The artifact group then ran 45 tests in
2.625 seconds with four expected Windows skips. The repeated fully poisoned
build passed in 654.397 seconds, checked all fourteen artifacts, preserved the
FAT contents, and staged `test_iso/hello.iso`.

At that checkpoint, the bootloader, SMP trampoline, pass-one ELF, final ELF,
raw kernel, disk image, and artifact policy had SHA-256 values
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`,
`b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90`,
`379437b3ad088f645e718773ae3c122d91e763e8af72fac12d14db1785cab0ef`,
`b282d1bbaf1e1cb2f2c254d64d32a2c4a9e18d94a2c72c806c4bb839a0a64c19`,
`9c3042e8a0963e904e805905b14da7aca3bb991abdbbc8a547a56b59be6e2698`,
`9045807b2bfffe41e2eaab92ab6fd4a4615fb7d72a26649ca2c037ae050bb15f`,
and `63d912a9e9d9399efc03826af8b4628737b685f3180f1df74a84ce9b7306f895`.
Its strong full private frontier passed in 787.369 seconds. The framebuffer
changed 52,616 pixels at 640x480. AC97 produced 32,149,003 stereo 44,100 Hz
frames with a peak of 25,600. The PC speaker produced 75,924 stereo 44,100 Hz
frames with a peak of 8,415. The direct-call, named-callback, typedef-callback,
overall feature-14, and JIT markers each appeared once and in order. The
144,309-byte log has SHA-256
`effdd6128933e99ada7b8203e16397a2d5c1ba7fcf864dc8f34fe4963e767ec2`
and no rejection markers. The source image stayed unchanged at SHA-256
`9045807b2bfffe41e2eaab92ab6fd4a4615fb7d72a26649ca2c037ae050bb15f`.

The preceding integrated checkpoint's first poisoned build reached the exact-size gate
after the complete checked Cupid build. The gate rejected only the three
rebuilt kernel outputs: a 9,345,464-byte pass-one ELF, a 9,472,440-byte final
ELF, and a 9,251,100-byte raw kernel. The artifact contract group then passed
all 46 tests in 4.160 seconds, with four expected Windows skips. After those
three policy rows were updated, a repeated fully poisoned `make -j4 all`
passed in 874.531 seconds. It checked all fourteen exact paths, preserved the
FAT contents, and staged `test_iso/hello.iso`.

| Historical integrated output | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/smp_trampoline.bin` | 4,096 | `b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90` |
| `kernel/kernel.elf.pass1` | 9,345,464 | `5dbd2c5acb7b1604cf6daf6f311e88015d0762125c60920da3737d7e10d76f06` |
| `kernel/kernel.elf` | 9,472,440 | `5810ddcb963cfadb4fea3b1343bb38c17ce3f762a48f25615b3feb653f1638e3` |
| `kernel/kernel.bin` | 9,251,100 | `4014b1b2acf34be4dd7483fb8aa9e8a8b0e76eea771c83669571cbf7b66fe0e3` |
| `cupidos.img` | 209,715,200 | `31b25b6881419b1bb8a04b2b3765323b21c5706ac114af1a07b514dcdcd07ea3` |
| `bootstrap/artifact-size-policy.json` | 2,960 | `7b12be6d0dd33f9016ecb4287f5c9414e1da79ffc61e7957aab60cea94850474` |
| `test_usb_partitioned.img` | 33,554,432 | `057e0c86874090c99095f0558e9fa604bd7f1929f4da357da2c1baca949bb2bb` |

The integrated strong private frontier passed in 883.513 seconds with e1000,
four `max` vCPUs, SMP, a private image, and the USB fixture. The 640x480
framebuffer changed 89,630 pixels. AC97 produced 36,877,878 stereo 44,100 Hz
frames with a peak of 25,600. The PC speaker produced 76,251 stereo 44,100 Hz
frames with a peak of 29,912. The direct-call, named-callback,
typedef-callback, global-callback, automatic-callback, and overall feature-14
PASS markers each appeared once and in order. The feature run then printed a
clean JIT completion. The 161,418-byte log has
SHA-256
`bc30f5083b96a36362bec5975c0a88437c4f23515de329328bb03d8f6c3e9326`
and no rejection markers. The source image stayed unchanged at SHA-256
`31b25b6881419b1bb8a04b2b3765323b21c5706ac114af1a07b514dcdcd07ea3`.
ADR 0303 records typedef callback parameters, ADR 0306 records global callback
storage, ADR 0310 records automatic objects and method parameters, ADR 0313
records static callback initialization, and ADR 0312 records the promoted seed
and build boundary.
_Avoid_: native fixed point, Python-free build, hosted Toolchain contract cohort

**Production CupidObj disk template**:
The normal image recipe runs checked CupidObj `disk-template` first on frozen boot and kernel inputs. The command writes the MBR, stage two, raw kernel at LBA 5, zeroed reserve, FAT16 boot sector, two pristine FATs, and empty root directory. The result ends before cluster 2, so the active 200 MiB geometry needs a 10,697,216-byte result instead of a 209,715,200-byte command buffer. Python builds an independent private oracle, preserves an existing FAT partition by applying only the pre-partition prefix, stages frozen files, checks live input and output drift, and publishes the complete candidate. Fresh, invalid, and force-formatted images consume the full template. The publisher rejects links, aliases, malformed reusable geometry, concurrent hostbuild publishers, and partial or mismatched candidates without changing the old image. ADR 0236 records the capability, ADR 0237 records seed carriage, and ADR 0238 records production ownership.
The guarded recipe built a fresh 209,715,200-byte image with SHA-256 `8ad90a91103bf48d1e8d1e20b1b3dee48122ed1e4059b3f94cce7d750c262f16`. A private four-CPU `/bin/ls.cc` JIT boot passed from that image in 61.9 seconds. At a later handoff checkpoint, a rebuild preserved the existing FAT data and produced image SHA-256 `d1bfab4aed1f2116768ceed3e301fb14ffe2a36418eb4d4ebdf1108097cb2b05`; a private four-CPU JIT boot passed from it in 66.8 seconds.
_Avoid_: complete disk owner, complete FAT staging, standalone image publisher

**Repository ISO fixture author**:
Checked CupidASM authors the 4,096-byte spanning lane from `test_iso/big_pattern.asm`; hostbuild checks the exact candidate and publishes it atomically. Checked-seed CupidObj authors the complete production ECMA-119 and `RRIP_1991A` image through `iso-fixture`. It consumes a frozen ASCII manifest and a typed inventory of loaded files and logical directories, then must match an independent Python render byte for byte. `test_iso/fixtures.manifest` fixes the repository fixture membership before hostbuild freezes the regular-file tree. Make declares the same seven portable paths explicitly, and a checked test prevents that safe prerequisite list from drifting away from the manifest. CupidObj emits a primary volume descriptor, both path-table byte orders, identifier-sorted block-bounded directories, a forward SUSP continuation, fixed UTC metadata, and contiguous file extents. Rock Ridge `NM` records retain guest names drawn from the portable letter, digit, dot, underscore, and dash alphabet, capped at 127 bytes. `PX` and `TF` records carry fixed read-only metadata for other readers; Cupid OS ignores them. CupidObj rejects missing or undeclared logical entries, unsafe names, case-only collisions, bad parent graphs, and more than eight directory levels. Hostbuild remains responsible for native-path safety, private ordinal file snapshots, aliases, special files, live drift, the independent renderer, the per-output publication lock, and atomic replacement. A checked failure, unsafe candidate, parity mismatch, concurrent publisher, or changed input preserves the old image. The tracked 61,440-byte fixture rebuilds without `mkisofs`, `genisoimage`, or `xorrisofs`. The ADR 0241 production-handoff image has SHA-256 `3f8c84cea61e5e8bfc4e6a5fc09a030a4d6451d258a4ca2ea6486a923d1d08e3`; its private four-vCPU e1000 frontier reaches the exact six-name ISO listing, `PASS feature17_iso`, and CupidC JIT completion. ADR 0191 records the image boundary, ADR 0227 records the lane fixture boundary, ADR 0239 records the CupidObj source capability, ADR 0240 records seed carriage, and ADR 0241 records the production handoff.
_Avoid_: general optical-disc mastering, bootable ISO, Joliet author, guest ISO reader

**Production external-program cohort**:
The current checked publication path compiles `hello.cc`, `ls.cc`, and
`cat.cc` with CupidC, links private ELF candidates with CupidLD, then runs
CupidDis with known-instruction, local-target, and static code-anchor policies.
All three tools share one frozen seed capture. A failed inspection or output on
either stream preserves the earlier executable. ADR 0326 records this gate.
The `hello.cc`, `ls.cc`, and `cat.cc` examples compiled by CupidC and linked by CupidLD. Linux runs the checked i386 Linux seed directly, while Windows runs the checked native PE execution seed directly. The compiler and linker pass their caller-owned seed captures to the shared runner. It rechecks the complete live five-tool cohort after each command, and drift detected then prevents publication. The normal build consumes the checked native tools but does not rebuild them. An explicit Windows oracle runs private compiler and linker snapshots and requires all six outputs to match the checked production seed byte for byte. The PE seed has clean stage-three to stage-four convergence and promoted-seed reproof evidence. Its clean proof passed in 1,152.7 seconds, and its 1,130.9-second reproof matched all five initial seed images. Before compilation, the user build captures the exact bytes of the six kernel and public declarations that define the shared i386 syscall contract, compares the reviewed layout, and rechecks every input before success. The reviewed contract is version 5 with 103 fields in 412 bytes, a 136-byte directory entry, an 8-byte file status record, and 101 pinned function providers. The build fixes the freestanding user profile, `_start`, and the `[0x01C00000, 0x01E00000)` arena, then validates the same ELF program-header rules enforced by the kernel loader. `user/build/` contains ignored local outputs. The guest gate boots each program from a separate private copy of the same staged image, binds syscall evidence to the loaded PID, checks output by byte count and FNV-1a fingerprint, copies the hostile cat fixture over the normal `/home/readme.txt` path in the cat copy, and requires the same PID to exit cleanly. ADR 0127 records the ABI gate and corrected VFS record layout. ADR 0130 records the optional native Windows driver path. ADR 0133 records the ABI snapshot and private guest checks. ADR 0187 records the current arena and its coordinated memory-map move. ADR 0188 records the checked-seed default on Windows. ADR 0246 records the shared invocation boundary. ADR 0272 records the checked PE execution seed and retained Linux ABI root, and ADR 0281 records the Windows promotion.
The PE timings above describe an earlier cohort. The ADR 0292 execution seed
passed its clean proof in 1,253.4 seconds and its promoted-seed reproof in
1,061.3 seconds with all five initial images equal. ADR 0292 records that
preceding promotion; ADR 0318 records the preceding linked-image execution
seed, ADR 0323 records the preceding code-anchor execution seed, and ADR 0336
records the current execution seed.
A fresh build in a unique output directory passed in 10.492 seconds and
reproduced the six object and executable identities from the promoted-seed
frontier. Disposable staged-copy runs returned 0 for hello in 54.546 seconds,
ls in 52.637 seconds, and cat in 80.043 seconds. Cat used a 62-byte
marker-shaped fixture and confirmed that fixture text could not satisfy the
serial-event boundary. The source and evidence images remained unchanged at
SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.
_Avoid_: every external program, hosted GCC examples, user-mode isolation

**Active assembly ownership**:
The generated contract covering every active assembly input. The current graph
contains 32 assembly sources, all owned by CupidASM. Five enter through the
Toolchain fixed-point startup cohort, five are production image inputs, and 22
are demos or includes. An ownerless source fails unless it has a reviewed
`host_fixture` or `host_oracle` classification with a reason. ADR 0327 records
the complete ownership rule.
_Avoid_: counting only normal image recipes, implicit startup ownership, NASM-owned transforms

**Cupid-built syscall ABI contract**:
The static i386 contract that checks the external-program table before CupidC compiles the tracked user programs. It snapshots and rereads six ABI declarations, checks version 5, 103 fields, 412 table bytes, public scalar types and constants, both VFS record layouts, and all 101 providers, then emits the reviewed fingerprints. On Linux, stage-three and stage-four CupidC compile it as part of the static Toolchain cohort. CupidLD links the matching ELF, and the checked publisher requires identical objects and executables. On Windows, the checked PE execution seed builds a temporary copy from a separate frozen 26-file closure. CupidC compiles the contract and runtime objects, CupidASM supplies startup, and CupidLD links a validated PE that runs directly. Both paths give the contract and Python oracle the same frozen six-file snapshot and require identical JSON. The Windows path never reads or changes the Linux contract publication. ADR 0264 records the semantic transfer, and ADR 0295 records native Windows execution.
_Avoid_: Python-only ABI checker, user-program runtime, kernel syscall implementation

**Hosted i386 ABI profile**:
The deterministic hosted C request used to compile an i386 tool closure. Linux requests search `/toolchain` for quoted and angle includes and the checked i386 Linux declaration set for angle includes only, define `__SIZEOF_POINTER__` as four, and leave `_WIN32` undefined. Only `kernel/lang/as_elf.cc` and `toolchain/tests/cupidasm_kernel_elf_contract.cc` also search `/kernel/lang`; all 33 `HOSTED_I386_LINUX` roots are kept outside that bridge. `HOSTED_I386_WINDOWS` defines `_WIN32=1` for `ctool_host.cc`, all five Cupid Toolchain driver roots, including CupidDis, and the CupidLD publication runtime while keeping the same four-byte pointer fact and declaration set. The definition on `cupiddis_main.cc` is required for byte parity between the native proof and the Linux path that reconstructs Windows behavior. The headerless Windows probe uses the separate `FREESTANDING_I386` profile. The Windows tool runtime selects the shared hosted implementation through `CUPID_RUNTIME_WINDOWS`. The CupidC command represents these roots with `-I` and `--include-angle` in caller order. Repeatable `-include` options represent preprocessing inputs that run in order before the primary source. Tool sources use strict C11. The Linux runtime, Windows runtime wrapper, and Linux behavior probe enable CupidC's GNU variadic built-ins.
_Avoid_: `HOSTED_TOOLCHAIN_64`, vendored libc, host system headers

**Hosted i386 Linux runtime**:
The repository-owned startup and narrow C service layer for static Cupid-built i386 Linux commands. CupidASM supplies process entry and `int 0x80` system-call wrappers. CupidC supplies allocation, unbuffered files, standard streams, fixed-width integer declarations, memory and string functions, `errno`, `getcwd`, formatted diagnostics, and the checked `printf`, `puts`, `snprintf`, `fputc`, and `fputs` surface. Integer formatting covers `int`, `long`, and `long long` decimal and hexadecimal values, including zero-padded widths. String formatting accepts a fixed or argument-supplied precision. A CupidC-built `.cc` runtime contract checks the heap, files, errors, arguments, formatting, memory, and string behavior under Linux or WSL. It is a separate behavior probe and does not enter the 19-source fixed-point plan.
_Avoid_: general libc, complete Windows toolchain runtime, test-only import providers

**Hosted i386 Windows runtime**:
The repository-owned startup and service layer for Cupid-built native Windows tools. CupidASM obtains the command line, aligns the i386 stack, and exposes cdecl bridges for imported stdcall APIs. CupidC parses quoted arguments into one allocation and supplies `VirtualAlloc` heap storage, standard streams, file reads and writes, seeking, `getcwd`, `_fullpath`, and `errno` mapping. CupidLD authors every IAT slot and commits the bounded one MiB tool stack in full. CupidLD alone adds exclusive creation, durable flush, atomic replacement, and candidate deletion. Windows runs help plus useful success and failure cases for every tool, along with direct runtime and compiler contracts. The five promoted PEs form the checked Windows execution seed and own output-bearing production work on Windows. Its 2,118-byte manifest has SHA-256 `751e1d7787a4be08e4e86814bbb7473979fe2eb8a3292baed0241967f772eaef` and binds revision `a17c9465911da41d59b7ada71733d36c39faa5ea` to Linux parent manifest `b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b`. ADR 0268 records the shared runtime, ADR 0269 records the CupidLD publication boundary, ADR 0272 records execution-seed adoption, ADR 0274 records the stack commitment, and ADR 0336 records the current promotion.
The current Windows manifest is 2,118 bytes with SHA-256
`751e1d7787a4be08e4e86814bbb7473979fe2eb8a3292baed0241967f772eaef`.
It binds revision `a17c9465911da41d59b7ada71733d36c39faa5ea` to Linux
manifest `b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b`.
_Avoid_: host C runtime, general Windows SDK, small Windows marker probe

**Hosted Toolchain contract cohort**:
The fifteen published `.cc` Toolchain contract programs and the separate
hosted runtime contract built as static i386 Linux executables by stage-three
and stage-four CupidC. The checked cohort snapshots its exact source and
declaration membership, reproduces that inventory under a private root,
compares seventeen objects and sixteen executables across the converged
compiler pair, runs the stage-four runtime behavior probe, and publishes the
stage-four executables with the five stage-four tools as 21 artifacts plus a
manifest. Its source-current inventory contains 75 inputs, including the exact-decimal
fixture, fingerprint-bound x86 catalogue corpus, `toolchain/x86.cc`, native
Windows runtime and startup, publication bridges, direct runtime contract,
hosted Windows declarations, six external-program ABI declarations, the
artifact-size and Toolchain manifest contract sources, both PE32 reader
headers, the CupidBuild declarations and Windows startup, and an independent
Python oracle. Live inventory discovery catches additions, removals, and restored
edits that changed a private copy. The output must be a dedicated
`cupidc-contracts` directory inside the source tree. An existing destination
must already verify, and arbitrary directories, source trees, files, or
symbolic links remain untouched. A run derives the cohort from its requested
executable and verifies the named artifact, all recorded hashes, and current
inputs first. The audit keeps the 75-file publication inventory distinct from
the publisher's 95 Make prerequisites. `toolchain:all` owns this path. Native
GCC or Clang builds are optional oracles under `native-oracles`, not normal
build inputs.
_Avoid_: checked seed, native contract suite, 19-source tool fixed point

**Cupid Toolchain manifest contract**:
The strict C11 program that verifies and authors the 21-artifact Toolchain
publication. Verification consumes a pinned `CUPMAN2` snapshot. Author mode
consumes a pinned `CUPMAN4` snapshot. It carries the independent artifact,
75-input, 55-bootstrap-input, Linux-seed, build-plan, and generation facts,
plus four exact raw stage-pair inventories. Those inventories contain 17
contract objects, 16 contract executables, 20 bootstrap objects, and five
bootstrap tools. The author compares and hashes both byte streams in every
pair. It derives the 17 object records from the compared bytes and checks the
16 executable digests and sizes against the artifact observations. Schema
`cupid.toolchain-contracts.v3` still stores each publication input and object
comparison as a two-field `sha256` and `size` record. It rejects a missing,
extra, duplicate, nonregular, empty, mismatched, malformed, truncated,
trailing, or inconsistent fact before emitting canonical JSON. Python captures the
no-follow filesystem view, requires agreement with an independent oracle, and
performs all four stage comparisons again only after author acceptance. It
also repeats the live closure and drift checks. Converged stage-four Linux
CupidC, CupidASM, and CupidLD remain the author producers. Linux runs a static
ELF, while Windows runs a validated PE built from the same source with the
checked Windows runtime, startup, and exact imports. Verification remains
host-selected: Windows builds and runs a native `CUPMAN2` PE, while Linux uses
a static ELF.
The checked graph assigns both operations to CupidC, CupidASM, CupidLD, the
semantic-contract class, and Host Python. ADR 0302 records verification, ADR
0304 records authoring, ADR 0307 records paired fixed-point evidence, and ADR
0322 records native Windows author execution.
_Avoid_: Python-free filesystem boundary, manifest-derived author oracle,
general JSON author

**CupidC compiler generation**:
A compiler process compiling unchanged source from its complete implementation. A checked seed builds stage two, stage two builds stage three, and stage three builds stage four. When the seed predates a code-generator change, stages two and three are transition generations. The fixed-point gate compares every stage-three and stage-four compiler object, startup object, and linked image byte for byte.
_Avoid_: checked seed, complete self-hosting

**Static i386 Toolchain fixed point**:
A stage boundary where one generation of CupidC, CupidASM, and CupidLD builds complete Linux images for CupidC, CupidASM, CupidDis, CupidLD, and CupidObj. The checked seed builds stage two, stage two builds stage three, and stage three builds stage four. The gate compares all 19 C objects, the independently assembled Linux startup object, and all five linked images between stages three and four. Both compared stages execute the five tools through real success and failure cases. The same convergence relationship applies to the native Windows runtime, startup, publication objects, tool images, and behavior checks. The Linux driver revalidates its live seed manifest and artifacts at every generation boundary and before publication. The Windows driver checks its PE execution seed and Linux plan seed independently at the same boundaries. Both current seeds bind the 50-input snapshot with SHA-256 `46c5335c80d822dd5085ee22077486ea647e5396482d42454847c87e4222aa67`. The Linux candidate proof passed cleanly, with every stage-three and stage-four image equal and 5/22/21 behavior. The Windows candidate proof passed cleanly with the same convergence and 5/8/9 behavior. Both matrices reject unmatched executable relocations, validate unrelocated local targets while excluding relocated operands, require every static ELF code anchor to begin at a decoded instruction, and validate source-resolved raw control edges. ADR 0279 records the convergence rule, and ADR 0336 records the current promotion. The later 55-source Linux proof also passes after the coordinator's executable-format probe was bounded to four bytes. It covers all three build generations, exact fixed-point comparisons, behavior cases, and native Windows evidence without loading complete tool images just to choose a runner.
The current fixed points bind snapshot
`46c5335c80d822dd5085ee22077486ea647e5396482d42454847c87e4222aa67`.
Their behavior inventories are 21/5/22 on Linux and 9/5/8 on Windows for
failure, help, and success groups. All five initial comparisons are true after
promotion.
_Avoid_: fresh-checkout bootstrap, Windows bootstrap-plan seed, Python-free coordination

**Host adapter link tracer**:
A static i386 executable used to check the boundary between CupidC objects, CupidASM startup code, and CupidLD. Its `_start` calls `ctool_host_adapter_init`, checks the resulting `{data, size}` fields, and exits through Linux `int 0x80`. Link-only providers resolve the adapter's file and allocation imports but do not implement a usable runtime.
_Avoid_: hosted Cupid tool, C runtime, bootstrap stage

**Immediate pointer qualification conversion**:
A representation-preserving C conversion that adds qualifiers to the object directly referenced by a pointer. CupidC accepts `char **` as `char *const *` because the intermediate pointer becomes read-only through the destination. It still rejects qualifier removal and unsafe deeper changes such as `char **` to `const char **`.
_Avoid_: pointer cast, ignoring nested qualifiers

**Typed null conversion**:
A representation-preserving conversion from a proved integer zero constant, directly or through an explicit cast to unqualified `void *`, to the destination object-pointer type. The frontend marks that proof on the conversion node. Linear IR rejects missing, misplaced, and runtime-pointer provenance before i386 emission keeps the same four-byte zero representation.
_Avoid_: general void pointer conversion, pointer reinterpretation

**Addressable unspecified-bound array**:
An incomplete external array whose element type is complete and has a nonzero target size. CupidC may take the linked object's address and apply ordinary array-to-pointer decay, but it does not treat the array itself as a complete value or storage object.
_Avoid_: fixed array, flexible array member, variable-length array

**Represented GNU assembly statement**:
A GNU-mode CupidC statement whose immutable frontend record owns a decoded template and a packed operand slice. Extended statements accept one to four typed integer register outputs, single-digit matching inputs, and the CSPRNG's RDTSC, CPUID, RDRAND, SETC, and tied NOP forms. The port-I/O subset adds width-preserving `a` and `d` inputs, 8-bit, 16-bit, or 32-bit `=a` outputs, and modifiable `+c`, `+S`, and `+D` operands. The checked seed retains the exact GNU `Nd` port constraint and selects its valid DX alternative for the active `inb` and `outb` templates. It accepts one `memory` clobber where the source requests it. One `=r` output may instead be a modifiable four-byte object or `void` pointer for the exact `mov %%gs:0, %0` per-CPU load. A separate output-only subset snapshots a 32-bit general register, ESP, EBP, `4(%ebp)`, or EFLAGS into one four-byte object; the EFLAGS form may finish with `cli`. Exact volatile `FNSTSW`, `FNSTCW`, and `STMXCSR` forms write one correctly sized integer lvalue through `=m`. The checked seed accepts an independent four-byte integer or data-pointer `r` input and an independent four-byte integer `c` input for exact CR0, CR2, CR3, and CR4 moves and RDMSR. The exact volatile `fxsave (%0)` form narrows its `r` input to a four-byte object or `void` pointer and retains one `memory` clobber. CupidC also captures the next instruction's address through the exact volatile `call 1f\n1: popl %0` form and one four-byte integer `=r` output. That form emits a zero-displacement call and immediate pop without a relocation. Function-body basic statements and extended statements with an empty output list own no operands and are implicitly volatile. An exact empty volatile extended template with one `memory` clobber and no operands remains an IR ordering point and emits no target bytes. Linear IR evaluates output addresses before input values, once each and in source order. The emitter preserves EBX and restores ESI or EDI after repeated string I/O. The checked seed also accepts the unchanged CPUID `a` input sharing the compatible `=a` output. Normal-build ownership remains a separate boundary.
The checked seed also accepts the exact volatile `ldmxcsr %0` form with one addressable, non-atomic 32-bit integer `m` input. Linear IR retains the operand address, and the shared x86 model emits `0F AE 10` through EAX. The same checked compiler accepts exact `fldcw %0` and one addressable, non-atomic 16-bit integer `m` input. A no-output GNU assembly statement is implicitly volatile. The emitter produces `D9 /5` through EAX. ADR 0258 records seed carriage.
The checked seed accepts the exact volatile MOVSS float-memory round trip used by `fpu_boot_smoke()`, plus its one-way load and store forms. Each form requires the `xmm0` clobber. Linear IR evaluates each object address once, and the shared x86 model emits `F3 0F 10 00` or `F3 0F 11 00` through EAX. It also accepts the exact volatile `fldl`, `fsin`, and `fstpl` block in `stress_sin()`. The normal build compiles `kernel/cpu/fpu.cc` through the frozen checked-seed wrapper. A typed production-object policy rejects helper calls and floating work before the CR4 write, requires `FNINIT` before a 32-bit memory `LDMXCSR`, and rejects any other floating work in `fpu_init_cpu()`. The four-vCPU runtime gate requires `[fpu] SSE2 enabled`, `[fpu] boot smoke ok`, and `FPU boot smoke passed`.
_Avoid_: general GNU assembly support, host-assembler escape

**Named GNU assembly operand**:
An optional C-identifier label in brackets before an extended-assembly output or input constraint. Checked-seed CupidC collects the complete output-then-input namespace before parsing ordinary operands, requires every label to be unique, and permits a declared label to remain unused. It rewrites each unescaped `%[identifier]` template reference to the corresponding numeric operand index. A `%%` pair remains literal and does not begin a named reference. Operand names stay parser-private: the frontend, Linear IR, and emitter continue to receive the existing canonical numeric template and packed operand slice. The normal numeric path therefore applies the same lvalue, addressability, atomic, bit-field, width, type, constraint, fixed-register, and template checks after normalization. Malformed, duplicate, and unknown names fail transactionally. Named matching constraints remain outside this term.
_Avoid_: public operand-name metadata, rewriting active source to numeric operands, treating `%%[name]` as substitution, general template substitution

**Represented GNU x87 double-power memory assembly**:
The exact volatile statement in `libm_pow_impl()` that consumes one modifiable `double` `=m` output, four addressable `double` `m` inputs, and one `memory` clobber. Checked-seed CupidC resolves its named operands to numeric indexes, evaluates all five addresses once in source order, and emits the complete `FYL2X`, `FRNDINT`, `F2XM1`, and `FSCALE` sequence with balanced x87 depth. The active source uses the corrected `DC E9` forward subtraction for `x - round(x)`. The checked seed retains the legacy `DC E1` reverse-subtraction form for source compatibility. ADRs 0207 through 0209 record the capability, seed carriage, and source correction.
_Avoid_: treating the mixed-width `libm_powf_impl()` form as all-double operands, arbitrary x87 stack programs, host-assembler escape

**Represented GNU x87 mixed-width float-power memory assembly**:
The exact volatile statement in `libm_powf_impl()` that consumes one modifiable `float` `=m` output, two addressable `float` `m` inputs, two addressable `double` `m` inputs, and one `memory` clobber. Checked-seed CupidC resolves its named operands to numeric indexes and evaluates all five addresses once in source order. The emitter shares the power sequence while selecting 32-bit loads and store for the float values and 64-bit loads for the constants.
_Avoid_: treating every operand as `double`, arbitrary x87 stack programs, host-assembler escape

**Represented GNU SSE2 square-root register assembly**:
The exact volatile `sqrtsd %1, %0` statement in `libm_sqrt_impl()` with one modifiable, non-atomic `double` `=x` output, one non-atomic `double` `x` input, and no clobbers. Linear IR evaluates the output address before the input value. The i386 emitter loads the value into XMM0, applies `SQRTSD XMM0, XMM0`, and stores through the saved output address. The focused function has 65 text bytes and no relocations.
_Avoid_: general `x` constraints, arbitrary XMM register allocation, host-assembler escape

**Represented GNU x87 atan2 memory assembly**:
The exact volatile statement in `libm_atan2_impl()` with one modifiable, non-atomic `double` `=m` output, two addressable, non-atomic `double` `m` inputs in `y`, `x` order, and one `memory` clobber. Named operands normalize before Linear IR freezes the statement. Linear IR evaluates the output, `y`, and `x` addresses once in source order. The i386 emitter uses Cupid's shared x86 model for both loads, `FPATAN`, and the final store. The focused function has 53 text bytes and no relocations.
_Avoid_: general x87 programs, reordered address evaluation, host-assembler escape

**Represented GNU x87 exponent memory assembly**:
The exact volatile statement in `libm_exp_impl()` with one modifiable, non-atomic `double` `=m` output, two addressable, non-atomic `double` `m` inputs in `x`, `log2e` order, and one `memory` clobber. Linear IR evaluates all three addresses once in source order. The i386 emitter runs the complete `exp2(x * log2(e))` pipeline through Cupid's shared x86 model. The focused function has 71 text bytes, no relocations, maximum x87 depth three, and balanced depth on return.

Compiler head and the checked seed distinguish both aligned GNU spellings of the exponent range subtraction. Legacy `fsub %st, %st(1)` retains GNU's `DC E1` reverse-subtract meaning. Corrected `fsubr %st, %st(1)` emits canonical `FSUB ST(1), ST(0)` as `DC E9`, which computes `x - round(x)`. The checked seed and source head have 604 forms, 249 canonical mnemonics, and fingerprint `55A8970F`. A fingerprint-bound contract reaches all 1,202 encodable legal-mode cases through the real encoder, both real decoders, and exact-form replay. It also locks 12 aliases, four invalid rows, two illegal-mode rejections, all row flags, and 2,641 proper prefixes under witness digest `8C570035`. The catalogue includes signed x87 `FILD` and `FISTP` memory operands at 16, 32, and 64 bits, plus canonical `SETP` and `SETNP` byte predicates. The four SHRD rows remain canonical at 16 and 32 bits with immediate or fixed CL counts. Active `libm.cc` uses the corrected subtraction spelling at all seven range-reduction sites. ADRs 0207 through 0209 record the exponent diagnosis, seed carriage, and runtime-tested source correction. ADR 0226 records SHRD, ADR 0228 records its seed carriage, ADR 0252 records the x87 integer forms, ADR 0258 records the preceding promoted seed, ADR 0259 records the parity predicates, ADRs 0265, 0280, and 0292 record preceding carriage, ADR 0298 records the every-form proof, and ADR 0312 records the preceding local-target seed, and ADR 0318 records the preceding linked-image seed, and ADR 0323 records the preceding code-anchor seed, and ADR 0336 records the current seed.
_Avoid_: general x87 programs, host-assembler escape, changing the active libm algorithm

**Represented GNU fabs file-scope assembly**:
The exact aligned `fabs` mask block and following `fabs` and `fabsf` wrappers in `kernel/cpu/libm.cc`. The mask effect owns the first 32 bytes of `.rodata` at alignment 16 and defines local `STT_NOTYPE` labels at offsets 0 and 16. The wrappers retain their source prototypes, emit through Cupid's shared x86 model, and carry one `R_386_32` relocation each to the matching mask.
_Avoid_: passing file-scope data to GAS, moving the masks behind ordinary read-only objects, converting assembly labels into C declarations

**Represented GNU libm rounding file-scope assembly**:
The exact `floor`, `floorf`, `ceil`, `ceilf`, `round`, `roundf`, `trunc`, and `truncf` definitions in `kernel/cpu/libm.cc`. The checked seed saves the incoming x87 control word, clears its rounding field with `0xf3ff`, installs `RC=01`, `RC=10`, `RC=00`, or `RC=11`, runs `FRNDINT`, and restores the original word. The nearest-even pair omits the OR instruction. Each function uses the source scalar width, returns through XMM0, reaches x87 depth one, balances ESP and x87 depth, and emits through Cupid's shared x86 model. The eight wrappers add 384 text bytes and no relocations.
_Avoid_: general GAS input, changing the active rounding mode, leaving the patched x87 control word active

**Represented GNU libm remainder file-scope assembly**:
The exact `fmod` and `fmodf` definitions in `kernel/cpu/libm.cc`. The checked seed loads `y` below `x`, repeats `FPREM` while status-word C2 remains set, discards ST(1), and returns the remaining value through XMM0 at the source width. The loop uses `FNSTSW AX`, `TEST AX, 0x0400`, and a checked rel8 `JNE` with displacement `-10`, all through Cupid's shared x86 model. Each 35-byte wrapper reaches x87 depth two, balances ESP and x87 depth, and has no relocation.
_Avoid_: one-shot `FPREM`, raw opcode append, near-branch substitution, host-assembler escape

**Represented GNU libm exponent/logarithm file-scope assembly**:
The exact aligned `libm_log2e_const` and `libm_ln2_const` block followed by `exp2`, `exp2f`, `exp`, `expf`, `log2`, `log2f`, `log`, and `logf` in `kernel/cpu/libm.cc`. The block owns 16 bytes of `.rodata` at alignment eight and defines two local `STT_NOTYPE` labels at offsets 0 and 8. The exponent functions share the source `FRNDINT`, `F2XM1`, and `FSCALE` pipeline. The logarithm functions use `FYL2X` with either `FLD1` or the stored `ln(2)` value. The four natural forms carry checked `R_386_32` relocations to the matching constant. The family emits 264 text bytes, reaches at most x87 depth three, and balances ESP and x87 depth.
_Avoid_: host floating-point constants, general GAS input, duplicating the exponent sequence, changing the active libm algorithm

**Represented GNU libm cdecl bridge assembly**:
The exact final 18 file-scope wrappers in `kernel/cpu/libm.cc`: the binary `pow`, `hypot`, and `nextafter` pairs plus the unary `asin`, `acos`, `sinh`, `cosh`, `tanh`, and `cbrt` pairs. Each wrapper copies its original cdecl argument words, calls the matching external `libm_*_impl` function through one `R_386_PC32` relocation with addend `-4`, reclaims the copied arguments, and moves the ST(0) result into XMM0 at the source width. The checked seed validates the wrapper and callee prototypes and emits all four one- or two-argument float or double stack shapes through Cupid's shared x86 model. The family owns 558 text bytes and 18 relocations. It completes deterministic checked-seed object emission for `kernel/cpu/libm.cc`.
_Avoid_: tail jump that changes the callee stack layout, raw opcode append, missing callee type validation, host-assembler escape

**Represented GNU dglibc jump assembly**:
The exact combined `dg_setjmp` and `dg_longjmp` file-scope effect in `kernel/doom/dglibc.cc`. Active dglibc uses the corrected form: setjmp is `returns_twice`, longjmp is `noreturn`, and the 31-byte setjmp body saves the caller's post-return `ESP + 4`. The checked seed also recognizes the historical 27-byte compatibility form. Both forms use Cupid's shared x86 model, have no relocation, preserve the rule that a zero jump value becomes one, and jump through the saved return address. ADR 0213 records seed promotion, and ADR 0214 records active adoption.

**Returns-twice call boundary**:
A direct CupidC call to a function marked with GNU `returns_twice` or `__returns_twice__`. CupidC merges the attribute across compatible declarations. A marked function must be called directly; conversion to a function pointer is rejected. Supported calls use four-byte cdecl arguments and may return void or any nonaggregate type. The emitter uses the validated Linear IR stack depth to copy every live four-byte operand below the arguments into call-owned frame slots. It restores those words after caller cleanup and before publishing the call result. A live-prefix call is rejected if any returns-twice continuation can reach it again, while a call with no live prefix may repeat. Aggregate, wide-integer, and wider-than-four-byte floating arguments and aggregate results fail with a specific unsupported diagnostic. The checked seed carries this boundary, and active dglibc uses it for the Doom shell-session envelope.
_Avoid_: indirect marked call, marked-function pointer conversion, outgoing-area argument, aggregate result, live-prefix checkpoint reentry, treating the hosted oracle as guest evidence, IWAD runtime claim

**Doom shell session**:
One call to `doom_main` inside the long-lived Cupid shell process. It owns one nonlocal-exit envelope, one exit-callback list, the recursive-error guard, config-string allocations, and the initial-value snapshot used to reset registered defaults. Landing after `I_Quit` or `I_Error` releases that session before control returns to the shell. ADR 0214 records the boundary.
_Avoid_: operating-system process, retaining callbacks between launches, resetting an active callback walk

**HomeFS replacement transaction**:
The ordered update used for Doom config, save slots, and the `HOMEFS.SYS` container. A caller writes and closes a temporary file, native same-mount rename publishes it, and FAT16 writes and syncs a new cluster chain before it publishes the directory entry. A failed directory sync restores the old sector when possible. Replacement frees the old chain only after the new entry is durable; deletion makes the removed entry durable before it frees that chain. Cache writeback errors travel back to the caller, and a failed read fills scratch storage before it can change a cache entry's identity. A failed cleanup may leak clusters, but it must not remove the newly published file. While HomeFS is mounted, a private FAT reservation blocks raw writes, deletion, and write-capable `/disk/HOMEFS.SYS` aliases. ADR 0211 records the boundary.
_Avoid_: copy-and-unlink rename, deleting the prior file first, mutating an evicted cache entry before a successful read, two live owners, claiming power-cut proof

**HomeFS mutation batch**:
A nestable group of related HomeFS mutations. Operations update the live tree and mark it dirty, but only the outermost `homefs_batch_end` publishes `HOMEFS.SYS` and reports the durable result. An unmatched end fails, depth overflow is rejected, and unmount remains busy while a batch is open. The guest dglibc diagnostic uses one batch so its many filesystem probes produce one final container replacement instead of rewriting the whole tree after each probe.
_Avoid_: transaction rollback for the in-memory tree, hiding the outer publication error, unmounting an open batch

**Represented GNU x87 round-down memory assembly**:
The exact volatile statement in `str_floor()` that loads one `double`, saves the x87 control word below ESP, selects round toward negative infinity, executes `frndint`, restores the saved word, and stores the result. It requires one modifiable `double` `=m` output, one addressable `double` `m` input, and the exact `ax` plus `memory` clobber set. Linear IR evaluates the output address before the input address. The emitter reuses the consumed input-address slot for the two control-word values without touching the pending output address. The checked seed emits the complete helper and the later double-to-`uint64_t` casts, so unchanged `kernel/core/string.cc` compiles completely and deterministically through its production recipe.
_Avoid_: general AX clobber, arbitrary x87 control-word template, frame scratch that changes the active offsets

**Represented GNU descriptor-table assembly**:
The four exact volatile assembly forms in `kernel/smp/percpu.cc` that load a packed six-byte GDTR, reload DS, ES, SS, and CS, and write a represented 16-bit selector to GS. The LGDT forms require one addressable, non-atomic, complete six-byte `m` input and the exact `ax` plus `memory` clobbers. The standalone code-segment reload requires its `memory` clobber, while the GS form requires one 16-bit `r` input and no clobbers. Linear IR retains the GDTR address or selector value. The emitter uses shared x86 encodings and replaces absolute local-label control transfers with a relative `CALL`, `JMP`, and `RETF` trampoline that leaves no relocation. The checked seed carries all four forms, and the normal build uses that path for the per-CPU root.
_Avoid_: general segment assembly, scalar GDTR substitute, absolute compiler-local label relocation

**Represented GNU flags-restore assembly**:
The exact volatile `pushl %0`, `popfl` statement used twice by `simd_cpu_has_cpuid()`. It requires one non-atomic 32-bit integer `r` input, no outputs, and exactly one `cc` clobber. Linear IR retains the input value and validates the frozen clobber metadata. The emitter consumes the value through EAX, pushes it back, and emits POPF through the shared x86 model without a frame temporary or relocation. The checked seed carries this form, and the normal build uses it in `kernel/cpu/simd.cc`.
_Avoid_: general CC clobber, arbitrary EFLAGS template, dropping a real flags effect

**Represented GNU packed SSE2 assembly**:
The six exact volatile statement shapes used by `simd_memcpy()`, `simd_memset32()`, `simd_blend_row()`, and `simd_add_rows()`. Checked-seed CupidC requires zero outputs, the source's ordered pointer or 32-bit integer `r` inputs, and each template's exact memory plus XMM0 through XMM7 clobber set. Linear IR evaluates the inputs once in source order. The i386 emitter consumes them through EAX, EDX, and the existing evaluation stack, then emits every packed operation through Cupid's shared x86 model. Two complete checked-seed builds of unchanged `kernel/cpu/simd.cc` produce the same validated 8,768-byte object. The normal recipe uses that checked path.
_Avoid_: general XMM allocation, arbitrary SSE template, host-assembler escape, changing the active SIMD source

**Represented GNU kernel-entry BSS-clear assembly**:
The exact operand-free volatile statement that is the direct first child of the external, prototyped `void _start(void)` body in `.text.start`. It installs the fixed kernel stack and clears the linked BSS range. The statement requires the exact EAX, ECX, EDI, and memory clobbers plus visible external object declarations for `_bss_start` and `_kernel_end`. The function cannot have a compiler-managed frame. Its nonzero stack top is written as one through eight hexadecimal digits and must be aligned to 4 KiB. The active source installs `0x01100000`. The emitter loads both symbols through `R_386_32` relocations, derives the doubleword count, clears EAX, and emits CLD plus REP STOSD through the shared x86 model. The following `kmain()` call uses stack-base residue zero. If it returns, `_start` enters an interrupt-disabled halt loop. Checked-seed CupidC emits unchanged `kernel/core/kernel.cc` completely as a deterministic 25,920-byte object. The normal production recipe uses that checked path.
_Avoid_: fixed numeric BSS bounds, missing clobbers, label-wrapped, nested, or reordered entry reset, compiler frame after ESP replacement, host-assembler escape

**Represented GNU fixed-register overlap**:
A GNU assembly input whose fixed register is also assigned to one compatible write-only output in the same statement. The input keeps its source constraint, while `matching_output` names the shared output slot. Frontend and Linear IR require the same fixed register, equal widths, and represented integer types. They reject a second input tied to that output, an independently colliding fixed input, or frozen same-width pointer, floating, and aggregate substitutions. The emitter repeats the integer and width checks before it loads the input into the selected output register. The checked seed uses this rule for the unchanged CPUID `=a` output and `a` leaf input in `kernel/cpu/simd.cc`; numeric matching constraints keep their existing representation.
_Avoid_: independent duplicate register, rewriting valid source to a numeric tie, read/write or early-clobber output

**File-scope GNU assembly effect**:
A GNU-mode CupidC translation-unit effect that owns one immutable basic assembly template outside every function. The frontend keeps these effects in a table separate from statement assembly, and Linear IR preserves their source order without pretending that they are function-body instructions. The checked seed emits all file-scope effects in `kernel/cpu/libm.cc`: the opening twelve x87/SSE wrappers, `fabs` data and wrappers, eight rounding functions, two remainder functions, the exponent/logarithm constant block and eight wrappers, and the final 18 cdecl bridge wrappers. It also emits the exact combined dglibc jump effect. Represented functions are prologue-free `STT_FUNC` symbols encoded through Cupid's shared x86 model. Arbitrary GAS text, operands, labels, directives, unrecognized data definitions, and a host-assembler escape remain outside this term.
_Avoid_: GNU assembly statement, general GAS input, hidden host assembler

**Represented GNU entity attribute**:
A GNU-mode `weak`, `section`, `unused`, `used`, `noinline`, `target("general-regs-only")`, or `naked` fact attached to the canonical file-scope object or function after compatible redeclarations merge. `weak` selects ELF weak binding. `section` owns one decoded nonreserved ELF section name and directs compatible object or function definitions there. `unused` records that a declaration may lack an ordinary use. `used` records that a definition must remain in object output. `noinline` preserves a function request for later inlining policy. `target("general-regs-only")` rejects compiler-generated floating work in that function while leaving explicit source assembly under its own contract. A represented naked function has type `void (void)` and owns exactly one complete IPI assembly statement: `pushal`, a canonical direct C-function call, `popal`, and `iret`, or the `cli`, `hlt`, and backward-jump panic loop. Its object path emits no C prologue, epilogue, local reservation, or synthetic return. Each IR function retains its canonical code generation mask, and emission rejects a mismatch. Every represented definition currently reaches ELF32 output and CupidC has no inliner, so `used` and `noinline` affect metadata but not bytes. Invalid placement, conflicting section names, malformed arguments, forged frozen metadata, and GNU-disabled use fail transactionally. The checked seed carries all seven facts.
_Avoid_: skipped attribute text, opaque target strings, every GNU attribute, hardcoded active section names, arbitrary naked C bodies

**Represented GNU integer atomic**:
A GNU-mode CupidC load, store, exchange, fetch-add, or fetch-or on a complete one-, two-, or four-byte integer object. Its public AST and Linear IR keep the object width, evaluated pointer type, and constant memory order. The i386 emitter uses ordinary loads and release stores, memory `XCHG` for exchanges and sequentially consistent stores, `LOCK XADD` for fetch-add, and a `LOCK CMPXCHG` retry loop for fetch-or. The retry loop returns the old value, retains competing updates, and preserves EBX. The checked seed carries all five operations. It compiles the active EHCI fetch-or path as well as the SMP byte flags and 32-bit counters. The normal build uses the seeded path for `acpi.cc` and `mp_tables.cc`; the renamed graph passes the four-vCPU gate with all four discovered processors online.
_Avoid_: volatile substitute, general atomic library, runtime memory order

**Aligned call site**:
An i386 call instruction emitted with ESP on a sixteen-byte boundary before the CPU pushes the return address. Hosted CupidC derives the required padding from the fixed frame, live Linear IR stack depth, and outgoing ABI storage.
_Avoid_: sixteen-byte function frame, aligned callee entry

**Variadic call site**:
A call through a prototyped function type whose final parameter is an ellipsis. Linear IR keeps the named parameter count in the function type. Each call instruction owns its actual argument count and a source-ordered slice of the actual post-conversion types. Hosted i386 emission transports represented four-byte integer and pointer values, signed or unsigned eight-byte integers, existing `double` or `long double` values, and source `float` values after default promotion to `double`. An eight-byte argument occupies adjacent low and high four-byte words in the sixteen-byte-aligned outgoing area; a `long double` occupies three words.
_Avoid_: unprototyped call, variadic macro

**Unprototyped call site**:
A call through a function type that does not declare parameter types. The frontend applies default argument promotions to every argument. Linear IR keeps the actual count and post-conversion type slice on the call instruction. Hosted i386 emission transports represented four-byte integers and pointers, signed or unsigned eight-byte integers, existing `double` or `long double` values, and source `float` values after promotion to `double`. A `long double` uses three adjacent cdecl words in both direct and indirect calls.
_Avoid_: variadic call, call with zero parameters

**Variadic cursor**:
The target `char *` value used by hosted CupidC to traverse unnamed i386 cdecl arguments. `va_start` points it just past the final named argument. A supported non-atomic four-byte integer or pointer `va_arg` advances it by four. A signed or unsigned eight-byte integer, represented wide enum, or `double` read advances it by eight and returns an instruction-owned snapshot. A `long double` read copies twelve bytes into its snapshot and advances by twelve, so the next four-byte value stays aligned. `va_arg(float)` is invalid because an unnamed `float` arrives as `double`. `va_copy` copies the cursor, and `va_end` consumes its evaluated address without changing stored state.
_Avoid_: host `va_list`, argument array

**C mode**:
The CupidC language mode for freestanding C source.
_Avoid_: Cupid mode

**Doom compatibility profile**:
The explicit compiler profile for source requirements audited in the vendored Doom cohort. It adds old-style implicit function declarations and marked i386 function/data pointer conversions to the exact Doom preprocessing and GNU-extension profile. It does not change ordinary C or plain GNU mode. Production uses the profile for three compatibility roots and the matching Doom-tree profile for the other 80 roots.
_Avoid_: GNU mode, ordinary C mode

**Cupid mode**:
The CupidC language mode for Cupid C source and its native extensions. Its
shared declaration frontend treats `U0` as `void`; `U8`, `U16`, `U32`, and
`U64` as unsigned target integers; `I8`, `I16`, `I32`, and `I64` as signed
target integers; `Bool` and `bool` as signed `int`; and `float4` or `double2`
as 16-byte vectors. The strict C11 profile leaves those spellings available
as ordinary identifiers and typedef names. This distinction lets unchanged
`kernel/cpu/simd_intrin.h` publish all 29 bindings under its proper profile.
The
private in-kernel JIT and AOT compiler keeps integer expressions in EAX and
scalar floating expressions in XMM registers. Runtime unary plus preserves an
arithmetic scalar. Runtime unary minus uses integer `NEG` for `char` and
`int`, or toggles only the IEEE-754 sign bit for `float` and `double`.
All six scalar floating comparisons return a normalized `int`. Matching
widths compare directly, while a mixed `float` and `double` pair compares as
`double`. Explicit parity handling keeps every unordered relation false
except `!=`. Scalar floating truth is also normalized in EAX before `!`,
`if`, `?:`, `while`, `for`, or `do` tests it. Positive and negative zero are
false. Every nonzero value, including infinity and NaN, is true. Void
expressions, structures by value, and SIMD vectors are not scalar truth
operands. Prefix and postfix `++` and `--` add or subtract an exact one from
scalar `float` and `double` lvalues. Direct variables, pointer dereferences,
fixed array elements, and scalar record fields use the same value rule. A
derived designator is evaluated once, and a postfix result preserves the old
floating payload while the updated value is stored.
Direct calls and both method-call forms use one mixed-width cdecl layout.
Four-byte scalar and pointer values may appear beside eight-byte `double`
values in any order. The implicit method `self` is the first four-byte slot.
Argument expressions keep left-to-right evaluation, while their complete
stack words are permuted into source order before the call. Callees use the
same widths for parameter offsets, and caller cleanup uses their exact sum.
Non-arithmetic operands receive a specific diagnostic, and a rejected REPL
expression does not poison the next compilation. The frontier accepts that
diagnostic only once, inside the completed `feature13_double.cc` command
slice. A stale, repeated, or out-of-context compiler error remains fatal. A
host oracle compiles the active emitter helpers and interprets their exact
bytes against binary32 and binary64 payloads. The kernel bridge publishes the
declared result type of all 557 bindings: 326 return a value and 231 return
`void`. The value group has 208 promoted integer, 41 unsigned-word, 25
`float`, 25 `double`, 19 character-pointer, and eight other pointer results.
Forty-six bindings for graphics effects, bitmap fonts, transforms, GUI
modules, and themes complete private AOT compilation for all 107 runnable
top-level programs. The fixed frontier emits and runs the graphics test in
both AOT and JIT forms, then runs nested-owner exit and remote-kill fixtures.
The exit leaves a generation-bound delayed request; a replacement owner must
survive that stale request, die to its own foreign helper, and yield the same
PID to another AOT graphics process. It
runs all 260 graphics-test frames through private JIT. Serial markers prove setup,
frame 0, frame 240, cleanup, and JIT return. The gate treats any unresolved
native symbol as an immediate failure. GodSong publishes its settings line,
then the popup publishes a second marker after it owns the shared writer and
raw keyboard queue. The harness waits for both markers and uses no timing
settle before sending dialog keys. Each confirm consumes one terminal key,
leaving later keys for later dialogs. Disposable theme, BMP, font, and AOT
artifacts stay in RamFS rather than publishing HomeFS. The gate requires each
asset, an exact custom-font pixel, an exact isolated blurred-surface pixel,
unchanged screen state, and center and off-center transformed-image pixels.
An off-origin point checks rotation and nonuniform scale, and popping the
transform must restore identity. The affine inverse retains the
full 32.32 determinant in signed 64-bit form while deriving each coefficient.
Its translation products stay wide until their range check. This prevents
identity transforms from reaching a zero divisor, retains representable
sub-word determinants and large scales, and rejects unrepresentable results.
The framebuffer, active gfx2d target, clip, blend mode, transforms, and
resource registries are shared across processes. Fullscreen, desktop,
retained-paint, and legacy-frame paths serialize access with one owner-token
handoff. A pending fullscreen request waits for earlier desktop work, while
process reaping releases abandoned ownership before a PID is reused. Desktop
keyboard pops and mouse-driven window mutations borrow the same writer while
a raw modal owns input. Raw gfx2d calls and borrowed resource pointers require
an outer render scope.
ADR 0189 records unary signs, ADR 0192
records scalar comparisons, ADR 0193 records scalar truth and binding-result
metadata, and ADR 0221 records the unsigned result split. ADR 0194 records
direct floating variable updates, ADR 0273 records derived floating lvalue
updates, ADR 0198 records mixed-width cdecl calls, and ADR 0233 records the
complete embedded-program binding frontier. ADR 0261 records the cross-process
render ownership and process-exit cleanup model.
_Avoid_: C mode, HolyC mode

**Private CupidC floating lvalue**:
A typed `float` or `double` object reached through a fixed array, pointer, or
record field. Global, automatic, block-static, and persistent REPL arrays keep
their scalar width through one, two, or three dimensions. Each subscript uses
the remaining row stride, and `sizeof(array[index])` reports that row without
evaluating the index. Depth-one floating pointers keep their pointee type
through declarations, returns, address expressions, parameter decay,
dereference, subscripting, assignment, arithmetic compound assignment, and
floating increment or decrement. A pointer, index, or member designator is
evaluated once for an update, and postfix keeps the old raw payload. Direct
pointer updates use the pointee width. The runtime proves both the JIT path and
an external `ccc`/`exec` path, including the loaded process exit. Structure and class objects, their
arrays, and their pointers retain scalar floating fields and one-dimensional
fixed floating field arrays. Every bound and allocation is checked before
storage is reserved. Floating pointer depth greater than one,
pointer-to-array types, and assignment through a pointer-valued floating
record field remain outside this boundary. ADR 0210 records the first array
slice; ADR 0215 records the broader lvalue model; ADR 0273 records derived
floating updates.
_Avoid_: complete multi-level floating pointer support

**Hosted CupidC decimal literal**:
A source-head decimal `float` or `double` token converted with a private
1536-bit unsigned integer workspace. The hosted frontend forms the exact
decimal ratio and rounds once to binary32 or binary64 with ties to even. An
`f` or `F` suffix selects binary32 before rounding. Decimal subnormals, finite
limits, overflow to infinity, underflow to zero, and unary-signed zero retain
their exact target bits through the frontend initializer forest, Linear IR
validation, and ELF32 constant data. Tokens may contain 95 characters,
including a suffix; the next character produces a focused diagnostic and the
same job recovers. The older bounded x87 decimal converter and the
hexadecimal-floating rejection remain separate boundaries. ADR 0293 records
the model.
_Avoid_: host `strtod`, binary64-first conversion of an `f` literal, treating
the x87 literal limit as a binary32 or binary64 limit

**Private CupidC decimal literal**:
A decimal `float` or `double` token converted with a 1536-bit integer
workspace. The converter forms an exact decimal ratio and rounds it once to
binary32 or binary64 using nearest-even. An `f` or `F` suffix selects binary32
before rounding. Decimal subnormals, finite limits, overflow to infinity,
underflow to zero, and a following unary sign keep their IEEE payloads. A
numeric token may contain at most 95 characters, including its suffix. The
lexer consumes the whole rejected token, leaves the next delimiter available,
and reports a focused length or exponent error. Parser recovery keeps that
first public diagnostic. Hexadecimal floating and `long double` literals are
outside this boundary. ADR 0217 records the model.
_Avoid_: host `strtod`, binary64-first conversion of an `f` literal

**Private CupidC SIMD value**:
A `float4` or `double2` value carried through direct packed arithmetic or a
fixed array with as many as three dimensions. Matching vectors support `+`,
`-`, `*`, and `/`. Global, automatic, block-static, and persistent REPL arrays
use 16-byte leaves, checked row strides, unaligned-safe loads and stores, plain
assignment, the four arithmetic compound assignments, whole-vector prefix and
postfix updates, lane reads, and typed row or vector `sizeof`. Direct vector
objects also work in automatic, global, block-static, and persistent REPL
storage. Prefix returns the stored vector. Postfix returns the exact old
128-bit payload. Declared rank is tracked separately from byte size, so unit
inner extents keep their row identity. Every evaluated index runs once. An
index inside unevaluated `sizeof` does not run. Const qualification is retained
through typedef aliases. Const direct vectors and fixed-array leaves remain
readable. Plain and arithmetic compound assignment, along with prefix and
postfix `++` and `--`, are rejected before any store.
Direct arithmetic keeps the written left value in the machine destination.
The SSE minimum and maximum intrinsics retain the second-operand rules for NaN
and signed zero. A both-NaN ADD or MUL result may carry either input payload,
depending on the processor or emulator. SIMD pointers, record fields,
allocation with `new`, array parameters, row values, lane updates, computed
vector updates remain outside this boundary. A fixed-prototype direct function
or method may pass `float4` and `double2` by value in complete 16-byte cdecl
slots and return either type in XMM0. Slots are packed at four-byte granularity,
so parameter loads and stores use `MOVUPS` and do not promise 16-byte call-site
alignment. Arguments still evaluate from left to right, parameters receive
independent copies, and the caller reclaims every 4-, 8-, or 16-byte slot.
Fixed SIMD parameters may precede a scalar variadic tail. SIMD values in the
tail and unprototyped SIMD calls receive focused diagnostics. A named
block-local function pointer with an explicit prototype carries a fixed
SIMD argument or result through the ordinary call path. A typedef-backed
ordinary function or Cupid class method parameter retains that metadata. A
declaration-initialized automatic object does too. A direct file object retains
the signature across null or compatible function initialization, checked
assignment, indirect call, and clearing. A later initialization target receives
an absolute data patch. A structure or class field declared directly with the
typedef keeps the signature through checked stores, named copies, null checks,
and clearing. Direct members, nested records, and indexed record arrays share
that path. Raw callback fields retain the same metadata, and direct postfix
calls through either field form use the typed cdecl path. A one-dimensional
raw callback array with static storage retains its signature at block, file,
and persistent REPL scope; block-static scalar raw callbacks use the same
data-backed path. Automatic raw callback arrays, raw callback array parameters,
raw record or class field arrays, multidimensional raw callback arrays, alias
chains, `void *`, and empty-`()` pointers still lack this support and keep
focused diagnostics.
A plain function initializer or direct `&function` address must match the local
pointer's result, record identity, fixed parameters, and variadic boundary.
Named local callback copies follow the same rule. Later target addresses are
patched and a prescan-only signature is checked against its definition. A
compatible conditional retains and checks every callback arm. A represented
integer constant expression that evaluates to zero is accepted, including
unary, cast, arithmetic, character, and `sizeof` forms. A
conditional keeps that proof only when all required arms remain constant.
Unproved scalar, mutable enum-storage, or object values are rejected. An
explicit `void *` cast deliberately erases the check, but only for the value it
actually covers. Null conditional arms are neutral; every non-null object arm
must be erased. Failed functions and methods restore their emitted state,
patches, signatures, labels, and control nesting. A failed source also restores
touched prototypes, definitions, kernel bindings, and a reused `__start`, then
drops its new patches. The implicit thunk has a complete `void(void)` signature.
Named SIMD intrinsics retain their existing inline lowering rather than
crossing this ABI.
ADR 0216 records the first fixed-array model, ADR 0257 records multidimensional
row descent, ADR 0294 records whole-vector updates, and ADR 0299 records the
fixed SIMD call boundary. ADR 0301 records the named local callback boundary,
ADR 0303 records typedef-backed ordinary callback parameters, ADR 0306 records
global callback storage, ADR 0310 records automatic objects and Cupid class
method parameters, ADR 0313 records static callback initialization, and ADR
0319 records direct explicit function addresses. ADR 0321 records
typedef-backed callback fields, and ADR 0324 records grouped runtime function
addresses.
_Avoid_: untyped SIMD storage, escaped row pointers, reordered packed operands,
an implied 16-byte private call-site alignment

**Browser JavaScript number lane**:
The numeric path shared by the Browser's JavaScript lexer, AST, and
interpreter. Decimal, hexadecimal, binary, and octal tokens enter a dedicated
`double` field and reach runtime without narrowing through an integer node.
Valid separators may appear between digits. The lexer rejects empty radix
bodies, invalid radix digits, misplaced separators, and identifier suffixes.
Primitive conversion decodes UTF-8 while trimming the ECMAScript whitespace
set, consumes the complete string, accepts decimal exponents, radices without
a sign, and signed `Infinity`, and returns NaN for invalid text or `undefined`.
Equality handles same-type primitives, the null and undefined pair,
Boolean-to-number conversion, and number/string conversion. Two strings
compare by UTF-16 code unit. Remainder and `%=` use `fmod`. String `+` and
`+=` append into the remaining 64 KiB string pool and report exhaustion.
Assignment resolves its binding, member receiver, or computed key once before
the right side runs, then stores through that saved identity. Each binding
records its owning scope; declarations search only their current scope, while
expression lookup may walk through parents. Checked value pushes restore an
expression's entry depth after overflow. Every string interning path reserves
the complete slice before it publishes a token, binding, property, or value;
failed global installation blocks queued scripts. Native function IDs travel
with their tags through stack, binding, property, and return lanes. Canonical
array-index writes grow an unsigned length lane through index 4,294,967,294.
The non-index key 4,294,967,295 remains an ordinary property, and direct
`length` assignment is rejected.
Finite formatting avoids out-of-range integer casts and covers plain values
below `1e21` plus scientific notation below `1e-6` or at least `1e21`. The
26-field asset-free self-test also requires ten useful syntax failures, a
600-byte concatenation, transactional pool exhaustion, side-effecting compound
targets, nested-call scope ownership, native round trips, array limits, full
and near-full value-stack failures, a 1,100-write balance check, and same-run
recovery.
ADR 0210 records the first binary64 slice; ADR 0218 records this boundary.
_Avoid_: complete ECMAScript coercion, object-to-primitive conversion, shortest decimal formatting

**Private CupidC adjacent string literal**:
One or more neighboring C string tokens emitted as one null-terminated data
object. Each decoded token may contain at most 1,023 bytes. The combined value
can use the remaining 8 MiB private data section and works in automatic
expressions, file-scope initializers, and persistent REPL declarations. The
lexer consumes a rejected overlong token before reporting it, while the parser
reports joined-data exhaustion without publishing a truncated string. ADR
0218 records the model.
_Avoid_: silently truncated C strings, one fixed buffer for the joined value

**Private CupidC tagged structure typedef**:
A private JIT, AOT, or persistent REPL declaration that gives one structure
both a tag and an ordinary typedef name. The typedef table keeps the structure
index beside the coarse value category, so alias chains and pointer aliases
retain the field layout used by `.` and `->`. Tagged and anonymous forms share
one field-layout parser. ADR 0219 records the model.
_Avoid_: dropping the structure index, rewriting a tagged typedef as anonymous

**Private CupidC record-field address**:
The address produced for a represented record member in private JIT or AOT
source. `&record.field` starts from the record object, while
`&pointer->field` first loads the pointed-to record. Both forms add the
declared field offset and leave the selected address in EAX instead of loading
the field value. ADR 0219 records the model.
_Avoid_: the address of the pointer variable, a field value used as an address

**Private CupidC checked allocation**:
The common size boundary for private fixed arrays, records, globals, persistent
REPL objects, and automatic frames. Array counts and strides must be positive,
and their product may not exceed `0x7ffffffc`. Record padding, each field, and
the final four-byte allocation alignment stay within that ceiling. Automatic
objects accumulate through one alignment-aware helper capped at `0x7ffffff0`,
leaving room for the function's final 16-byte frame rounding. Signed constant
expression arithmetic rejects overflow before evaluation; expressions with an
unsigned operand wrap modulo `2^32`. Decimal and hexadecimal integer literals
span the represented `uint32_t` range, and their suffix counts toward the
95-character token boundary. ADR 0219 records the model.
_Avoid_: checking only a wrapped product, direct local-offset subtraction

**Private CupidC REPL record checkpoint**:
A copy of every committed private structure definition held by the persistent
REPL. Rollback restores the records and the table count, so a failed line
cannot complete an older forward tag or expose rejected fields to later input.
ADR 0219 records the model.
_Avoid_: restoring only `struct_count`, fields from rejected REPL source

**Cupid ASM**:
The assembly language native to Cupid OS.
_Avoid_: CupidASM when referring to the language, NASM syntax

**CupidASM**:
The assembler for Cupid ASM source.
The active demo corpus contains 22 shipped `.asm` sources. Its hosted
fixed-image contract supplies each source with the kernel binding spellings it
needs. The contract assembles each source twice and requires identical bytes
and region metadata. The `parity_gfx2d.asm` fixture carries both
`gfx2d_fullscreen_enter` and `gfx2d_fullscreen_exit`, matching the calls used
by the demo and the exports in `kernel/lang/as.cc`.
_Avoid_: Cupid ASM when referring to the assembler

**Kernel CupidASM artifact request**:
The typed in-OS adapter request for raw binary, unlinked ELF32 relocatable, or
linked executable output. All three forms use the shared CupidASM core. A raw
result carries borrowed code16, code32, and data ranges plus its origin, and
the adapter can render those ranges as `cupid.raw-map.v1`. An ELF32 result
keeps its sections, undefined symbols, and relocations for a later link. The
executable form selects `main` or `_start` and passes the object to in-kernel
CupidLD. Raw image and map publication writes both private candidates before
moving either target and restores previous targets after a command failure.
Matching pair-level commit records sit beside the artifact and map. Each names
the exact backup and marker paths from the finished publication, so a later
command can finish deferred cleanup after reusing either member. Without a
valid record, retained backups belong to an interrupted publication and must
be restored.
This rollback pair is not crash-atomic and does not lock concurrent shell
publishers. ADR 0337 records the boundary.
_Avoid_: separate kernel assembler, inferred byte-only mode map, crash-safe pair

**Cupid ASM alignment statement**:
The `align POWER_OF_TWO[, FILL_BYTE]` statement. In raw output it aligns the
absolute `ORG` address. In ELF32 objects it aligns the current section offset
and raises that section's `sh_addralign`. In fixed images it aligns the
absolute region placement and each later statement. The optional fill is one
byte and defaults to zero. A NOBITS section may only use zero fill because its
padding occupies memory without occupying the file.
_Avoid_: loader-provided alignment, placing an object first as an alignment guarantee, NASM escape

**CupidLD**:
The Cupid Toolchain linker. Its checked-seed CLI publishes complete ELF and PE
images through an adjacent candidate created with exclusive-create semantics
and one filesystem replacement call. After closing the candidate, it reopens
the file and checks its size and contents before replacement. On POSIX,
CupidLD requests mode `0777`; the process umask may remove any permission bits.
This path requires a caller-controlled, stable output directory and does not
lock or pin the destination. Its fixed PE32 profile accepts named DLL imports,
builds `.idata`, binds IAT slot symbols, and rejects any imported reference
that is not a zero-addend absolute relocation.
_Avoid_: host linker, guarded multi-process publisher, crash-durable publication

**Fixed-layout PE32 image**:
A deterministic i386 PE32 console image serialized by CupidLD for one
prescribed memory layout. It may be import-free or carry a canonical `.idata`
section with import and IAT directories. Empty output categories are omitted
from the section table. Writable executable input is outside the profile and
fails transactionally. The static i386 Linux seed carries this profile and
uses it to build the checked five-tool Windows execution seed. The Windows
seed runs production commands directly. It carries no native build plan, so
the native fixed-point driver pairs it with the separately verified Linux plan
seed to rebuild stages two through four.
_Avoid_: general PE linker, standalone Windows bootstrap seed, dynamic relocation

**Cupid-built Windows runtime probe**:
A freestanding i386 command compiled by checked-seed CupidC, assembled by
CupidASM, linked with three `KERNEL32.dll` imports by CupidLD, and loaded
directly by Windows. It prints a fixed marker and exits with status 37. The
checked producers remain static i386 Linux programs executed through WSL, so
the probe proves the Windows loader boundary rather than a native bootstrap.
_Avoid_: host-built Windows oracle, native Toolchain fixed point, Windows seed

**CupidObj**:
The Cupid Toolchain object and binary transformation utility. `wrap` keeps
binary input unchanged, while `wrap-text` converts CRLF pairs to LF before it
builds an ELF32 object. A lone carriage return remains part of the input. Its
checked-seed `wrap-jpeg` operation validates one sequential SOF0 or SOF1 frame,
at least one scan, entropy stuffing and restart markers, and a terminal EOI,
then uses the ordinary binary wrapper without changing the input bytes.
Progressive, unsupported, and malformed frames fail transactionally. The
production recipe now runs this command before its Python parity and
publication checks. Its typed `install-source` operation emits the bin, docs,
or demos installation
table from a validated repository path inventory. The checked seed rejects
more than 512 paths across all request lists before mode dispatch, accumulates that
total without overflow, and preserves caller order across mixed home-asset
extensions. The Python oracle follows the same order. The command is
self-hosted and byte-compatible with that oracle. The normal Make recipes
invoke the checked-seed command and depend on the complete CupidObj trust
inputs. ADR 0204 records the production transfer, and ADR 0205 records the
corrected seed.
The checked seed also compares every complete emitted binary symbol before it
writes a table. Distinct paths that normalize to one symbol fail, while the
exact same BMP path may appear once in the docs list and once in the home list
because both entries use one wrapped object. The Python oracle enforces the
same rule. ADR 0206 records its promotion.
Checked-seed CupidObj also provides `disk-template`. It emits the deterministic
prefix of a new Cupid disk through the empty FAT16 root directory and places
the kernel at LBA 5. The normal image path consumes that template first.
Python preserves persistent FAT data, stages files, verifies parity and drift,
extends the image, and publishes it. ADR 0236 records the source boundary, ADR
0237 records seed carriage, and ADR 0238 records the production handoff.
_Avoid_: objcopy

**Installation source table**:
One generated `.cc` file that installs an auto-discovered source or asset
cohort into the boot filesystem. Checked-seed CupidObj emits the byte-exact
bin, docs, and demos formats. Checked-seed CupidC compiles the result.
_Avoid_: checked-in file list, embedded source object

**Canonical text wrap**:
The CupidObj transform used for source, manuals, demos, and vocabulary data.
It makes the embedded bytes independent of a host checkout's line endings
without changing binary assets.
_Avoid_: source formatting, binary wrapping

**CupidDis**:
The Cupid Toolchain disassembler and binary inspector. Hosted source head
accepts raw images, static i386 ELF32, and CupidLD's bounded static i386 PE32
profile. Its PE views report headers, sections, named imports, and decoded
executable sections. Strict PE inspection validates the entry and constant
direct relative targets against shared x86 instruction starts. PE symbols,
relocations, dynamic images, ordinal imports, and general layouts remain
outside the represented profile. The reader also enforces CupidLD's 2 GiB RVA
limit for the loaded image. Complete seed reports match an independent Python
reconstruction of their sections and imports. The promoted seed images
predate PE input support.
_Avoid_: Cupid disassembler when naming the tool

**Conditional move family**:
The sixteen i686 `CMOVcc` operations represented by one shared x86 encoding and decoding rule. A canonical mnemonic names each condition, while conventional alternative spellings remain aliases.
_Avoid_: conditional jump, `SETcc`, separate assembler and disassembler definitions

**Parity SETcc pair**:
The canonical `SETP` and `SETNP` byte predicates represented by the shared x86 model. Each accepts one byte register or memory destination in 16-bit or 32-bit mode. Private CupidC uses the pair to merge the parity flag into floating comparison and truth results, and CupidDis keeps those sequences aligned. The guest disassembles and executes the bounded `test_fpaug.cc` parity cases before running the full feature-13 behavior. `SETPE` and `SETPO` are outside this source-driven slice.
In GUI mode, the shell keeps disassembly listings in the terminal and mirrors
them to serial after the ordinary sink and redirection checks. The runtime
gate therefore observes production CupidDis output without changing text mode.
_Avoid_: the complete `SETcc` family, parity aliases, private decoder exception

**Immediate multiply family**:
The three-operand `IMUL` operation represented by the shared `69 /r` full-immediate and `6B /r` sign-extended-immediate encodings. Its destination is a 16-bit or 32-bit register, its source is a same-width register or memory operand, and the encoder chooses the shorter form only when the value fits a signed byte.
_Avoid_: one-operand multiply, two-operand `IMUL`, decoder-only instruction support

**Padding NOP family**:
The ordinary compiler alignment instructions represented by the single-byte `90`, operand-size-overridden `66 90`, and `0F 1F /0` register or memory encodings. Their operand and address sizes follow the normal 16-bit and 32-bit mode rules.
_Avoid_: PAUSE, arbitrary repeated legacy prefixes, treating every `0F 1F /r` group digit as NOP

**Clang repeated-prefix padding**:
Five exact 32-bit decode-only alignment NOPs with two through six leading `66` bytes followed by `2E 0F 1F 84 00 00 00 00 00`. They preserve the general rule that other repeated legacy prefixes are invalid and have no encodable form.
_Avoid_: a general repeated-prefix grammar, CupidASM output, a catalogue form

**Raw range map**:
An ordered set of byte ranges that classifies one flat image as 16-bit code,
32-bit code, or literal data. A caller may supply the ranges directly, or
CupidASM may serialize them from statement kinds and active `BITS` modes in a
`cupid.raw-map.v1` or `cupid.raw-map.v2` file. The first range starts at offset
zero, and later starts increase within the source. V2 also records ordered
source-resolved control edges. CupidDis decodes code ranges and renders data
ranges as `db` rows without entering the x86 decoder. The assembler, not a
byte heuristic, places source-derived transitions at statement boundaries.
_Avoid_: raw mode map, automatic code or mode detection, one kind per retained instruction

**In-kernel raw disassembly request**:
The public `dis_disassemble_raw` adapter request for a borrowed byte buffer.
It selects fixed 16-bit code, fixed 32-bit code, or a shared typed raw range
map. Its optional strict-known rule checks CupidDis's typed decode summary and
returns a VFS error before rendering when selected code contains an unknown,
invalid, or truncated instruction. The older `dis_disassemble` call is a
permissive fixed-32 wrapper.
_Avoid_: raw-map parser, inferred mode, strict legacy listing

**Source-resolved raw control edge**:
One ordered v2 map row for a represented direct relative transfer, immediate
far call or jump, or explicit indirect transfer. A local row records the image
offset, linear address, target mode, and far segment when present. A resolved
external row records the address and far metadata outside the image. An
indirect row remains explicitly unprovable. Production boot and SMP
transactions require every represented edge to agree with decoded bytes and
preserve the previous image on failure. ADR 0340 records the source rule, and
ADR 0336 records seed carriage and adoption.
_Avoid_: inferred source label, treating an indirect target as proved, v1 range row

**Raw layout policy**:
The artifact owner's accepted size, origin, and ordered raw range map for one
flat image. CupidASM reports source layout, while the owner decides whether
that layout is safe to inspect and publish.
_Avoid_: raw range map, assembler approval, inferred policy

**Local relative target check**:
The explicit CupidDis policy that checks constant direct relative calls and
jumps against decoded instruction starts. For raw input, a target must stay in
the image and land in same-mode code. The report separates outside-image,
data, wrong-mode, and mid-instruction failures. For static ELF32 `ET_REL`
input, each executable `PROGBITS` section is checked separately. An
unrelocated target must stay in its source section and land at an instruction
start there. A relocated operand is left to the existing relocation-ownership
and link rules. The object report separates outside-section and
mid-instruction failures. Far pointers and indirect transfers remain outside
the policy. Checked CupidDis also applies it to linked i386 `ET_EXEC`
input. It checks constant direct relative targets against instruction starts
across nonoverlapping file-backed executable load regions. The linked report
separates targets outside loaded memory, in loaded memory without file-backed
executable code, and inside an instruction. A `PT_DYNAMIC` or `PT_INTERP`
header rejects the image as outside the static certification domain.
Source-head hosted CupidDis applies the same start-map rule to CupidLD PE32
images. A PE target may cross executable sections, but it must land on a
decoded start. The report separates a target in loaded non-executable data,
outside loaded sections, or inside an instruction.

The CLI exposes the policy as `--require-local-targets` beside
`--require-known`. The promoted seeds carry both forms. The bootloader
transaction covers nine targets, and the SMP transaction covers four.
Production CupidASM object publication applies the relocatable form after its
structural check. The normal kernel transaction applies the linked form to its
frozen pass-one and final ELFs before CupidObj flattening. A failure preserves
the prior output. ADR 0300 records the
raw source rule, ADR 0305 records its seed carriage and adoption, ADR 0309
records the relocatable-object rule, and ADR 0312 records its carriage and
adoption. ADR 0314 records the linked-image rule, and ADR 0318 records its
carriage and production adoption.
_Avoid_: source-label proof, automatic code discovery, overlapping executable
load regions

**Static ELF code anchor**:
The `e_entry` address or one defined `STT_FUNC` symbol in a static i386 ELF32
image. In `ET_REL`, the function must start at a decoded instruction in its
executable `PROGBITS` section. In `ET_EXEC`, the anchor must start in
file-backed executable code. Function aliases count separately. Undefined
functions and other symbol types do not count. Absolute functions are outside
the linked-image count. The report separates anchors outside executable code
from anchors in the middle of an instruction. The policy shares an
instruction-start map with local-target validation when both are selected.
ADR 0320 records the linked source rule, ADR 0323 records its seed carriage and
production adoption, ADR 0335 records the relocatable form, and ADR 0336
records its seed carriage and active assembly adoption.
_Avoid_: debug-line entry, inferred instruction start, unchecked production input

**Explicit assembly function symbol**:
An assembly symbol declared as `global name:function` or
`extern name:function`. CupidASM records it as `STT_FUNC` without inferring a
function from global binding. An unannotated assembly symbol remains
`STT_NOTYPE`. Production uses this form for 54 ISR, context-switch, and hosted
startup exports.
_Avoid_: treating every global as code, disassembly-inferred function

**Static PE entry anchor**:
The `AddressOfEntryPoint` in a CupidLD-profile i386 PE32 image. Source-head
hosted CupidDis requires it to equal a decoded instruction start in a
file-backed executable section. PE function symbols are not represented, so
the entry is the only PE code anchor. The promoted seeds do not carry this
inspection path yet.
_Avoid_: inferred export, arbitrary PE entry, checked-seed carriage

**Relocatable entry symbol**:
The caller-priority code label selected by CupidASM for an ELF32 relocatable
object. The assembler publishes its spelling and promotes only that label to a
global symbol. CupidLD resolves it after final placement.
_Avoid_: prelinked entry address, promoting every candidate, fixed-image entry

**Fixed-frame stack probe**:
A read from each newly reserved stack page while CupidC enters a function with
more than 4,096 bytes of fixed local storage. Reservations of one page or less
keep the existing one-step prologue. Larger reservations advance in steps no
greater than one page and touch every step, including the final partial page.
_Avoid_: fully committed stack, source workaround, writable probe

**Strict decode summary**:
The typed CupidDis counts of known, unknown, invalid, and truncated instructions across selected code regions. Source head and the checked production seeds also report total and unmatched relocations that target executable sections in an ELF32 relocatable object. Declared raw data and non-executable ELF regions do not enter the summary. The hosted strict policy accepts the report only when its three fallback counts and unmatched relocation count are zero.
_Avoid_: searching rendered `db` rows, counting data as instructions, a replacement for ordinary disassembly output

**Executable relocation ownership**:
The match between one ELF32 code relocation and one decoded four-byte instruction field at the same section offset. `R_386_PC32` owns a relative field, while `R_386_32` owns a non-relative field. Relocations in data sections do not take part. Both checked production seeds carry the rule, and the public CupidASM object transaction rejects unmatched executable relocations before publication.
_Avoid_: any relocation inside an instruction, parsing rendered operands, data relocation validation

### Bootstrap

**Self-hosting**:
The state in which Cupid Toolchain source is built by the Cupid Toolchain itself.
_Avoid_: merely building Cupid OS with Cupid tools

**Bootstrap seed**:
A checked-in Cupid Toolchain executable that starts a bootstrap without an external code-generation toolchain.
_Avoid_: oracle toolchain

**Checked i386 Linux bootstrap seed**:
The manifest-bound set of static CupidC, CupidASM, CupidDis, CupidLD, and
CupidObj executables under `bootstrap/seeds/i386-linux/`. Verification binds
their hashes, sizes, ELF properties, target ABI, producer lineage, source
revision, and exact 19-source build plan before execution. The current seed is
generation four from revision
`a17c9465911da41d59b7ada71733d36c39faa5ea`. CupidC is 2,687,436 bytes with
SHA-256
`273f2621401878f673cc3d2987e267cf188ed016ac2005dc9573b3242b225094`.
CupidASM is 462,600 bytes with SHA-256
`a6c2f07e722fb4b5152326773a240722d1065785c1110d65c593445b0e88dc80`.
CupidDis is 476,092 bytes with SHA-256
`2853dfa068af8716dde1e501fb5a6c11e73dcaae182fdba6ca45ef4f2a65fb89`.
CupidLD is 312,792 bytes with SHA-256
`a2119556894903b662d2e131a9a2436b99a3afdd1b1600a3df4d4669569a0295`.
CupidObj is 392,688 bytes with SHA-256
`99111b5db7586ac4b2ed00005f2fe2e89c66ed48f007d796206b116a088cdf7a`.
The 5,573-byte manifest has SHA-256
`b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b`.
It binds the revision, generation four, the 50-input source snapshot
`46c5335c80d822dd5085ee22077486ea647e5396482d42454847c87e4222aa67`,
and the stage-three producers.
The seed carries bounded decimal `long double` constants, transactional
sequential-JPEG validation, pristine disk-template, ISO fixture, and
profile-manifest construction. It also carries deterministic PE32 imports,
signed x87 integer conversion forms, runtime and static integer to
`long double` conversion, static long-double controls, canonical x87 zero,
subnormal, normal, infinity, and NaN payloads, static long-double arithmetic,
ordinary `float` and `double` updates, wide integer conversion to `float` and
`double`, indexed typed CupidDis inspection, executable relocation ownership,
the corrected raw `EQU` rule, the 604-row shared x86 catalogue with `SETP` and
`SETNP`, and strict raw-image, relocatable, linked-target, and static ELF
code-anchor and source-edge policies. The candidate changed CupidASM and
CupidDis relative to the preceding seed. The promoted-seed reproof matched all
five initial images.
Stages
three and four matched nineteen C objects, startup, and five tools, then passed
the 5/22/21 behavior matrix. ADR 0336 records the current seed.
The preceding broad-only production gate inspected all 427 audited root object
outputs plus the pass-one and final kernel ELFs before flattening. Its
9,028-byte graph-ordered input manifest has SHA-256
`48bdef348f6575881b9808631173e7265abc9ea89dfb84d48de72b3d2304749e`.
That gate passed in 185.526 seconds with exit 0 and empty streams, but it did not
enforce local direct targets in the linked images. The following 187.054-second
hostbuild transaction also predates linked-image adoption. It published an
8,946,332-byte `kernel.bin` with SHA-256
`4f5f2591d01bcc4007773844e9bfb8112a16dd17fbd178014cc2056fefaab67d`.

The current production path freezes the selected seed manifest and five
artifacts, the 431-entry input manifest and cohort, and the existing
`kernel.bin` boundary in one hostbuild transaction. Checked CupidDis validates
the private cohort, applies strict linked local-target and static code-anchor
validation to the frozen pass-one and final ELFs, then checked CupidObj flattens the final image into a
private candidate. Hostbuild rechecks the live trust inputs and output before
parent-relative atomic publication. Any failure preserves the prior raw
kernel. A source-consistent normal build completed the broad, linked, and flat
checks in 3,785.83 seconds. The exact-size gate then caught the expected 28-byte
manual correction. After its policy row moved, the fourteen-artifact contract
passed twice and the image was restaged at that checkpoint. A first consistency
pass corrected four active embedded manuals, rebuilt their CupidObj wrappers,
and relinked both kernels. Production publication repeated the broad, linked,
and flat checks in 560.05 seconds. Its 9,271,332-byte policy row passed the
contract twice before the image was restaged. A final provenance pass corrected
two more active manuals, rebuilt their wrappers, and repeated publication in
562.55 seconds. The 9,271,380-byte policy row passed the contract twice before
another image restage. The current raw kernel has
SHA-256
`e1801128cceeb5a510671684cded5a0aef04220dfafe90fa686df963e7abf37f`.
Moving private flatten extraction onto the shared pinned-path helper remains
deferred maintenance.
On 2026-08-13, a preceding poisoned-host `make -j2 all` build completed
through the checked native Windows execution seed. The command harness stopped
the first invocation after 602.5 seconds; the resumed build finished in another
968.5 seconds, for 1,571.0 seconds of cumulative build work. This checkpoint
superseded the older output identities below when it was recorded.
`boot/boot.bin` is 2,560 bytes
with SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`.
The 9,056,612-byte pass-one ELF has SHA-256
`e2f63b5cd9c4e2769b9d6bc893ab5cf778951b97aec954ece6cbac0cc429e92a`,
the 9,179,492-byte final ELF has SHA-256
`1bc06263dbf9849e6d2c594b6fb4be2a3f3b673c91f69d23a2d2e639b1f64776`,
and the 8,962,776-byte raw kernel has SHA-256
`3170aa71eafa656b1f6e23c918f1f472860f513c9c5cd0376d7d4f5f8a7d891c`.
The exact-size gate accepted all nine artifacts before image publication. The
resulting 209,715,200-byte `cupidos.img` has SHA-256
`3b5dd6523a90d6ed0543a6ab2464892f3289b876654f9869f88db0901940b91e`.
A four-vCPU RTL8139 frontier passed from this image in 820.7 seconds. All four
CPUs came online. Private CupidC emitted the broad indirect-update marker,
compiled `/bin/feature13_derived_aot.cc`, loaded the resulting ELF as PID 4,
emitted `[feature13-derived-aot] PASS score=41 once=2 zero=0x80000000`, and
reported that same PID exiting. The 640 by 480 framebuffer changed 96,101
pixels. AC97 produced 33,452,396 frames at peak 25,600, and the PC speaker
produced 76,614 frames at peak 31,877. USB detach/replug and the post-replug
survival window also passed. The private run left the source image unchanged.

The guarded 2026-08-14 production checkpoint includes in-kernel CupidLD and
the guarded normal boot edge. A poisoned-host normal build passed in 674.693 seconds
after CupidDis accepted all 431 production inputs. The pass-one ELF is
9,211,340 bytes with SHA-256
`2a6f5deafb580b30254483179d6dade9ed4ed7b17b39f9368137b1ff14932263`.
The final ELF is 9,334,220 bytes with SHA-256
`bc855462c1f8f42e34d94a974443f7c6e565d60b1913e3b6f33b3e6e375f3ed6`,
and the raw kernel is 9,114,084 bytes with SHA-256
`8b5d73e74538ce11c1fb074f88b3852d690038aa5cb3a8de3ce222e9df88cade`.
The 209,715,200-byte image has SHA-256
`813c9b0c78f795c1ac9fcff59b9c4111a958a07eb1e3943dc7af60c536521110`.
Its five-sector boot image is unchanged. A private four-vCPU QEMU boot ran
`ls` and reached JIT completion in 49.257 seconds.

A separate private guest smoke assembled `/demos/hello.asm` to a 15,680-byte
`ET_REL` object, linked an 8,536-byte two-segment ELF at `0x01A00000`, ran it
as PID 4, and observed a normal exit. The complete AOT smoke passed in 76.174
seconds.

The preceding dual-NIC checkpoint used image SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.
E1000 exited 0 in 725.058 seconds, and RTL8139 exited 0 in 725.406 seconds.
Both used four CPUs, the partitioned USB fixture, SMP and full frontier
verification, and private image copies.
With `CC`, `CXX`, `CPP`, `HOSTCC`, `HOSTCXX`, `ASM`, `AS`, `LD`, `AR`, `NM`,
and `OBJCOPY` pointed at invalid commands, normal `make -j2` passed in
1,057.969 seconds. That earlier build ran the separate strict CupidDis gate
before CupidObj flattened the kernel. The build produced a 2,560-byte
`boot/boot.bin` with SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`, a
9,039,936-byte pass-one kernel with SHA-256
`b21fa8954499a7857ee4b12fa3950fcc08ff3c6a6234c8ae72effc38c51fdc6d`, a
9,162,816-byte final kernel ELF with SHA-256
`a0b57cd886369762b65d657bb3f2915ada8f30b52102535add89466eaf4f5976`, an
8,946,332-byte raw kernel with SHA-256
`4f5f2591d01bcc4007773844e9bfb8112a16dd17fbd178014cc2056fefaab67d`, and a
209,715,200-byte image with SHA-256
`4548005bd0aa1a3cffb74620c2309d53c6b291ea2505ed187034bf6b13f1bb37`.
Definitive four-vCPU boot frontiers passed from that image with `--cpu max`,
SMP and frontier runtime verification, the partitioned USB fixture, and a
private image copy. E1000 passed with exit 0 in 794.034 seconds. RTL8139 passed
with exit 0 in 758.667 seconds. Both exercised the framebuffer, AC97, and PC
speaker checks. The source image kept the SHA-256 recorded above.
Those image and boot results remain pre-freeze runtime evidence.
ADR 0266 records the indexed decoder, and ADR 0265 records this promotion.
_Avoid_: current normal-build toolchain, native Windows seed, unverified binary cache

**Checked i386 Windows execution seed**:
The manifest-bound set of native PE32 CupidC, CupidASM, CupidDis, CupidLD, and
CupidObj executables under `bootstrap/seeds/i386-windows/`. The manifest binds
their hashes, sizes, target ABI, exact imports, producing revision, 50-input
source snapshot, parent Linux seed, and clean stage-four provenance. Its
2,118-byte encoding has SHA-256
`751e1d7787a4be08e4e86814bbb7473979fe2eb8a3292baed0241967f772eaef`.
Windows runs
these images directly for output-bearing production work. The cohort is an
execution seed, not a bootstrap seed: it does not carry a native Windows build
plan. The native fixed-point command pairs it with the separately verified
Linux plan seed. Static Linux Toolchain contract paths continue to use the
checked Linux bootstrap lineage. The `CUPMAN4` author keeps that producer
lineage but runs as a native PE on Windows. The `CUPMAN2` verifier follows the
host and uses the PE cohort directly on Windows. Artifact-size policy keeps the
Linux manifest as semantic provenance, while Windows uses the PE cohort to
build and run the private policy contract directly. The Windows user syscall ABI gate
uses the PE cohort in the same temporary role. ADR 0272 records the seed boundary, ADR
0295 records the native ABI gate, ADR 0297 records the size-policy gate, and
ADRs 0278 and 0279 record native reconstruction and convergence, and ADR 0336
records the current promotion. The seed binds revision
`a17c9465911da41d59b7ada71733d36c39faa5ea`, source snapshot
`46c5335c80d822dd5085ee22077486ea647e5396482d42454847c87e4222aa67`,
and Linux parent manifest
`b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b`.
Its CupidASM, CupidC, CupidDis, CupidLD, and CupidObj images are 444,928,
2,613,760, 452,608, 296,448, and 375,808 bytes with SHA-256 values
`5c21d79b1822831e5d81359fa2b31d85b731ead5a88c6596ced38585e64b87cb`,
`c768223d4dcd36023e9793b65d86f7bcbd641e921d6a6febf0a255eb7a0e1002`,
`1e357223bfa0d967e5fe96ac180279f508271c4efed0f20d2c8d094726ff0eef`,
`9fe3bd4fda9b87d678aa2eb6305e65b706ecdff074b16722faab23ce05cd8e02`,
and `079bc115e74772e6224e4da164115cc5696e357cca0cb1a0583985b88381cb79`.
_Avoid_: native Windows fixed point, Windows bootstrap seed, unverified PE cache

**Production seed**:
The checked tool cohort selected for output-bearing work on the current host.
Windows selects the checked i386 Windows execution seed; Linux selects the
checked i386 Linux bootstrap seed. A recipe that needs Linux bootstrap
provenance names the bootstrap seed separately instead of inheriting this
platform choice.
_Avoid_: bootstrap seed, oracle toolchain, whichever tool happens to be installed

**Checked-seed invocation**:
One tool call launched from a private capture of a manifest-bound five-tool
seed. The shared runner executes the selected image, then verifies the live
manifest and all five images before returning success. Linux ELF images run
directly on Linux and through WSL on Windows. Native PE images run directly on
Windows. A command fails when the post-run reload finds any live seed member
changed from the frozen capture, even when the private command succeeds.
Checked production CupidC and checked user CupidLD wrappers supply the capture
they already own, while their source snapshots, ELF checks, output guards, and
publication rules stay local. ADR 0190 defines the trust rule, ADR 0246 applies
one runner to the checked production CupidC and checked user CupidLD paths,
and ADR 0272 adds the native Windows execution cohort.
_Avoid_: live-seed execution, verified executable alone, native oracle command

**Cupid artifact size policy**:
The canonical checked-in size contract for fourteen deterministic Cupid-owned
outputs: four OS outputs, five Linux seed images, and five Windows seed images.
Make runs one `ARTIFACT_SIZE_CONTRACT` command with `--checked-manifest`.
Its Host Python wrapper pins the raw policy and Linux policy manifest, all
fourteen observations, and the complete Windows seed directory: its manifest
plus five PE tools. A `CUPSIZE2` request carries the Linux manifest digest, the
raw Windows manifest, and regular-file size and digest observations for all
five PE tools. The C contract validates the Windows target, provenance, Linux
parent link, exact inventory, and observed bytes beside the policy and Linux
manifest. On Windows, the
wrapper verifies the captured PE cohort, builds and runs the native contract
from those captured bytes, and compares the report with an independent Python
oracle. Before success, Python rereads the captured Windows byte sequence and
walks the pinned repository view again. A leaf, parent, membership, or byte
replacement fails. Linux builds and runs the static ELF contract from its
checked seed. The source-current focused modules contain 22, 16, and 13 tests,
for 51 total. They pass with four existing platform-specific skips. The
source-head artifact contract passes against all fourteen exact artifacts. The
earlier artifact group ran 46 tests in 4.160 seconds, with four expected
Windows skips. That checkpoint reached the exact-size gate with changed
pass-one ELF, final ELF, and raw-kernel outputs. After those three policy rows
were updated, its repeat passed in 874.531 seconds and checked all fourteen
artifacts.
The source-head kernel outputs are a 9,580,120-byte
`kernel/kernel.elf.pass1` with SHA-256
`3197dcc79ee68193b94ca3bfa104e9a3a592ae9a7905416e6a351e5879b8afd8`, a
9,711,192-byte `kernel/kernel.elf` with SHA-256
`394c8984c896a6f2c7d8475a41cf4fab4bd1f51a6703a6bff95f716c9a718337`,
and a 9,482,844-byte `kernel/kernel.bin` with SHA-256
`3f9bc2f5009274d9ec0a4cfe548d5c1e07cf88634057bca4973d6890cb2d6d35`.
The published 209,715,200-byte `cupidos.img` has SHA-256
`797c2a7bce559564f96319f5bfb04c5292c8aebb756b8957184935f99ab00612`.
The normal build linked both ELFs and completed strict inspection of all 431
production inputs with local-target and code-anchor checks. A private
four-vCPU E1000 frontier smoke booted the image and passed its SMP, terminal,
framebuffer, and audio checks.
The verifier is a direct prerequisite of `cupidos.img`, so a failure prevents
image publication and preserves the existing image. Missing, unknown,
duplicate, linked, nonregular, or differently sized members fail. An
intentional size change updates the implementation and policy together. This
is a regression sentinel, not a host-compiler comparison or linker capacity
limit. ADR 0297 records the contract transfer, and ADR 0305 records the
fourteen-artifact expansion.
_Avoid_: output-quality oracle, approximate size budget, hard-coded seed directory

**Frozen fixed-point source closure**:
The source-current 55 inputs copied into one private compiler root before a
checked-seed bootstrap runs. The closure includes the small Windows probe,
the native Windows tool runtime and startup, CupidLD's publication runtime and
bridge, the direct runtime contract, the hosted Windows declarations, the
PE32 reader interface and implementation header, and the first CupidBuild
headers and Windows startup. All
built stages and their behavior checks consume that root. The
harness rehashes the private and live closures at each boundary, then publishes
stages two through four, behavior evidence, and the report as one complete directory only
after every gate passes.
The promoted seed manifests retain their historical 50-input snapshots until
a later fixed-point promotion.
_Avoid_: live source root, source hash alone, public staging directory

**Native Windows fixed-point driver**:
The `bootstrap-windows` operation that freezes a checked Windows execution
seed and the checked Linux build-plan seed, derives the PE plan, and builds
Windows stages two through four. Source head uses the Linux seed's CupidC for
stage-two C objects because the older Windows compiler exhausts its practical
32-bit address space on the enlarged frontend. The Windows seed still supplies
stage-two assembly, inspection, and linking. The resulting native CupidC uses
64 KiB arena blocks, matching Windows allocation granularity. Native stage two
builds stage three, native stage three builds stage four, and the driver
compares every object and tool between the final two generations. It runs
native behavior checks on those compared stages, rehashes the live closure,
and publishes one evidence bundle. The two manifest roles remain separate and
are each revalidated with their listed artifacts at every generation boundary
and immediately before publication. Drift in either role prevents publication.
An uncapped preliminary proof matched 20 C objects, two assembly objects, and
all five tools, then passed the 5/5/5 behavior gates in 20 minutes 43 seconds. It began
from uncommitted source, so it is not seed-promotion evidence.
_Avoid_: Windows execution seed as Linux bootstrap seed, copied build plan, partial publication

**Bootstrap stage**:
One toolchain generation produced by the preceding generation during a bootstrap.
_Avoid_: build phase

**Fixed point**:
The bootstrap state in which consecutive toolchain generations are identical.
_Avoid_: successful compile

**Normal build**:
The supported path that builds Cupid OS with the Cupid Toolchain as its code-producing toolchain.
_Avoid_: oracle build, host build

**Oracle build**:
An optional comparison build used to establish external reference behavior or output.
_Avoid_: normal build

**Host toolchain**:
External compilers, assemblers, linkers, and binary utilities confined to explicit development and oracle paths. They do not produce normal Cupid OS or Toolchain artifacts. Host Python still coordinates the checked build. Windows runs output-bearing production tools, the user syscall ABI contract, the artifact-size contract, and the Toolchain manifest verifier from the native checked execution seed. The manifest verifier also checks the Linux publication seed. Source head reconstructs Windows stage two with checked Linux CupidC plus the Windows execution seed, then uses native PE tools for stages three and four. Linux fixed-point reconstruction, that one-generation Windows C bridge, and the full published Toolchain contract cohort use WSL on Windows. Preliminary native and Linux convergence proofs passed on one frozen uncommitted snapshot. Both platforms now have a clean stage-four proof, seed promotion, and promoted-seed reproof. Python-free coordination and promotion of a Windows seed carrying the 64 KiB arena-block policy remain open. ADR 0341 records the bridge.
_Avoid_: build orchestrator

**Checked native-tool cleanup**:
The Windows checked-seed runner stages each native tool in a private directory.
After the child exits, cleanup retries only sharing violation 32 for up to two
seconds. Other filesystem errors fail immediately, and a persistent lock still
stops the build. Seed capture, the live five-tool recheck, and atomic output
publication are unchanged. ADR 0329 records this boundary.
_Avoid_: unbounded retry, retrying unrelated cleanup errors
