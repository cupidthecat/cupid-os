# Filesystem-aware verification fixtures

The output-parent test now follows the filesystem's actual rename result.
On Windows, a retained directory handle must block the rename. Linux on DrvFS
can retain that restriction too. When renaming is blocked, the parent has not
changed and successful publication is valid. The old test recorded that result
but still required a failure whenever the host identified itself as Linux.

The fixture now waits for its own checked-tool launch checkpoint. It no longer
polls globally visible candidate files. Windows still must block renaming.
Any successful rename still requires transaction rejection, the exact previous
bytes at the displaced path and no output in the replacement directory.
The original twenty-second observer and ninety-second execution bounds remain.

## Evidence

- Isolated descriptor probes record held directory identities and both output
  locations. DrvFS denies renaming and publishes the valid 10,320-byte object.
  Native Linux permits renaming, rejects publication and preserves all 22 old
  bytes. The final launch checkpoint rejects before publication without the
  cleanup diagnostic observed at the later experimental checkpoint.
- Complete 216-method selections pass on native Windows and Linux, with twelve
  and eight existing platform skips. Five more DrvFS executions pass using the
  independently retained native caller. Fresh single-method builds also pass
  on Windows and DrvFS.
- The audit's include inventory already records the two Doom keyboard includes:
  2,642 includes, including 2,299 quoted and 343 angle operands across 758 files.
  The exact original method fails against its old 2,640 expectation. Updating
  only the two stale Python counts makes both that check and its source-drift
  negative pass on both native hosts. The canonical audit stays unchanged.

The first parent checkpoint did not fix the DrvFS failure. A later probe stopped
on its incorrect assumption that renaming would succeed; its corrected version
records the denied operation explicitly. Both attempts remain in the evidence.
Compiler and host-adapter source, all 114 producer inputs, installed seeds,
OS bytes and the 449 CupidBuild / three Python transforms remain unchanged.

The [archive](evidence/filesystem-parent-drift-fixture-20261109.json.gz)
retains 2,697 file records, 179 local payloads and 523 independently verified
references to the preceding verification archive. SHA-256:
`8bf58a05726b1681405cb95622603896d10bcd616eeeed9f0c6fa2dd3c488a48`.

The incomplete paired `ec57b945` replays were deliberately superseded after
these fixture corrections. Both original wrappers retained their partial
products, checked their source guards and independently reread their archives;
neither run establishes complete acceptance. Replacement paired full replays
use the immutable `b22abffd` source, the complete original scope and tool bounds,
and no aggregate test timeout. Full acceptance and installed source-owner seed
adoption remain open.
