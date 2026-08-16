# CupidC Compiler

CupidC is a HolyC-inspired C compiler built into the cupid-os kernel. It compiles `.cc` source files to native x86 machine code. Programs run directly in ring 0 without a virtual machine or interpreter.

---

## Overview

| Feature | Details |
|---------|---------|
| Language | C-like (HolyC-inspired) |
| Target | x86 32-bit machine code |
| Compiler type | Single-pass recursive descent |
| Calling convention | cdecl |
| Execution modes | JIT (in-memory) and AOT (ELF32 binary) |
| Privilege level | Ring 0 - full system access |
| Source extension | `.cc` |
| Code/data limit | 1 MB code buffer, 8 MB data/string buffer |
| Max source file | 256 KB |
| Max functions | 1024 |
| Max symbols | 4096 |
| Max structs | 64 (up to 32 fields each) |

### Hosted fixed-frame stack growth

The hosted compiler probes fixed frames larger than 4,096 bytes. Frames up to
one page keep the original single reservation. A larger frame reserves at most
4,096 bytes per step and touches the new page with a read after every step,
including the final partial page. This lets Windows and other guarded stacks
grow before the prologue moves beyond the guard page.

Naked functions still have no compiler prologue, and the kernel entry keeps
its zero-frame path. The current checked PE tools also commit their full one
MiB stack as a defense from ADR 0274. The compiler rule in ADR 0275 is the
general fix and does not require active source to avoid large local frames.
Five focused tests passed in 23.344 seconds. A broader self-host run reached
generation three and timed out after 904 seconds without reporting a failure.
That timeout is not fixed-point evidence.

---

## Getting Started

### JIT Mode - Compile and Run Instantly

```
> cupidc program.cc
```

CupidC compiles the source into memory and executes it without saving a binary. This mode is useful for short edit-and-test cycles.

### AOT Mode - Compile to ELF Binary

```
> ccc program.cc -o program
> exec program
```

Compiles the source to a persistent ELF32 binary on the FAT16 disk. The binary can be re-executed without recompilation.

If `-o` is omitted, the output name is derived from the source file (e.g., `program.cc` -> `program`).

---

## Language Reference

### Types

This table describes the private in-OS JIT, AOT, and REPL compiler.

| Type | Size | Description |
|------|------|-------------|
| `int` | 32-bit | Signed integer |
| `unsigned int`, `uint32_t`, `U32` | 32-bit | Unsigned integer with preserved private runtime semantics |
| `char` | 8-bit | Character / byte |
| `bool` | 32-bit | Boolean (alias for int) |
| `U0` | - | HolyC-style `void` spelling |
| `U8`, `I8` | 8-bit | Unsigned/signed byte spellings |
| `U16`, `I16` | 16-bit | Unsigned/signed word spellings |
| `U32`, `I32` | 32-bit | Unsigned/signed dword spellings |
| `U64`, `I64` | parsed | Accepted C/HolyC compatibility spellings; current codegen remains 32-bit |
| `long`, `short`, `signed`, `unsigned` | parsed | Accepted C compatibility spellings; represented 32-bit signedness is preserved |
| `float`, `double` | 32/64-bit | SSE scalar floating point |
| `float4`, `double2` | 128-bit | SSE vector types |
| `void` | - | No value (functions only) |
| `int*` | 32-bit | Pointer to int |
| `char*` | 32-bit | Pointer to char |
| `struct` | varies | User-defined composite type |
| `struct*` | 32-bit | Pointer to struct |

### Shared bootstrap frontend type spellings

The shared frontend now gives Cupid's native spellings exact i386 type-graph
identities in Cupid mode. `U0` is `void`. `U8`, `U16`, `U32`, and `U64` are
unsigned integers of 1, 2, 4, and 8 bytes. `I8`, `I16`, `I32`, and `I64` are
the matching signed types. `Bool` and `bool` are signed 4-byte `int` types.
`float4` and `double2` are distinct 16-byte vectors aligned to 16 bytes.

Strict C11 mode does not reserve those names. Existing C source may still use
them as identifiers or typedef names. Invalid mixed specifiers fail at the
second token, leave no partial type behind, and allow another parse in the
same job. With the Cupid profile selected, unchanged
`kernel/cpu/simd_intrin.h` publishes all 29 intrinsic bindings. ADR 0225
records this boundary.

### Hosted driver language selection

The hosted `cupidc` command accepts `--cupid` for source that uses the shared
frontend's Cupid vocabulary. The switch selects Cupid mode in both the
preprocessor and parser, so native integer and SIMD spellings work without a
private test harness. `--gnu` remains an independent extension switch and may
appear on either side of `--cupid`. Doom compatibility is a separate source
profile and cannot be combined with Cupid mode. Without an explicit language
switch, the hosted driver keeps its C11 default. ADR 0270 records this command
boundary.

### Private typedef declarators

Private JIT, AOT, and persistent REPL source accepts comma-separated value and
pointer aliases. One-dimensional fixed-array aliases keep their count and
element type in automatic, global, block-static, structure, class, and REPL
storage. Array parameters decay to element pointers, while `sizeof` keeps the
complete object size. An array field keeps that size and a record element's
identity through `.` or `->`; indexed reads and writes may continue to another
member, even when the outer record is itself selected from an array. The
compiler rejects an unsized or multidimensional alias and the other
unrepresented array-declarator combinations before it publishes an incorrect
layout.

### Unsigned 32-bit operations

Private `unsigned int` values retain their type through objects, pointers,
calls, enums, unary operations, and usual arithmetic conversion. Comparisons,
division, remainder, and right shift use unsigned i386 behavior. `/=`, `%=`,
and `>>=` keep that signedness and evaluate the destination once. Conversion
to `float` and `double` is exact across the sign boundary, including ordinary
and method returns. Values in C's defined interval convert back from either
floating width through casts, initialization, assignment, fixed arguments,
and returns. Conditional expressions use both arms to choose their integer
type, and `sizeof` produces unsigned `size_t`. The 40 kernel calls declared
with unsigned-word results publish the same type. ADR 0249 records the
conversion and mutation boundary. The feature-13 guest checks four conversion
boundaries, signed and high-bit unsigned `%=` results, and one evaluation of a
side-effecting destination. Its boot marker is `[feature13-unsigned] PASS
conversions=4 remainders=2 once=1`.

### Runtime unary signs

Unary plus accepts `char`, `int`, `float`, and `double`. It keeps the value
and applies the usual integer promotion to `char`. Unary minus uses integer
`NEG` for `char` and `int`. For `float` and `double`, it changes only the
IEEE-754 sign bit, preserving signed zero and every other payload bit.

Other operand types produce
`unary sign requires an arithmetic scalar operand`. The compiler rolls that
failed expression back, so the next REPL expression can still compile. The
four-CPU frontier allows this deliberate diagnostic only during the
`feature13_double.cc` check. A stale or repeated copy is still treated as a
compiler failure.

### Runtime scalar comparisons

The private JIT and AOT compiler accepts `==`, `!=`, `<`, `>`, `<=`, and
`>=` for scalar `float` and `double` values. Matching widths compare
directly, while a mixed-width pair compares as `double`. Each operator
returns `0` or `1` as an `int`.

Signed zero compares equal to positive zero. If either input is NaN, `!=` is
true and the other five relations are false. Floating operators reject
pointers, aggregates, function pointers, and SIMD vectors instead of
converting their storage bits.

### Runtime scalar truth

The private compiler accepts `float` and `double` wherever C requires a
scalar truth value. Unary `!`, `if`, `?:`, `while`, `for`, and
`do ... while` all use the same conversion. Positive and negative zero are
false. Every finite nonzero value, either infinity, and every NaN are true.
The normalized result lives in EAX before the existing branch code uses it.

Integer and pointer conditions keep their existing EAX path. Void
expressions, structures by value, `float4`, and `double2` fail with
`truth test requires a scalar operand`.

Kernel calls participate in the same type rules. Each of the 557 registered
bindings carries its declared result type. The table contains 326 value calls
and 231 `void` calls. Its values split into 208 promoted integers, 41 unsigned
words, 25 `float`, 25 `double`, 19 character pointers, and eight other
pointers. A result from `input_dialog`, for example, can control an `if`
without losing its integer type, while a high-bit `htonl` result keeps
unsigned comparison.

The table includes the 46 effects, bitmap-font, transform, GUI, and theme
bindings that were missing for `gfxgui_test.cc`. Three accessors return
pointers to the built-in constant themes. The remaining entries call linked
kernel implementations directly. All 107 runnable top-level programs pass
private AOT compilation. The fixed guest frontier runs the graphics test as
both an ELF and private JIT program, then verifies nested-owner cleanup after
voluntary exit and remote kill. A delayed request for the old lifetime must
skip the reused PID before the replacement's helper kills it; the next ELF
graphics run reuses that PID. It requires an
exact custom-font pixel, an isolated blurred-surface pixel with unchanged
screen state, center and off-center transformed-image pixels, an off-origin
rotation and scale point, identity after popping the transform, frame 240,
cleanup, and clean JIT return. The later GodSong command waits for its own
settings line and the popup's post-acquisition input marker. It uses no timed
settle or startup-only graphics diagnostic.

### Floating variable updates

Scalar `float` and `double` lvalues support prefix and postfix `++` and `--`.
Each form adds or subtracts exactly 1.0 at the operand's own width. Prefix
expressions return the stored value. Postfix expressions return the exact old
payload, including negative-zero and NaN bits, after storing the update.

The same typed path handles direct locals, parameters, and globals as well as
pointer dereferences, fixed-array elements, direct and pointer members, and
members of indexed records. A derived designator is evaluated once. The path
also works in standalone statements and `for` increments. Aggregate values,
incomplete array rows, function pointers, `float4`, and `double2` are not
scalar update targets. The feature-13 gate requires
`[feature13-indirect-update] PASS score=41 once=3 zero=0x80000000`, compiles
`/bin/feature13_derived_aot.cc`, runs the resulting ELF, and requires
`[feature13-derived-aot] PASS score=41 once=2 zero=0x80000000` before the same
loaded PID exits.

### Mixed-width function calls

Private CupidC uses one cdecl layout for direct calls and both method-call
forms. Integers, pointers, function pointers, `float`, and the implicit method
`self` value occupy four bytes. A `double` occupies eight bytes with its low
word at the lower stack address. A fixed `float4` or `double2` parameter
occupies one inline 16-byte slot in low-address word order.

Arguments are still evaluated from left to right. The compiler then arranges
their complete words at increasing addresses in source order. Callees use the
same widths when they assign parameter offsets, and callers reclaim the whole
outgoing area. Vector parameters are loaded with `MOVUPS`, so mixed slots may
be packed at four-byte granularity. This private ABI does not promise 16-byte
call-site alignment. Matching vectors return through XMM0. A parameter is an
independent 16-byte copy, and a const parameter cannot be modified.

A fixed SIMD prefix may precede scalar variadic values. SIMD values in the
variadic tail and unprototyped SIMD calls are rejected because no fixed type is
available. Stored function pointers still erase their parameter and result
signatures, so SIMD arguments and returns through them fail explicitly. Scalar
function-pointer calls keep their existing source-width behavior. Named SIMD
intrinsics continue to lower inline. `feature13_double.cc` retains its ten
mixed-scalar calls, while `feature14_simd.cc` checks six nested vector calls.

A fixed `int` or `unsigned int` parameter may also receive a represented
object pointer as one unchanged i386 word. Narrow and floating destinations
remain rejected, and the existing represented pointer-category rule is
unchanged. The unchanged `/bin/ctxt.cc` call to `ctxt_parse_action` reaches
this coercion boundary. The file is an include fragment, and
`/bin/notepad.cc` includes it completely and passes private AOT compilation.
ADR 0230 records the rule and recovery contracts.

### Arrays

Fixed-size arrays, both local (stack-allocated) and global (data section):

```c
// Global arrays - stored in data section
int scores[100];
char buffer[256];
double samples[32];

void main() {
    // Local arrays - stack-allocated
    int arr[10];
    char buf[64];
    float weights[8];

    arr[0] = 42;
    buf[0] = 'A';
    weights[0] = samples[0];
}
```

Array elements are accessed with `arr[i]` and can be assigned with `arr[i] = value`.

Compound assignment also works: `arr[i] += value`, `arr[i] -= value`, `arr[i] *= value`, `arr[i] /= value`.

The private compiler keeps the declared element type on one-, two-, and
three-dimensional fixed `float` and `double` arrays. Global, local,
block-static, and persistent REPL arrays share the same checked allocation and
typed SSE access. Each subscript scales by the remaining row size, and
`sizeof(array[index])` reports that row without evaluating the index.
Depth-one floating pointers retain their pointee width through address
expressions, returns, array-parameter decay, dereference, subscripting, direct
pointer updates, assignment, and floating increment or decrement. Structure
and class objects, their arrays, and their pointers retain scalar floating
fields and one-dimensional fixed floating field arrays. Pointer, index, and
member updates evaluate the derived destination once, and postfix keeps the
old raw payload. Deeper floating pointers, pointer-to-array types, and
assignment through a pointer-valued floating field subscript remain
unsupported.

Fixed `float4` and `double2` arrays with one, two, or three dimensions use a
16-byte vector leaf in global, local, block-static, and persistent REPL
storage. Declared rank remains separate from byte stride, so an inner extent of
one cannot collapse a row into a vector leaf. Outer indexes scale by the
complete remaining row or middle slice. The final access uses unaligned-safe
packed moves, so automatic arrays do not depend on accidental stack alignment.
Plain assignment and `+=`, `-=`, `*=`, and `/=` retain the vector type,
evaluate every index once, and allow `.x`, `.y`, `.z`, or `.w` lane access
where the type permits it. Row and vector `sizeof` keep their complete sizes
without evaluating an index. Incomplete rows are rejected instead of escaping
as untyped pointers. Direct whole vectors and fully indexed leaves support
prefix and postfix `++` and `--`. The update broadcasts an exact one to every
lane, evaluates an indexed destination once, and writes through the retained
address. Prefix returns the new vector. Postfix restores the exact old 128-bit
payload after the store. Const qualification is retained through typedef
aliases. Const direct vectors and fixed-array leaves remain readable. Plain
and arithmetic compound assignment, plus prefix and postfix `++` and `--`, are
rejected before a store. Global, block-static, and persistent REPL direct
vectors use complete 16-byte storage. SIMD pointers, record fields, row values,
lane updates, and computed vector updates remain outside this boundary.
Fixed-prototype direct functions and methods use the cdecl vector boundary
described above. Matching vectors also support direct `+`, `-`, `*`, and `/`
expressions. Every direct operation keeps the written left value in the
machine destination. MIN and MAX intrinsics keep the second operand for NaN
and equal signed-zero inputs. For a both-NaN ADD or MUL, the processor or
emulator may preserve either input payload.
ADR 0294 records this whole-vector update path, and ADR 0299 records fixed SIMD
calls.

Array bounds at file scope and inside structs accept constant integer
expressions, including enum values and simple arithmetic. That keeps
feature-test code like `int table[BASE + EXTRA];` source-compatible
with normal C examples without adding a full C preprocessor.

### Structs

User-defined composite types with named fields:

```c
struct Point {
    int x;
    int y;
};

struct Rect {
    struct Point origin;
    int width;
    int height;
};

typedef struct Pair {
    int left;
    int right;
} Pair;

typedef Pair *PairPointer;

void main() {
    struct Point p;
    p.x = 10;
    p.y = 20;
    print_int(p.x);

    // Heap-allocated structs via pointer
    struct Point *hp = kmalloc(sizeof(struct Point));
    hp->x = 100;
    hp->y = 200;
    print_int(hp->x);
    kfree(hp);
}
```

**Struct features:**
- Up to 64 named struct types, each with up to 32 fields
- Fields use represented integer, floating, pointer, fixed-array, and complete
  nested-record types, subject to the SIMD restrictions below
- Stack-allocated structs (`struct Foo s;`) are zero-initialized
- Heap-allocated structs via `kmalloc(sizeof(struct Foo))`
- Member access with `.` (value) and `->` (pointer)
- Member addresses with `&value.field` and `&pointer->field`; the pointer form
  loads the pointee before applying the field offset
- Chained access: `rect.origin.x`, `ptr->origin.y`
- Fields use their represented alignment; final record allocation rounds to a
  4-byte boundary
- Private JIT, AOT, and persistent REPL source may use anonymous or tagged
  structure typedefs. Alias chains and pointer aliases keep the structure
  layout. Declarations may contain several value or pointer aliases. The
  private typedef table holds sixteen aliases in total.
- One-dimensional fixed-array aliases keep complete storage in automatic,
  global, block-static, structure, class, and persistent REPL declarations.
  Function and method parameters decay to element pointers, while `sizeof`
  keeps the complete array size. Array members preserve complete size and
  record-element identity through direct, pointer, and indexed outer-record
  access.
- Fixed field arrays require a positive count and a checked byte product.
  Padding, cumulative field size, and final allocation alignment stay within
  the signed parser range. A failed REPL line restores every committed record,
  including a forward tag that the rejected line tried to complete.

### sizeof Operator

Compute the size of a type at compile time:

```c
int a = sizeof(int);           // 4
int b = sizeof(char);          // 1
int c = sizeof(struct Point);  // 8 (two ints)

struct Foo *p = kmalloc(sizeof(struct Foo));
```

### Enumerations

Define named integer constants:

```c
enum {
    RED,        // 0
    GREEN,      // 1
    BLUE        // 2
};

enum Colors {
    BLACK = 0,
    WHITE = 15,
    YELLOW = 14
};

void main() {
    int color = RED;       // 0
    int bg = WHITE;        // 15
    print_int(color);
}
```

