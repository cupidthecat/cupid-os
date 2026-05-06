/* i_sound_cupidos.c — SFX path for DOOM via the CupidOS mixer.
 * Music path lands in Task 17.
 *
 * Built with CFLAGS_DOOM (no dglibc_compat.h alias), so we pull
 * kernel symbols explicitly.
 */

#include "../types.h"
#include "../memory.h"
#include "../../drivers/serial.h"
#include "../audio/mixer.h"
#include "src/i_sound.h"
#include "src/w_wad.h"

/* Per-lump cache: u8/11025Hz mono -> s16/22050Hz mono, allocated lazily.
 * Indexed by WAD lump number.  WADs rarely exceed 4096 lumps so 4096
 * slots keeps memory use reasonable (4096 * 12 bytes = 48 KB for the
 * table itself; PCM buffers are heap-allocated on demand).
 */
#define MAX_CACHED_SFX 4096

typedef struct {
    int16_t  *pcm;
    uint32_t  frames;
    uint8_t   cached;
} sfx_cache_entry_t;

static sfx_cache_entry_t s_cache[MAX_CACHED_SFX];

/* ------------------------------------------------------------------ */
/* Resample a DOOM SFX lump into the cache                            */
/* ------------------------------------------------------------------ */

static int cache_sfx(sfxinfo_t *sfx)
{
    int lumpnum = sfx->lumpnum;
    if (lumpnum < 0 || lumpnum >= MAX_CACHED_SFX) { return -1; }

    sfx_cache_entry_t *e = &s_cache[lumpnum];
    if (e->cached) { return 0; }

    int len = W_LumpLength((unsigned int)lumpnum);
    uint8_t *raw = (uint8_t *)W_CacheLumpNum(lumpnum, 1); /* PU_STATIC = 1 */
    if (!raw || len < 8) { return -1; }

    /* DOOM DMX SFX header layout:
     *   bytes 0-1: format word (should be 3)
     *   bytes 2-3: sample rate (little-endian u16)
     *   bytes 4-7: sample count (little-endian u32)
     *   bytes 8..: raw u8 PCM samples (0=min, 255=max, 128=silence)
     */
    uint32_t rate = (uint32_t)raw[2] | ((uint32_t)raw[3] << 8);
    if (rate == 0) { rate = 11025u; }

    uint32_t samples = (uint32_t)raw[4]
                     | ((uint32_t)raw[5] << 8)
                     | ((uint32_t)raw[6] << 16)
                     | ((uint32_t)raw[7] << 24);

    /* Clamp to actual lump data */
    if (samples + 8u > (uint32_t)len) {
        samples = (uint32_t)len - 8u;
    }
    if (samples == 0) { return -1; }

    /* Linear resample to mixer rate (22050 Hz) */
    uint32_t out_frames = (uint32_t)(((uint64_t)samples * 22050u) / rate);
    if (out_frames == 0) { out_frames = 1; }

    int16_t *out = (int16_t *)kmalloc(out_frames * sizeof(int16_t));
    if (!out) { return -1; }

    for (uint32_t i = 0; i < out_frames; i++) {
        /* Fixed-point source index (16.16) */
        uint64_t src_q16 = ((uint64_t)i * (uint64_t)rate * 65536u) / 22050u;
        uint32_t si = (uint32_t)(src_q16 >> 16);
        uint32_t fr = (uint32_t)(src_q16 & 0xFFFFu);

        /* Guard against overrun at the last frame */
        if (si + 1u >= samples) {
            si = (samples > 1u) ? samples - 2u : 0u;
        }

        /* u8 -> s16: centre at 128, scale to s16 range */
        int32_t s0 = ((int32_t)raw[8u + si]      - 128) * 256;
        int32_t s1 = ((int32_t)raw[8u + si + 1u] - 128) * 256;
        int32_t v  = s0 + (((s1 - s0) * (int32_t)fr) >> 16);

        /* Clamp to s16 */
        if (v >  32767) { v =  32767; }
        if (v < -32768) { v = -32768; }
        out[i] = (int16_t)v;
    }

    e->pcm    = out;
    e->frames = out_frames;
    e->cached = 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* sound_module_t implementations                                      */
/* ------------------------------------------------------------------ */

static boolean cup_init(boolean use_sfx_prefix)
{
    (void)use_sfx_prefix;
    uint32_t i;
    for (i = 0; i < MAX_CACHED_SFX; i++) {
        s_cache[i].pcm    = (int16_t *)0;
        s_cache[i].frames = 0;
        s_cache[i].cached = 0;
    }
    serial_write_string("[i_sound] init\n");
    return true;
}

static void cup_shutdown(void)
{
    /* Leave cache in place; memory released at process end. */
}

static int cup_get_sfx_lumpnum(sfxinfo_t *sfx)
{
    /* DOOM convention: prefix "DS" + uppercase sfx name */
    char n[16];
    int i;
    n[0] = 'D';
    n[1] = 'S';
    for (i = 0; i < 13 && sfx->name[i] != '\0'; i++) {
        char c = sfx->name[i];
        if (c >= 'a' && c <= 'z') { c = (char)(c - ('a' - 'A')); }
        n[i + 2] = c;
    }
    n[i + 2] = '\0';
    return W_GetNumForName(n);
}

static void cup_update(void) {}

static void cup_update_sound_params(int channel, int vol, int sep)
{
    int slot = channel & 7;
    /* sep: 1..254 (127 = centre); left weight ∝ (254 - sep) */
    if (sep < 1)   { sep = 1; }
    if (sep > 254) { sep = 254; }
    uint32_t l = ((uint32_t)vol * (uint32_t)(254 - sep)) / 254u;
    uint32_t r = ((uint32_t)vol * (uint32_t)sep)         / 254u;
    /* Clamp to 0-255 */
    if (l > 255u) { l = 255u; }
    if (r > 255u) { r = 255u; }
    mixer_set_volume(slot, (uint8_t)l, (uint8_t)r);
}

/* StartSound — 4 parameters per the actual i_sound.h (no pitch arg) */
static int cup_start_sound(sfxinfo_t *sfx, int channel, int vol, int sep)
{
    if (cache_sfx(sfx) != 0) { return -1; }

    sfx_cache_entry_t *e = &s_cache[sfx->lumpnum];
    int slot = channel & 7;

    if (sep < 1)   { sep = 1; }
    if (sep > 254) { sep = 254; }
    uint32_t l = ((uint32_t)vol * (uint32_t)(254 - sep)) / 254u;
    uint32_t r = ((uint32_t)vol * (uint32_t)sep)         / 254u;
    if (l > 255u) { l = 255u; }
    if (r > 255u) { r = 255u; }

    mixer_play(slot, e->pcm, e->frames,
               /*channels=*/1, /*loop=*/0,
               (uint8_t)l, (uint8_t)r);
    return slot;
}

static void cup_stop_sound(int channel)
{
    mixer_stop(channel & 7);
}

static boolean cup_sound_is_playing(int channel)
{
    return mixer_active(channel & 7) ? true : false;
}

static void cup_precache_sounds(sfxinfo_t *sounds, int num_sounds)
{
    int i;
    for (i = 0; i < num_sounds; i++) {
        cache_sfx(&sounds[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Module descriptor                                                   */
/* ------------------------------------------------------------------ */

/* sound_devices must be non-const snddevice_t* per the struct def */
static snddevice_t cup_devices[] = { SNDDEVICE_SB };

sound_module_t cupidos_sound_module = {
    .sound_devices      = cup_devices,
    .num_sound_devices  = 1,
    .Init               = cup_init,
    .Shutdown           = cup_shutdown,
    .GetSfxLumpNum      = cup_get_sfx_lumpnum,
    .Update             = cup_update,
    .UpdateSoundParams  = cup_update_sound_params,
    .StartSound         = cup_start_sound,
    .StopSound          = cup_stop_sound,
    .SoundIsPlaying     = cup_sound_is_playing,
    .CacheSounds        = cup_precache_sounds,
};

/* ------------------------------------------------------------------ */
/* Top-level I_ wrappers — these are what the DOOM source calls       */
/* ------------------------------------------------------------------ */

void I_InitSound(boolean use_sfx_prefix)
{
    cup_init(use_sfx_prefix);
}

void I_ShutdownSound(void)
{
    cup_shutdown();
}

int I_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
    return cup_get_sfx_lumpnum(sfxinfo);
}

void I_UpdateSound(void)
{
    cup_update();
}

void I_UpdateSoundParams(int channel, int vol, int sep)
{
    cup_update_sound_params(channel, vol, sep);
}

/* Match the 4-parameter signature in i_sound.h */
int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    return cup_start_sound(sfxinfo, channel, vol, sep);
}

void I_StopSound(int channel)
{
    cup_stop_sound(channel);
}

boolean I_SoundIsPlaying(int channel)
{
    return cup_sound_is_playing(channel);
}

void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    cup_precache_sounds(sounds, num_sounds);
}
