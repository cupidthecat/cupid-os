#ifndef KERNEL_DOOM_PLATFORM_H
#define KERNEL_DOOM_PLATFORM_H

#include "types.h"

/* doom_main - entry point invoked from shell.
 * argv[0]="doom", optional flags include -iwad <path>.
 * Returns to shell on user quit (F10 -> Y) or fatal error.
 */
int doom_main(int argc, char **argv);

#endif /* KERNEL_DOOM_PLATFORM_H */