Enum values are stored as global integers in the data section. Values auto-increment from 0, or can be set explicitly with `= value` (including negative values).

Enum initializers also accept constant integer expressions, so later
declarations can use enum-derived bounds or bit flags.

### C Compatibility Tokens

CupidC is a small single-pass compiler that accepts several common C spellings
used by larger examples and demos:

- storage/qualifier spellings: `extern`, `inline`, `register`, `restrict`, `const`, `volatile`
- wide type spellings: `long`, `short`, `signed`, `unsigned`, `long long`
- attributes: the private in-OS compiler accepts compatibility spellings; the
  shared bootstrap compiler gives `weak`, `section`, `unused`, and `used`
  canonical declaration meaning, and the current checked seed carries all
  four into production. The checked seed also gives `returns_twice` a
  direct-call control-flow meaning
- labels and `goto` for simple local control-flow cases

Most of these are compatibility front-end features, not a promise of
full hosted C semantics. Generated code still targets the 32-bit flat
kernel ABI.

### Self-hosting compiler path

Cupid OS has a shared CupidC frontend, linear IR, and ELF32 emitter for the self-hosting migration. This path is separate from the in-kernel JIT and AOT compiler described elsewhere on this page. It assigns target-sized i386 stack storage to referenced fixed arrays and records with alignment up to four bytes. One-byte, two-byte, and four-byte integers work across locals, file objects, members, indexed access, conditions, conversions, assignment, mutation, and prototyped, variadic, or unprototyped direct and indirect calls. Narrow loads produce canonical 32-bit values, while stores use the declared byte or word width. Represented cdecl scalar arguments keep four-byte stack slots, and callers and callees normalize narrow results.

The refreshed checked seed preserves canonical GNU `weak`, named `section`,
and `unused` metadata, typed static null pointers, left-to-right comma
expressions, and non-fallthrough known-true loops. Represented function
pointers may cast
to another function-pointer type or to and from a represented 32-bit integer
without changing the target bits. Exact output-only GNU assembly can snapshot
a general register, ESP, EBP, `4(%ebp)`, or EFLAGS into one four-byte object.
These forms have positive and negative frontend, IR, and deterministic ELF32
contracts. The original 20 source-driven roots carried by this seed remain in
production, and eight more strict roots now use the same checked path.

The checked seed accepts GNU `used` and `__used__` on file-scope objects
and functions. Compatible redeclarations merge the flag, and the Linear IR
and object boundaries reject invalid frozen metadata. Current ELF32 bytes do
not change because every represented definition is already emitted. The
generated kernel-symbol source passes deterministic compiler-head emission,
and the checked seed now carries the capability. The normal recipe compiles
`kernel/cpu/ksyms_data.cc` through the checked production wrapper.

The checked seed accepts the exact volatile
`call 1f\n1: popl %0` form used by the stack-trace helpers in
`kernel/lang/as.cc` and `kernel/lang/cupidc.cc`. It requires one modifiable
four-byte integer `=r` output. The shared x86 model emits a
zero-displacement `CALL` followed by `POP r32`, which captures the address of
the pop while restoring ESP. Both unchanged roots compile reproducibly as
validated i386 ELF32 objects under the full kernel profile. `as.cc` produces
148,056 bytes, and `cupidc.cc` produces 288,180 bytes. Their normal recipes
now use the checked seed. Other call templates and general inline-assembly
labels remain unsupported.

The checked seed handles the two exact FXSAVE statements in unchanged
`kernel/core/process.cc`. The volatile `fxsave (%0)` form accepts one
four-byte object or `void` pointer `r` input and one `memory` clobber. Linear
IR evaluates the pointer once, and the shared x86 model emits `0F AE 00` at
`[EAX]`. The complete source compiles twice to the same validated
30,216-byte ELF32 object, with one FXSAVE in each process-creation path. The
normal build now compiles `process.cc` with the checked seed.

The shared path also carries signed and unsigned eight-byte integer values. Full-width constants, matching conditional arms, fixed direct and indirect call results, object loads, declared parameters, and named call arguments use one Linear IR handle backed by a private eight-byte frame snapshot. File objects, block statics, fixed automatic objects, pointer dereferences, ordinary members, and indexed elements can be initialized, loaded, assigned, mutated, chained, discarded, and returned. A declared wide argument occupies eight cdecl stack bytes, and later parameter addresses include its full width. The return boundary places the low word in EAX and the high word in EDX. Addition, subtraction, multiplication, division, remainder, unary plus, unary minus, bitwise complement, left shift, signed or unsigned right shift, AND, OR, XOR, all six signed or unsigned comparisons, logical not, short-circuit logical operators, conditional selection, structured scalar conditions, signed or unsigned switch dispatch, all ten compound assignments, prefix and postfix update, and conversion to or from represented integer widths use the same snapshot path. A wide switch evaluates its condition once, duplicates the snapshot handle, and compares both words of each case value. Wide mutation evaluates the destination once and keeps one semantic load and store. Multiplication combines the full low-word product with the low halves of both cross-word products. Division and remainder use a fixed 64-step restoring loop over unsigned magnitudes, then apply the quotient or dividend sign. Each multiplication, division, or remainder result receives a fresh snapshot. The usual arithmetic rules can convert `signed long long` to `unsigned long long`, and GNU wide enums promote to their compatible signed or unsigned wide type. Exact source guards cover `ctool_buffer_put_le64`, `ctool_buffer_patch_le64`, `pp_if_value_truth`, `pp_if_is_negative`, `pp_if_signed_less`, `pp_if_signed_magnitude`, and X25519 `fe_carry`; focused fixtures lower and emit the required operation shapes. The unchanged `cfront_constant_apply_binary` body guards signed and unsigned quotient and remainder. CupidASM's number parser and unary expression branch guard the arithmetic, while X25519's `fe_mul_u32` helper guards wide-by-narrow multiplication. Runtime cases that C leaves undefined promise neither a trap nor a result. Signed and unsigned wide integers can also pass through an ellipsis or a call without a prototype.

The shared path carries `float` and `double` values through objects,
assignment, calls, variadic reads, and returns. It supports conversion between
the two widths, matching or mixed arithmetic, conditional values, compound
arithmetic assignment, default argument promotion, `va_arg(double)`, and all
six comparisons. A mixed `float` and `double` pair compares as `double`.
Every represented signed or unsigned integer through 64 bits may convert to
either floating width through a cast, initialization, plain assignment,
return, or fixed argument. Runtime `+`, `-`, `*`, `/`, all six comparisons,
and conditional selection apply the same usual arithmetic conversions. A
conditional converts only its selected integer arm and keeps the floating
type.
The four arithmetic compound operators accept a floating lvalue with an
integer right operand and an integer lvalue with a floating right operand.
The usual arithmetic conversions select `float`, `double`, or `long double`;
assignment conversion restores the declared left type. The compiler evaluates
the destination once and returns the stored value. Represented integer bit
fields follow the same rule. Atomic mixed compound assignment remains
unsupported.
`UCOMISS` and `UCOMISD` produce a normalized signed `int` and handle unordered
operands so only `!=` is true for NaN. Decimal constants are published as
exact IEEE bits. Static-duration scalar and aggregate leaves use integer-only
IEEE binary32 and binary64 evaluation for unary signs, addition, subtraction,
multiplication, division, comparisons, casts, scalar truth, short-circuit
logic, and conditional selection. Represented file and block enumerators and
signed or unsigned integers through 64 bits can feed the evaluator. It rounds
each operation to nearest with ties to even and preserves signed zero before
the object reaches `.rodata`, `.data`, or `.bss`. Runtime integer inputs
through four bytes use the SSE conversion path. An eight-byte input uses x87
`FILD`, including the unsigned 2^64 correction, then stores at binary32 or
binary64 width. Floating-to-signed conversion and floating-to-unsigned
conversion through represented four-byte targets keep their established
target paths. Unsigned four-byte input and output use exact splits across the
sign boundary. The x87 transport model, SSE conversion
oracle, and comparison execution oracle check rounding, operand order, signed
zero, infinities, quiet and signaling NaNs, call alignment, and frame state.
Source-head hosted CupidC forms each decimal `float` or `double` ratio in a
private 1536-bit integer workspace and rounds once at the requested IEEE width.
The public frontend, Linear IR, and ELF32 contracts cover both halfway
parities, minimum subnormal and normal values, maximum finite values, infinity,
signed underflow zero, extreme exponents, and the 95-character token boundary.
The checked seed predates this source-head change.
Non-atomic `long double` values now use twelve-byte target objects and x87
80-bit memory loads and stores. Bounded finite normal decimal `L` tokens
round an exact integer ratio to a 64-bit explicit significand with ties to
even. The emitter writes the significand and positive token's biased exponent
as three exact snapshot words. Automatic values use frame snapshots.
Static-duration scalars, fixed arrays, and complete records may contain
long-double leaves. Implicit initialization zeros the complete object; an
explicit leaf accepts a represented integer constant expression or a
bounded decimal `L` literal with parentheses and unary signs. The ten x87
value bytes stay exact, the two padding bytes are zero, and the object uses
`.bss`, `.data`, or `.rodata` according to its payload and qualifiers. Atomic
leaves fail recursively without following pointers. Conversions among `float`,
`double`, and `long double`, unary plus and minus, and addition, subtraction,
multiplication, and division work on that path. Direct and indirect fixed,
variadic, and unprototyped arguments occupy twelve cdecl bytes.
Functions return the value in x87 `ST0`, and direct or indirect callers store
it in a twelve-byte snapshot. `va_arg(long double)` copies twelve bytes and
leaves the cursor at the following four-byte slot. All six comparisons accept
matching long-double operands or a mixed `float` or `double` input. A balanced
`FUCOMIP` sequence preserves signed-zero and unordered behavior. Runtime
`float`, `double`, and automatic `long double` values work with unary `!`,
`&&`, `||`, the controlling operand of `?:`, the conditions of `if`, `while`,
`do`, and `for`, and conversion to `_Bool`. Both signed zeros are false; finite
nonzero values, subnormals, infinities, and NaNs are true. Runtime conversions between
`long double` and signed or unsigned integers cover every i386 width. The
emitter restores the complete x87 control word after truncating integer
output. Unsigned 64-bit corrections use 64-bit x87 precision while retaining
the caller's rounding mode, then restore the saved word before the result
store. Runtime arithmetic, all six comparisons, and conditional selection
apply the same conversion to every represented value integer and enum. Linear
IR records a usual-arithmetic conversion to `long double`, and a conditional
converts only its selected arm.
Static initializer conversion covers `_Bool`, plain `char`, each signed or
unsigned i386 integer width, and an enum whose compatible integer type has the
represented target layout. For a nonzero magnitude `M` with bit width `w`, the
frontend publishes significand `M << (64 - w)`. It writes exponent
`0x3fff + w - 1` and the source sign into the high word. For integer
destinations other than `_Bool`, the reverse path truncates the decoded x87
significand toward zero before the range check. `_Bool` tests the original
floating value: both signed zeros are false, and every represented finite
nonzero value is true. The shared fixture converts `-0.5L` to both targets, so
it becomes true for `_Bool` but zero for an unsigned integer. Integer-valued
zero keeps a `ZERO` initializer record. Linear IR requires each unwrapped
primitive base to have a recognized standard integer kind with Cupid's
canonical target size, signedness, and alignment. The declared wrapper must
match its unwrapped base on size, signedness, integer, object, and completeness
flags. An enum's compatible type must also have a recognized standard integer
kind. The enum, its unwrapped base, and its compatible type must agree on those
five fields and on alignment. A `QUALIFIED` node copies referenced alignment
unless it introduces `_Atomic`. An atomic introduction at any layer raises
alignment to at least the target atomic alignment. An `ALIGNED` node
requires an explicit, nonzero power-of-two alignment and may lower the
referenced alignment. `_Bool` has one payload bit; other kinds use their full
target width. The same check runs during whole-unit initializer ownership and
block-static declaration lowering. Static long-double truth, all six
comparisons, short-circuit logic, conditional selection, and floating-width
conversion use a shared target-only decoder. It accepts canonical x87 zero,
subnormal, normal, infinity, and NaN payloads and rejects pseudo encodings.
Folded values become final initializer records and add no runtime IR.
Checked-seed CupidC folds static long-double `+`, `-`, `*`, and `/` with a
separate unsigned 128-bit target packer. It rounds once to the explicit x87
significand, handles gradual underflow and canonical special results, and also
leaves no runtime IR.

Private runtime floating comparisons use `SETP` and `SETNP` when they merge
the x86 parity flag into a normalized Boolean. Checked-seed CupidDis names
both byte predicates from the shared catalogue, so a `dis` listing stays
aligned through the following merge and `MOVZX` instructions. The guest uses
the bounded `test_fpaug.cc` cases for inspection and execution, then retains
feature 13 for the broader comparison and truth behavior. In GUI mode, the
shell writes the listing to the terminal and mirrors it to serial only after
the usual sink and redirection checks. That gives the runtime gate production
CupidDis evidence without duplicating ordinary text-mode output.
Hexadecimal floating literals, hexadecimal or subnormal long-double literals,
long-double decimals beyond the bounded ratio parser, other floating-to-wide
conversions, atomic floating compound assignment, atomic and `long double`
increment or decrement, SIMD values, and over-aligned object emission remain
unfinished.
ADR 0229 records the exact decimal representation and automatic object proof.
ADR 0250 records runtime conversion to unsigned four-byte targets. ADR 0251
records exact static long-double data. ADR 0253 records runtime conversions
between `long double` and integers. ADR 0254 records static initializer
conversion. ADR 0255 records static controls and finite width conversion.
ADR 0256 records canonical x87 classes and special floating-width conversion.
ADR 0259 records the shared parity predicates. ADR 0260 records static
long-double arithmetic. ADR 0287 records the first source-head conditional
conversion between represented integers and `float` or `double`.
ADR 0288 records runtime integer and long-double arithmetic, comparisons, and
conditional selection.
ADR 0289 removes the four-byte integer limit for ordinary `float` and `double`
conversion and usual arithmetic. ADR 0293 records exact source-head decimal
`float` and `double` literals.
ADR 0296 records mixed arithmetic compound assignment.

Plain assignment, all ten compound assignments, and prefix and postfix update work for represented non-atomic integer bit fields when the declared storage unit is four bytes and fits inside the record. The compiler evaluates the record designator once and applies the target's integer-promotion rules before a compound operation. Partial fields preserve the other bits in their unit. Assignment, compound assignment, and prefix update return the stored lane after width truncation and signed extension, while postfix update returns the extracted old value. A 32-bit field uses the direct load and store path. Volatile 32-bit updates perform one read and one store. Partial volatile mutation, atomic fields, and other storage-unit sizes remain unsupported.

The shared path lowers explicit casts to `void`. It evaluates the operand once, discards a represented integer, pointer, supported structure, or floating result, and leaves a `void` operand off the abstract stack. An eight-byte integer or `double` lvalue is copied into its snapshot before that handle is removed; a `float` keeps its raw four-byte value.

Automatic initializer lists work for fixed arrays and complete structures whose alignment does not exceed four bytes. The lowerer zero-initializes the full object once, then evaluates explicit integer, pointer, supported structure, and narrow character-array string leaves in source order. Scalar and structure values store through the selected array and member path. A string leaf copies the retained bytes with `REP MOVSB`, so unused tail elements keep their implicit zero value. Nested lists, direct designators, and omitted subobjects retain their frontend meaning. The i386 emitter performs the initial zeroing with `REP STOSB`.

The shared path carries compatible, complete structure values through lvalue conversion, automatic expression initialization, plain and chained assignment, matching conditional expressions, casts to `void` and other discarded expressions, fixed direct and indirect calls, parameters, and returns. Each lvalue conversion and structure call result receives its own frame snapshot. Copies cover the full target object size, so a supported structure may contain wide or floating members even when an operation on that member remains outside the same-kind arithmetic boundary.

Structure arguments occupy inline cdecl stack storage in parameter order, and each argument is rounded up to four bytes. The caller clears ABI padding before it copies arguments. A structure-returning call passes a hidden destination pointer before the explicit arguments. The callee reads that pointer at `[ebp+8]`, reads its first explicit argument at `[ebp+12]`, copies the result into the destination, returns the pointer in `eax`, and uses `ret 4`. The caller cleans the explicit argument bytes.

Structure snapshots can contain nested unions because a copy preserves every target byte. A scalar member can also be loaded directly from a returned structure value. A union used directly as a parameter or result and an aggregate member selected from a structure rvalue remain unfinished.

The hosted cast path accepts a direct four-byte integer literal zero as a
represented function-pointer null. It also preserves an object-pointer null
written as `((void *)0)` when the frontend converts it to the destination
pointer type. Represented function pointers may cast between signatures or to
and from a represented 32-bit integer. Object pointers can convert to signed
or unsigned eight-byte integers with a zero high word, and conversion back
keeps the low word. Object-pointer and function-pointer interchange, narrow
integer forms, and conversions between function pointers and wide integers
remain unsupported.

Static compatible character and void pointers accept an ordinary string
literal through parentheses or a macro. Checked-seed CupidC also preserves a static
string or linked binding address through an explicit cast between non-atomic
pointer types. A cast through an integer remains unsupported. Pointer
qualification accepts the safe `char **` to `char *const *` conversion. It
rejects `char **` to `const char **`, which would add a qualifier at an unsafe
nested level, and rejects removing the nested `const`.

An external array may omit its bound when its element type is complete. The shared IR can take that linked object's address, decay it to the compatible element pointer, apply the element scale, and continue through member access. The array remains incomplete, so it cannot be loaded as a value or used as if its storage size were known.

