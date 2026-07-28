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

| Type | Size | Description |
|------|------|-------------|
| `int` | 32-bit | Signed integer |
| `char` | 8-bit | Character / byte |
| `bool` | 32-bit | Boolean (alias for int) |
| `U0` | - | HolyC-style `void` spelling |
| `U8`, `I8` | 8-bit | Unsigned/signed byte spellings |
| `U16`, `I16` | 16-bit | Unsigned/signed word spellings |
| `U32`, `I32` | 32-bit | Unsigned/signed dword spellings |
| `U64`, `I64` | parsed | Accepted C/HolyC compatibility spellings; current codegen remains 32-bit |
| `long`, `short`, `signed`, `unsigned` | parsed | Accepted C compatibility spellings; width is normalized by the 32-bit codegen |
| `float`, `double` | 32/64-bit | SSE scalar floating point |
| `float4`, `double2` | 128-bit | SSE vector types |
| `void` | - | No value (functions only) |
| `int*` | 32-bit | Pointer to int |
| `char*` | 32-bit | Pointer to char |
| `struct` | varies | User-defined composite type |
| `struct*` | 32-bit | Pointer to struct |

### Arrays

Fixed-size arrays, both local (stack-allocated) and global (data section):

```c
// Global arrays - stored in data section
int scores[100];
char buffer[256];

void main() {
    // Local arrays - stack-allocated
    int arr[10];
    char buf[64];

    arr[0] = 42;
    buf[0] = 'A';
}
```

Array elements are accessed with `arr[i]` and can be assigned with `arr[i] = value`.

Compound assignment also works: `arr[i] += value`, `arr[i] -= value`, `arr[i] *= value`, `arr[i] /= value`.

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
- Field types: `int`, `char`, `void*`, `int*`, `char*`, nested `struct`
- Stack-allocated structs (`struct Foo s;`) are zero-initialized
- Heap-allocated structs via `kmalloc(sizeof(struct Foo))`
- Member access with `.` (value) and `->` (pointer)
- Chained access: `rect.origin.x`, `ptr->origin.y`
- All fields are 4-byte aligned for x86 compatibility

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
  four into production
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

Compiler head also accepts GNU `used` and `__used__` on file-scope objects
and functions. Compatible redeclarations merge the flag, and the Linear IR
and object boundaries reject invalid frozen metadata. Current ELF32 bytes do
not change because every represented definition is already emitted. The
generated kernel-symbol source passes deterministic compiler-head emission,
and the checked seed now carries the capability. The normal recipe compiles
`kernel/cpu/ksyms_data.cc` through the checked production wrapper.

Compiler head also accepts the exact volatile
`call 1f\n1: popl %0` form used by the stack-trace helpers in
`kernel/lang/as.cc` and `kernel/lang/cupidc.cc`. It requires one modifiable
four-byte integer `=r` output. The shared x86 model emits a
zero-displacement `CALL` followed by `POP r32`, which captures the address of
the pop while restoring ESP. Both unchanged roots compile reproducibly as
validated i386 ELF32 objects under the full kernel profile. `as.cc` produces
148,056 bytes, and `cupidc.cc` produces 288,180 bytes. Their normal recipes
now use the checked seed. Other call templates and general inline-assembly
labels remain unsupported.

Compiler head also handles the two exact FXSAVE statements in unchanged
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
`UCOMISS` and `UCOMISD` produce a normalized signed `int` and handle unordered
operands so only `!=` is true for NaN. Decimal constants are published as
exact IEEE bits. Static-duration scalar and aggregate leaves use integer-only
IEEE binary32 and binary64 evaluation for unary signs, addition, subtraction,
multiplication, division, comparisons, casts, scalar truth, short-circuit
logic, and conditional selection. Represented file and block enumerators and
signed or unsigned integers through 64 bits can feed the evaluator. It rounds
each operation to nearest with ties to even and preserves signed zero before
the object reaches `.rodata`, `.data`, or `.bss`. Represented runtime
integer-to-floating conversions, floating-to-signed conversions,
floating-to-unsigned byte or word conversions, and mixed integer and floating
arithmetic use the SSE object path. Unsigned four-byte input uses an exact
split across the sign boundary. The x87 transport model, SSE conversion
oracle, and comparison execution oracle check rounding, operand order, signed
zero, infinities, quiet and signaling NaNs, call alignment, and frame state.
Runtime floating truth, hexadecimal floating literals, `long double`,
runtime conversion to unsigned four-byte integers or `_Bool`, runtime mixed
wide and floating arithmetic or conditional arms, floating increment and
decrement, SIMD values, floating atomics, and over-aligned object emission
remain unfinished.

