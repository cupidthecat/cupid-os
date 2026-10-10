# Bound CupidDis to the Cupid OS machine-code domain

CupidDis will provide objdump-class inspection with DWARF v4 source information. It will decode 16-bit and 32-bit x86 through x87, MMX, and SSE2. Cupid OS does not need x86-64 or later instruction families. Keeping them outside CupidDis's scope avoids an open-ended ISA compatibility project.