The exact hosted gate checks 42 strict C11 roots and three GNU-enabled runtime
roots under four-byte i386 targets. It covers the 19-source static Linux tool
union, `kernel/lang/as_elf.cc`, the runtime implementation and probes, and all
fifteen Linux Toolchain contract programs. `HOSTED_I386_LINUX` owns 33 strict Linux
roots that can include only the Toolchain tree and the angle-only hosted
declarations. `HOSTED_I386_WINDOWS` owns six roots with `_WIN32=1`: the host
adapter, four platform-sensitive driver mains, and CupidLD's publication runtime.
`FREESTANDING_I386` owns the headerless Windows command probe.
The GNU profile is limited to the Linux runtime, its probe, and the Windows
runtime wrapper.
`HOSTED_I386_KERNEL_BRIDGE` owns the assembler ELF adapter and its contract,
which may also include `/kernel/lang`. The retired 64-bit hosted audit
profiles have no active roots.
Stage-three and stage-four CupidC emit the contract objects, CupidLD links the
static executables, and the harness rejects a cross-stage byte difference in
any of the seventeen new objects or sixteen executables. It runs and publishes
the stage-four cohort. Publication accepts
only a dedicated `cupidc-contracts` directory inside the source tree and
checks the target before work and again before promotion. An existing
destination must already verify as a complete cohort. Arbitrary directories,
source trees, files, and symbolic links remain untouched. The initial,
private, and newly discovered contract inventories must match exactly, which
catches added or removed inputs and restored edits that changed a copied
file. Every run derives its cohort from the requested executable, requires a
named manifest artifact, and verifies the complete cohort, live 67-input
contract set, checked seed manifest, and 50-file fixed-point source inventory
before execution. The contract set includes the user syscall ABI contract and
its six declarations, the Toolchain Makefile, the publisher, and the
independent Python ABI oracle. Seed-manifest hashing, JSON decoding, schema validation, and
build-plan use share one captured byte sequence.
The runner copies the verified cohort before execution and rejects later live
replacement. The user ABI contract and Python oracle inspect one shared
six-file snapshot, while the contract also rereads the live source tree.

Fourteen ordinary contracts compile through the bounded worker pool with
900-second plan budgets. That pool closes before `cupidc-object` compiles alone
with a 1,800-second budget. The hosted runtime compile and the parallel link
pool keep their 360-second limits. Timeout errors name the stage, source, and
budget. ADR 0282 records this policy.
The staged kernel ELF contract carries the same `as_elf`, CupidLD, CupidASM,
x86, and ELF32 closure as its native oracle. A checked link exposed the old
three-object omission after the heavyweight compile, then failed safely before
publication. The plan now has direct closure coverage.
The contract manifest carries the stage-three and stage-four convergence pair
from the four-generation bootstrap. A complete 4,480.3-second private rebuild
exposed the stale verifier only after every compile, link, comparison, and
runtime check passed. The failed check published nothing, and exact positive
and wrong-pair tests now cover the record.
The final supported gate passed in 4,589.9 seconds. It compared stage-three and
stage-four contract outputs, ran and published stage four, verified 21
artifacts from 65 inputs, passed the hosted runtime and syscall ABI, and
matched the hello, ls, and cat objects and executables built by native Windows
CupidC and CupidLD to checked-seed output. The warmed path passed in 12.2
seconds.
The test programs stay outside the 19-source fixed-point plan because they do
not contribute to a tool image.

The repository runtime supplies the checked file, heap, memory, string,
`errno`, `getcwd`, fixed-width integer, and formatted-output interfaces
required by the five commands. The public slice includes `printf`, `puts`,
`snprintf`, `fputc`, `fputs`, `memmove`, and `strstr`. CupidC emits the
runtime and the `.cc` behavior contract, CupidASM assembles `_start` and the
system-call boundary, and CupidLD links five deterministic static Linux i386
tools plus the contract without unresolved symbols. The runtime probe checks
allocation, tail release, files, seeks, errors, arguments, formatting, memory
comparison, and strings. Its formatter covers signed and unsigned `long long`
values, padded 64-bit hexadecimal output, and fixed or argument-supplied
string precision. The runtime has unbuffered streams and single-threaded heap,
stream, and `errno` state. ADR 0195 records the probe rename, and ADR 0196
records the full contract transfer.

The same runtime source now has a Windows edge selected by
`CUPID_RUNTIME_WINDOWS`. It parses the native command line, allocates through
`VirtualAlloc`, separates stdout and stderr, and implements file reads,
writes, seeking, current-directory lookup, and useful `errno` values. CupidASM
supplies the process entry and cdecl bridges to imported Windows APIs. CupidLD
links that runtime with all five hosted closures. Its own image adds `_fullpath`
and four publication imports for exclusive creation, durable flush, atomic
replacement, and cleanup. At the promoted-seed checkpoint, stages two and
three produced matching PE images. Windows ran help plus a useful success and
failure path for every tool, and
CupidDis also checks quoted raw-input parity with the Linux tool. ADR 0268
records the shared runtime, and ADR 0269 records CupidLD publication.

The `cupidc` driver compiles one input to an ELF32 object. C11 is the default;
`--cupid` selects the Cupid language profile. It accepts definitions,
undefinitions, forced inputs, GNU or freestanding mode, and ordered include
roots. `-I` enables quoted and angle lookup;
`--include-angle` enables angle lookup only. Repeatable `-include` options run
before the primary source in caller order. These path options accept native
paths or absolute logical paths under `--root`. A compile failure preserves
the previous output.

Empty volatile extended assembly with one `memory` clobber remains an IR
ordering point and emits no instruction bytes. The explicit `--doom-compat`
switch gives the five audited calls in `i_system.cc` old-style
`extern int name()` declarations and permits eleven audited, bit-preserving
conversions between unqualified function pointers and unqualified four-byte
data or `void` pointers. Strict C and plain GNU mode still reject those
implicit conversions, and explicit function/data casts remain outside Linear
IR. One-active-member union initialization compiles `info.cc`, while
ordinary narrow bit-field promotion compiles `i_video.cc`. The
checked seed emits all 80 audited Doom-tree objects.

The production wrapper completes the three-root compatibility frontier. It
retains the explicit static string cast in `doom_libc_stubs.cc` and emits the exact
`dg_setjmp` and `dg_longjmp` block through Cupid's x86 model. Two seed compiles
produce byte-identical objects for all three roots. All 83 sources use `.cc`
and the normal graph compiles them through the checked seed. The wrapper fixes
the exact source memberships, freezes all 291 profile headers, and rechecks
the visible `.c` and `.cc` tree before publishing each object. A legacy `.c`
file or unlisted `.cc` file fails the closed scan. The validator also accepts
the two static-subobject `R_386_32` addends of 4 in `g_game.cc`, while
direct-call `R_386_PC32` relocations remain fixed at -4. The active object is
52,004 bytes with SHA-256
`51aff2138ff2ee51bae9cc18e1dcc415567c6be1699ef0ef6f1ed2b009c30df1`.
The 67,155-byte dglibc source produces a 93,332-byte object with SHA-256
`e2496b01c93a7858a0c035b53aea0ad834d95d2be3f7ae49574d1759ebec34d6`.
Repeated compatibility compiles also reproduce the 17,084-byte libc-stub and
10,352-byte platform objects. The 69,366-byte closed profile manifest has
SHA-256
`47ba35158cac0a7df253a0056235223e62fee24df74701800f88763e588611c2`.

Checked-seed CupidObj carries the deterministic `profile-manifest`
operation. CupidC compiles its freestanding SHA-256, bounded `CUPROF1` parser,
portable identity checks, canonical sorter, and JSON emitter into a
220,508-byte object. The poisoned-host rebuild matches all stage-two and
stage-three objects and tools, and both stages pass five help cases, fifteen
successful operations, and thirteen useful failures. The normal wrapper
derives its snapshot and independent Python oracle from one stable capture,
runs CupidObj from the exact frozen seed, and requires byte parity. It rechecks
the seed, profile inputs, candidate, output directory, and existing output
under an adjacent no-follow lock. Identical bytes retain their timestamp;
changed bytes publish atomically. CupidObj authors the production bytes, while
Python retains the host transaction. ADR 0242 records the source capability,
ADR 0243 records seed carriage, and ADR 0244 records production ownership.

Active dglibc uses the corrected form. Its 31-byte `dg_setjmp` saves the
caller's post-return `ESP + 4` and is declared `returns_twice`; `dg_longjmp`,
`dg_exit`, and `dg_abort` are declared `noreturn`. The checked compiler retains
the historical compatibility form for reproducibility.

A marked function must remain a direct call target. Supported calls use
four-byte cdecl arguments and may return void or any nonaggregate type. At each
live-prefix call, the emitter saves the live four-byte expression operands in
call-owned slots and restores them after cleanup. Branch-exclusive sites use
separate regions. A live-prefix site fails if any returns-twice
continuation can reach it again, while a call with no live prefix may repeat.
Aggregate, wide-integer, and wider-than-four-byte floating arguments,
aggregate results, and marked-function pointer conversions fail explicitly.

A decoder-driven i386 oracle models first and second returns with transfer
values zero and seven. The guest self-test executes active longjmp and exit
landings, then runs two quit and two error sessions to check callback order,
filtering, and cleanup. ADR 0212 records the compiler boundary, ADR 0213 its
checked-seed promotion, and ADR 0214 active adoption.

Earlier gates passed two missing-IWAD launches in one shell on both supported
NICs. The fixed frontier now passes normal discovery, an explicit missing
path, the shell-return marker, and a fresh CupidC-built `ls`, followed by a
separate stateful four-CPU frontier. The latter reaches the diagnostic after
the swap feature has retained a FAT handle, so the handle-exhaustion check
must account for live system state. This remains asset-free evidence, not a
claim about gameplay.

The Doom port also uses production config and save routines through dglibc.
Known relative paths resolve under `/home/doom`, config parsing is bounded,
registered defaults reset between shell sessions, and checked temporary files
commit through native same-mount VFS rename. HomeFS and RamFS reject busy
replacement. Block-cache misses stage incoming bytes before changing a
victim's identity. FAT16 distinguishes missing, handle-pool, I/O, invalid, and
busy opens; blocks replacement of live entries; enforces one-level 8.3 paths;
and publishes a flushed replacement or deletion before freeing the detached
chain. HomeFS rejects malformed containers and duplicate mounts, reserves
`HOMEFS.SYS` against raw FAT mutation, and supports nested mutation batches
whose outer end reports the durable publish result. The guest self-test uses
test-only files to cover these paths. A staged WAD is still needed for
gameplay, input, game audio, menu-driven save/load, and reboot persistence.
ADR 0211 records the storage boundary.

The five static i386 Linux tools have a checked seed. The manifest binds their
hashes, sizes, target ABI, source revision, producer lineage, 19-source plan,
and five link orders. The current CupidC image is the 2,666,324-byte
stage-four output from clean revision
`bf52d135348bc33ff32e66d549bbee5edc69d8ad`, with SHA-256
`8b6b0f0508b1565d095297f3571ef9bb4d444d19be0700165706877b210b087c`.
It retains the complete 83-root Doom frontier, GNU entity metadata, x87 and
SSE forms, descriptor and segment assembly, the `libm.cc` effects, the dglibc
jump block, pointer-preserving static address casts, naked IPI entries, the
kernel-entry BSS clear, and packed SSE2 statements. It also carries runtime
floating truth, runtime and static integer to `long double` conversion,
canonical static x87 payloads, static long-double arithmetic, ordinary
`float` and `double` updates, the returns-twice call boundary, and Cupid's
native type spellings. The same seed carries the 604-row, 249-mnemonic x86
catalogue with fingerprint `55A8970F`, signed x87 integer forms, `SETP`,
`SETNP`, typed CupidDis raw ranges, strict inspection, executable relocation
ownership, and an immutable first-opcode decoder index. Its
CupidLD image carries deterministic PE32 imports. Its 392,688-byte CupidObj
image has SHA-256
`99111b5db7586ac4b2ed00005f2fe2e89c66ed48f007d796206b116a088cdf7a`
and carries the complete installation-source bounds, ordering, and
wrapped-symbol contract, transactional kernel-symbol source generation,
sequential-JPEG validation, pristine disk-template construction, deterministic
ISO fixture authoring, and deterministic `profile-manifest` authoring.
Its plan uses `.cc` for all 19 C roots and has
SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.
The 5,573-byte manifest has SHA-256
`d571125256d11dd707f661299738891edc5c1a8d3358554076875a3e0cac22d0`.
It records generation four, the clean revision, the 50-input source count and
snapshot, and the stage-three producer set. ADRs 0265 and 0280 preserve the
earlier promotion records. ADR 0292 records the current promotion.

The bootstrap copies the 50-input source closure into a private compiler root.
Both rebuilt stages compile from that root, and the harness checks the private
and live closures at each stage and behavior boundary. The clean Linux proof
passed in 1,294.3 seconds. Stages three and four matched every one of the 19
C objects, startup, and five linked tools, and both stages passed all five help
paths, 18 successful operations, and 16 failure cases. The source snapshot has
SHA-256
`e76d36ed4edc7679e91ac237135fe476dff6e69946bbffca56077afbf19a47f9`.
A 1,473.9-second reproof from the promoted manifest reproduced all five
initial seed images, the artifact fixed point, and the 5/18/17 behavior matrix.
The added failure rejects an executable relocation that has no decoded owner.
An earlier clean 801.9-second proof remains the provenance record for the
preceding Windows execution seed. The clean native Windows proof later passed
in 1,253.4 seconds, and the promoted-seed reproof passed in 1,061.3 seconds
with the 5/5/6 matrix. ADR 0266 records the decoder index, ADR 0281 records the
preceding Windows promotion, and ADR 0292 records the current promotion.
The normal kernel path runs strict checked-seed CupidDis and checked CupidObj
flat extraction against one frozen cohort of all 429 audited root object
outputs plus the pass-one and final kernel ELFs. Its 9,076-byte graph-ordered input manifest has SHA-256
`4f1936423ae06418fc2f75603c29a91997608fe82f48c323321523aed25a2ab0`.
The first separate gate for the preceding 429-path cohort passed in 185.526 seconds with empty streams and exit
0. At the next handoff checkpoint, hostbuild froze the selected seed manifest
and all five artifacts, the 431-entry input manifest and cohort, and the
existing `kernel.bin` boundary. Checked CupidDis validated the private cohort
before checked CupidObj flattened the frozen final ELF. Hostbuild rechecked
live trust inputs and the output before parent-relative atomic publication.
Every failure preserved the prior raw kernel. The transaction passed with exit 0 in 187.054
seconds and published an 8,946,332-byte `kernel.bin` with SHA-256
`4f5f2591d01bcc4007773844e9bfb8112a16dd17fbd178014cc2056fefaab67d`.
The focused hostbuild suites each passed 31 tests on Windows and in WSL;
platform-specific cases were skipped on the opposite host. Moving private
flatten extraction onto the shared pinned-path helper remains deferred
maintenance.

