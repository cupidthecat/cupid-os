# ADR 0387: Keep Windows kernel inspection within the command-line limit

Date: 2026-09-19

Status: Accepted

## Context

The full Windows OS replay with the `962e476b` candidates compiled every
object and linked both kernels, then failed when CupidBuild launched the
broad CupidDis inspection. The 431 absolute input arguments occupied 45,255
quoted bytes before the executable and options. The Windows launcher accepts
at most 32,767 characters. The previous raw kernel remained unchanged and the
disk-image publisher did not run.

The earlier `0232cb57` publisher passed short filenames inside its private
working directory. The later POSIX sealed-input work changed both hosts to
absolute frozen paths. That is necessary for Linux's `/proc/self/fd/N` inputs,
but it also removed the Windows argument-length protection. The existing
27-input CLI case and two-input fixed-point case did not reach the limit.

## Decision

On Windows, the flatten transaction derives each argument's final path
component from its retained frozen-path storage. The directory remains the
child's private working directory. Its handle denies delete sharing, frozen
files remain retained read-only, and the transaction verifies named and
retained identities around execution. The basename is never a pointer into
the loop's temporary filename buffer.

Linux retains the complete descriptor path. Host-side reads retain the full
`linked_frozen` identity on both platforms. No input is omitted or split into
a weaker inspection, and the independent flat-image renderer and publication
checks remain unchanged.

The standalone regression supplies 500 distinct ELF inputs, including both
required linked kernels, beneath a repository path containing spaces. Its
old absolute argument payload alone is 72,500 characters on the Windows
validation checkout. Successful output must match CupidObj twice. A malformed
final input must reach CupidDis and fail with the old bytes and timestamp
intact, without transaction residue. Windows diagnostics must name
`code-499.bin`; Linux keeps its descriptor-based diagnostic names.

Both fixed-point definitions now exercise all 500 inputs and reject the last
one after successful publication. The malformed-manifest case remains too.
The expected failure/help/success totals become 33/7/38 on Linux and 21/7/25
on native Windows. Fresh proof reports, not these expected counts, establish
candidate acceptance.

## Evidence and limits

The hosted Windows regression failed before the change with the same checked
CupidDis launch diagnostic as the full OS build. With complete `962e476b`
candidate cohorts selected, both tests passed on Windows in 35.699 seconds
and Linux in 54.562 seconds. Default original-cohort Linux tests passed in
51.583 seconds. Original-cohort Windows passed its negative case and skipped
only the positive case: that exact manifest predates CupidObj's retained
candidate support. The skip requires its full manifest digest and source
revision. Any other cohort must run the positive test.

Flattening still republishes equal bytes. This repair adds no timestamp-reuse
promise; it requires timestamp preservation after failure.

The checked cohorts and normal recipes do not change in this source step.
The pending promotion needs fresh paired reconstruction and a successful
full OS build and runtime replay. No C or assembly source is reduced, no
suffix migration is added, and `TempleOS/` remains reference material.
