# Image profiles and portable verification fixtures

The 82-input release fixture now supplies a synthetic CupidBuild image with the
imports declared by that historical plan. Its current copied worker-enabled
image had different imports. Only the verifier reads this replacement image;
the test does not execute it. Matching hashes and exact sizes still cannot
authorize worker imports under the older plan, as a new negative case verifies.
All existing plan, parent, target, image corruption and recovery checks remain.

The seed-image fixture recognizes all five exact source-owner worker profiles.
The actual Cupid-built Windows ABI command also selects the worker plan,
assembly bridge and imports together. Its first rebuild exposed an unresolved
symbol when the bridge was omitted. The complete corrected ten-method module
passes on both hosts, with one existing Windows platform skip.

Python oracle subprocesses use the interpreter running the test. POSIX wide-file
fixtures use the native temporary filesystem by default; Windows keeps its
existing location and explicit product paths remain available. This avoids
DrvFS allocation behavior while retaining the physical-sparsity assertion,
4 GiB offsets, all-ones positions, marker checks, mutations and error cases.

## Evidence

- All 58 native selected methods pass with prospective seeds on Windows. The
  initial checked ABI setup then fails for the missing bridge; its corrected
  complete module passes. The installed Windows selection also passes all 58.
- The prospective Linux selection passes all 59 methods, including its original
  checked ABI method. The complete ten-method ABI rerun also passes.
- A one-change probe turns the original fourteen artifact subtest failures into
  a complete seventeen-method pass before adding the new negative test.
- Linux verification now has Clang 21.1.8 and NASM 3.01. The strict preflight also
  confirms GCC, Python 3 and readelf. Normal OS producers do not change.

The original full Linux replay completed normally: 3,112 test executions,
88 failures, 48 errors and 108 skips. Its missing verification tools and other
fixture failures require a new complete replay. These focused results do not
qualify installed seed adoption. All fifteen installed seed files and production
release pins remain unchanged.

The [evidence archive](evidence/source-owner-image-profile-portability-20261109.json.gz)
retains 59 file records, 44 local payloads and fourteen qualified-archive
references. Its SHA-256 is
`92415aa1a847c0cfad08c93364611235a1dda1595e26ca1a438ba0f785a23323`.
It includes original failures, the full Linux log, corrected checks and seed guards.
