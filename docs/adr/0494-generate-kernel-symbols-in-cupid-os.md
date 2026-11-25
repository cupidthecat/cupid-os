# Generate kernel symbols in Cupid OS

The symbol worker reads a linked ELF32 image. CupidDis renders its canonical
symbol rows into a bounded buffer. CupidObj converts those rows into the
word-packed `.ksyms` source used by the normal kernel build. The worker
publishes the completed source through the shared HomeFS job adapter.

CupidC also accepts a source under `/home`. Quoted includes can resolve
beside the writable source. The generated kernel unit can then compile
with its original `ksyms.h` and `types.h` beside it.

The kernel replay uses the generated source and object between the two CupidLD
passes. It compares each persisted product with the checked production build.