Plain assignment, all ten compound assignments, and prefix and postfix update work for represented non-atomic integer bit fields when the declared storage unit is four bytes and fits inside the record. The compiler evaluates the record designator once and applies the target's integer-promotion rules before a compound operation. Partial fields preserve the other bits in their unit. Assignment, compound assignment, and prefix update return the stored lane after width truncation and signed extension, while postfix update returns the extracted old value. A 32-bit field uses the direct load and store path. Volatile 32-bit updates perform one read and one store. Partial volatile mutation, atomic fields, and other storage-unit sizes remain unsupported.

The shared path lowers explicit casts to `void`. It evaluates the operand once, discards a represented integer, pointer, supported structure, or floating result, and leaves a `void` operand off the abstract stack. An eight-byte integer or `double` lvalue is copied into its snapshot before that handle is removed; a `float` keeps its raw four-byte value.

Automatic initializer lists work for fixed arrays and complete structures whose alignment does not exceed four bytes. The lowerer zero-initializes the full object once, then evaluates explicit integer, pointer, supported structure, and narrow character-array string leaves in source order. Scalar and structure values store through the selected array and member path. A string leaf copies the retained bytes with `REP MOVSB`, so unused tail elements keep their implicit zero value. Nested lists, direct designators, and omitted subobjects retain their frontend meaning. The i386 emitter performs the initial zeroing with `REP STOSB`.

The shared path carries compatible, complete structure values through lvalue conversion, automatic expression initialization, plain and chained assignment, matching conditional expressions, casts to `void` and other discarded expressions, fixed direct and indirect calls, parameters, and returns. Each lvalue conversion and structure call result receives its own frame snapshot. Copies cover the full target object size, so a supported structure may contain wide or floating members even when an operation on that member remains outside the same-kind arithmetic boundary.

Structure arguments occupy inline cdecl stack storage in parameter order, and each argument is rounded up to four bytes. The caller clears ABI padding before it copies arguments. A structure-returning call passes a hidden destination pointer before the explicit arguments. The callee reads that pointer at `[ebp+8]`, reads its first explicit argument at `[ebp+12]`, copies the result into the destination, returns the pointer in `eax`, and uses `ret 4`. The caller cleans the explicit argument bytes.

Structure snapshots can contain nested unions because a copy preserves every target byte. A scalar member can also be loaded directly from a returned structure value. A union used directly as a parameter or result and an aggregate member selected from a structure rvalue remain unfinished.

The hosted cast path accepts a direct four-byte integer literal zero as a represented function-pointer null. It also preserves an object-pointer null written as `((void *)0)` when the frontend converts it to the destination pointer type. Represented function pointers may cast between signatures or to and from a represented 32-bit integer. Object pointers can convert to signed or unsigned eight-byte integers with a zero high word, and conversion back keeps the low word. Object-pointer and function-pointer interchange, narrow integer forms, and conversions between function pointers and wide integers remain unsupported. Static compatible character and void pointers accept an ordinary string literal through parentheses or a macro, but arithmetic and explicit casts on that static address remain outside the boundary. Pointer qualification accepts the safe `char **` to `char *const *` conversion. It rejects `char **` to `const char **`, which would add a qualifier at an unsafe nested level, and rejects removing the nested `const`.

An external array may omit its bound when its element type is complete. The shared IR can take that linked object's address, decay it to the compatible element pointer, apply the element scale, and continue through member access. The array remains incomplete, so it cannot be loaded as a value or used as if its storage size were known.

The exact hosted gate covers the hermetic Toolchain sources, `kernel/lang/as_elf.cc`, and complete command closures for CupidC, CupidASM, CupidDis, CupidLD, and CupidObj. The static-tool preprocessing audit runs all 20 C sources under the checked four-byte i386 Linux target: 19 strict C11 files and the GNU-enabled runtime. Repeated emission produces identical ELF32 objects, and Cupid's ELF32 reader checks each object before linking.

The repository runtime supplies the checked file, heap, memory, string, `errno`, `getcwd`, and formatted-output interfaces required by the five commands. CupidC emits the runtime, CupidASM assembles `_start` and the system-call boundary, and CupidLD links five deterministic static Linux i386 executables without unresolved symbols. A sixth executable checks allocation, tail release, files, seeks, errors, arguments, memory comparison, and strings. The runtime has unbuffered streams and single-threaded heap, stream, and `errno` state.

