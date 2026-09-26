# ADR 0407: Bind the conditional-assembly seed parent

Date: 2026-09-26

Status: Accepted; paired production acceptance verified

The first UTF-8 seed promotion passed Python verification but failed the shared
C manifest reader. The proposed release names the installed conditional-assembly
release as its parent. The reader accepted only the two preceding parent
generations, so checked CupidBuild rejected that release before running a tool.

The reader now also accepts this exact tuple:

| Identity | Value |
| --- | --- |
| Revision | `e4f2ed652e756b1abb375ec061923f5259799e01` |
| Linux manifest | `da26556401dd20d039ed1175f3bf857c4c8bebd50fb52b3bd75a06b95fdf41ed` |
| Windows manifest | `c715ce354c28b97c6b9c4e5702c98d368d07dff9e52bc2f0deb71ab3194d2395` |

Linux requires the matching revision and manifest. Windows requires both
matching manifests and both matching revision fields. The earlier complete
tuples remain accepted. Unknown identities and fields mixed between generations
fail. This preserves the explicit parent window instead of accepting arbitrary
hexadecimal provenance.

Python installed-seed verification selects the exact Windows import profile
from the reviewed plan digest and source count after checking provenance.
The 66-input ANSI and 73-input UTF-8 profiles remain distinct. Unknown roles,
unknown plans, noninteger counts and mixed plan/count pairs fail.

The failed promotion was reverted to the accepted seed pair. Its payloads and
test evidence remain available separately. The reader source is a producer
input, so the preceding staged proofs cannot establish this revision's fixed
point. New proofs and production acceptance are required before promotion.

A focused regression reproduced the original provenance failure before the
change. Tests cover the complete parent tuple, each altered field, release
matching, the Linux manifest's actual-byte binding, immutable inputs, bounded
diagnostics and recovery. The 98 focused manifest, pair, release, import-profile,
release-identity and artifact-policy tests pass on Windows and Linux.

The audit must also account for the UTF-8 capability inputs introduced by the
preceding commit. Its publication inventory now has 87 inputs, and the manifest
contract transform has 151 distinct inputs. The active preprocessing corpus
adds the codec and four Windows role profiles, using the exact definitions
and GNU settings from the Windows build plan. The object contract compiles
the codec as well. These C test changes require a fresh publication; they do
not change the 73 tool producer inputs used by the staged proofs.

The regenerated graph passes its inventory contracts. A checked-Cupid
preprocessor executable passes all 412 active cases, the 62 conditional
probes and four diagnostic/recovery modes. The fixed-point mutation replay
also passes. Independent hashes confirm that the four C test/fixture changes
leave the 73 producer inputs unchanged. The fresh publication now passes all
65 stage pairs and publishes 22 artifacts. Its checked manifest reader and
independent verification bind all 87 publication and 73 producer inputs.

Both staged proofs now pass independent byte verification: 39 Windows and
32 Linux artifacts converge against the same 73 producer inputs. All 124
graph-audit methods are covered by the broad run, the three corrected
inventory replays and the fixed-point mutation run. AST comparison confirms
unchanged bodies for methods that were not replayed. Paired OS/runtime acceptance
passes independent verification of 1,525 sources, sixteen artifacts and 431 link
inputs. Both hosts produce identical images and user binaries. Their private
four-CPU max/e1000 disassembly, shell and SMP smokes preserve the source images.
The preceding accepted images remain unchanged. Promotion still requires a
verified producer commit and separate promoted-seed acceptance.

All 28 Unicode-path commands pass across the six converged Windows tools and
four Unicode roots. The corrected reader therefore has both staged-byte and
real wide-path execution evidence. Publication and OS acceptance are tracked
separately.
