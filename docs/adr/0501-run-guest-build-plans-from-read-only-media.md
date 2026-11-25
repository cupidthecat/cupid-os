# Run guest build plans from read-only media

The source rebuild used a host-authored `HOMEFS.SYS` to carry the plan,
requests, and headers. The guest can read the same inputs from mounted
ISOs and leave HomeFS for its own outputs.

Link a separate CupidBuild entry point for source media. It opens
`/plan/cupidbuild.job` and validates all request paths under `/plan/jobs/`
before running the first step. A missing or malformed media plan fails
even if `/home/cupidbuild.job` exists. The ordinary worker still opens
`/home/cupidbuild.job` for editable plans.

The complete source replay uses a dedicated plan ISO. Combining its
requests with the kernel sources would exceed CupidObj's 512-entry ISO
limit. The replay uses four mounts, including the separate Doom and
asset ISOs. The plans keep the same order and use the existing worker
job formats.

Boot assembly, the linker script, and generated-source headers are read
from `/src`. Installation sources and the kernel symbol source are
generated in `/home`. CupidC reads their headers through explicit include
roots on the source ISO. The host still derives the plan from Make and
publishes the ISOs for the replay.
