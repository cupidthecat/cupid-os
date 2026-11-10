# Native artifact policy observations

Source-head CupidBuild validates artifact policy with captured seed facts under
[ADR 0415](../adr/0415-validate-artifact-policy-from-reviewed-seed-observations.md).
The installed seed still uses its previous implementation until a separate
paired release passes production acceptance.

`verify-artifact-sizes` retains the selected policy, reviewed release, Linux
manifest, Windows manifest, twelve tool images and sixteen artifact observations.
It validates the complete release-bound pair, supported manifest plans and
actual ELF32/PE32 images before constructing a policy request. Alternate Linux
execution cohorts receive the same release validation. Every observation and
seed-directory membership is rechecked before close and success output.

`artifact_size_policy_validate_observations` decides the policy from immutable
`CUPSIZE3` bytes. It requires canonical policy order, all sixteen exact paths and
owners, seed sizes matching policy rows, all six Windows size/digest observations,
and complete artifact observations. Missing, unavailable, linked or wrong-size
artifacts retain the existing diagnostics. Success reports the existing artifact
count and total. Failure clears the result and respects diagnostic capacity.
The API keeps no pointers or mutable global state.

The caller owns release authority and must bind seed facts to validated manifests
and actual image bytes. The policy API neither reads files nor authenticates those
facts. The original `artifact_size_policy_validate` API continues to accept only
`CUPSIZE2`, including its historical manifest and parent checks.

## CUPSIZE3 layout

All integers are little endian. A byte string has a four-byte length followed by
exactly that many bytes. Tool facts use the fixed role order CupidASM, CupidC,
CupidDis, CupidLD, CupidObj, CupidBuild.

| Field | Encoding |
| --- | --- |
| Magic | Eight literal bytes `CUPSIZE3` |
| Policy | Byte string containing the existing policy JSON |
| Linux manifest path | Logical-path byte string |
| Windows manifest path | Exact `bootstrap/seeds/i386-windows/manifest.json` byte string |
| Linux facts | Six eight-byte positive sizes, at most `UINT32_MAX` |
| Windows facts | Six pairs of eight-byte positive sizes and lowercase SHA-256 byte strings |
| Windows observations | Four-byte count six, then path, four-byte kind, eight-byte size and SHA-256 strings |
| Artifact observations | Four-byte count sixteen, then path, four-byte kind and eight-byte size |

Trailing bytes, incomplete fields, invalid sizes or digests, unsafe paths,
duplicate observations and unknown paths fail. CUPSIZE2 and CUPSIZE3 entry
points reject each other's magic. The native command retains its existing
interface and `Cupid artifact sizes: ok (16 exact artifacts)` success line.

## Source checkpoint acceptance

The Windows and Linux selections contain 96 methods each. Windows executes
95 and retains one POSIX skip. Linux passes all 96 across the closed groups
and recorded retries. The checks include legacy policy behavior, the new byte
API, native observation lifetime and allocation failures, and freshly
Cupid-built dispatchers accepting 82-input release fixtures with new parents.
Synthetic fixture claims do not establish producer provenance.

A fresh Windows kernel build passes with host code-producing tools forbidden.
The added manual bytes increase the raw kernel by 1,008 bytes to 9,586,032.
Both ELF sizes remain unchanged. The installed verifier rejects the stale raw
size, then passes all sixteen artifacts after that single measured policy-row
change. Make image publication reuses the freshly built kernel and boot outputs
through four explicit `-o` targets. The strict private four-CPU max/e1000 boot
and completed `ls` check pass. Base image, artifact and manual bytes stay
unchanged after the boot. Complete paired release acceptance and installation
remain separate requirements.
