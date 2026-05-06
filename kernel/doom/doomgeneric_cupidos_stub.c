/* doomgeneric_cupidos_stub.c
 * TEMPORARY stubs for DG_* platform functions.
 * Replaced in Task 13 by the real CupidOS platform shim.
 *
 * Built with CFLAGS_DOOM_TREE (no -include dglibc_compat.h needed here
 * since we reference kernel types directly).
 */

#include "../types.h"
#include "../../drivers/serial.h"
#include "../../drivers/timer.h"

void DG_Init(void)
{
    serial_write_string("[doom] DG_Init stub — Task 13 not yet wired\n");
}

void DG_DrawFrame(void)
{
    /* no-op until Task 13 */
}

void DG_SleepMs(uint32_t ms)
{
    timer_sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void)
{
    return timer_get_uptime_ms();
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    (void)pressed;
    (void)key;
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}
