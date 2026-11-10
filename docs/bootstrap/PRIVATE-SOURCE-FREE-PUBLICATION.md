# Open publication without an implicit source

The current private wide adapter can open a publication transaction without
capturing a repository source. The caller registers each input explicitly.
This restores the constructor contract from ADR 0457 in the current store and
checked-output implementation. Complete external observer capture and the
normal image-command handoff remain open.

`cupidbuild_host_publication_transaction_open_wide` accepts an output beneath
the retained root and a capacity from 1 through 2^61 - 1 bytes. It retains the
output parent, lock, previous-output observation and private storage. Its
frozen-source accessor returns an empty string. Explicit input capture accepts
an absolute live pathname. All five earlier constructors still capture their
source.

## Verification

- Eighty small native/Cupid-built executions pass across Windows and Linux.
  They cover explicit capture, nonempty and empty publication, initially absent
  outputs, unfinished close, maximum capacity and eight useful rejections.
  Rejected operations preserve the previous bytes, identity and modification
  time, and leave no publication debris.
- Twenty complete image cases pass with the actual checked CupidObj producer,
  disk renderer and FAT16 staging core. Twelve outputs match the retained
  200 MiB oracles by whole-file hash and all 3,200 independent block comparisons.
  Eight stage failures preserve the previous image and its identity and time.
  Unicode naming, empty files and shorter replacement remain covered.
- Both builds use the qualified core objects unchanged. Windows uses its normal
  configuration with only the two wide-store imports, without fixture time
  bindings. Linux retains 32 MiB and 10,240 descriptors. Producer, small-caller
  and image-caller bounds remain 60, 120 and 18,000 seconds.

The complete image checks use the immutable earlier 9,416,096-byte kernel and
the v57 oracles. They do not establish acceptance for the current OS image.
No new guest boot is claimed; the positive images equal the retained oracle
used by the earlier strict boot.

## Module extraction and failed fixtures

The 558-line constructor body now lives in a private include at its original
token position. Reconstructing the original file preserves every byte.
Complete checked objects before and after extraction are identical on both
hosts. The three existing store includes and normal Windows bridge are
unchanged.

The first caller contained a UTF-8 BOM and failed CupidC lexical validation.
After that correction, its explicit-input case passed a relative name to an
API requiring an absolute live path. The corrected caller passes both complete
small matrices. Both failed fixtures and their outputs remain in the evidence.

The archive retains the implementation, callers, SDK, builders, runtime records
and independent source, object, guard and FAT checks. Installed seeds, all 114
producer inputs, current OS artifacts and the 449 CupidBuild / three Python
ownership counts remain unchanged.

[Evidence archive](evidence/private-source-free-publication-constructor-20261109.json.gz) SHA-256:
`7e6f286cf9a8fa1cc5addf724e5b0efe6f3f633d6cba45ea47221b8382b1986e`.
