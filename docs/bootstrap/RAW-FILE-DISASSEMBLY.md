# Saved raw images in the in-OS disassembler

The shell can inspect the flat image and map produced by
`as -f bin --map MAP -o IMAGE SOURCE`. This completes the saved-file interface
left open by ADR 0334. It uses the existing decoder and renderer.

```text
dis --raw --mode 16 --base 0x7c00 boot.bin
dis --raw --mode 32 --base 0 image.bin
dis --raw --range-map image.map --require-known image.bin
```

Fixed-mode requests require a mode and a decimal or hexadecimal base. A map
request gets its origin and code16, code32, or data boundaries from the map.
Both `cupid.raw-map.v1` and `v2` are accepted. The recorded size must match
the loaded image. The map, mode, and base forms are mutually exclusive.

`--require-known` rejects unknown, invalid, or truncated selected code before
rendering. Data ranges still produce `db` rows. This option does not require
local branch targets or source-edge agreement. Those hosted policies remain
separate. Legacy `dis FILE`, `exec -d FILE`, and permissive fixed-32 JIT
listings retain their existing adapters.

Paths use unquoted tokens and resolve against the shell's current directory.
The raw command rejects a full joined spelling that would exceed the VFS
path buffer, including the current directory and separator. Duplicate flags,
missing values, unknown options, extra operands, and conflicting forms fail
before opening a file. `--` ends option parsing for the input filename.

## Implementation

`dis_command_parse` returns a typed command with borrowed argument spans and
clears its result after a parse failure. The shell resolves its paths and
calls `dis_raw_file`. This adapter opens read-only VFS sources, supplies the
shared request, checks strict counts, renders, and closes both jobs.

`ctool_dis_parse_raw_map` owns the v1/v2 text grammar shared by the hosted and
kernel adapters. It retains the hosted grammar and edge validation. It counts
record slots, allocates exact arrays in the caller's arena, and rewinds its
allocations after failure. Successful arrays do not borrow the text buffer.
The hosted wrapper copies them into its existing CLI-owned arrays before
closing the parsing job. The kernel keeps a separate map job alive through
inspection and rendering.

Maps have a 1 MiB text limit. The kernel map loader applies that limit before
reading payload bytes; raw input retains its 64 MiB source limit. A constrained
arena can report an allocation failure before a malformed line is diagnosed.
Both outcomes leave the map result empty and preserve earlier arena data.

Source review caught an accidentally removed hosted option-value helper
during extraction; it was restored before compilation. Review also caught the
shell resolver's silent truncation of long relative paths. The raw command
now checks the complete joined spelling before calling that resolver.

## Verification

The native public adapter and shared map contracts pass. The CupidDis module
completes 38 tests: 37 pass and one expected Windows `/dev/full` case skips.
Checked CupidC also compiles all four changed kernel/toolchain units.
Independent readbacks verify the closed logs, results, source bodies, and
successful process cleanup.

The first normal `make -j2 all` attempt reached the outer runner's
1,200-second limit during compilation. Cleanup stopped its six remaining
processes. A fresh attempt with a 3,600-second outer limit completed kernel
production and strict flattening, then returned Make status 2 because three
exact artifact sizes still described the preceding kernel:

| Artifact | Previous exact bytes | Measured exact bytes |
| --- | ---: | ---: |
| `kernel/kernel.bin` | 9,369,664 | 9,403,588 |
| `kernel/kernel.elf` | 9,598,000 | 9,631,376 |
| `kernel/kernel.elf.pass1` | 9,466,928 | 9,500,304 |

ADR 0267 requires a measured policy update for intentional output changes.
Only those three policy integers changed. The remaining 13 exact artifacts
matched. After freezing the complete 451-product cohort and the unchanged
producer inputs, apart from that policy correction, image completion used
the four-option image-completion precedent in
[the artifact observations](ARTIFACT-OBSERVATIONS.md):

```text
make -j2 -o boot/boot.bin -o kernel/kernel.bin -o kernel/kernel.elf -o kernel/kernel.elf.pass1 all
```

This command returned 0, accepted all 16 exact artifacts, and created the
200 MiB normal image without recompiling or relinking the frozen products.
Both independent artifact reviews check the full reused cohort and image
chain. The original timeout and size rejection remain failed attempts; the
build evidence combines completed production with this guarded completion.

The guest smoke stages a 74-byte assembly fixture into a separate image
copy. It assembles the saved 10-byte image and v2 map under persistent
`/home`, reads the image in fixed 16-bit mode, submits a duplicate-mode
command, and then repeats strict mapped inspection twice. Each mapped
listing contains these adjacent rows:

```text
00008000:  B8 34 12  mov ax, 0x1234
00008003:  90  db 0x90
00008004:  B8 78 56 34 12  mov eax, 0x12345678
00008009:  C3  ret
```

The first guest attempt stopped at its 60-second command deadline while
assembly was flushing the `/home` backing store. A fresh attempt changed
only that UI wait to 300 seconds and passed in 378.502 seconds, within the
unchanged 1,200-second outer limit. It records five command completions,
two listings with the expected mapped rows, and both the new raw-file and
existing adapter DEBUG boot markers. The serial matcher permits telemetry
after an instruction row; the retained raw log includes that suffix. The malformed-command step proves completion and
recovery; the native parser contract proves argument rejection. The harness
runs a private writable copy, and the normal and staged input images remain
unchanged. All owned guest processes close successfully.

The normal image SHA-256 is
`ffa78630145e2a0c6855170f4c54dc50063dca6e86283b3faf37c48a39f02ce7`.
The passing guest serial log is 971,292 bytes with SHA-256
`03c9cb4e06f9a085c180116c8545a1032653e0470deaed267db35f6e257b7d8f`.
The retained review bundles are `raw-file-dis-validation-r1-20261016`,
`raw-file-dis-os-build-r1-20261016`, `raw-file-dis-os-build-r2-20261016`,
`raw-file-dis-os-completion-r1-20261016`, `raw-file-dis-guest-stage-r1-20261016`,
and the separate `raw-file-dis-guest-run-r1-20261016` and `r2` bundles.

The kernel contract uses an in-memory file store and checks both modes, v1/v2
mixed maps, strict rejection without a listing, permissive fallback, missing
files, stale sizes, invalid requests, source limits before reads, and recovery.
The shared contract checks text lifetime, all edge classes, malformed records,
exact text bounds, arena rollback, allocation failures, and same-job recovery.
The DEBUG guest self-test adds actual VFS map/file reads and strict failure
beside the existing buffer and ELF checks.

## Ownership

This interface adds no production build action and promotes no seed. Normal
ownership remains 449 CupidBuild actions and three Python coordinators. The
remaining image-input, SDK, and broader source qualification gates retain
their existing status. `TempleOS/` remains read-only reference material and
is excluded from progress counts.