The next 2026-08-13 poisoned-host checkpoint completed through the checked
native Windows execution seed. Its first invocation stopped at the 602.5-second
command limit; the resumed build finished in 968.5 seconds, for 1,571.0 seconds
of cumulative work. These outputs superseded the earlier identities below when
the checkpoint was recorded. The
2,560-byte `boot.bin` has SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`.
The 9,056,612-byte pass-one ELF has SHA-256
`e2f63b5cd9c4e2769b9d6bc893ab5cf778951b97aec954ece6cbac0cc429e92a`,
the 9,179,492-byte final ELF has SHA-256
`1bc06263dbf9849e6d2c594b6fb4be2a3f3b673c91f69d23a2d2e639b1f64776`,
and the 8,962,776-byte raw kernel has SHA-256
`3170aa71eafa656b1f6e23c918f1f472860f513c9c5cd0376d7d4f5f8a7d891c`.

The exact-size prerequisite accepted all nine artifacts before publishing the
209,715,200-byte image with SHA-256
`3b5dd6523a90d6ed0543a6ab2464892f3289b876654f9869f88db0901940b91e`.
A four-vCPU RTL8139 frontier passed from this image in 820.7 seconds. Private
CupidC emitted both feature-13 derived-update markers, compiled and loaded the
dedicated external ELF as PID 4, and reported that same PID exiting. The full
SMP, framebuffer, audio, USB detach/replug, and survival checks passed. The
completed dual-NIC checkpoint immediately before this
rebuild used image SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.

The current production checkpoint includes in-kernel CupidLD and the guarded
normal boot edge. A poisoned-host normal build passed in 674.693 seconds
after CupidDis accepted all 431 inputs. The pass-one ELF is 9,211,340 bytes
with SHA-256
`2a6f5deafb580b30254483179d6dade9ed4ed7b17b39f9368137b1ff14932263`.
The final ELF is 9,334,220 bytes with SHA-256
`bc855462c1f8f42e34d94a974443f7c6e565d60b1913e3b6f33b3e6e375f3ed6`,
and the 9,114,084-byte raw kernel has SHA-256
`8b5d73e74538ce11c1fb074f88b3852d690038aa5cb3a8de3ce222e9df88cade`.
The 209,715,200-byte image has SHA-256
`813c9b0c78f795c1ac9fcff59b9c4111a958a07eb1e3943dc7af60c536521110`.
A private four-vCPU QEMU boot reached JIT completion in 49.257 seconds.

The build audit finds seventeen tracked `.c` files outside `TempleOS/` and none
in this active CupidC cohort. It records seven historical copies, three
superseded implementations, one dormant runtime draft, five native host test
fixtures, and one optional host oracle. Renaming a `bin/*.c` copy would activate
it through wildcard discovery, while a fixture rename would silently select
C++ semantics. An active tracked `.c` source assigned to CupidC now fails the
audit. The reverse claim needs independent evidence from a checked compile
edge, the checked Toolchain contract, or an exact runtime-delivery policy entry
with a CupidObj edge. That policy also locks the seventeen residual `.c` paths
and three unreachable `.cc` paths. A `.cc` rename still follows a real checked
build and behavior proof. The safe suffix-only rename set is empty. ADR 0284
records the first gate, and ADR 0291 records the complete provenance rule.
Every audit requires the active ownership evidence, including an audit without
a policy file. A nonproduction audit accepts policy, a recorded source
relation, or an explicit Make exclusion for an unreachable `.cc`. The complete
production graph requires exact policy coverage, while a partial production
view defers that census.
The residual native C evidence passes all 56 kernel, USB, and ELF32 oracle
cases. The process fixture keeps its host language mode while supplying the
four adapters required by the current production cleanup path.

A fresh build of hello, ls, and cat in a unique output directory passed in
10.492 seconds. It reproduced the promoted frontier's six files:

| Program | Object bytes | Object SHA-256 | Executable bytes | Executable SHA-256 |
| --- | ---: | --- | ---: | --- |
| hello | 6,124 | `64e0a6ee0d7a45a0901d3db614e73481cdc6b30903345c5015601b2bf344be04` | 13,992 | `4c5622969f39ffe7c2427d65abae2d293dfbd76db2aa80c96f9e6cf01613600c` |
| ls | 7,120 | `e0627996a1d9cd6fd428642ffdfada7e07afa81d9267bc714360014af0dd3971` | 18,112 | `094b017eb6914bce6fbc1e99adeae845d5dc05280c1c1d897e68ab9d687c8d79` |
| cat | 6,292 | `ff002fc4710704c3941bf6320249e772a3448d15f99269987ab1b9b608b3acb4` | 13,992 | `b66cba4c98221f5006ad4aeee70349a82db20410e027aa863bc33fa5818b5f4c` |

Disposable staged-copy runs returned 0 for hello in 54.546 seconds, ls in
52.637 seconds, and cat in 80.043 seconds. Cat used a 62-byte marker-shaped
fixture and passed the negative serial-event boundary. The source and evidence
images retained the current image hash above.

The earlier poisoned-host normal `make -j2` passed in 1,057.969 seconds with
all eleven host code-generation variables pointed at invalid commands. It ran
the separate strict gate before flattening and produced a 209,715,200-byte `cupidos.img`
with SHA-256
`4548005bd0aa1a3cffb74620c2309d53c6b291ea2505ed187034bf6b13f1bb37`.
Definitive four-vCPU E1000 and RTL8139 boot frontiers passed from that image
with exits 0 in 794.034 and 758.667 seconds. Both passed SMP, frontier,
framebuffer, AC97, and PC speaker checks without changing the source image.
Those boot frontiers remain pre-freeze runtime evidence.

Checked-seed CupidLD includes deterministic fixed-layout i386 PE32 serialization
and canonical imports. That proof uses the same rebuilt tools, plus a
repository Windows startup and a headerless freestanding CupidC contract.
Both CupidC stages emit the same object, and both CupidLD stages link the same
imported image. Windows validates and runs the stage-two image, checks its
exact marker and empty stderr, and requires exit 37. The checked-seed matrix is
5/18/16. Import ordering uses an in-place heap, name imports stay below the
PE32 high-bit boundary, and the independent validator reconstructs the exact
`.idata` cursor. The bootstrap report retains both stages' object and image
hashes and the observed Windows result. The first loader proof did not use PE
output in the normal build. At the promoted-seed checkpoint, source head linked
and ran complete native images for all five hosted tools through the same PE
path. Stages two and three produced matching images, and Windows ran help plus
useful success and failure cases for every tool. CupidDis also checked exact
raw-report parity. CupidLD checked exact linked output,
candidate collision, failure diagnostics, and cleanup. Those images formed
the preceding checked Windows execution seed used by output-bearing production
recipes. The native driver pairs the execution seed with the verified Linux
plan and builds native stages two through four. Stage three and stage four are the convergence
pair. Under the old comparison, a source-stable Windows run stopped safely at
`cupidobj_main` after 821.9 seconds, and Linux stopped at the same transition
after 883.3 seconds. Neither run published. Later uncapped proofs passed the
complete final-pair artifact and behavior gates. Windows finished in 20 minutes
43 seconds, and Linux finished in 24 minutes 22 seconds. Both reports bind the
same 50-input snapshot, SHA-256
`d8481a39e0d1c7f42779a8c9f5fc5de10d7e5b9bc4df63ce6afe9ddd9c9716da`.
Both runs began from uncommitted source and remain preliminary history. Linux
later passed a clean proof in 1,383.775 seconds, promoted the stage-four seed,
and passed a 1,411.998-second reproof from that seed. Native Windows later
passed a clean 1,253.4-second proof and promoted the current stage-four PE32
cohort. The current CupidC image is 2,592,768 bytes with SHA-256
`765fa14724c1615088fb9280a16f3457a4c4f14fa2d1915d3c56ff73b2b797cd`.
The 1,061.3-second reproof from that cohort matched all five initial seed
images and repeated the 20/2/5 artifact and 5/5/6 behavior gates.
ADRs 0247 and 0248 record the format and small loader boundaries, ADR 0258
records seed carriage, ADR 0268 records the shared runtime, and ADR 0269
records CupidLD publication. ADR 0272 records native carriage and production
selection, ADR 0278 records native reconstruction, and ADR 0279 records the
convergence generation. ADRs 0280 and 0281 record the preceding Linux and
Windows promotions. ADR 0292 records the current promotion on both hosts.

The preliminary Linux behavior reconstruction found one Windows-profile
difference. Its 387,584-byte CupidDis image had SHA-256
`ad6147cd426e204756ec8bf52ae85c64fff9ad39b0bc26e5744f3c421be1e9aa`,
while the direct Windows proof produced SHA-256
`07cff807224c425d686e32d54dc1ad541f57aaa624f7b736bba0f9ef5001ce6a`.
The other four tools matched. The reconstructed plan had omitted `_WIN32=1`
from `cupiddis_main.cc`; the corrected profile, parity test, and audit guard
now cover all five driver mains.

Those rebuilt CupidLD images publish ELF and PE output through an adjacent
candidate created with exclusive-create semantics. They write and close the
candidate, reopen the file, check its size and contents, and close the
verification read before one replacement call. The hosted fault harness
preserves a sentinel destination across partial-write, close, verification, and
replacement errors. On POSIX, CupidLD requests mode `0777`; the process umask
may remove any permission bits. The directory must remain stable under the
caller's control; the publisher has no destination lock or directory pin.

The checked seed now carries CupidObj's bounded `iso-fixture` operation. Its
hosted command reproduces the exact 61,440-byte repository image from the
manifest and typed logical inventory. Both rebuilt stages exercise the command
and its preserved-output failure in the 5/18/17 behavior matrix. ADR 0239
records the source capability, and ADR 0240 records the promotion. The normal
ISO recipe now runs that checked image as its first byte author, with Python
retained as the independent renderer and guarded publisher; ADR 0241 records
that handoff.

The normal Toolchain build snapshots 66 contract inputs, including the small
Windows probe, the native Windows tool runtime and startup, CupidLD publication
runtime and bridge, the direct contract, `direct.h`, `windows.h`, the user syscall
ABI contract and its six declarations, the Toolchain Makefile, the publisher,
and the independent Python ABI oracle. It reproduces that exact inventory under a private root and
uses both rebuilt stages for all fifteen contract programs and the runtime
probe. It compares seventeen new objects and sixteen linked executables. Every
invocation verifies its named artifact, the
complete cohort, both source inventories, and the checked seed manifest. The
seed manifest is captured once for its digest, decoded data, schema checks, and
build plan. The ISO source-capability cohort passed in 2,764.533 seconds.
Stage-two and stage-three objects and executables match, the hosted runtime
passes, and all 20 published artifacts verify. Its 18,232-byte manifest has
SHA-256
`8cd0ea08454d9d672e6890e040fce85ba02b2c101c21599aa3933b0d89eee202`.
The manifest records all 50 inputs, the 43-file source snapshot, and the
checked seed. Current hashes are retained in the bootstrap log.
That cohort predates the seed promotion; the post-promotion bootstrap proves
the promoted trust unit independently.
See [Toolchain Bootstrap](Toolchain-Bootstrap) for the commands and report
layout. Native contract runners and hosted development commands are explicit
host-built oracles; normal OS and Toolchain artifacts do not depend on them.

Root image assembly, object, link, and inspection commands use that checked
five-tool seed. Checked production kernel, generated-install, and user CupidC
calls plus checked user CupidLD links use the same runner with their existing
frozen seed capture. The runner verifies the complete live trust unit again
after each command, and Make passes wildcard-discovered output lists through
`$(sort ...)` before generation or link. The repository's runtime JPEG
contains sequential baseline bytes.
Hostbuild freezes the input and asks checked CupidObj `wrap-jpeg` to validate
and wrap the private snapshot. This replaces the old host FFmpeg conversion.
Progressive, unsupported, and malformed frames fail before any candidate is
published. Python checks CupidObj's accepted snapshot independently, requires
the same bytes, rechecks live inputs, and publishes atomically. ADR 0231
records the source capability, ADR 0234 records seed carriage, and ADR 0235
records the production transfer. The first Windows
and Linux comparison
matched 426 of 430 kernel artifacts and
traced the other four to that conversion. The Linux kernel build passed in
607.7 seconds, and the Windows root build passed in 341.6 seconds. All 430
frozen kernel artifacts match byte for byte. A fresh 209,715,200-byte image
has SHA-256
`e815d2ef67f114a26181f0e2cbde85f892cdadd487f8d9cbee9715e720800b3e`.
A private `/bin/ls.cc` JIT boot from that image passed in 49.8 seconds.

Supported direct and indirect calls put ESP on a sixteen-byte boundary immediately before `call`. The emitter chooses zero, four, eight, or twelve bytes of padding from the function frame, live Linear IR stack, and outgoing target-sized argument area. Prototyped, variadic, unprototyped, nested, structure, and wide calls follow the same rule.

For a variadic call, the shared frontend applies lvalue conversion, array and
function decay, integer promotion, and `float` to `double` promotion to the
ellipsis arguments as required. Every call instruction owns a contiguous
slice of post-conversion actual argument types in a packed Linear IR array. A
shared validator requires one complete ordered partition and rejects gaps,
overlaps, invalid types, trailing entries, and metadata on non-call
instructions. Named slots use declared parameter types after compatibility
checking, while unnamed slots use the packed actual types. The emitter uses
the validated slice and actual count for cdecl order, slot widths, the saved
indirect callee, stack alignment, and caller cleanup. Direct and indirect
calls can pass represented four-byte integers and pointers, signed and
unsigned eight-byte integers, existing `double` or `long double` values, or
source `float` values promoted to `double` through an ellipsis. An eight-byte
unnamed value uses two adjacent words. A `long double` value uses three.
Arguments occupy increasing addresses in source order, with lower words
first. Each argument still has one abstract IR handle, and an indirect callee
remains below the argument handles while the emitter prepares the outgoing
area.

In GNU C mode, the shared frontend treats `__builtin_va_list` as a target
`char *` cursor and retains typed start, argument, copy, and end operations.
The i386 emitter starts the cursor after the full width of the final named
cdecl parameter. A four-byte pointer, integer, or enum read advances the
stored cursor by four bytes. A signed or unsigned eight-byte integer, 64-bit
enum, or `double` is copied into a fresh private snapshot and advances the
cursor by eight bytes. A `long double` read copies twelve bytes and advances
the cursor by twelve. All represented widths keep the i386 cursor on
four-byte slot alignment. Execution contracts read successive wide integer,
`double`, and `long double` slots through the original cursor and the first
slot through a copied cursor. The static i386 runtime also reads a four-byte
value immediately after a long double. Nested callers check aligned calls,
cleanup, and complete returned values. Atomic, `float`, and aggregate reads
remain unsupported. Calling `va_arg` with `float` is invalid C because a
variadic `float` arrives as `double`. The unchanged Doom compatibility header
parses under its generated profile.

An empty identifier-list definition has zero parameters and keeps its
non-prototype function type. Calls through a function type without a
prototype apply default argument promotions to every argument. Each call
keeps its actual count and post-conversion type slice in Linear IR, and the
i386 emitter accepts represented four-byte integers and pointers, signed or
unsigned eight-byte integers, existing `double` or `long double` values, and
source `float` values promoted to `double`. The static i386 runtime executes
both direct and indirect unprototyped long-double calls.

Block-scope `struct` and `union` tags follow lexical C scope. The shared frontend handles forward declarations, same-scope completion, ordinary references, nested shadowing, and restoration after a nested block ends. A record tag declared in a function definition's parameter list stays visible through the outer body, then expires with the definition. A tag-only declaration may use the represented `typedef`, `extern`, `static`, `auto`, or `register` spelling, or a represented type qualifier, when it introduces a tag, and has no runtime work. An empty declaration with storage or type qualification cannot merely repeat a visible tag. A `for` initializer may use a visible record type or an anonymous record definition for its object, but it cannot introduce a named tag or omit the object. An anonymous definition can supply the type for a local or block-static object, including Doom's unchanged block-static `packs` array.

A block-scope `extern` object keeps a lexical alias to one canonical linked object. Compatible repeats share identity, an incomplete array can be completed, and a visible file-scope `static` object keeps internal linkage. The declaration creates no automatic storage or runtime IR. Block typedefs also follow the ordinary identifier scope. They retain stable scalar, record, or function types, allow exact same-type repetition and nested shadowing, and create no runtime IR or ELF record.

A block function declaration keeps a lexical alias and visible type alongside one canonical linked function. Plain and `extern` forms share compatible identity. A visible prior declaration contributes to the alias's composite type, but an expired sibling prototype does not change the type seen by a later old-style declaration. A visible file-scope `static` function keeps internal linkage, while a function introduced only in a block stays out of file lookup until a later file declaration publishes it. The declaration emits no runtime IR or storage. Direct calls use `R_386_PC32`, and function addresses use `R_386_32`. Active-source guards cover 27 declarations across nine files. The exact Doom profile still parses all of `kernel/doom/src/d_main.cc`, including its local `forwardmove` and `sidemove` declarations.

Block enums keep each enumerator in the ordinary lexical binding stream. A later enumerator can use an earlier value, nested tags and constants shadow their outer names, and scope exit restores those names. Definitions work in declarations, record members, function-definition parameter lists, and block type names. Function prefixes and expression or initializer activation records preserve the exact point where each name becomes visible. Linear IR checks that lexical order before lowering runtime control flow, including type names in case values, loop headers, variadic reads, aggregate designators, and compound literals. Represented uses become integer constants, so enums need no frame slot, symbol, relocation, or runtime declaration instruction. File and block enumerators also feed static floating arithmetic, comparison, and conditional expressions. This covers the cursor constants in the production CupidC-built `kernel/gui/desktop.cc` object; the REPL limits remain a separate active guard in `kernel/lang/shell.cc`. Block declaration attributes, nested function definitions, nonempty identifier lists, atomic variadic access, aggregate arguments without declared parameter types, and aggregate variadic reads remain unfinished.

Block-static objects use static storage in the shared ELF32 path. The emitter places top-level `const` objects in `.rodata`, writable zero-filled objects in `.bss`, and other writable objects in `.data`. Each object receives a local symbol derived from its absolute block-binding index, so shadowed names remain distinct. `LOCAL_ADDRESS` reaches that symbol through an `R_386_32` relocation instead of an EBP-relative frame slot, and the declaration emits no runtime initialization code. Unused and unreachable block statics still receive storage.

Block-scope compound literals use one persistent unnamed automatic object per source site. Their initializer runs each time execution reaches the expression, and the resulting lvalue can flow through ordinary member access, indexing, address-taking, loads, stores, and calls. Repeated evaluation in one function invocation reuses the same object. Recursive calls receive a fresh object in their own frame. Aggregate lists are assembled in separate staging storage and committed only after every initializer read has finished. A narrow string root zeros and copies directly into its persistent character array.

One-active-member union initializer lists use the same aggregate paths. A positional clause selects the first eligible named member, and a direct `.member` designator selects that member instead. The initializer forest owns one edge for the selection. Runtime lowering zeros the whole union before storing the selected value; static emission writes the member over zero-filled storage. Multiple clauses and override semantics remain unfinished.

Runtime narrow string expressions receive local `.rodata` symbols and `R_386_32` relocations. They can decay into pointers for initialization, arguments, indexing, and returns. Supported structure graphs have alignment no greater than four bytes and contain no stored `volatile` or `_Atomic` subobjects. A graph may contain a nested union, but top-level union and class values remain unsupported.

The shared frontend publishes decimal `float` and `double` constants as exact
IEEE bits. It uses bounded integer arithmetic and rounds once to nearest with
ties to even, so self-hosted compilation does not depend on a host floating
library. A second integer-only evaluator handles static-duration arithmetic,
comparisons, casts, scalar truth, short-circuit logic, conditional selection,
enumerators, and represented signed or unsigned integer conversion through 64
bits. It rounds after each operation at the expression's binary32 or binary64
width and places the final bits, including signed zero, through the ordinary
read-only, writable, or zero-filled policy. The IR and SSE object path cover
represented runtime integer-to-floating conversions, floating-to-signed
conversions, floating-to-unsigned conversions through four-byte targets, an explicit
non-atomic `double` to `unsigned long long` cast, mixed integer and floating
addition, subtraction, multiplication, and division, and all six matching or
mixed-width comparisons. Unsigned four-byte input and output use exact splits
across the sign boundary. Unsigned-wide output splits around 2^32
and derives each word through a 2^31-safe truncation.

Non-atomic `long double` values use x87 80-bit memory transport for
bounded finite normal decimal `L` tokens, floating-width conversions, unary
plus and minus, all four arithmetic
operators, twelve-byte direct and indirect fixed, variadic, and unprototyped
arguments, function returns, direct and indirect call results, and
`va_arg(long double)`. Static-duration scalars, arrays, and complete records
accept implicit zero, a represented integer constant expression, or a
bounded decimal `L` literal with parentheses and unary signs. Runtime truth
and conversion to `_Bool` cover all three represented floating widths.
Runtime arithmetic, all six comparisons, and conditional selection convert
every represented value integer and enum to `long double` through the usual
arithmetic rules. A conditional converts only its selected arm. The four
arithmetic compound operators accept mixed integer and floating operands in
either lvalue direction, convert the result back to the left type, and
evaluate the destination once.
Static long-double truth, comparison, short-circuit logic, conditional
selection, and conversion to or from binary32 and binary64 fold through the
target representation and emit no runtime work. Canonical x87 infinity and
NaN cross the same path, and the decoder accepts canonical subnormal payloads.
Hexadecimal floating literals, hexadecimal or subnormal long-double literals,
long-double ratios beyond the bounded parser, other floating-to-wide
conversions, atomic floating compound assignment, and atomic or `long double`
increment and decrement remain unsupported.
Static `+`, `-`, `*`, and `/` fold with integer-only x87 target arithmetic and
produce final initializer data.
Matching or mixed-width floating conditional arms keep their established x87
path. ADR 0296 records mixed arithmetic compound assignment.

The static object contract pins exact payloads for `1.0L`, the next
represented value above one, the largest bounded literal, signed zero, and
`-1.0L`. It checks scalar and aggregate leaves at file and block scope,
section placement, symbol offsets, two zero padding bytes, and deterministic
repeated emission. The hosted i386 runtime reads all three target words for
every literal payload. A shared conversion fixture covers every represented
integer kind, signed and unsigned enums, both signed 64-bit endpoints,
`ULLONG_MAX`, and the `_Bool` and unsigned results of `-0.5L`.
`sizeof(float) - 4` keeps the accepted `ZERO` record. ADR 0251 records the
static-data boundary, ADR 0254 records integer conversion, and ADR 0255 records
static controls and finite width conversion.

The checked seed retains GNU `noinline` and
`target("general-regs-only")` on canonical file-scope functions.
`noinline` records the request for a future inliner and does not change
current object bytes. Each IR function carries the canonical code generation
mask, and emission rejects a mismatch. The target option rejects
compiler-generated floating work while explicit source assembly stays under
its own typed contract.
The checked seed accepts the exact volatile `ldmxcsr %0` form with one
addressable, non-atomic 32-bit integer `m` input. Linear IR evaluates its
address once, and the shared x86 model emits `0F AE 10` at `[EAX]`.
The checked seed accepts exact `fldcw %0` through the same state-memory input
seam. It requires one addressable, non-atomic 16-bit integer `m` input. GNU
semantics make the no-output statement volatile even when the keyword is
omitted. Linear IR evaluates the address once, and the emitter produces
`D9 /5` at `[EAX]`. ADR 0258 records checked-seed carriage.
It also accepts the exact MOVSS float-memory round trip in
`fpu_boot_smoke()` and the matching one-way load and store. Each form
requires the `xmm0` clobber, evaluates each object address once, and emits
`F3 0F 10 00` or `F3 0F 11 00` through EAX. The exact volatile x87 block in
`stress_sin()` is represented as one modifiable `double` output and one
addressable `double` input with no clobbers. It emits `FLD`, `FSIN`, and
`FSTP` through the shared x86 model, keeps the x87 stack balanced, and uses
no frame temporary. The normal build now compiles `kernel/cpu/fpu.cc` through
the checked wrapper. Two checked compiles produce the same validated
6,620-byte object with SHA-256
`14c3ea232b7d4455ceabd561c69293cc5849abae24d9f210aa69d64ed8c8a5cb`.
The production object policy decodes `fpu_init_cpu()` and rejects helper calls
or floating work before the CR4 write. It requires one `FNINIT` followed by
one 32-bit memory `LDMXCSR`. Replacing the CR4 write with NOPs is a required
negative failure.

The checked seed also accepts the two exact EFLAGS restore statements in
`simd_cpu_has_cpuid()`. Each statement is volatile, takes one non-atomic
32-bit integer through `r`, has no output, and requires one `cc` clobber.
Linear IR retains that effect. The shared x86 emitter consumes the value
through EAX, pushes it back, and emits POPF with balanced ESP.

It also accepts a fixed-register input when one compatible
write-only output already owns that register. The unchanged CPUID statement
keeps its `a` input and `=a` output. CupidC records output zero as their
match, checks the relationship again in Linear IR, and loads the leaf into
EAX immediately before CPUID. Linear IR and emission both require represented
integer operands of equal width, so a forged same-width float or pointer
fails instead of becoming register bits. Existing numeric ties are unchanged,
and a second input cannot claim the same output. Read/write outputs cannot
receive another fixed input. The checked seed also accepts the six packed SSE2
statement shapes in `kernel/cpu/simd.cc`, including their ordered pointer and
32-bit integer inputs and exact memory plus XMM0 through XMM7 clobbers. The
production wrapper freezes the source and its seven-header closure. Two
checked compiles produce the same validated 8,768-byte object with SHA-256
`fd280c321b8eb38a90d4f0982d70b8df0364585e3da322eb2c9de722e071f8d4`.
The normal recipe now uses this checked object.

The checked seed accepts the exact volatile x87 round-down block in
`str_floor()`. It requires one modifiable, non-atomic `double` `=m` output,
one addressable, non-atomic `double` `m` input, and the exact `ax` plus
`memory` clobber set. Linear IR evaluates the output address before the input
address. After `FLD`, the emitter reuses the consumed input-address slot
below ESP for the saved and temporary x87 control words, without touching the
pending output address. It selects round toward negative infinity for
`FRNDINT`, restores the incoming control word, and stores the result with
balanced x87 depth.

The shared decoder checks the exact 44-byte direct sequence. A bounded state
oracle runs eight binary64 inputs under all four incoming rounding modes and
checks the rounded bits, scratch memory, register state, and restored control
word without executing native x87 code. The exact unchanged helper compiles
twice to the same 420-byte object. The checked seed also emits the later
explicit double-to-`uint64_t` casts. The shared-decoder oracle covers zero,
positive and negative fractions, both sides of 2^32, 2^53 minus one, 2^63,
the active `1.8e19` guard, and the largest binary64 value below 2^64. Full
unchanged `kernel/core/string.cc` compiles twice to the same 14,460-byte object
with SHA-256
`d48bb6ea18b7124fbefeaca0d5d5ee8a517db950f21ea88e30ededd6c5c2a577`.
The production wrapper freezes the source and its two headers, validates the
ELF32 object, and publishes it without a host compiler.

Compiler head accepts the exact operand-free BSS-clear statement only as the
direct first child of the external, prototyped `void _start(void)` body in
`.text.start`. It requires the EAX, ECX, EDI, and memory clobbers, visible
external object declarations for `_bss_start` and `_kernel_end`, and no
compiler-managed frame. Frontend statement depth and Linear IR body identity
reject leading, label-wrapped, or otherwise nested copies.

The checked seed loads both linker symbols through
`R_386_32` relocations, derives the doubleword count, clears EAX, and emits
CLD plus REP STOSD through the shared x86 model. It accepts a
nonzero stack top written as one through eight hexadecimal digits, provided
it is aligned to 4 KiB, and emits that value in `MOV ESP, imm32`. The rest of
the statement remains exact. The following `kmain()` call uses stack-base
residue zero. If it returns, `_start` disables interrupts and halts instead of
using the discarded frame. The active source installs `0x01100000`.

The exact fixture has a 27-byte assembly body inside a 42-byte function. Its
three relocations name `_bss_start`, `_kernel_end`, and `kmain`. Two runs of
the Cupid-built compiler emit `kernel/core/kernel.cc` as the same
25,920-byte object with SHA-256
`ed42676ad0d7f16b1fb83442ead1b0082781324dca719104922099cee34b5ab0`.
The normal image built with that object passes the four-CPU frontier gate on
both supported NICs. The production wrapper freezes the source and its
63-header closure, and the normal recipe uses this checked object. ADR 0185
records the compiler-head stack-top boundary, ADR 0186 records its seed
carriage, and ADR 0187 records the active placement.

The checked seed accepts the four exact descriptor-table and
segment-register statements in `kernel/smp/percpu.cc`. A packed
six-byte object supplies LGDT through `m`; the two data-segment templates
require the exact `ax` plus `memory` clobbers. The standalone CS reload keeps
its `memory` clobber, and the GS form takes one represented 16-bit selector
through `r`.

Linear IR carries either the GDTR address or the selector value. The shared
x86 emitter writes LGDT, DS, ES, SS, and GS directly. It reloads CS through a
relative `CALL`, `JMP`, and `RETF` trampoline, so the object needs no
compiler-local label relocation. Two complete compiles produce the same
6,760-byte object with SHA-256
`3c2c6f0e00e5edec1ca16cba91e9fc593d1c42e24f4ebd3591e5f574fb0dd772`.
The checked normal wrapper owns the object and its frozen recursive closure.

The checked seed retains GNU `naked` and `__naked__` on a canonical
file-scope `void (void)` function. The represented body is one complete IPI
entry statement. The reschedule and call forms emit exact `pushal`, a direct
C-function call, `popal`, and `iret` instructions. The panic form emits
`cli`, `hlt`, and a backward jump. CupidC adds no prologue, local storage,
epilogue, or synthetic return. The earlier `smp.c` compiler proof produced an
8,444-byte object with SHA-256
`806509a6dd1ac7eb34b7ffcb67a1f8852950663a274145584d0260da76dcba54`.
The checked production root is now `kernel/smp/smp.cc`. Its object remains
8,444 bytes and has SHA-256
`bd3189b2a1a6d15728c559172f5d6acca0889103428085cec8cc1024742a22d1`.
The existing `__FILE__` diagnostic accounts for the new hash.

Static-duration and variable-length compound literals, the named-aggregate backward-jump alias case, explicit bit-field initializer leaves, Boolean mutation, atomic variadic access, aggregate arguments without declared parameter types, aggregate variadic reads, wide strings, and literal pooling remain unfinished in the shared path. A block-static initializer may now take the address of another block-static object. Static initializers can also reuse a direct integer initializer from an earlier non-atomic `const` integer. This narrow Cupid C extension preserves the unchanged Toolchain object contract's address tables; it is not an ISO C integer constant expression. Mutable, automatic, atomic, indirect, and non-integer cases remain rejected.

Across the root and supplemental builds, CupidC participates in 248
transforms. Of those, 246 are ordinary C-output transforms. The checked native
Windows user ABI and artifact-size verifications supply two more. Its normal
cohort has 240 transforms: 239 checked-in sources plus the generated
`kernel/cpu/ksyms_data.cc` source. All 240 sources use `.cc`.
The five shared Toolchain roots also belong to the 19-source i386 Linux
fixed-point plan, and native GCC or Clang rules select C with `-x c`. ADRs
0124 and 0126 record the first two naming steps, ADR 0129 records the lexer
transfer, ADR 0135 records the Nuked OPL3 transfer, ADR 0139 records the JPEG
and glyph-raster transfer, ADR 0167 records the FPU and SMP transfer, and ADR
0176 records the libm transfer, ADR 0180 records the kernel entry and SIMD
transfer, ADR 0181 records the string transfer, and ADR 0184 records the Doom
transfer. No checked-in normal root remains host-owned.
Three generated installation tables and the `hello.cc`, `ls.cc`, and
`cat.cc` programs account for six more CupidC transforms. One contract
transform builds the Windows user syscall ABI checker as a private PE before
those programs compile. The other builds the artifact-size checker as a static
ELF on Linux or a native PE on Windows. Both run without WSL on Windows and
leave the Linux Toolchain contract publication untouched. ADRs 0295 and 0297
record these boundaries.

The Nuked OPL3 recipe compiles from a private snapshot of its source and
three-header closure. The wrapper compares every live input before replacing
the object, so a concurrent edit cannot publish a mixed result.

The strict kernel cohort has 156 approved checked-in sources. The latest
complete two-pass proof predates the last addition and covers 155 sources
against a 445-file snapshot with SHA-256
`99d03de14f544f6a76d21ed147e62018873f1e2e8dfa2f4459830b69314432c2`.
Both 155-object sets are byte-identical; each totals 3,749,796 bytes. The
combined graph keeps the ISO fixture as an explicit image input. Strong
four-vCPU runtime gates pass with e1000 and RTL8139 networking through SMP,
RDRAND, all 62 crypto checks, USB storage, audio, TrueType glyphs, a baseline
JPEG decode, the desktop, terminal, and in-OS CupidC execution. Both runs
print `[fpu] SSE2 enabled`, `[fpu] boot smoke ok`, and
`FPU boot smoke passed`, then finish
`feature16_asm_fpu.cc`. Checked CupidObj generates the symbol source from
canonical CupidDis text, while Python checks the bytes before publication. The
current 156-source production build passes. A broader two-generation frontier
run timed out after 1,204 seconds and remains incomplete. The
current 114,851-byte logical blob uses little-endian `unsigned int` words with
one trailing pad byte.

Forced poisoned-host builds cover every production wrapper recipe, and each
recipe declares its exact recursive header closure. A valid data-only object
may omit `.text` while its remaining sections and symbols still receive bounds
checks. The CSPRNG assembly emits RDTSC, CPUID, RDRAND, and SETC through
Cupid's x86 model while preserving EBX. The combined four-vCPU GUI gate reaches
SMP, all 62 crypto checks, e1000 traffic, the desktop, terminal, and CupidC
execution at `0x01100000`. A separate gate loads and reaps the same
external program twice at `0x01C00000`. ADR 0124 records the exact build and
runtime evidence. No supported transform invokes a host C compiler. Python
participates in all 452 transforms across the three audited roots, and CupidC
participates in 248. CupidObj participates in 192, CupidASM in seven, CupidLD
in seven, and CupidDis in six. Root `all` has 443 transforms, and every one has
a Cupid participant. The size verifier emits no OS artifact; it runs a private
CupidC contract with CupidASM startup and a CupidLD link. The normal graph runs
CupidC, CupidASM, CupidObj, CupidLD, and CupidDis from the manifest-checked
seed; `toolchain:all` uses the rebuilt static tools for its contract cohort.
The final `make bootstrap-audit` passed in 68.8 seconds. The private
in-kernel CupidC compiler
still handles embedded runtime compilation. The checked user compiler creates
approved output directories for default and overridden `BUILD` paths. It uses
no-follow POSIX descriptors or parent-relative Windows handles and checks the
resolved output before releasing the directory pins. Empty-directory setup is
no longer a separate graph transform. ADR 0245 records this publisher
boundary. ADR 0246 records the shared checked-seed invocation.

The checked seed decides C11 inline linkage from the complete file-scope
declaration set. This covers the ordinary header declaration and inline
definition of `OPL3_Generate4Ch` in `kernel/audio/nuked_opl3.cc`. Two complete
kernel-profile compiles produce the same validated 40,424-byte object. It
defines the global function and imports only `memset`. Prior `static` linkage
stays internal, even when a later definition is spelled `extern inline`. An
external-linkage inline declaration without a definition fails during
finalization, while a pure external inline definition remains unsupported
during lowering. The closed production recipe, frontier, image builds, and
dual-NIC runtime gates pass.

CupidC also accepts operand-free GNU assembly inside functions. Basic statements and extended statements with an empty output list are implicitly volatile. Exact sequences of PAUSE, NOP, STI, HLT, CLI, CLD, SFENCE, and FNINIT emit without a temporary frame slot or EBX traffic. These semantics serve the four normal-build e1000, desktop, socket, and TCP objects. The checked seed also keeps an exact empty volatile extended template with one `memory` clobber as an IR ordering point and emits no instruction bytes. That form compiles the unchanged Doom sound driver through its production CupidC recipe.

File-scope GNU basic assembly has a separate translation-unit representation
in the checked seed. The frontend owns immutable templates outside the function
statement table, and Linear IR keeps their source order. The i386 emitter
recognizes the twelve exact x87/SSE floating wrapper definitions at the start
of the then-named `kernel/cpu/libm.c`. It writes prologue-free global function
symbols through Cupid's shared x86 encoder. The fixture has 248 exact text
bytes and no relocations. The checked seed accepts `[identifier]` labels before
GNU statement inputs and outputs, then resolves `%[identifier]` to the
existing numeric operand index before IR. Named and numeric operands share
the same semantic checks, and `%%` stays escaped text.

The checked seed emits the exact x87 programs in `libm_pow_impl()` and
`libm_powf_impl()`. The double form has one `double` output and four `double`
inputs. The mixed form has one `float` output, two `float` inputs, and two
`double` inputs. Both use a memory clobber. Linear IR evaluates each set of
five addresses once in source order. Each emitter proof produces 116 exact
text bytes with no relocations, uses `DC E9` for `FSUB ST(1), ST(0)`, reaches
a maximum x87 depth of three, and returns to the incoming depth.

All seven active range-reduction sites use corrected `fsubr %st, %st(1)` or
its escaped statement form. CupidC emits the intended `x - round(x)`
remainder. The old `fsub` spelling still emits `DC E1` and remains covered as
a compatibility case. ADR 0209 records the source correction.

The checked seed also emits the exact volatile `sqrtsd %1, %0` statement. It
accepts one modifiable, non-atomic `double` `=x` output, one non-atomic
`double` `x` input, and no clobbers. Linear IR evaluates the output address
before the input value. The 65-byte focused function uses Cupid's shared
`MOVSD` and `SQRTSD` encodings and has no relocations.

The checked seed also emits the exact volatile x87 statement in
`libm_atan2_impl()`. It accepts one modifiable, non-atomic `double` `=m`
output, two addressable, non-atomic `double` `m` inputs in `y`, `x` order,
and one `memory` clobber. Linear IR evaluates all three addresses once in
source order. The 53-byte focused function uses the shared model for both
loads, `FPATAN`, and the final store, with no relocations. The full source now
proceeds to the x87 exponent statement in `libm_exp_impl()`.

The checked seed also emits that exact volatile x87 exponent statement. It
accepts one modifiable, non-atomic `double` `=m` output, two addressable,
non-atomic `double` `m` inputs in `x`, `log2e` order, and one `memory`
clobber. Linear IR evaluates all three addresses once in source order. The
71-byte focused function has no relocations, reaches x87 depth three, and
returns to its incoming depth.

The checked seed also emits the exact aligned `fabs` mask block and the
following `fabs` and `fabsf` wrappers. The masks occupy the first 32 bytes of
`.rodata`, with local `STT_NOTYPE` labels at offsets 0 and 16. The wrappers
contain 15 and 14 text bytes and carry one `R_386_32` relocation each to the
matching mask.

The checked seed also emits the next eight file-scope rounding wrappers. The
`floor`, `ceil`, `round`, and `trunc` pairs select x87 round down, round up,
nearest-even, and toward-zero modes. Every double and float wrapper saves
the caller's control word, runs `FRNDINT` under its selected mode, restores
the original word, and returns through XMM0. The family adds 384 exact text
bytes with no relocations. Each wrapper reaches x87 depth one and balances
ESP and x87 depth.

The checked seed also emits the exact `fmod` and `fmodf` definitions. Each
loads `y` below `x`, repeats `FPREM` while status-word C2 remains set, and
uses the source's short backward branch. It then discards ST(1), returns the
remainder through XMM0 at the source width, and restores ESP and x87 depth.
Both wrappers contain 35 text bytes and no relocation.

The checked seed also emits the aligned `libm_log2e_const` and
`libm_ln2_const` block and the exact `exp2`, `exp2f`, `exp`, `expf`, `log2`,
`log2f`, `log`, and `logf` definitions. The constants occupy 16 bytes of
`.rodata` at alignment eight. The wrappers add 264 text bytes. The four
natural forms each carry one `R_386_32` relocation to the matching local
constant, while the base-two forms have none. All eight wrappers balance ESP
and x87 depth and reach no deeper than three x87 values. The full source now
proceeds to `pow` at line 846.

The checked seed emits that wrapper and the 17 cdecl bridges that follow it.
The binary `pow`, `hypot`, and `nextafter` pairs and unary `asin`, `acos`,
`sinh`, `cosh`, `tanh`, and `cbrt` pairs copy their original argument words,
call matching external `libm_*_impl` functions, reclaim the copied words,
and move the ST(0) result into XMM0. Four shared stack shapes cover float and
double functions with one or two arguments. The family has 558 text bytes
and 18 `R_386_PC32` relocations with addend `-4`. Two exact compiles of
corrected `kernel/cpu/libm.cc` produce the same 16,164-byte ELF32 relocatable
object with SHA-256
`c0911732361f2e1ea78aa778f834719ba12208cc2d9f0a312455a5e6a38a75b4`.

General GAS syntax and other file-scope templates remain unsupported. The
normal `libm.cc` recipe uses the checked production wrapper. Its frozen
closure contains `kernel/core/types.h` and `kernel/cpu/libm.h`. The guest gate
runs `/bin/feature15_libm.cc` and requires seven x87 range checks, all 29
checks, both zero-failure summaries, and `PASS feature15_libm`. ADR 0176
records the ownership transfer, and ADR 0209 records the numerical correction.

The same checked seed accepts a modifiable four-byte object or `void` pointer
as the single `=r` output of `mov %%gs:0, %0`. It retains the pointer type,
evaluates the output destination once, and emits the absolute GS load as
`65 A1 00 00 00 00`.

The checked seed also accepts independent `r` and `c` inputs for the exact
privileged statements in `kernel/cpu/idt.cc`, `kernel/mm/paging.cc`, and
`kernel/smp/lapic.cc`. A four-byte integer or data pointer may use `r`; a
four-byte integer may use `c` and arrives in ECX. The i386 emitter handles
CR0, CR2, CR3, and CR4 moves and RDMSR directly. Its exact seven-function
object has 199 text bytes, eight symbols, five sections, no relocations, and
no EBX traffic. The decoder checks the privileged bytes without executing
them. All three unchanged roots compile twice to deterministic validated
objects, and their normal recipes use the checked seed. WRMSR, unsupported
control-register directions, fixed EBX and `q` inputs, arbitrary templates,
and general clobbers remain open.

The checked seed accepts one memory output for three machine-state
snapshots. Exact volatile `fnstsw %0` and `fnstcw %0` statements write a
modifiable 16-bit integer through `=m`; `stmxcsr %0` writes a modifiable
32-bit integer. Linear IR evaluates the lvalue once, and the emitter sends
the direct memory instruction through Cupid's x86 encoder without an output
staging slot. Other `=m` templates remain unsupported. The separate exact
call-next support also handles the later local-label statement in unchanged
`kernel/core/panic.cc`. Two full kernel-profile compiles produce the same
validated 10,212-byte ELF32 object. The checked seed carries this capability;
the normal build now uses it for the panic root.

The checked seed represents `__atomic_load_n`, `__atomic_store_n`,
`__atomic_exchange_n`, `__atomic_fetch_add`, and `__atomic_fetch_or` on
one-, two-, and four-byte integer objects. Constant memory orders stay in the
typed AST and Linear IR. The i386 emitter uses ordinary loads and release
stores, memory `XCHG` for exchange and sequentially consistent store,
`LOCK XADD` for fetch-add, and a `LOCK CMPXCHG` retry loop for fetch-or. The
loop rebuilds the desired value from the latest EAX value and preserves EBX.
The six order names are target predefines in every language mode, while the
expressions remain GNU-only. A decoded i386 oracle checks returned old
values, memory changes, narrow signedness, wraparound, cdecl state, one-time
operand evaluation, and a forced competing fetch-or update. Runtime orders,
pointer atomics, HLE flags, and eight-byte atomics remain outside this slice.
The checked seed carries fetch-or and compiles the active EHCI path.

The checked seed parses all eight unchanged helpers in
`kernel/core/ports.h`. The byte, word, and doubleword IN and OUT forms keep
their declared integer widths while binding the accumulator and DX inputs.
The repeated word forms retain their read/write buffer and count operands.
INSW also retains its `memory` clobber. Output destinations are evaluated
once, and the string forms write back the final pointer and count while
preserving ESI or EDI across the cdecl call.

The i386 path emits `EC`, `EE`, `66 ED`, `66 EF`, `ED`, and `EF` for scalar
port I/O. The string forms emit `FC F3 66 6D` and `FC F3 66 6F` through the
shared x86 model. The checked-seed C11 standalone sweep passes 161 of 164
active non-Doom headers. `scheduler.h`, `simd_intrin.h`, and the macro-driven
exact-decimal test fixture remain exact C11-profile failures. The checked seed
parses all 29 declarations in
`simd_intrin.h` under the Cupid profile through the native type spellings
described above.

The refreshed checked seed carries this port-I/O support. The normal build
uses it in the 156-source checked-in CupidC cohort. Earlier frontier evidence
measured the ACPI and MP-table objects at 5,708 and 4,156 bytes. Their current
`.cc` paths must pass the shared validator and re-run the four-vCPU contract
with both supported NIC paths.

The checked seed accepts the GNU `Nd` alternative in the 8259 PIC helpers.
It selects DX for the port and emits the exact `outb %0, %1` and
`inb %1, %0` forms. This keeps the unchanged source contract and produces a
deterministic object for `kernel/cpu/pic.cc`. The production recipe now uses
this checked-seed capability.

A block type name or record member can reuse a visible enum tag or define a new one.

### Global Variables

Variables declared outside functions are stored in the data section:

```c
// Scalar globals with optional initializers
int count = 0;
int max_size = 1024;
int error_code = -1;
char *greeting = "Hello";

// Global arrays
int data[256];
char name[64];

// Global struct variables
struct Point origin;

void main() {
    count = 42;
    data[0] = 100;
    origin.x = 10;
    origin.y = 20;
    print(greeting);
}
```

Global variables support:
- Integer and character literal initializers (including negative values)
- String literal initializers (pointer to data section string)
- Arrays of any supported element type
- Struct variables (zero-initialized)
- All operators: `=`, `+=`, `-=`, `*=`, `/=`, `++`, `--`

### Built-in Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `NULL` | 0 | Null pointer constant |
| `true` | 1 | Boolean true |
| `false` | 0 | Boolean false |

### Operators

Full C operator precedence is supported:

| Precedence | Operators | Description |
|-----------|-----------|-------------|
| 10 | `*` `/` `%` | Multiplication, division, modulo |
| 9 | `+` `-` | Addition, subtraction |
| 8 | `<<` `>>` | Bitwise shift |
| 7 | `<` `>` `<=` `>=` | Relational comparison |
| 6 | `==` `!=` | Equality comparison |
| 5 | `&` | Bitwise AND |
| 4 | `^` | Bitwise XOR |
| 3 | `\|` | Bitwise OR |
| 2 | `&&` | Logical AND |
| 1 | `\|\|` | Logical OR |

**Unary operators:** `!` (logical NOT), `~` (bitwise NOT), `-` (negate), `*` (dereference), `&` (address-of), `++` / `--` (increment/decrement)

**Assignment operators:** `=`, `+=`, `-=`, `*=`, `/=`

**Type casts:** `(int)expr`, `(char*)ptr`

### Control Flow

```c
// If/else
if (x > 5) {
    print("big");
} else {
    print("small");
}

// While loop
while (i < 10) {
    i++;
}

// Do-while loop
do {
    process();
    i++;
} while (i < 10);

// For loop
for (int i = 0; i < 10; i++) {
    print_int(i);
}

// Switch/case
switch (cmd) {
    case 'a':
        print("add");
        break;
    case 'd':
        print("delete");
        break;
    default:
        print("unknown");
        break;
}
```

The private compiler tags each breakable control as a loop or switch. `break` exits the innermost control. `continue` finds the nearest loop and removes the saved selector for each switch it crosses. `while` continues at its condition, `do-while` continues at its condition trampoline, and `for` continues at its iteration expression.

CupidC accepts 128 active loop-or-switch control frames and reserves 64 `break` patches per frame. It also accepts 1,024 active statement-parser calls. The next entry fails before further recursion with `control nesting too deep` or `statement nesting too deep`, and a failed REPL evaluation restores both counters. The recursive dispatcher keeps a four-byte checked CupidC frame, while token-heavy simple statements run in a helper that is no longer live across nested statement parsing. The compiler resolves recorded jump patches when each construct ends.

### Functions

Functions use the cdecl calling convention and may declare up to 32 parameters.

```c
int add(int a, int b) {
    return a + b;
}

void main() {
    int result = add(5, 10);
    print_int(result);
}
```

Every program must have a `main()` function - it is the entry point.

Forward references are supported: functions can call other functions that are defined later in the file. The compiler resolves these after parsing is complete.

### String and Character Literals

```c
char* msg = "Hello, CupidOS!\n";
char ch = 'A';
char esc = '\x1B';  // ESC character for ANSI codes
```

#### Escape Sequences

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

**Hexadecimal Escapes**: The `\xNN` escape sequence allows specifying a byte value in hexadecimal. Both uppercase and lowercase hex digits are supported:

```c
char *red = "\x1B[31m";     // ANSI red color code
print("\x48\x69");          // Prints "Hi" (0x48='H', 0x69='i')
```

String literals are stored in the data section and their address is loaded into registers.

### Comments

```c
// Line comments

/* Block
   comments */
