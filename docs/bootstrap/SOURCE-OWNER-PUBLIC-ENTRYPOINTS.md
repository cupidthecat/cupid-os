# Public bootstrap fixture and entry checks

The bootstrap tests now keep historical disk fixtures separate from the current
source-owner producer plan. A small fixture module reconstructs the historical
source/link shape and selects worker imports through the exact seed profile.
Current full-build assertions retain all 114 inputs, 44 Linux C sources and
54 Windows objects. Production pins and installed seed files are unchanged.

Four 159-method selections pass: provisional and installed-seed checks on each
host, for 636 executions. Each Linux selection retains seventeen platform skips.
The retained fixture snapshot and current module differ in one separately
rerun heavy method; the other 167 class methods are AST-identical.

## Complete entry checks

The full public fixed-point method passes on native Linux in 3,093.492 seconds
and on Windows in 3,450.088 seconds. Both retain the original 6,000-second inner
CLI bound. Windows long-path/alias preparation also passes with all 114 inputs,
54 objects and its unreleased-parent negative.

The actual supported native Windows Make target passes with all nine
conventional producers forbidden. Independent review rereads its full source
guards and all 3,492 published files. Its sixty final object/image pairs match
the qualified native Windows producer bytes.

The actual native Linux Make target also passes, in 2,808.426 seconds under
its original 12,000-second bound. All nine conventional producers are forbidden.
Independent review rereads all 4,423 published files, the complete source guards
and all 114 captured inputs. Its 51 final object/tool pairs match the qualified
Linux stage-four bytes. These checks leave the installed seeds unchanged.

## Retained failure and corrected reference

The original Windows public CLI returns zero and passes its behavior checks,
then the unittest rejects an older runtime-PE golden. The qualified producer
already retains two identical current references: 41,984 bytes with SHA-256
`1e2cf939707b109370ff2cfb80dc467a101dc46ed2be54b7a077f63c131793a9`.
Updating the expected size and digest from those actual files makes the fresh
complete method pass. This changes a test fixture, not toolchain output.

The first independent review also expects both sizes in a truncated unittest
dictionary diff. The corrected review binds the retained old fixture and the
digest fragments actually printed, then reads the complete qualified reference.
All source, runtime and artifact checks remain intact.

The [evidence archive](evidence/source-owner-public-bootstrap-entrypoints-20261109.json.gz)
retains 3,524 file records, 32 embedded payloads and 530 references to the
qualified producer archive. It includes complete native Make products, fixture
sources, guarded receipts, logs and independent review. Its SHA-256 is
`2349ce315c0cc53f2e8a4c647eb2f721d5b4f7b3cce583341ffeb2f3659af274`.
The [four SDK publications](SOURCE-OWNER-SDK-PUBLICATIONS.md) pass their separate
acceptance. Installed seed adoption remains open.

The [Linux Make companion](evidence/source-owner-native-linux-make-20261109.json.gz)
retains all 4,423 products and the guarded source, driver and review records.
Its 6,595 file records use 1,535 local payloads and 633 qualified-archive
references. Existing evidence archives and the two Git LFS assets remain bound
by the complete receipt inventory rather than being embedded again. SHA-256:
`22cbd262f5b43f9ac67d8cb1d8509f7fba3dd9d0759108d97e0d846426fd92ce`.
