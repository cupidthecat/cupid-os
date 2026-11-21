# Carry compiler profiles in guest jobs

Allow optional lines after the compiler job's source and output. The guest
adapter maps them to the typed preprocessor and parser requests already used
by hosted CupidC. Plain two-line jobs use the default toolchain include roots.
Jobs with options supply their include roots in search order.

`include` selects both header forms, `angle` selects angle headers, and
`force` loads a named header before the translation unit. `define` sets a
macro, with a default value of one when the value is omitted. `doom` enables
the same two parser rules as hosted `--doom-compat`. Canonical include paths
use the source's filesystem. The reader reserves room for 64 search roots,
16 forced headers and 64 definitions, including both parts of each definition.

The production replay takes its options and input files from the existing
checked kernel and Doom profiles. Source files and headers keep their normal
logical paths inside the source ISO.
