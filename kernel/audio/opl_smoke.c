/* opl_smoke - verify Nuked-OPL3 -> mixer -> AC97 path.
 * Plays a single sustained FM note for ~2 seconds via slot 8
 * using a streaming-source pull callback that resamples
 * 49716 -> 22050 Hz with linear interpolation.
 */
#include "types.h"
#include "serial.h"
#include "nuked_opl3.h"
#include "mixer.h"
#include "ac97.h"
#include "opl_smoke.h"

static opl3_chip g_chip;

/* Generate native at 49716 Hz, resample to 22050 Hz via linear interp.
 * Mixer requests `frames` at 22050; we render `(frames * 49716 + 22049) / 22050`
 * native samples then walk the output table.
 */
static void opl_pull(int16_t *out, uint32_t frames, void *ctx) {
    static int16_t native[2048u * 2u];
    uint32_t need;
    uint32_t f;

    (void)ctx;

    need = (frames * 49716u + 22049u) / 22050u;
    if (need > 2048u) { need = 2048u; }
    OPL3_GenerateStream(&g_chip, native, need);

    for (f = 0u; f < frames; f++) {
        uint32_t src_q16 = (f * 49716u * 65536u) / 22050u;
        uint32_t i       = src_q16 >> 16;
        uint32_t fr;
        int32_t l0, l1, r0, r1, l, r;

        if (i >= need - 1u) { i = need - 2u; }
        fr = src_q16 & 0xFFFFu;

        l0 = (int32_t)native[i * 2u + 0u];
        l1 = (int32_t)native[(i + 1u) * 2u + 0u];
        r0 = (int32_t)native[i * 2u + 1u];
        r1 = (int32_t)native[(i + 1u) * 2u + 1u];

        l = l0 + (((l1 - l0) * (int32_t)fr) >> 16);
        r = r0 + (((r1 - r0) * (int32_t)fr) >> 16);

        out[f * 2u + 0u] = (int16_t)l;
        out[f * 2u + 1u] = (int16_t)r;
    }
}

void opl_smoke(void) {
    if (!ac97_is_present()) {
        serial_write_string("[SKIP] audiotest opl: no AC97\n");
        return;
    }

    OPL3_Reset(&g_chip, 49716u);

    /* Enable OPL3 mode */
    OPL3_WriteRegBuffered(&g_chip, 0x105u, 0x01u);

    /* Patch - channel 0, op0 (mod) and op3 (car). 2-op FM connection. */
    /* Modulator (register slot offset 0x00) */
    OPL3_WriteRegBuffered(&g_chip, 0x20u, 0x01u);   /* mult */
    OPL3_WriteRegBuffered(&g_chip, 0x40u, 0x10u);   /* level */
    OPL3_WriteRegBuffered(&g_chip, 0x60u, 0xF0u);   /* attack/decay */
    OPL3_WriteRegBuffered(&g_chip, 0x80u, 0x77u);   /* sustain/release */
    OPL3_WriteRegBuffered(&g_chip, 0xE0u, 0x00u);   /* sine waveform */

    /* Carrier (register slot offset 0x03) */
    OPL3_WriteRegBuffered(&g_chip, 0x23u, 0x01u);
    OPL3_WriteRegBuffered(&g_chip, 0x43u, 0x00u);
    OPL3_WriteRegBuffered(&g_chip, 0x63u, 0xF0u);
    OPL3_WriteRegBuffered(&g_chip, 0x83u, 0x77u);
    OPL3_WriteRegBuffered(&g_chip, 0xE3u, 0x00u);

    /* Channel feedback + connection + L+R speakers */
    OPL3_WriteRegBuffered(&g_chip, 0xC0u, 0x30u);

    /* Note A4 (440 Hz): F-num=0x289 (≈657), block=4, key on */
    OPL3_WriteRegBuffered(&g_chip, 0xA0u, 0x89u);
    OPL3_WriteRegBuffered(&g_chip, 0xB0u, 0x32u);   /* key on, octave 4, F-num msb 0x1 */

    /* Hand mixer the streaming source on slot 8 */
    mixer_play_stream(8, opl_pull, (void *)0, (uint8_t)100u, (uint8_t)100u);

    ac97_tsc_sleep_ms(2000u);

    /* Key off */
    OPL3_WriteRegBuffered(&g_chip, 0xB0u, 0x12u);
    ac97_tsc_sleep_ms(500u);
    mixer_stop(8);

    serial_write_string("[PASS] audiotest opl\n");
}
