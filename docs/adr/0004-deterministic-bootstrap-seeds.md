# Bootstrap from checked-in deterministic seeds to a fixed point

Checked-in Windows and Linux Cupid Toolchain seeds will be the trusted bootstrap root, and a seed update is valid only when stage 2 and stage 3 tool outputs are byte-identical with their provenance recorded. This makes fresh-checkout bootstraps independent of external code-generation tools and turns self-reproduction into a deterministic, auditable acceptance gate.
