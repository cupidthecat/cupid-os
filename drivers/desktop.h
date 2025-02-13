#ifndef DESKTOP_H
#define DESKTOP_H

#include "../kernel/types.h"
#include "window.h"

// Desktop background color
#define DESKTOP_BG_COLOR 0x1D
#define MAX_ACTIVE_WINDOWS 5

// Function prototypes
void desktop_init(void);
void desktop_run(void);

#endif
