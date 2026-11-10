# Keep CupidDis inspection typed and rendering streaming

CupidDis will compose the shared ELF32 and x86 modules behind two small
operations: inspection produces a typed report whose arrays live in the job
arena, and rendering streams a selected textual view through a platform text
sink.  Source bytes, names, raw labels, and ELF payload views remain borrowed,
so both the source and inspecting job outlive rendering.  A report keeps
the input kind, requested views, raw-mode metadata and labels, and the shared
ELF32 read model.  It does not retain one allocation per decoded instruction;
rendering walks each executable region through the shared x86 decoder.  Host
and kernel drivers own argument parsing, file loading, VFS status mapping, and
their stdout or shell adapters.  They do not own ELF traversal, opcode tables,
operand formatting, relocation overlays, or report ordering.

Raw inspection requires an explicit base address and either one 16-bit or
32-bit mode or the ordered mode ranges recorded by ADR 0080. A mapped request
can decode one mixed-mode flat image without copying or splitting its bytes.
ELF inspection accepts static
little-endian i386 relocatable objects and executables.  Dynamic symbol tables
and their relocation ownership are explicitly unsupported until the typed
model represents multiple symbol-table domains.
The shared ELF32 read model therefore gains file-header and bounded program-
header data plus read-only `ET_EXEC` support, while its canonical writer stays
`ET_REL`-only.  Sectionless CupidC and CupidASM executables are disassembled
from executable load segments; relocatable objects are disassembled from
executable sections.  This supersedes ADR 0002 only where that decision kept
program headers and executables outside the reader; final executable layout
still belongs to CupidLD.

Human reports preserve serialized file order for headers, sections, symbols,
and relocations, followed by executable regions in file order.  The dedicated
`nm` text view sorts symbols by address with serialized symbol index as its
tie-breaker and remains compatible with the staged `nm -n` consumer.  Typed
in-process consumers use the report instead of parsing either text format.
View-gated report indexes restore serialized relocation order over the ELF
reader's target-grouped slices, provide target/offset relocation-site lookup,
and sort exact-count function labels once.  Region rendering then advances
through labels and binary-searches relocation fields instead of rescanning
symbols or relocation slices for every instruction.  Renderer callbacks are
never invoked while a temporary arena allocation is subject to rewind.
Unknown or invalid instruction bytes render as data and make deterministic
progress; a truncated tail renders once.  A relocation may annotate a decoded
field, but raw decode never invents relocation ownership.

The CLI renders familiar text, but command-line text is not the
architectural interface.  A fully normalized instruction document was
rejected because its arena cost scales with every byte of large kernel images.
A text-only single call was rejected because future symbol, linker, and debug
consumers would have to scrape presentation output.  Streaming means an output
adapter failure may occur after an earlier prefix was accepted; inspection
itself remains transactional and zeros its report on failure.

## Production ownership status

The normal two-pass kernel build now passes the hosted CupidDis executable to the existing numeric-reader subprocess seam. CupidDis therefore owns the one composite symbol-inspection transform; Python still filters and serializes the generated C blob, and the host compiler still compiles that source. GNU/LLVM `nm` remains an optional transition oracle rather than a required build input. The current pass-one kernel yields the same 3,790 consumed text/weak symbols and byte-identical 91,190-byte blob through both readers; the rebuilt image boots and a deliberate panic resolves `kernel_panic` from that blob.

The address order and serialized-symbol-index tie break specified above remain the Cupid contract. Generic `nm -n` implementations may order equal-address non-consumed rows lexically instead. No current consumed kernel symbol shares an address, and a hosted contract pins CupidDis's deterministic index ordering; future aliases therefore deepen the explicit Cupid policy rather than silently inheriting a host utility's tie rule.
