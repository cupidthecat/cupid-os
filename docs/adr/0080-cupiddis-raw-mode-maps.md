# Decode mixed-mode raw images with caller-supplied ranges

- Status: Accepted
- Date: 2026-07-22

## Context

CupidDis previously gave one decode mode to an entire raw image. That works for a homogeneous code buffer, but it cannot describe the active boot artifacts. `boot/boot.asm` changes from 16-bit code to 32-bit code and later returns to 16-bit data and descriptor declarations. `kernel/smp/smp_trampoline.S` changes from 16-bit startup code to a 32-bit entry point.

The bytes do not contain enough information for a disassembler to infer every mode transition reliably. Requiring callers to cut a flat artifact into temporary files would also lose the original base address, labels, and one-pass presentation.

## Decision

A raw inspection request can select `CTOOL_DIS_RAW_MODE_MAP` and provide a borrowed array of `ctool_dis_raw_range_t` records. Each record contains a byte offset and a 16-bit or 32-bit x86 mode. The first offset must be zero. Later offsets must increase strictly and remain inside a nonempty source. The range count cannot exceed the source byte count. A fixed-mode request leaves the range pointer null and the count zero.

Inspection validates the complete map before publishing a report. Rendering walks each range in order, preserves one label cursor, adds the range offset to the requested base address, and passes the selected mode to the shared x86 decoder. The source and range storage remain borrowed and must outlive rendering.

The hosted CLI accepts repeated `--mode-at OFFSET:16|32` options after the initial `--mode 16|32`. For example:

```text
cupiddis --raw --mode 16 --mode-at 0x80:32 --mode-at 0x140:16 --base 0x7c00 boot.bin
```

The caller owns transition placement. CupidDis validates the map structure, but it cannot prove that an offset falls between instructions.

Invalid maps use the existing invalid-request diagnostic path. Inspection zeros the output report and rewinds its temporary state. Rendering rejects a malformed or changed report before it writes output. A later valid request on the same job still succeeds.

## Rejected alternatives

Automatic mode inference was rejected because the same byte sequence can decode validly in more than one x86 mode. A wrong guess would look authoritative.

Copying each range into a separate request was rejected because it would repeat inspection setup, fragment labels, and make callers reconstruct addresses and output order.

Attaching a mode to every decoded instruction was rejected because the streaming report deliberately avoids one retained record per instruction. Ordered source ranges express the active requirement with bounded metadata.

## Consequences and evidence

The raw contract decodes one 16-bit, 32-bit, 16-bit fixture at base `0x7c00`, keeps a label at the first transition, and reproduces identical output on a second render. Negative cases cover absent storage, zero ranges, empty input, a nonzero first offset, an invalid mode, an offset outside the source, duplicate or decreasing offsets, too many ranges, fixed mode combined with range data, and a changed report. Every inspection failure leaves a zero report and permits recovery in the same job.

Source guards pin the active transitions in `boot/boot.asm` and `kernel/smp/smp_trampoline.S`. CLI tests cover both `--mode-at VALUE` and `--mode-at=VALUE`. The in-kernel fixed-mode callers initialize the added request fields explicitly.

This change does not move tool ownership or remove a host dependency. CupidDis still owns the normal kernel symbol inspection transform, while GCC or Clang builds the hosted CLI and the normal C objects. Dynamic ELF domains, DWARF views, the rest of the bounded x86 domain, production use of raw maps, and Cupid-built CupidDis remain open.