```

### Inline Assembly

Direct x86 assembly inside CupidC functions:

```c
void disable_interrupts() {
    asm {
        cli;
        hlt;
    }
}
```

**Supported instructions:**

| Category | Instructions |
|----------|-------------|
| No operand | `cli`, `sti`, `hlt`, `nop`, `ret`, `iret`, `pushad`, `popad`, `cdq` |
| Register | `push`, `pop`, `inc`, `dec` |
| Reg, Reg/Imm | `mov`, `add`, `sub`, `xor`, `cmp` |
| I/O | `in al, dx` / `out dx, al` |
| Control | `call`, `int` |

Registers: `eax`, `ecx`, `edx`, `ebx`, `esp`, `ebp`, `esi`, `edi`, `al`, `cl`, `dl`, `bl`

---

## Kernel Bindings

CupidC programs can call kernel functions directly. These are pre-registered in the symbol table at compile time:

### Console Output

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `void print(char* s)` | Print string to screen |
| `println` | `void println(char* s)` | Print string + newline |
| `putchar` | `void putchar(char c)` | Print single character |
| `print_int` | `void print_int(int n)` | Print integer |
| `print_hex` | `void print_hex(int n)` | Print hex value |
| `clear_screen` | `void clear_screen()` | Clear the display |

### Console Input

| Function | Signature | Description |
|----------|-----------|-------------|
| `getchar` | `int getchar()` | Read a single character from the keyboard (blocking) |

### Memory Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `kmalloc` | `void* kmalloc(int size)` | Allocate memory (kernel heap) |
| `kfree` | `void kfree(void* ptr)` | Free allocated memory |

### String Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `strlen` | `int strlen(char* s)` | Get string length |
| `strcmp` | `int strcmp(char* a, char* b)` | Compare two strings |
| `strncmp` | `int strncmp(char* a, char* b, int n)` | Compare up to n characters |
| `strcpy` | `char* strcpy(char* dst, char* src)` | Copy string (including null terminator) |
| `strncpy` | `char* strncpy(char* dst, char* src, int n)` | Copy up to n characters (pads with nulls) |
| `strcat` | `char* strcat(char* dst, char* src)` | Concatenate src onto end of dst |
| `strchr` | `char* strchr(char* s, int c)` | Find first occurrence of character c in s |
| `strstr` | `char* strstr(char* haystack, char* needle)` | Find first occurrence of substring |
| `memset` | `void* memset(void* p, int val, int n)` | Fill memory |
| `memcpy` | `void* memcpy(void* dst, void* src, int n)` | Copy memory |
| `memcmp` | `int memcmp(void* a, void* b, int n)` | Compare n bytes of memory |

### Port I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `outb` | `void outb(int port, int value)` | Write byte to I/O port |
| `inb` | `int inb(int port)` | Read byte from I/O port |

### File System

| Function | Signature | Description |
|----------|-----------|-------------|
| `vfs_open` | `int vfs_open(char* path, int flags)` | Open a file |
| `vfs_close` | `int vfs_close(int fd)` | Close a file |
| `vfs_read` | `int vfs_read(int fd, void* buf, int size)` | Read from file |
| `vfs_write` | `int vfs_write(int fd, void* buf, int size)` | Write to file |
| `vfs_seek` | `int vfs_seek(int fd, int offset, int whence)` | Seek within an open file |
| `vfs_stat` | `int vfs_stat(char* path, void* stat_buf)` | Get file info (size + type) |
| `vfs_readdir` | `int vfs_readdir(int fd, void* dirent)` | Read next directory entry |
| `vfs_mkdir` | `int vfs_mkdir(char* path)` | Create a directory |
| `vfs_unlink` | `int vfs_unlink(char* path)` | Delete a file |
| `vfs_rename` | `int vfs_rename(char* old, char* new)` | Native rename within one mount; cross-mount moves return `EXDEV` |

### Shell Integration

| Function | Signature | Description |
|----------|-----------|-------------|
| `get_args` | `char* get_args()` | Get command-line arguments from the shell |
| `get_cwd` | `char* get_cwd()` | Get current working directory |
| `set_cwd` | `void set_cwd(char* path)` | Set the shell's current working directory |
| `resolve_path` | `void resolve_path(char* input, char* out)` | Resolve a relative path against CWD (out must be 256 bytes) |
| `get_history_count` | `int get_history_count()` | Get the number of entries in shell history |
| `get_history_entry` | `char* get_history_entry(int index)` | Get history entry by offset from newest (0 = most recent) |

### Timer

| Function | Signature | Description |
|----------|-----------|-------------|
| `uptime_ms` | `int uptime_ms()` | Get system uptime in milliseconds |

### RTC (Real-Time Clock)

| Function | Signature | Description |
|----------|-----------|-------------|
| `rtc_hour` | `int rtc_hour()` | Current hour (0-23) |
| `rtc_minute` | `int rtc_minute()` | Current minute (0-59) |
| `rtc_second` | `int rtc_second()` | Current second (0-59) |
| `rtc_day` | `int rtc_day()` | Current day of month (1-31) |
| `rtc_month` | `int rtc_month()` | Current month (1-12) |
| `rtc_year` | `int rtc_year()` | Current year (e.g. 2026) |
| `rtc_weekday` | `int rtc_weekday()` | Day of week (0=Sunday, 6=Saturday) |
| `rtc_epoch` | `int rtc_epoch()` | Seconds since Unix epoch (Jan 1, 1970) |
| `date_full_string` | `char* date_full_string()` | Formatted date: "Thursday, February 6, 2026" |
| `date_short_string` | `char* date_short_string()` | Formatted date: "Feb 6, 2026" |
| `time_string` | `char* time_string()` | Formatted time: "6:32:15 PM" |
| `time_short_string` | `char* time_short_string()` | Formatted time: "6:32 PM" |

### Process Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `yield` | `void yield()` | Yield CPU to scheduler |
| `exit` | `void exit()` | Terminate current process |
| `exec` | `int exec(char* path, char* args)` | Execute a program |
| `process_list` | `void process_list()` | Print all processes (PID, state, name) |
| `process_kill` | `void process_kill(int pid)` | Terminate a process by PID |
| `spawn_test` | `int spawn_test(int count)` | Spawn N test counting processes (max 16), returns count spawned |

### Mount Info

| Function | Signature | Description |
|----------|-----------|-------------|
| `mount_count` | `int mount_count()` | Get the number of mounted filesystems |
| `mount_name` | `char* mount_name(int index)` | Get the filesystem name for mount at index |
| `mount_path` | `char* mount_path(int index)` | Get the mount path for mount at index |

### Diagnostics

| Function | Signature | Description |
|----------|-----------|-------------|
| `memstats` | `void memstats()` | Print heap and physical memory statistics |
| `detect_memory_leaks` | `void detect_memory_leaks(int ms)` | Report allocations older than `ms` milliseconds |
| `heap_check_integrity` | `void heap_check_integrity()` | Walk heap blocks and verify canary values |
| `pmm_free_pages` | `int pmm_free_pages()` | Number of free physical 4 KB pages |
| `pmm_total_pages` | `int pmm_total_pages()` | Total number of physical 4 KB pages |
| `dump_stack_trace` | `void dump_stack_trace()` | Print current call stack (EBP frame chain) |
| `dump_registers` | `void dump_registers()` | Print all CPU registers + EFLAGS |
| `peek_byte` | `int peek_byte(int addr)` | Read one byte from a memory address |
| `print_hex_byte` | `void print_hex_byte(int val)` | Print a byte as 2 hex digits |
| `get_cpu_mhz` | `int get_cpu_mhz()` | CPU frequency in MHz |
| `timer_get_frequency` | `int timer_get_frequency()` | Timer interrupt rate in Hz |
| `process_get_count` | `int process_get_count()` | Number of running processes |

### BMP Image Encoding/Decoding

| Function | Signature | Description |
|----------|-----------|-------------|
| `bmp_get_info` | `int bmp_get_info(char* path, void* info)` | Get BMP dimensions without loading pixels (fills `bmp_info_t`) |
| `bmp_decode` | `int bmp_decode(char* path, int* buf, int size)` | Decode 24-bit BMP to 32bpp XRGB buffer |
| `bmp_encode` | `int bmp_encode(char* path, int* buf, int w, int h)` | Encode 32bpp XRGB buffer to 24-bit BMP file |
| `bmp_decode_to_fb` | `int bmp_decode_to_fb(char* path, int x, int y)` | Decode BMP directly to framebuffer at position |

All BMP functions return `0` on success. Error codes: `-1` (invalid), `-2` (unsupported format), `-3` (I/O error), `-4` (buffer too small).

### gfx2d Image Pool (BMP / PNG / JPEG)

| Function | Signature | Description |
|----------|-----------|-------------|
| `gfx2d_image_load` | `int gfx2d_image_load(char* path)` | Load image from VFS path. Format auto-detected by signature: BMP, PNG, JPEG. Returns image handle (>= 0) or -1. |
| `gfx2d_image_load_mem` | `int gfx2d_image_load_mem(char* buf, int len)` | Decode a PNG or JPEG byte buffer directly (no VFS round-trip). Used by the browser to display network-fetched images. Returns handle or -1. |
| `gfx2d_image_free` | `void gfx2d_image_free(int handle)` | Release an image handle. |
| `gfx2d_image_draw` | `void gfx2d_image_draw(int handle, int x, int y)` | Blit image at (x, y), unscaled. |
| `gfx2d_image_draw_scaled` | `void gfx2d_image_draw_scaled(int handle, int x, int y, int w, int h)` | Blit scaled to (w, h). |
| `gfx2d_image_width` | `int gfx2d_image_width(int handle)` | Image width in pixels. |
| `gfx2d_image_height` | `int gfx2d_image_height(int handle)` | Image height in pixels. |

PNG decoder handles 8-bit color types 0/2/3/6 (gray, RGB, palette, RGBA), filters None/Sub/Up/Average/Paeth, non-interlaced.  JPEG decoder handles SOF0/SOF1 baseline at 8-bit precision, 1- or 3-component images, sub-samplings 1x1/2x1/1x2/2x2, restart markers.  Progressive JPEG, arithmetic coding, 12-bit, and CMYK are rejected.

### File Dialogs

| Function | Signature | Description |
|----------|-----------|-------------|
| `file_dialog_open` | `int file_dialog_open(char* start, char* result, char* ext)` | Show modal file open dialog; returns 1 if selected, 0 if cancelled |
| `file_dialog_save` | `int file_dialog_save(char* start, char* name, char* result, char* ext)` | Show modal file save dialog; returns 1 if confirmed, 0 if cancelled |

The `result` buffer must be 128 bytes. Pass `0` for `ext` to show all files.

### VFS Helpers

| Function | Signature | Description |
|----------|-----------|-------------|
| `vfs_read_all` | `int vfs_read_all(char* path, void* buf, int max)` | Read entire file into buffer; returns bytes read or negative error |
| `vfs_write_all` | `int vfs_write_all(char* path, void* buf, int size)` | Write buffer to file (creates/truncates); returns bytes written or negative error |
| `vfs_read_text` | `int vfs_read_text(char* path, char* buf, int max)` | Read text file as null-terminated string; returns string length |
| `vfs_write_text` | `int vfs_write_text(char* path, char* text)` | Write null-terminated string to file; returns bytes written |

### Block Cache

| Function | Signature | Description |
|----------|-----------|-------------|
| `blockcache_sync` | `int blockcache_sync()` | Flush dirty cache blocks and report device failure |
| `blockcache_stats` | `void blockcache_stats()` | Print cache hit/miss statistics |

### Serial Log Control

| Function | Signature | Description |
|----------|-----------|-------------|
| `set_log_level` | `void set_log_level(int level)` | Set log level (0=debug, 1=info, 2=warn, 3=error, 4=panic) |
| `get_log_level_name` | `char* get_log_level_name()` | Current log level as a string |
| `print_log_buffer` | `void print_log_buffer()` | Print the circular log buffer contents |

### Crash Testing

| Function | Signature | Description |
|----------|-----------|-------------|
| `kernel_panic` | `void kernel_panic(char* msg)` | Trigger a kernel panic with message |
| `crashtest_nullptr` | `void crashtest_nullptr()` | Dereference NULL pointer |
| `crashtest_divzero` | `void crashtest_divzero()` | Divide by zero |
| `crashtest_overflow` | `void crashtest_overflow()` | Overflow heap buffer (canary detection) |
| `crashtest_stackoverflow` | `void crashtest_stackoverflow()` | Allocate 64 KB on stack (page fault) |

### Networking - BSD sockets

Ports passed to / returned from these calls are network byte order - wrap
literals in `htons()`. See [Networking](Networking) for full protocol
details.

| Function | Signature | Description |
|----------|-----------|-------------|
| `socket` | `int socket(int type)` | `2`=TCP, `1`=UDP. Returns fd or negative error |
| `bind` | `int bind(int fd, U32 ip, U16 port)` | `ip=0` for INADDR_ANY |
| `listen` | `int listen(int fd, int backlog)` | Mark TCP socket passive |
| `accept` | `int accept(int fd, U32 *peer_ip, U16 *peer_port)` | Block until incoming SYN completes 3-way handshake |
| `connect` | `int connect(int fd, U32 ip, U16 port)` | Block until ESTABLISHED, refused, or 30 s timeout |
| `send` / `recv` | `int send(int fd, void *buf, U32 len)` / `recv(...)` | Stream I/O on TCP socket |
| `sendto` / `recvfrom` | `int sendto(int fd, void *buf, U32 len, U32 ip, U16 port)` / `recvfrom(...)` | UDP datagram I/O |
| `close` | `int close(int fd)` | Tear down socket (FIN handshake for TCP) |
| `dns_resolve` | `int dns_resolve(char *name, U32 *ip_out)` | UDP/53 A-record lookup, 16-entry cache |
| `htons` / `ntohs` | `U16 htons(U16)` / `U16 ntohs(U16)` | 16-bit byte swap |
| `htonl` / `ntohl` | `U32 htonl(U32)` / `U32 ntohl(U32)` | 32-bit byte swap |
| `IP_PROTO_ICMP` / `IP_PROTO_UDP` / `IP_PROTO_TCP` | `U32 IP_PROTO_TCP()` | Constants exposed as 0-arg getters |

### Networking - interface info & raw protocol

| Function | Signature | Description |
|----------|-----------|-------------|
| `net_get_ip` | `U32 net_get_ip()` | Local IPv4 of the primary NIC |
| `net_get_gateway` | `U32 net_get_gateway()` | Default gateway IPv4 |
| `net_get_dns` | `U32 net_get_dns()` | DNS server IPv4 |
| `net_get_mask` | `U32 net_get_mask()` | Subnet mask |
| `net_get_mac` | `void net_get_mac(U8 *out)` | Fills 6-byte MAC |
| `net_link_up` | `U32 net_link_up()` | 1 if link up, else 0 |
| `net_rx_packets` / `net_tx_packets` | `U32` | Counters since boot |
| `net_rx_drops` / `net_tx_errors` | `U32` | Error counters |
| `ip_parse` | `int ip_parse(char *s, U32 *out)` | `"a.b.c.d"` -> uint32 |
| `ipv4_send` | `int ipv4_send(U32 dst, U8 proto, U8 *payload, U32 plen)` | Build + send raw IPv4 (auto-fragments) |
| `arp_resolve` | `int arp_resolve(U32 ip, U8 *mac_out)` | Blocking resolve, 500 ms timeout |
| `arp_dump` | `void arp_dump()` | Print cache to serial |
| `arp_get_entries` | `int arp_get_entries(U32 *ips, U8 macs[][6], int max)` | Bulk read |
| `icmp_send_echo` | `int icmp_send_echo(U32 dst, U16 id, U16 seq, U32 paylen)` | Send echo request |
| `icmp_wait_reply` | `int icmp_wait_reply(U32 src, U16 id, U16 seq, U32 timeout_ms)` | Block for matching reply |
| `udp_send_raw` | `int udp_send_raw(U32 dst, U16 sport, U16 dport, U8 *data, U32 len)` | One-shot UDP datagram |

### Block devices (ATA / loopdev / USB-MSC)

| Function | Signature | Description |
|----------|-----------|-------------|
| `blkdev_count` | `int blkdev_count()` | Number of registered block devices |
| `blkdev_read` | `int blkdev_read(int idx, U32 lba, U32 count, void *buf)` | Read N sectors from blkdev[idx] |
| `blkdev_write` | `int blkdev_write(int idx, U32 lba, U32 count, void *buf)` | Write N sectors |
| `ata_read_sectors` | `int ata_read_sectors(U8 drive, U32 lba, U8 count, void *buf)` | Direct ATA read (drive 0 = master) |
| `ata_write_sectors` | `int ata_write_sectors(U8 drive, U32 lba, U8 count, void *buf)` | Direct ATA write |

### Keyboard, serial, PIT - direct driver access

| Function | Signature | Description |
|----------|-----------|-------------|
| `keyboard_read_event` | `bool keyboard_read_event(key_event_t *out)` | Pop one event (returns false if queue empty) |
| `keyboard_inject_scancode` | `void keyboard_inject_scancode(U8 sc)` | Synthesize a make/break scancode |
| `keyboard_get_shift` / `_ctrl` / `_alt` / `_caps_lock` | `bool` | Modifier-key state |
| `serial_read_char` | `int serial_read_char()` | Non-blocking COM1 RX, returns -1 if empty |
| `serial_write_char` | `void serial_write_char(char c)` | One byte to COM1 |
| `serial_write_string` | `void serial_write_string(char *s)` | NUL-terminated string |
| `serial_has_rx` | `int serial_has_rx()` | 1 if a byte is pending |
| `pit_set_frequency` | `void pit_set_frequency(U32 channel, U32 hz)` | Reprogram PIT channel |
| `timer_delay_us` | `void timer_delay_us(U32 us)` | TSC-based busy delay |

### PCI introspection (by index)

`idx` ranges from 0 to `pci_device_count()-1`. The kernel hides the
opaque `pci_device_t *` behind these index-based getters.

| Function | Returns |
|---|---|
| `pci_device_count()` | Number of PCI devices found at boot |
| `pci_get_vendor(idx)` | 16-bit vendor ID |
| `pci_get_device_id(idx)` | 16-bit device ID |
| `pci_get_class(idx)` | Packed `class<<16 | sub<<8 | prog_if` |
| `pci_get_irq(idx)` | IRQ line from PCI config space |
| `pci_get_bar(idx, bar)` | BAR value, `bar` = 0..5 |
| `pci_bar_is_mmio(idx, bar)` | 1 if MMIO, 0 if I/O port |
| `pci_enable_bus_master(idx)` | Set bus-master bit in command register |

### SMP / LAPIC / paging / PMM

> Misusing these functions can deadlock or corrupt the kernel. Use
> `bkl_lock` and `bkl_unlock` when the operation must be atomic with respect to
> other CPUs.

| Function | Signature | Description |
|----------|-----------|-------------|
| `lapic_get_id` | `U32 lapic_get_id()` | Local APIC ID of the calling CPU |
| `lapic_eoi` | `void lapic_eoi()` | End-of-interrupt (only call from a real ISR) |
| `bkl_lock` / `bkl_unlock` | `void bkl_lock()` / `bkl_unlock()` | Big kernel lock - recursive ticket spinlock, IRQ-save |
| `paging_map_mmio` | `void paging_map_mmio(U32 phys, U32 size)` | Identity-map a physical region with PWT|PCD bits |
| `pmm_alloc_page` | `void *pmm_alloc_page()` | Allocate one 4 KB physical page |
| `pmm_free_page` | `void pmm_free_page(void *page)` | Return a page to the PMM |

---

## Examples

### Hello World

```c
void main() {
    println("Hello from CupidC!");
}
```

### Fibonacci

```c
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

