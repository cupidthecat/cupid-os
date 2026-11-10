# Source guards after formatting

The remaining indentation-sensitive checks for USB callback ownership,
CupidDis summary reuse, CupidBuild publication ordering and Windows imports
now read C tokens. Their operation counts, arguments and ordering checks stay
intact. The live SDK reader also requires its added spelling header and the
exact 113-input contract inventory; its producer inventory stays unchanged.

Syscall ABI negative tests now replace exactly one C token sequence. A missing
or ambiguous target fails before the checker runs. This repairs two mutations
that silently stopped changing their fixtures after formatting. Comments and
string contents cannot supply a code target.

All 35 focused methods pass on native Linux. Windows initially passes 34; the
remaining method rejects a concurrent repository-directory change. Its quiet
retry passes unchanged. Both hosts reject seven deliberate changes to the
actual source reads. Helper cases also reject missing, duplicate, comment-only
and literal-only mutation targets.

The older frozen full Make run finishes with 3,274 tests, 51 failures, 11 errors
and 74 skips. It predates these and earlier fixture repairs and uses the old
seed. Its complete failed log remains in the evidence. The current full suite,
kernel fingerprint calibration and replacement-seed acceptance remain separate
work; these focused results do not establish a passing full suite.

The [evidence archive](evidence/formatting-source-guard-fixtures-20261110.json.gz)
retains 37 file payloads, including before/after fixtures, failed attempts,
controllers and passing readbacks. All exported bytes pass independent size
and SHA-256 checks. The archive is 1,126,876 bytes with SHA-256
`ece87c666cc05915a51d7b5bced6806475be30c83e5474f8ed50f1740e36f90d`.
