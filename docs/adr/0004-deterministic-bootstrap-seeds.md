# Bootstrap from checked-in deterministic seeds to a fixed point

Checked-in Windows and Linux Cupid Toolchain seeds will be the trusted bootstrap root. A seed update is valid only when stage 2 and stage 3 tool outputs are byte-identical and their provenance is recorded. Fresh-checkout bootstraps will not depend on external code-generation tools, and self-reproduction will be a deterministic acceptance gate that can be audited.