void main() {
    for (int i = 0; i < 10; i++) {
        print_int(fib(i));
        print(" ");
    }
    print("\n");
}
```

### Working with Pointers

```c
void swap(int* a, int* b) {
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

void main() {
    int x = 10;
    int y = 20;
    swap(&x, &y);
    print_int(x);   // 20
    print(" ");
    print_int(y);   // 10
    print("\n");
}
```

### Direct Hardware Access

```c
void main() {
    // Write 'A' to COM1 serial port
    outb(0x3F8, 65);

    // Read keyboard scancode
    int code = inb(0x60);
    print_hex(code);
    print("\n");
}
```

### Inline Assembly

```c
void main() {
    int result;

    asm {
        mov eax, 42;
    }

    // EAX now contains 42
    print("The answer is: ");
    print_int(42);
    print("\n");

    // Disable then re-enable interrupts
    asm {
        cli;
        sti;
    }
}
```

### Array Manipulation

```c
void main() {
    int arr[5];

    for (int i = 0; i < 5; i++) {
        arr[i] = i * i;
    }

    for (int i = 0; i < 5; i++) {
        print_int(arr[i]);
        print(" ");
    }
    print("\n");
}
```

### String Processing

```c
void main() {
    char* msg = "CupidOS";
    int len = strlen(msg);

    print("String: ");
    print(msg);
    print("\nLength: ");
    print_int(len);
    print("\n");
}
```

### File I/O

```c
void main() {
    int fd = vfs_open("/home/hello.txt", 0);
    if (fd < 0) {
        println("Cannot open file");
        return;
    }

    char buf[128];
    int n = vfs_read(fd, buf, 127);
    if (n > 0) {
        buf[n] = 0;
        println(buf);
    }

    vfs_close(fd);
}
```

---

## Common Patterns

### Parsing Command-Line Arguments

Programs receive arguments as a single string via `get_args()`. To parse multiple space-separated arguments, use a token parsing function:

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

This pattern is used in commands like `rm` to handle multiple file arguments.

### Error Handling with VFS

VFS functions return negative error codes on failure. Check return values and report a specific error:

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

Common VFS error codes:
- `-2`: No such file or directory (ENOENT)
- `-13`: Permission denied (EACCES)
- `-21`: Is a directory (EISDIR)

### Using ANSI Colors

Use hexadecimal escape sequences to embed ANSI color codes:

```c
void main() {
    char *red = "\x1B[31m";
    char *green = "\x1B[32m";
    char *reset = "\x1B[0m";

    print(red);
    print("Error: ");
    print(reset);
    print("Something went wrong\n");

    print(green);
    print("Success!\n");
    print(reset);
}
```

Common ANSI codes:
- `\x1B[0m` - Reset all attributes
- `\x1B[31m` - Red text
- `\x1B[32m` - Green text
- `\x1B[33m` - Yellow text
- `\x1B[34m` - Blue text
- `\x1B[35m` - Magenta text
- `\x1B[36m` - Cyan text
- `\x1B[95m` - Bright magenta

---

## Compiler Architecture

### Pipeline

The private in-kernel compiler preprocesses one translation unit, then lexes, parses, and emits x86 machine code in a direct pass. It does not build an AST or a separate IR.

```
Source (.cc)
    │
    ▼
┌─────────────────┐
│  Lexer           │  cupidc_lex.cc
│  (Tokenization)  │  Keywords, identifiers, literals, operators
└────────┬────────┘
         │ token stream
         ▼
┌─────────────────┐
│  Parser +        │  cupidc_parse.cc
│  Code Generator  │  Recursive descent -> x86 machine code
└────────┬────────┘
         │ raw bytes
         ▼
┌─────────────────┐
│  JIT: Execute    │  cupidc.cc - copy to memory, jump to main()
│  AOT: Write ELF  │  cupidc_elf.cc - emit ELF32 binary to disk
└─────────────────┘
```

### Source Files

| File | Lines | Role |
|------|-------|------|
| `cupidc.h` | 487 | Tokens, types, limits, compiler state, and public API |
| `cupidc.cc` | 4,147 | JIT/AOT driver, preprocessor, kernel bindings, and state setup |
| `cupidc_lex.cc` | 1,325 | Lexer for keywords, literals, operators, and delimiters |
| `cupidc_parse.cc` | 9,658 | Recursive-descent parser and direct x86/SSE code generator |
| `cupidc_elf.cc` | 147 | Fixed-address ELF32 executable writer for AOT mode |

### Lexer

The lexer (`cupidc_lex.cc`) recognizes a broader set than the short list below; these are representative groups:

- **Types and declarations:** C integer spellings, Cupid aliases, `float`, `double`, `float4`, `double2`, pointers, structures, classes, enums, qualifiers, storage classes, and function-pointer declarators
- **Control and expressions:** selection and loop keywords, `switch`, `goto`, inline `asm`, `new`, `del`, the full operator token set, `?:`, member access, and ellipsis
- **Literals:** decimal and hexadecimal integers, floating literals with fractions, exponents, or an `f` suffix, strings, and character literals
- **Names and punctuation:** identifiers plus the usual C delimiters

Whitespace, `//` comments, and `/* ... */` comments are skipped.

### Parser & Code Generator

The parser (`cupidc_parse.cc`) is recursive descent and writes x86 machine-code bytes directly into the code buffer. There is no AST or intermediate representation in this private compiler.

**Key parsing functions:**

| Function | Purpose |
|----------|---------|
| `cc_parse_program()` | Top-level: parse functions and globals |
| `cc_parse_function()` | Function definition with prologue/epilogue |
| `cc_parse_statement()` | Statement dispatch (if, while, for, return, etc.) |
| `cc_parse_expression()` | Operator precedence climbing |
| `cc_parse_primary()` | Atoms: numbers, strings, identifiers, unary ops |
| `cc_parse_asm_block()` | Inline assembly parsing and encoding |

**Code generation pattern:**

- Integer and pointer expressions use `EAX`; scalar floating and SIMD expressions use XMM registers, normally `XMM0`
- Unary plus preserves an arithmetic scalar. Unary minus uses `NEG EAX` for
  `char` and `int`, or flips only the IEEE-754 sign bit in XMM0 for `float`
  and `double`. Other operand types receive a specific diagnostic.
- Integer binary operations use the stack with `EAX` and `EBX`; floating and vector operations use XMM registers and explicit spills when needed
- Direct calls, stored function-pointer calls, and methods use four-byte
  scalar or pointer slots and eight-byte `double` slots. They preserve
  left-to-right evaluation, arrange complete words in source order, and clean
  the exact outgoing size. Floating results use the private compiler's XMM
  return path.
- Locals use `[EBP - offset]`, parameters use `[EBP + offset]`, and globals live in the data region
- Fixed floating arrays keep their scalar type and remaining row stride through
  one, two, or three dimensions. Leaf subscripts move values through XMM
  registers, and indexed arithmetic compound assignment uses the matching
  scalar SSE operation.
- Depth-one floating pointers retain their pointee width through declarations,
  address expressions, returns, function and method array parameters,
  dereference, subscripting, direct pointer updates, and assignment. Floating
  fields keep the same behavior through structure or class objects, arrays,
  and pointers.
- General `sizeof(expression)` parses its operand without keeping code, data,
  symbol, or stack side effects. An indexed multidimensional array reports the
  complete remaining row instead of its scalar leaf width.
- Direct functions and methods retain parsed fixed parameter types. Calls
  convert represented integer, `char`, `float`, and `double` arguments to the
  declared slot type before laying out cdecl words. A parsed variadic tail
  widens `float` to `double` and promotes `char` to `int`. Function-pointer
  calls, kernel bindings, and calls without fixed parameter metadata retain
  source-width slots. Represented pointer categories and integer null forms
  can fill a known pointer slot.
- Character operands undergo integer promotion in integer arithmetic and use
  the integer conversion opcodes for floating arithmetic, assignment, casts,
  and known fixed calls.
- Each primary expression starts with fresh subscript metadata. Fixed array
  symbols, address expressions, pointer casts, and `new` results publish their
  own known stride instead of inheriting one from a previous expression.
- Decimal `float` and `double` tokens use a fixed 1536-bit integer workspace.
  The converter rounds the exact ratio once to nearest with ties to even. An
  `f` or `F` suffix selects binary32 before rounding. It covers subnormals,
  finite limits, infinity, and signed zero. Numeric tokens may contain up to
  95 characters including a suffix. Longer tokens and missing exponent digits
  receive focused diagnostics, and parser recovery keeps the first one.
- Decimal and hexadecimal integer tokens accumulate in `uint32_t`. They reject
  values above `UINT32_MAX`, require at least one hexadecimal digit, and count
  `u` or `U` inside the 95-character token limit. Signed constant-expression
  arithmetic checks every operation; an unsigned operand wraps modulo `2^32`.
- Fixed arrays, records, globals, persistent REPL declarations, and automatic
  frames check their complete size before reservation. One cumulative frame
  path covers arrays, records, SIMD values, scalars, multi-declarator
  statements, and function pointers.
- Adjacent C string tokens are written directly to one data object. Each token
  may decode to at most 1,023 bytes, but the joined value can use the remaining
  8 MiB private data section. Automatic expressions, file-scope pointer
  initializers, and persistent REPL declarations share this path. An overlong
  token or a joined value that exhausts the data section fails without
  publishing a truncated string.

[ADR 0189](../docs/adr/0189-preserve-floating-values-in-private-cupidc-unary-signs.md)
records the unary-sign decision, signed-zero evidence, useful type failure,
and same-REPL recovery.

[ADR 0198](../docs/adr/0198-layout-private-cupidc-mixed-width-calls.md)
records the private scalar cdecl slot widths, shared call layout, parameter
offsets, cleanup, and guest evidence.

[ADR 0210](../docs/adr/0210-use-native-binary64-browser-numbers.md)
records typed private floating arrays and the Browser binary64 path that
requires them.

[ADR 0215](../docs/adr/0215-type-private-floating-lvalues.md) records typed
floating pointers, multidimensional arrays, record fields, array-parameter
decay, and unevaluated row sizes in the private compiler.

[ADR 0273](../docs/adr/0273-update-private-derived-floating-lvalues.md)
records one-time destination evaluation and exact postfix results for pointer,
index, and member floating updates in the private compiler.

[ADR 0216](../docs/adr/0216-private-simd-arrays-and-operators.md) records
matching packed arithmetic, one-dimensional fixed SIMD arrays, observable
operand order, and the remaining private compiler boundary.

[ADR 0257](../docs/adr/0257-descend-private-multidimensional-simd-arrays.md)
records two-dimensional and three-dimensional SIMD storage, checked row
descent, unevaluated row sizes, and complete-subscript assignment.

[ADR 0299](../docs/adr/0299-pass-private-simd-values-through-cdecl.md) records
the fixed-prototype 16-byte stack slots, XMM0 returns, and the unsupported
metadata-free call boundaries.

[ADR 0217](../docs/adr/0217-round-private-decimal-literals-exactly.md) records
the integer decimal converter, target-width rounding, token boundary, and
diagnostic recovery.

[ADR 0218](../docs/adr/0218-extend-browser-numbers-and-private-strings.md)
records the Browser primitive-number lane and the adjacent private C strings
needed by its active self-test.

[ADR 0219](../docs/adr/0219-support-private-tagged-struct-typedefs.md) records
tagged structure typedefs, checked private allocation arithmetic, integer
constant rules, member addresses, and persistent REPL record rollback.

### Symbol Table

Symbols are stored in a 4,096-entry flat array and searched backward so that locals shadow globals:

| Kind | Description | Storage |
|------|-------------|---------|
| `SYM_LOCAL` | Local variable | `[EBP - offset]` |
| `SYM_PARAM` | Function parameter | `[EBP + offset]` |
| `SYM_FUNC` | User-defined function | Code offset |
| `SYM_KERNEL` | Kernel binding | Absolute address |
| `SYM_GLOBAL` | Global variable | Data section address |

### Memory Layout

```
JIT Mode:
  0x01100000 - 0x011FFFFF  Code region (1 MB)
  0x01200000 - 0x019FFFFF  Data region (8 MB strings/globals)

AOT Mode:
  0x01100000 - 0x011FFFFF  Code segment
  0x01200000 - 0x019FFFFF  Data segment
  Code and data packed into a fixed-address ELF32 executable
```

### Forward References

When the parser encounters a call to an undefined function, it emits a placeholder `call` instruction and records a **patch entry**. After the entire program is parsed, `cc_parse_program()` resolves all forward references by patching the `call` targets with the correct addresses.

---

## Limitations

- Each invocation has a 1 MiB code buffer, an 8 MiB data/string buffer, and a 1 MiB preprocessor-output ceiling.
- Compiler state allows 4,096 symbols, 256 locals per function, 32 parameters, 4,096 forward patches, 1,024 functions, 64 structures, and 32 fields per structure.
- The private preprocessor supports quoted includes, `#pragma once`, object-like `#define`, `#ifdef`, `#ifndef`, `#else`, `#endif`, and Cupid `#exe`. Function-like macros and general `#if` expressions are not implemented.
- One preprocessed translation unit is compiled per invocation. AOT writes a fixed-address ELF32 executable directly; it does not produce separate relocatable objects for a later link.
- Programs use Cupid OS kernel bindings rather than a general hosted C standard library.
- Variadic declarations and definitions parse, but compiled CupidC code cannot yet traverse unnamed arguments.
- Direct code generation has no optimization pass.
- Numeric tokens are limited to 95 characters including a suffix. Integer
  literals are limited to the represented `uint32_t` range. Hexadecimal
  floating and `long double` literals are not implemented by the private
  compiler.
- One decoded string token is limited to 1,023 bytes. Adjacent tokens can fill
  the private data section, but wide strings and literal pooling are not
  implemented.
- Floating pointer depth greater than one, pointer-to-array types, and
  assignment through a pointer-valued floating field subscript remain
  unsupported. Indirect integer `++` and `--` also remain outside the private
  compiler boundary.
- SIMD pointers, SIMD record fields, allocation with `new`, SIMD array
  parameters, and row values remain unsupported. SIMD values cross only fixed
  direct function or method boundaries; variadic tails, unprototyped calls,
  and signature-erased function pointers remain rejected.

The private compiler implements a broader runtime floating and SIMD language.
The hosted self-hosting path converts between `float` and `double`, evaluates
matching or mixed floating arithmetic and all six comparisons, selects
matching or mixed floating conditional arms, and converts every represented
signed or unsigned integer through 64 bits to either floating width through
casts and assignment conversion. Runtime arithmetic, all six comparisons, and
conditional selection apply the same usual arithmetic conversions. Only the
selected integer arm of a conditional converts. Inputs through four bytes use
SSE; wide inputs use x87 `FILD` and the unsigned correction before the result
is stored at its C width. The path stores
`+=`, `-=`, `*=`, and `/=` results at the left width. Prefix and postfix `++`
and `--` work on
modifiable non-atomic lvalues. Each update evaluates its destination once and
adds or subtracts exact-width `1.0`. A postfix expression returns the original
raw payload, including negative-zero or NaN bits. It also carries existing
`double` values and
source `float` values promoted to `double` through ellipsis and unprototyped
calls and supports `va_arg(double)`. Decimal constants, represented integer
conversions, mixed integer and floating arithmetic, and comparisons use the
public SSE path. Static initializers use the integer-only IEEE evaluator
described above. Non-atomic `long double` values use x87 80-bit
transport for bounded finite normal decimal `L` tokens, floating-width
conversions, unary plus and minus, all four
arithmetic operators, twelve-byte direct and indirect fixed, variadic, and
unprototyped arguments, function returns, direct and indirect call results,
and `va_arg(long double)`. Runtime truth, structured conditions, and `_Bool`
conversion cover `float`, `double`, and automatic `long double`. Runtime
arithmetic, comparisons, and conditional selection convert every represented
value integer and enum to `long double`. Operations that mix an eight-byte
integer with `float` or `double`, atomic and `long double` updates,
hexadecimal floating literals, hexadecimal or subnormal long-double literals,
long-double ratios beyond the bounded parser and SIMD remain open in the
hosted path. Static long-double
arithmetic folds with integer-only 128-bit intermediates.
Static long-double
truth, comparisons, short-circuit logic, conditional selection, and
floating-width conversion fold into target data. Runtime integer conversions
involving `long double` cover all signed and unsigned i386 widths.

---

## Shell Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `cupidc` | `cupidc <file.cc>` | JIT compile and execute a CupidC source file |
| `ccc` | `ccc <file.cc> -o <output>` | AOT compile to ELF32 binary |

---

## User Programs (TempleOS-style)

The shell discovers CupidC programs in `/bin/` and `/home/bin/`. Programs added to `/home/bin/` can run without rebuilding the kernel.

### How It Works

When you type a command the shell doesn't recognize, it searches in order:

1. `/bin/<cmd>` - ELF/CUPD binary in ramfs
2. `/bin/<cmd>.cc` - CupidC source in ramfs (JIT compiled)
3. `/home/bin/<cmd>` - ELF binary on disk
4. `/home/bin/<cmd>.cc` - CupidC source on disk (JIT compiled)

The shell JIT-compiles a discovered `.cc` file before running it.

### Example: `mv` (Move/Rename Files)

The `mv` command is a CupidC user program at `/bin/mv.cc`:

```
> mv old.txt new.txt        # rename a file
> mv report.txt /tmp/       # move into a directory
> mv /home/a.txt /home/b/   # absolute paths
```

Source (`bin/mv.cc`):

```c
// mv.cc - move/rename files for CupidOS

void resolve_path(char *out, char *path) {
    int i = 0;
    if (path[0] == '/') {
        while (path[i]) { out[i] = path[i]; i = i + 1; }
        out[i] = 0;
        return;
    }
    char *cwd = (char*)get_cwd();
    int ci = 0;
    while (cwd[ci]) { out[i] = cwd[ci]; i = i + 1; ci = ci + 1; }
    if (i > 1) { out[i] = '/'; i = i + 1; }
    int pi = 0;
    while (path[pi]) { out[i] = path[pi]; i = i + 1; pi = pi + 1; }
    out[i] = 0;
}

void main() {
    char *args = (char*)get_args();
    // ... parse source and dest, resolve paths,
    // check if dest is a directory, call vfs_rename()
}
```

Key patterns used:
- `get_args()` - retrieve shell arguments
- `get_cwd()` - resolve relative paths
- `vfs_stat()` - check if destination is a directory
- `vfs_rename()` - perform the move

### Writing Your Own Program

See the [User Programs](User-Programs) page for a complete guide.

---

## Workflow

1. Write a `.cc` source file using the `ed` editor:
   ```
   > ed /home/hello.cc
   a
   void main() {
       println("Hello from CupidC!");
   }
   .
   w
   q
   ```

2. Run it with JIT compilation:
   ```
   > cupidc /home/hello.cc
   Hello from CupidC!
   ```

3. Or compile to a persistent binary:
   ```
   > ccc /home/hello.cc -o /home/hello
   Compiled: 42 bytes code, 20 bytes data
   Written to /home/hello
   > exec /home/hello
   Hello from CupidC!
   ```

---

## Comparison with HolyC

CupidC draws direct inspiration from TempleOS's HolyC:

| Feature | HolyC (TempleOS) | CupidC (cupid-os) |
|---------|-------------------|--------------------|
| Execution | JIT compiled | JIT + AOT (ELF) |
| Architecture | x86-64 | x86-32 |
| Privilege | Ring 0 | Ring 0 |
| Types | Full C types + classes | int, char, void, bool, pointers, arrays, structs |
| Enums | Yes | Yes |
| Inline ASM | Yes | Yes |
| Port I/O | Direct access | `inb()`/`outb()` builtins |
| Hardware access | Full | Full |
| Structs | Yes | Yes |
| Classes | Yes | No |
| Preprocessor | `#include` | No |

## Shell REPL

The normal shell prompt includes a TempleOS-style CupidC REPL.
The shell tries to compile each line as CupidC first and only falls back to normal
command dispatch when REPL parsing or compilation fails. TempleOS's
`ExeCmdLine()`, `LexStmt2Bin()`, and `CmdLinePmt()` are the design references.

### Prompt behavior

- End statements and expressions with semicolons, HolyC-style.
- Zero-argument REPL functions can be invoked as `Foo;`.
- Multi-line blocks stay open until braces balance.
- The next prompt shows the elapsed execution time.
- Expression results are surfaced as TempleOS-style `ans`.
- Forward-call fixups persist across prompt entries, so later function
  definitions can resolve earlier REPL-defined callers.

Example:

```c
/home> U32 x = 7;
/home> x + 5;
0.001s ans=0x0000000C=12
/home> ans;
0.000s ans=0x0000000C=12
/home> U32 Add(U32 a, U32 b) {
..>   return a + b;
..> }
/home> Add(2, 3);
0.000s ans=0x00000005=5
```

### Fallback behavior

These inputs resolve as shell commands because they are not valid CupidC REPL input:

```text
help
ls /home
```

### Resetting REPL state

Use:

```text
reset
```

This clears persistent REPL variables, functions, structs, typedefs, and `ans`.

## Current checked-seed proof

The 2026-08-14 integration keeps the full OS build green after adding integer
and long-double usual conversions and wide integer conversion to `float` and
`double`. The poisoned-host image build passed in 625.8 seconds. A private
four-vCPU guest then compiled and ran `/bin/ls.cc` through the in-OS compiler
as part of a 60.5-second parallel smoke pair. The current Linux and Windows
seeds carry these conversions through the fixed point. ADR 0292 records that
promotion.
