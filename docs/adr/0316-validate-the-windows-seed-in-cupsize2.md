# ADR 0316: Validate the Windows seed in CUPSIZE2

## Status

Accepted on 2026-08-21.

## Context

ADR 0297 moved the artifact-size decision into a CupidC-built contract. The
`CUPSIZE1` request carried the policy, Linux bootstrap manifest, and artifact
observations. Later work expanded the policy to five Windows seed images and
made Python capture the complete Windows cohort, but the C contract still did
not parse its manifest.

That split left Python as the only semantic authority for the native execution
seed inside this gate. The contract could check the five policy sizes without
proving which Windows tools the manifest named, how they were produced, or
whether the files matched their recorded digests.

## Decision

Advance the private request protocol to `CUPSIZE2`. Keep the report schema at
`cupid.artifact-size-verification.v1`.

Add the captured Linux manifest digest, the raw Windows execution-seed
manifest, and five typed Windows file observations to the request. Each
observation carries its logical path, regular-file kind, size, and SHA-256.

Make the CupidC-built contract validate the Windows manifest schema, exact
five-tool inventory, filenames, producer flags, target fields, and provenance.
The provenance must name the paired stage-four generation, the fixed-point
command and result, the three producer lineages, the 50-input source count, and
the source revision and snapshot. Its parent digest and source revision must
match the captured Linux manifest, and its source snapshot must match the
Linux provenance.

Match every Windows manifest size and digest against its captured file
observation. Match each Windows seed size in the artifact policy against the
same manifest. Reject missing, extra, duplicate, unsafe, malformed, truncated,
or trailing request data.

Keep filesystem ownership in Python. The wrapper still pins the repository,
captures both seed roles, materializes the native execution cohort, runs an
independent policy oracle, rereads live inputs, and controls the publication
boundary.

## Evidence

The focused semantic-contract module contains 22 tests. The checked runner
module contains 16 tests, and the independent policy module contains 13 tests.
The 51 focused tests pass with four existing platform-specific skips.

The negative matrix covers schema, target, provenance, parent linkage, source
identity, exact tool inventory, filenames, producer flags, policy sizes,
observed sizes and digests, duplicate or missing observations, unsafe paths,
malformed JSON, truncation, and trailing input.

No result is recorded for the still-running Make target at this decision
boundary.

A later source-head artifact-contract run passed twice against all fourteen
exact artifacts. It measured these kernel outputs:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 9,366,752 | `263c124ab0e3c801196b5e24e86b362460eccd3b17366501fe41bdd3a907887c` |
| `kernel/kernel.elf` | 9,493,728 | `00727f9d73cdf0be5dbd01f561a8a82aba0a99bc4e1c679756349aa934056de7` |
| `kernel/kernel.bin` | 9,270,116 | `9045039d62810684c38747a2c487ac629308da3e266b76450ddbd56375488532` |
| `cupidos.img` | 209,715,200 | `07bb498567798b72d5f9658f18c51aff8fc600ee419b9b95add26eb2bb298ac7` |

This later result does not promote a checked seed or adopt a new production
selector.

## Rejected alternatives

Leaving Windows manifest semantics only in Python was rejected. The
CupidC-built contract already owns the artifact-size decision and receives the
same frozen cohort.

Passing only five Windows sizes was rejected. Size alone does not bind a tool
name, digest, producer role, target, or parent seed.

Changing the report schema was rejected. The successful output still reports
the same fourteen-artifact decision; only the private request and validation
inputs change.

## Consequences

The artifact-size gate now checks the Windows execution seed independently in
CupidC and Python from one frozen cohort. A manifest or file disagreement
blocks image publication before the normal publisher can proceed.

This logic lives in a source-built contract. The checked seeds compile,
assemble, link, and run that contract, but they do not carry the `CUPSIZE2`
policy logic themselves. No seed promotion, owner transfer, host dependency,
or report-schema change follows from this decision. `TempleOS/` remains
read-only reference material.
