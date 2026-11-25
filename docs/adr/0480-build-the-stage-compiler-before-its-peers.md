# ADR 0480: Build the stage compiler before its peers

Current peer source can require a language feature implemented by the current
compiler but absent from an installed seed. The image-input owner's flexible
array access is one such requirement. Compiling every stage source with the
incoming compiler prevents that feature from bootstrapping itself.

Partition each stage's C sources by the CupidC link prerequisites. Compile
those prerequisites with the incoming compiler, assemble the stage inputs,
then link and validate the current CupidC image. Use that image to compile
the remaining peer sources, and link the other five tools. Preserve every
planned object, all six images, their link order and the returned role order.

Every compiler prerequisite is certified before the compiler link. Its image
must pass format validation before any peer compile starts. Peer objects keep
their ordinary certification before their consuming links. Invalid source
partitions and unknown compiler prerequisites fail closed. An assembler-only
compiler prerequisite list remains valid.

The Windows stage-two bridge from ADR 0341 still supplies the incoming checked
Linux CupidC. Its native Windows successor compiles the peers after validation.
No host compiler or assembler is introduced. Compiler source itself must remain
expressible by the incoming compiler; this order does not solve an unsupported
construct used inside the compiler's own prerequisites.

Linux stage two builds all 45 objects and six images. Windows builds all 54
objects and six images; its initial final-image check found an unsorted expected
worker import list. Correcting the profile order and relinking reproduces all
six images byte for byte. These stage-two results do not replace the complete
stage-three/stage-four fixed point or seed-promotion gates. See the
[integration record](../bootstrap/SOURCE-OWNER-INTEGRATION.md).
