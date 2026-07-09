# Use ELF32 relocatable objects as the toolchain interchange

CupidC and CupidASM will emit deterministic ELF32 `ET_REL` objects, and CupidLD will consume them while preserving the subset of GNU linker-script behavior used by Cupid OS. This supports relocations, symbols, mixed-toolchain migration, and existing image layout without committing Cupid OS to either flat-binary-only compilation or an unrelated custom object format.
