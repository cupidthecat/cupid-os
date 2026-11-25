# Keyboard FIFO repair

`keyboard_get_scancode()` consumes the oldest queued event and advances the
reader cursor. It previously read and advanced the writer cursor, corrupting
FIFO order. The event reader and scancode reader now share the same queue
correctly. Capacity stays at 255 events, with the oldest dropped on overflow.
Empty scancode reads return zero without consuming COM1 input. Physical key
and modifier state remain unchanged.

The scancode API has no active callers. This repair does not explain or resolve
the separate Doom exit diagnostic.

## Verification

All 21 focused checks pass on Windows and Linux, including fixtures compiled
and linked with checked CupidC, CupidASM and CupidLD. They cover every starting
cursor position, mixed readers, wraparound, overflow, null destinations and
serial fallback. The original driver fails six of the eight native/checked
FIFO cases on each host.

The original complete kernel method also passes on both hosts:

| Check | Windows | Native Linux |
| --- | ---: | ---: |
| Elapsed seconds | 1,756.196 | 1,760.209 |
| Sources compiled twice | 156 | 156 |
| Object bytes per pass | 4,253,532 | 4,253,532 |
| Original deadline, seconds | 2,340 | 2,340 |

Only the keyboard source changes in the 487-input snapshot. The driver object
stays at 11,652 bytes. Its three field-offset immediates change from `head` to
`tail`; the other 36 functions, relocations and data stay identical. The kernel
test's complete AST stays identical after two measured hash substitutions.
All remaining object locks, counts, byte sizes and timeouts remain checked.

The incremental image reconstruction reuses 426 verified baseline objects and
rebuilds the driver, embedded USB manual and derived symbols. The symbol object
stays byte-identical, leaving 427 of 429 objects unchanged. The manual adds 256
bytes to the raw kernel; alignment adds one page to the final ELF and leaves
pass one's size unchanged. All three kernel products contain the current manual.
Only the two changed artifact-size rows are updated.

An independent linker replay reconstructs both complete 4,094,181-byte text
sections from all 429 input objects, checking 3,436 strong symbol locations and
46,720 relocations per image. It accounts for the relocated data/BSS addresses
after manual growth as well as the three driver instruction changes. Separate
readback checks raw flattening, five-sector boot placement and the whole image.
All 15 installed seed files remain unchanged. Both audit checks pass.

The private four-CPU guest passes the full 31-command graphics, USB and audio
frontier in 859.485 seconds. Ordered readback finds all command success and
interaction markers, 62 TLS successes and 32 terminal completions. Independent
framebuffer decoding finds 100,734 changed pixels. Closed AC97 and PC speaker
captures contain nonzero PCM. This does not establish Doom music quality.

## Evidence

The image has SHA-256
`0197b14c5ed3a8c2a71120f8a2902125e40dcc2096250bb473257996b58b191f`.
The [archive](evidence/keyboard-fifo-repair-20261110.json.gz) has 987 file records,
65 local payloads and 919 verified parent references. Its 15,369,618 bytes have
SHA-256 `104d9d07d884fa2b71de18905d591c37637b6a988fa7d9c2c1a13c6c5330feaa`.
Whole source images remain local hash references.

Early observers incorrectly expected the symbol object to change and only
three linked text bytes to differ. Full relocation replay accounts for the
actual changes. Its initial zero-filled text padding was also corrected to
CupidLD's `0xCC` padding. These observer corrections change no production output.

Complete repository acceptance, seed adoption and in-guest self-hosting remain
open.
