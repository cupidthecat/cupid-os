# Build e1000, desktop, socket, and TCP with checked-seed CupidC

- Status: Accepted
- Date: 2026-07-24

## Context

ADR 0099 added the operand-free GNU assembly used by
`drivers/e1000.c`, `kernel/gui/desktop.c`, `kernel/network/socket.c`, and
`kernel/network/tcp.c`. ADR 0102 carried that compiler through the checked
i386 Linux seed. A detached hybrid build proved that the four resulting
objects could survive both CupidLD passes, CupidObj conversion, and a GUI
boot, but the root Make graph still assigned them to GCC or Clang.

These sources are not disposable leaf code. They cover a production network
driver, the desktop event loop, the socket API, and the TCP state machine.
Their hand-off therefore needs both the normal graphical boot and live
network behavior. Object validation and the earlier hybrid link are not
enough on their own.

## Decision

The checked-seed kernel wrapper owns an exact 26-source allowlist:

- all 20 `kernel/crypto` translation units;
- `kernel/smp/acpi.c` and `kernel/smp/mp_tables.c`;
- `drivers/e1000.c`;
- `kernel/gui/desktop.c`;
- `kernel/network/socket.c`; and
- `kernel/network/tcp.c`.

This does not approve whole driver, GUI, or network directories. Nearby
sources remain host-owned until CupidC supports their unchanged requirements
and they receive their own behavior gates.

The four Make recipes call `$(CUPIDC_KERNEL_COMPILE)` and list their complete
recursive header closures plus the shared wrapper, frontier, seed verifier,
manifest, and checked tools. The wrapper owns the fixed `KERNEL_I386`
compiler profile, so the desktop recipe no longer passes the host-only
optimization setting.

Before each compile, the wrapper verifies and freezes the seed. It accepts
only a validated i386 ELF32 relocatable object and replaces the requested
target after the complete output is ready. The deterministic frontier watches
314 source, header, profile, and control inputs. It rejects case-insensitive
artifact-name collisions, compiles all 26 approved sources twice, validates
both runs, and publishes only a complete matching directory.

The four link positions remain unchanged:

| Object | Static kernel-object position |
| --- | ---: |
| `kernel/gui/desktop.o` | 35 |
| `kernel/network/socket.o` | 79 |
| `kernel/network/tcp.o` | 80 |
| `drivers/e1000.o` | 85 |

## Rejected alternatives

Rewriting the four sources to avoid their existing assembly, initializer,
structure, string, wide-integer, or lexical-scope requirements was rejected.
Those are ordinary requirements of active Cupid OS code, so the compiler must
represent them.

Approving every source beside the four files was rejected. A broad directory
rule would hide real compiler gaps and weaken the allowlist's ownership
meaning.

Keeping the detached hybrid as the final proof was rejected. It showed that
the bytes linked and booted, but it did not transfer the normal build recipes
or protect them from a host compiler regression.

Poisoning the host compiler for the complete image build was rejected as
invalid evidence. The audit still assigns 271 C transforms to GCC or Clang.
The acceptance gate poisons the 26 CupidC-owned recipes instead.

Reducing the kernel heap or managed-memory range to fit the network harness's
old 128 MiB QEMU machine was rejected. Cupid OS defines a 512 MiB managed
range and a 256 MiB initial heap. The test machine now supplies the memory
that the unchanged OS expects.

## Consequences and evidence

The strict frontier contains 366,592 byte-identical object bytes. The four
new production objects are:

| Source | Object bytes | SHA-256 |
| --- | ---: | --- |
| `drivers/e1000.c` | 8,780 | `38e896c6b1d0359c858a7601d6c0b692786b9ff439d78c933fdde7af2d07d875` |
| `kernel/gui/desktop.c` | 111,196 | `f6f0edc79419ebd8ecfaf9254a17dfb8fe8b6cc7139bf16f872c0ce0a8fba340` |
| `kernel/network/socket.c` | 12,416 | `dff17d1b2e668f577aab6d45ef341a226ebaf7ae7278c5c8a2d0aafcd0346ee5` |
| `kernel/network/tcp.c` | 20,204 | `831f2a82687ab327f4b48b28fef69104cc94af0770dc6caf7b8a8df5b87a7368` |

