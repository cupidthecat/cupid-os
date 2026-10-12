# Private CupidC halfword candidate

This candidate gives the private i386 compiler real two-byte `short`,
`unsigned short`, `I16` and `U16` objects. They previously occupied the
compiler's four-byte integer representation. The candidate remains outside
the engineering checkout's active compiler while OS qualification is pending.
The standalone checked compiler and its 99 producer inputs are unchanged.

## Representation and conversion

New signed and unsigned halfword types and pointer types append to the private
type enumeration, preserving all preceding enum values. Objects occupy two
bytes and align to two bytes. Pointers and fixed cdecl parameter slots occupy
four bytes. Halfword loads sign-extend or zero-extend into EAX; stores write
exactly two bytes. Casts, assignment, fixed arguments and returns normalize
the low sixteen bits. Default argument promotion converts either halfword
type to `int`. Floating conversion truncates before narrowing; floating
compound assignment computes in its floating type before assignment conversion.

The tests cover direct values, neighboring array elements, packed record
fields, fixed callbacks, two- and three-dimensional arrays, pointer strides,
compound assignment and prefix/postfix updates. Integer global initializer
lists store their declared one-, two- or four-byte widths, reject excess
elements and nonconstant values, and diagnose division by zero. Character
constants, escapes and the constant parser's existing arithmetic forms work
in those lists.

## Failures and review repairs

Five initial requirement methods produce ten assertion failures on each host
against the original private compiler. Early candidates expose incorrect
dereference sizing, array stores, global initialization, pointer strides,
floating compound conversion and trailing declaration specifiers. One patch
lands in the wrong parser branch and fails the harness build; that version's
recipe and log are retained.

Review then finds unsigned-word widening, lost multidimensional row shape,
constant metadata that disagrees with narrowed runtime values, and repeated
`short` specifiers. Their regressions produce ten assertion failures before
the fixes. A final review finds character constants rejected by the new
integer-array initializer. Its JIT and AOT regressions both fail before the
constant parser accepts character tokens. All twenty-four new methods now
pass. Both final review axes report no remaining required finding within
this candidate's scope.

## Evidence and limits

The four complete integration modules pass 517 selections on each host.
Windows runs all selections; Linux skips eight Windows-only cases. Their
unittest times are recorded in the
[evidence index](evidence/private-short-integration-20261012.json.gz), alongside
the runner times and all 2,245 unchanged controlled inputs. Only the private
header, parser and ABI test change from the isolated network projection.

The 306-member originals archive has 3,870,663 bytes and SHA-256
`57db55467902a624307815c17bf02698cfdd7734c1d802548c0e0d0b9cd7a9e5`.
Every member is reread. It retains the final complete source bytes, reviews,
positive and negative logs, and selected source snapshots for the last
character-constant regression. Some earlier experiments retain hashes and
logs without complete intermediate parser snapshots; the index states that
limit.

This does not establish complete C initializer or pointer semantics. The new
flat integer-list helper is wired into full-program global declarations;
REPL global arrays and initialized block-static arrays still need their own
qualification. Pointer difference and indirect pointer compound assignment
also remain outside this increment. Native bindings that accept halfwords
retain their legacy registrations in this preserved type-only candidate. A
[separate network follow-up](PRIVATE-HALFWORD-BINDINGS.md) adds eight fixed
signatures and declaration-specific ABI oracles. The checked Windows seed
compiles both changed kernel units in
116.008 runner seconds with all nine host-producer variables disabled. The
parser object has 485,792 bytes and the binding object has 299,688 bytes. The
[source-build index](evidence/private-short-checked-source-20261012.json.gz)
binds both complete objects and logs in a 363,215-byte archive, SHA-256
`1676d96124e3b2b8c93c4e566c427fbed527d72e79decbb3b33ae712bc9fa4a6`.
All six members are reread. Complete paired OS builds and guest execution
remain required before adoption. No
source owner transfers, installed seed changes or `.c` naming transfers are
claimed here. `TempleOS/` remains read-only reference material.
