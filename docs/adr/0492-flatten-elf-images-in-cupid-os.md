# Flatten ELF images in Cupid OS

Add an opt-in CupidObj worker for the raw image step. It runs the shared
`CTOOL_OBJ_EXTRACT_FLAT` operation through the ordinary toolchain invocation.
That operation uses the ELF load addresses and excludes uninitialized bytes.

The compiler and object worker share the two-path job parser. A CupidObj job
names a guest ELF input and a raw output under `/home`. The worker accepts
inputs up to 16 MiB and uses a bounded job arena. The existing HomeFS rename
publishes completed output. Failed requests preserve the preceding output.

The source ISO carries the shared CupidASM, CupidDis and CupidObj sources
and their headers. The `peer-objects` target builds checked references for
those same translation units.