The active graph still contains 698 inputs, 252 feature requirements, 501
transforms, and 39 accounted unreachable files. Ownership moves from 22 to
26 checked-seed CupidC transforms and from 275 to 271 host-C transforms.
Python owns nine existing transforms and launches the 26 checked-seed
compiles, so it appears on 35 transforms in all.

Positive tests compile every approved source and compare repeated output.
Negative tests cover unapproved sibling sources, a missing source, malformed
ELF, late failure after partial output, nondeterministic output, source drift,
header drift, rollback, and case-insensitive artifact-name collisions. The
Make contract also checks each complete recursive header closure and a
poisoned host command.

The runtime acceptance has two parts. The strong four-vCPU GUI gate requires
SMP discovery, every CPU online, RDRAND, all 62 crypto checks, e1000, the
desktop, the terminal, and embedded CupidC completion. The network gate runs
both RTL8139 and e1000 machines and checks DHCP, ARP, two ICMP exchanges, the
guest TCP client, and the guest TCP server. Its standard-library PCAP reader
correlates ARP, DHCP, and ICMP exchanges plus one public guest-client
handshake and one inbound guest-server handshake. Each direction must have
its own bidirectional teardown with coherent TCP sequence and acknowledgment
state. A causal sequence graph accepts overlapping retransmission,
simultaneous close, and crossed or reordered data, but it rejects late data,
impossible acknowledgments, and sequence-valid resets. The reader also
rejects multicast client destinations, guest self-connections, bad IPv4
checksums, and invalid handshake or close flags.

The final forced four-target build succeeds with
`CC=__cupid_host_cc_must_not_run__` and reproduces every object hash above.
`make test-kernel-cupidc-frontier` compiles the complete production cohort in
135.280 seconds. The final `make test-net` run completes both live NICs and
their direction-aware packet checks in 193.177 seconds.

The clean normal build completes in 108.074 seconds. Both CupidLD link passes
expose the same 3,889 text and weak symbols. The generated symbol blob is
93,384 bytes.
`_loaded_end` leaves 2,029,496 bytes in its reserved area, `_kernel_end`
leaves 849,648 bytes below the fixed stack, and the raw kernel matches the
image at LBA 5. The accepted preboot artifacts are:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel.elf.pass1` | 6,444,404 | `5af0d2f1aba1655f1d5c868b8dbc2b43d158eafc409da8acf942a1f2b97215f5` |
| `kernel.elf` | 6,534,516 | `1b9909ce6e02706201e97db12a19ccad5f534429b13c7a4828f552e5e2335e27` |
| `kernel.bin` | 6,356,552 | `3b9d786dfaee479f6c598e49506e516bb35ecdcc2a3d16813a7ff937a0db193b` |
| Disk image | 209,715,200 | `c74cffe2b603d60598939e3e31d03abf652a1fce112104bce18627af7218b425` |

A copy of that image passes the strong four-vCPU GUI runtime contract in
48.729 seconds. Its 21,662-byte log has SHA-256
`46c171cf00c70160daf56d32ed9ecb1cc1af373e1e667a1ee8af6bf53a51c5e8`.
The complete repository gate passes 532 tests in 2,138.320 seconds with one
expected skip. The checked audit assigns 26 transforms to CupidC and 271 to
the host C compiler; its JSON SHA-256 is
`dfe800a37e358e54db58fa7db4e98dbc12e6f7c83af4f63689dcebf1d92d763c`.

Python and WSL remain launch dependencies for the checked Linux seed on
Windows. Native hosted commands and contracts remain host-built. The private
in-kernel CupidC compiler continues to own embedded JIT and AOT compilation.
The next production C cohort should be chosen from unchanged source
requirements, not from which files are easiest to rewrite. Issue #28 remains
open for the remaining strict kernel, driver, and generated-C cohorts.
