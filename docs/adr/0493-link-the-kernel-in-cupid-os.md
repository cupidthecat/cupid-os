# Link the kernel in Cupid OS

The guest CupidLD job also accepts `script` and a canonical VFS script path
in place of the fixed text address and entry symbol. It loads the script
through the same VFS callbacks as the ordered object list and uses CupidLD's
ELF32 script layout. The 16 MiB image span and 128 MiB job arena cover the
production kernel links. Fixed-address jobs keep their existing limits.

The replay reads both ordered object lists from the normal Make recipes.
It stages existing objects and the production script in FAT16, compares
the two linked ELFs and flattened kernel after HomeFS readback, then boots
the raw kernel. Kernel symbol generation still supplies the checked
symbol-table object for the second pass.
