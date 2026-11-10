# ADR 0479: Address flexible members without completing the array

The image-input owner exposes borrowed const views of its copied strings.
Native warning checks rejected freeing those strings through the views. Its
private storage now owns linked allocations with a `char bytes[]` tail, so
cleanup frees the allocation and leaves the public views const.

That ordinary C representation exposed a CupidC gap. The frontend laid out
the record correctly, but IR member validation and emission required every
member to be a complete object. Even returning `value->bytes` failed.

Allow an addressable, unspecified array as the final non-bit-field member of
a struct. Its element must remain a complete, nonzero-sized object, and both
member and designator layouts retain their existing consistency checks. Decay
preserves element qualification. Address-taking also permits an addressable
unspecified array; it does not supply a size for that array.

Record size and member offset remain distinct. A flexible byte member can
start inside trailing record padding. Pointer arithmetic on its elements uses
their ordinary stride. `sizeof` of the incomplete array, arithmetic on a pointer
to the whole incomplete array, invalid elements, union members and nonfinal
flexible members retain their rejection contracts.

Native Windows and Linux regression callers compile, strictly disassemble and
execute the address/qualification/alignment cases. The original owner also
compiles and passes strict object certification. Its native and checked API
contracts pass on both hosts. Complete producer and normal OS acceptance remain
separate gates; see the [integration record](../bootstrap/SOURCE-OWNER-INTEGRATION.md).
