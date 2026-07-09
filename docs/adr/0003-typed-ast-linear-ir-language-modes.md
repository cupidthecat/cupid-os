# Share a typed AST and linear IR across two language modes

CupidC will parse C mode and Cupid mode into a shared typed AST and lower both through a shared linear IR, with C semantics and Cupid-specific extensions kept distinct at the language boundary. This replaces direct parser-to-machine-code generation while avoiding two independent compilers, enabling semantic checking and optimization without forcing Cupid extensions into ordinary C.