The `cupidc` driver compiles one C11 input to an ELF32 object. It accepts definitions, undefinitions, forced inputs, GNU or freestanding mode, and ordered include roots. `-I` enables quoted and angle lookup; `--include-angle` enables angle lookup only. Repeatable `-include` options run before the primary source in caller order. These path options accept native paths or absolute logical paths under `--root`. A compile failure preserves the previous output. Empty volatile extended assembly with one `memory` clobber remains an IR ordering point and emits no instruction bytes. The exact audited Doom-tree command now emits 73 of 80 objects, including unchanged `kernel/doom/src/am_map.c`. Seven pinned sources still need IR, union-initialization, implicit-call, or pointer-conversion work before ownership can move.

The five static i386 Linux tools have a checked seed. The manifest binds their hashes, sizes, target ABI, source revision, producer lineage, 19-source plan, and five link orders. The current CupidC image is the 2,109,488-byte stage-three output from revision `7e7029637ef22a4f18c382ffb225fd6a2ea84b85`, with SHA-256 `39a5783a5ba07a4891b887ea36a5686098dc9ca128b29419aea1e0c2cd8ee86e`. It carries exact static floating data and all six floating comparisons. Its plan uses `.cc` for all 19 C roots and has SHA-256 `59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

The bootstrap copies the 40-input source closure into a private compiler root. Both rebuilt stages compile from that root, and the harness checks the private and live closures at each stage and behavior boundary. The checked seed, stage two, and stage three all contain the same five tool images. The two rebuilt stages also match every C and startup object and agree on all five help paths, ten successful operations, and six failure cases. Their stage directories, behavior evidence, and report are published together only after the complete gate passes. See [Toolchain Bootstrap](Toolchain-Bootstrap) for the commands and report layout. Native contract runners, hosted development commands, and 90 normal Cupid OS root objects still come from a host compiler.

Supported direct and indirect calls put ESP on a sixteen-byte boundary immediately before `call`. The emitter chooses zero, four, eight, or twelve bytes of padding from the function frame, live Linear IR stack, and outgoing target-sized argument area. Prototyped, variadic, unprototyped, nested, structure, and wide calls follow the same rule.

For a variadic call, the shared frontend applies lvalue conversion, array and function decay, integer promotion, and `float` to `double` promotion to the ellipsis arguments as required. Every call instruction owns a contiguous slice of post-conversion actual argument types in a packed Linear IR array. A shared validator requires one complete ordered partition and rejects gaps, overlaps, invalid types, trailing entries, and metadata on non-call instructions. Named slots use declared parameter types after compatibility checking, while unnamed slots use the packed actual types. The emitter uses the validated slice and actual count for cdecl order, slot widths, the saved indirect callee, stack alignment, and caller cleanup. Direct and indirect calls can pass represented four-byte integers and pointers, signed and unsigned eight-byte integers, existing `double` values, or source `float` values promoted to `double` through an ellipsis. An eight-byte unnamed value selects the outgoing-area path. Arguments occupy increasing addresses in source order, with the low word before the high word for an eight-byte value. Each argument still has one abstract IR handle, and an indirect callee remains below the argument handles while the emitter prepares the outgoing area.

In GNU C mode, the shared frontend treats `__builtin_va_list` as a target `char *` cursor and retains typed start, argument, copy, and end operations. The i386 emitter starts the cursor after the full width of the final named cdecl parameter. A four-byte pointer, integer, or enum read advances the stored cursor by four bytes. A signed or unsigned eight-byte integer, 64-bit enum, or `double` is copied into a fresh private snapshot and advances the cursor by eight bytes. Both widths keep the i386 cursor on four-byte slot alignment. Execution contracts read successive wide integer and `double` slots through the original cursor and the first slot through a copied cursor. Nested callers also check aligned calls, cleanup, and complete returned values. Atomic, `float`, and aggregate reads remain unsupported. Calling `va_arg` with `float` is invalid C because variadic `float` arrives as `double`. The unchanged Doom compatibility header parses under its generated profile.

An empty identifier-list definition has zero parameters and keeps its non-prototype function type. Calls through a function type without a prototype apply default argument promotions to every argument. Each call keeps its actual count and post-conversion type slice in Linear IR, and the i386 emitter accepts represented four-byte integers and pointers, signed or unsigned eight-byte integers, existing `double` values, and source `float` values promoted to `double`.

Block-scope `struct` and `union` tags follow lexical C scope. The shared frontend handles forward declarations, same-scope completion, ordinary references, nested shadowing, and restoration after a nested block ends. A record tag declared in a function definition's parameter list stays visible through the outer body, then expires with the definition. A tag-only declaration may use the represented `typedef`, `extern`, `static`, `auto`, or `register` spelling, or a represented type qualifier, when it introduces a tag, and has no runtime work. An empty declaration with storage or type qualification cannot merely repeat a visible tag. A `for` initializer may use a visible record type or an anonymous record definition for its object, but it cannot introduce a named tag or omit the object. An anonymous definition can supply the type for a local or block-static object, including Doom's unchanged block-static `packs` array.

A block-scope `extern` object keeps a lexical alias to one canonical linked object. Compatible repeats share identity, an incomplete array can be completed, and a visible file-scope `static` object keeps internal linkage. The declaration creates no automatic storage or runtime IR. Block typedefs also follow the ordinary identifier scope. They retain stable scalar, record, or function types, allow exact same-type repetition and nested shadowing, and create no runtime IR or ELF record.

A block function declaration keeps a lexical alias and visible type alongside one canonical linked function. Plain and `extern` forms share compatible identity. A visible prior declaration contributes to the alias's composite type, but an expired sibling prototype does not change the type seen by a later old-style declaration. A visible file-scope `static` function keeps internal linkage, while a function introduced only in a block stays out of file lookup until a later file declaration publishes it. The declaration emits no runtime IR or storage. Direct calls use `R_386_PC32`, and function addresses use `R_386_32`. Active-source guards cover 27 declarations across nine files. The exact Doom profile still parses all of `kernel/doom/src/d_main.c`, including its local `forwardmove` and `sidemove` declarations.

Block enums keep each enumerator in the ordinary lexical binding stream. A later enumerator can use an earlier value, nested tags and constants shadow their outer names, and scope exit restores those names. Definitions work in declarations, record members, function-definition parameter lists, and block type names. Function prefixes and expression or initializer activation records preserve the exact point where each name becomes visible. Linear IR checks that lexical order before lowering runtime control flow, including type names in case values, loop headers, variadic reads, aggregate designators, and compound literals. Represented uses become integer constants, so enums need no frame slot, symbol, relocation, or runtime declaration instruction. File and block enumerators also feed static floating arithmetic, comparison, and conditional expressions. This covers the cursor constants in the production CupidC-built `kernel/gui/desktop.cc` object; the REPL limits remain a separate active guard in `kernel/lang/shell.cc`. Block declaration attributes, nested function definitions, nonempty identifier lists, atomic variadic access, aggregate arguments without declared parameter types, and aggregate variadic reads remain unfinished.

Block-static objects use static storage in the shared ELF32 path. The emitter places top-level `const` objects in `.rodata`, writable zero-filled objects in `.bss`, and other writable objects in `.data`. Each object receives a local symbol derived from its absolute block-binding index, so shadowed names remain distinct. `LOCAL_ADDRESS` reaches that symbol through an `R_386_32` relocation instead of an EBP-relative frame slot, and the declaration emits no runtime initialization code. Unused and unreachable block statics still receive storage.

Block-scope compound literals use one persistent unnamed automatic object per source site. Their initializer runs each time execution reaches the expression, and the resulting lvalue can flow through ordinary member access, indexing, address-taking, loads, stores, and calls. Repeated evaluation in one function invocation reuses the same object. Recursive calls receive a fresh object in their own frame. Aggregate lists are assembled in separate staging storage and committed only after every initializer read has finished. A narrow string root zeros and copies directly into its persistent character array.

Runtime narrow string expressions receive local `.rodata` symbols and `R_386_32` relocations. They can decay into pointers for initialization, arguments, indexing, and returns. Supported structure graphs have alignment no greater than four bytes and contain no stored `volatile` or `_Atomic` subobjects. A graph may contain a nested union, but top-level union and class values remain unsupported.

The shared frontend publishes decimal `float` and `double` constants as exact IEEE bits. It uses bounded integer arithmetic and rounds once to nearest with ties to even, so self-hosted compilation does not depend on a host floating library. A second integer-only evaluator handles static-duration arithmetic, comparisons, casts, scalar truth, short-circuit logic, conditional selection, enumerators, and represented signed or unsigned integer conversion through 64 bits. It rounds after each operation at the expression's binary32 or binary64 width and places the final bits, including signed zero, through the ordinary read-only, writable, or zero-filled policy. The IR and SSE object path cover represented runtime integer-to-floating conversions, floating-to-signed conversions, floating-to-unsigned byte or word conversions, mixed integer and floating addition, subtraction, multiplication, and division, and all six matching or mixed-width comparisons. Unsigned four-byte input uses an exact split conversion across the sign boundary. Hexadecimal floating literals, `long double`, runtime conversion to unsigned four-byte integers or `_Bool`, runtime floating truth, runtime mixed wide and floating arithmetic or conditional arms, and floating increment and decrement remain unsupported. Matching or mixed-width floating conditional arms and the four arithmetic compound assignments keep their established x87 path.

Compiler head retains GNU `noinline` and
`target("general-regs-only")` on canonical file-scope functions.
`noinline` records the request for a future inliner and does not change
current object bytes. Each IR function carries the canonical code generation
mask, and emission rejects a mismatch. The target option rejects
compiler-generated floating work while explicit source assembly stays under
its own typed contract.
Compiler head also accepts the exact volatile `ldmxcsr %0` form with one
addressable, non-atomic 32-bit integer `m` input. Linear IR evaluates its
address once, and the shared x86 model emits `0F AE 10` at `[EAX]`.
Unchanged `kernel/cpu/fpu.c` passes line 28 and now stops at the floating
`=m` output in the multiline MOVSS round trip on line 63. The checked seed
does not carry either compiler-head increment.

Static-duration and variable-length compound literals, the named-aggregate backward-jump alias case, explicit bit-field initializer leaves, Boolean mutation, atomic variadic access, aggregate arguments without declared parameter types, aggregate variadic reads, wide strings, literal pooling, and block-static addresses in other block-static initializers also remain unfinished in the shared path.

Across the root and supplemental builds, the checked CupidC seed owns 155 C
transforms. Its normal cohort has 149 transforms: 148 checked-in sources plus
the generated `kernel/cpu/ksyms_data.cc` source. All 149 sources use `.cc`.
The five shared Toolchain roots also belong to the 19-source i386 Linux
fixed-point plan, and native GCC or Clang rules select C with `-x c`. ADRs
0124 and 0126 record the first two naming steps, ADR 0129 records the lexer
transfer, ADR 0135 records the Nuked OPL3 transfer, and ADR 0139 records the
JPEG and glyph-raster transfer. Seven strict checked-in roots remain
host-owned.
Three generated installation tables and the `hello.cc`, `ls.cc`, and
`cat.cc` programs account for the other six CupidC transforms.

The Nuked OPL3 recipe compiles from a private snapshot of its source and
three-header closure. The wrapper compares every live input before replacing
the object, so a concurrent edit cannot publish a mixed result.

The strict kernel frontier must compile all 148 approved checked-in sources
twice. The full frontier passes against a 436-file snapshot with SHA-256
`333b915fb5bf42f7ed11456a1e09b3544dff74ece4c4d72fdecbabdf5e4cbfa7`.
Both 148-object sets are byte-identical and total 3,621,852 bytes. The
combined graph keeps the ISO fixture as an explicit image input. Strong
four-vCPU runtime gates pass with e1000 and RTL8139 networking through SMP,
RDRAND, all 62 crypto checks, USB storage, audio, TrueType glyphs, a baseline
JPEG decode, the desktop, terminal, and in-OS CupidC execution. The generated symbol source stores a logical
105,242-byte blob as little-endian `unsigned int` words with two trailing
pad bytes.

Forced poisoned-host builds cover every production wrapper recipe, and each
recipe declares its exact recursive header closure. A valid data-only object
may omit `.text` while its remaining sections and symbols still receive bounds
checks. The CSPRNG assembly emits RDTSC, CPUID, RDRAND, and SETC through
Cupid's x86 model while preserving EBX. The combined four-vCPU GUI gate reaches
SMP, all 62 crypto checks, e1000 traffic, the desktop, terminal, and CupidC
execution at `0x01100000`. A separate gate loads and reaps the same
external program twice at `0x00F00000`. ADR 0124 records the exact build and
runtime evidence. The host C compiler owns 142
transforms and 90 root objects. Host Python owns 170 transforms. The private
in-kernel CupidC compiler still handles embedded runtime compilation.

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

CupidC also accepts operand-free GNU assembly inside functions. Basic statements and extended statements with an empty output list are implicitly volatile. Exact sequences of PAUSE, NOP, STI, HLT, CLI, CLD, SFENCE, and FNINIT emit without a temporary frame slot or EBX traffic. These semantics serve the four normal-build e1000, desktop, socket, and TCP objects. At compiler head, an exact empty volatile extended template with one `memory` clobber and no operands remains an IR ordering point and emits no instruction bytes. That form compiles the unchanged Doom sound driver, though its build recipe remains host-owned until a later seed promotion.

The same compiler head accepts a modifiable four-byte object or `void` pointer
as the single `=r` output of `mov %%gs:0, %0`. It retains the pointer type,
evaluates the output destination once, and emits the absolute GS load as
`65 A1 00 00 00 00`.

Compiler head also accepts independent `r` and `c` inputs for the exact
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

Compiler head accepts one memory output for three machine-state
snapshots. Exact volatile `fnstsw %0` and `fnstcw %0` statements write a
modifiable 16-bit integer through `=m`; `stmxcsr %0` writes a modifiable
32-bit integer. Linear IR evaluates the lvalue once, and the emitter sends
the direct memory instruction through Cupid's x86 encoder without an output
staging slot. Other `=m` templates remain unsupported. The separate exact
call-next support also handles the later local-label statement in unchanged
`kernel/core/panic.cc`. Two full kernel-profile compiles produce the same
validated 10,212-byte ELF32 object. The checked seed carries this capability;
the normal build now uses it for the panic root.

Compiler head also represents `__atomic_load_n`, `__atomic_store_n`,
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

Compiler head and the checked seed parse all eight unchanged helpers in
`kernel/core/ports.h`. The byte, word, and doubleword IN and OUT forms keep
their declared integer widths while binding the accumulator and DX inputs.
The repeated word forms retain their read/write buffer and count operands.
INSW also retains its `memory` clobber. Output destinations are evaluated
once, and the string forms write back the final pointer and count while
preserving ESI or EDI across the cdecl call.

The i386 path emits `EC`, `EE`, `66 ED`, `66 EF`, `ED`, and `EF` for scalar
port I/O. The string forms emit `FC F3 66 6D` and `FC F3 66 6F` through the
shared x86 model. This brings the active non-Doom header gate to 155/155 at
compiler head.

The refreshed checked seed carries this port-I/O support. The normal build
uses it in the 148-source checked-in CupidC cohort. Earlier frontier evidence
measured the ACPI and MP-table objects at 5,708 and 4,156 bytes. Their current
`.cc` paths must pass the shared validator and re-run the four-vCPU contract
with both supported NIC paths.

Compiler head also accepts the GNU `Nd` alternative in the 8259 PIC helpers.
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
| `vfs_rename` | `int vfs_rename(char* old, char* new)` | Move/rename a file (copy + delete) |

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
| `blockcache_sync` | `void blockcache_sync()` | Flush all dirty cache blocks to disk |
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
| `cupidc.h` | 459 | Tokens, types, limits, compiler state, and public API |
| `cupidc.cc` | 3,963 | JIT/AOT driver, preprocessor, kernel bindings, and state setup |
| `cupidc_lex.cc` | 833 | Lexer for keywords, literals, operators, and delimiters |
| `cupidc_parse.cc` | 7,371 | Recursive-descent parser and direct x86/SSE code generator |
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
- Integer binary operations use the stack with `EAX` and `EBX`; floating and vector operations use XMM registers and explicit spills when needed
- Direct calls and stored function-pointer calls use cdecl stack arguments and caller cleanup; floating results use the private compiler's XMM return path
- Locals use `[EBP - offset]`, parameters use `[EBP + offset]`, and globals live in the data region

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

The private compiler implements a broader runtime floating and SIMD language. The hosted self-hosting path converts between `float` and `double`, evaluates matching or mixed floating arithmetic and all six comparisons, selects matching or mixed floating conditional arms, and stores `+=`, `-=`, `*=`, and `/=` results at the left width. It also carries existing `double` values and source `float` values promoted to `double` through ellipsis and unprototyped calls and supports `va_arg(double)`. Decimal constants, represented integer conversions, mixed integer and floating arithmetic, and comparisons use the public SSE path. Static initializers use the integer-only IEEE evaluator described above. Runtime floating truth, runtime mixed wide and floating arithmetic or conditional arms, increment and decrement, hexadecimal floating literals, `long double`, and SIMD remain open in the hosted path.

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
