/* i_sound_cupidos.c — SFX path for DOOM via the CupidOS mixer.
 * Music path lands in Task 17.
 *
 * Built with CFLAGS_DOOM (no dglibc_compat.h alias), so we pull
 * kernel symbols explicitly.
 */

#include "../types.h"
#include "../string.h"
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

/* ── Music path: MUS → MIDI → OPL3 → mixer slot 8 ──────────────────────
 *
 * Status: SMF (format 0/1) parser + multi-track timeline merge work,
 * tempo + delta-time + meta events handled, midiopl gets correctly
 * paced channel events. Synthesis is partially correct: events fire,
 * frequencies use chocolate-doom's freq table, GENMIDI patch operator
 * registers are programmed in the right order, velocity + channel
 * volume scale carrier TL.
 *
 * Known gaps that keep music from sounding right (vs vanilla DOOM):
 *   - Percussion (MIDI ch 9) not handled — drum hits play as wrong-
 *     pitched melody notes. Need GENMIDI percussion bank (slots
 *     175..302) and per-note fixed_note byte from each patch.
 *   - 2-voice instruments (GENMIDI flag 0x04) only program voice 0;
 *     vanilla layers voice 0 + voice 1 with finetune offset.
 *   - Voice priority/stealing is plain LRU; vanilla scores by
 *     attack+sustain rates so noisy patches don't evict pads.
 *   - OPL3 stereo / second 9-voice bank not used (we only allocate 9).
 *   - Channel pan, modulation wheel, sustain pedal CCs ignored.
 *
 * TODO: full port of chocolate-doom's i_oplmusic.c (≈1900 lines) to
 * close these. Until then DOOM music will play but won't sound like
 * the original. SFX is unaffected — that path is 100% of vanilla.
 */
#include "../audio/midiopl.h"
/* Forward-declare mus2midi_convert directly to avoid the boolean typedef
 * conflict between mus2midi.h (boolean=int) and doomtype.h (boolean=uint). */
int mus2midi_convert(const uint8_t *mus, uint32_t mus_len,
                     uint8_t **out_midi, uint32_t *out_len);

static int      s_music_inited = 0;
static uint8_t *s_midi_buf     = 0;
static uint32_t s_midi_len     = 0;
static int      s_music_loop   = 0;

/* ── Standard MIDI File (SMF) player ─────────────────────────────────────
 *
 * The original code streamed raw bytes from s_midi_buf through
 * midiopl_feed at audio-buffer cadence. That works only for a paced
 * raw MIDI stream — but both the mus2midi output and the lumps
 * Freedoom ships are full SMF files: MThd + (one per track) MTrk
 * chunks, delta-time VLQs, and meta events. midiopl_feed silently
 * discards orphan data bytes, so SMF input either plays nothing or
 * plays garbled events with no tempo control.
 *
 * Freedoom in particular uses SMF format 1 — a "conductor" track
 * (track 0) carrying tempo/meta events and N parallel music tracks
 * that all play simultaneously. D_E1M1 has 18 such tracks. A single-
 * track parser only sees the conductor, which is the symptom we hit:
 * lots of meta events, zero notes.
 *
 * This player handles SMF format 0 and 1 by maintaining a per-track
 * cursor with its own delta-time wait counter. Tempo (set by track 0
 * meta events in format 1) is global. On each music_pull we step
 * every cursor forward in real time and fire the earliest pending
 * event(s) until pull_us is exhausted, then render audio. */
#define SMF_MAX_TRACKS 32

typedef struct {
    uint32_t pos;           /* offset into s_midi_buf of next byte */
    uint32_t end;           /* exclusive end of this MTrk */
    uint32_t wait_us;       /* us remaining until this track's next event */
    uint8_t  running_status;
    uint8_t  ended;
} smf_track_t;

typedef struct {
    smf_track_t tracks[SMF_MAX_TRACKS];
    uint16_t    n_tracks;
    uint16_t    division;   /* ticks per quarter note */
    uint32_t    us_per_tick;
    uint8_t     all_ended;
} smf_player_t;

static smf_player_t s_smf;

/* Read a 32-bit big-endian value at off, return it; advance off by 4. */
static uint32_t smf_be32(const uint8_t *buf, uint32_t off) {
    return ((uint32_t)buf[off]   << 24)
         | ((uint32_t)buf[off+1] << 16)
         | ((uint32_t)buf[off+2] <<  8)
         |  (uint32_t)buf[off+3];
}
static uint16_t smf_be16(const uint8_t *buf, uint32_t off) {
    return (uint16_t)(((uint16_t)buf[off] << 8) | buf[off+1]);
}

/* Decode a SMF variable-length quantity. Returns the value; *off is
 * advanced past the consumed bytes. Stops after 4 bytes max. */
static uint32_t smf_vlq(const uint8_t *buf, uint32_t *off, uint32_t end) {
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        if (*off >= end) return v;
        uint8_t b = buf[(*off)++];
        v = (v << 7) | (uint32_t)(b & 0x7Fu);
        if ((b & 0x80u) == 0) break;
    }
    return v;
}

/* Parse MThd + every MTrk inside s_midi_buf. Returns 0 on success and
 * fills s_smf with division + per-track cursors. */
static int smf_init(void) {
    s_smf.all_ended = 1;
    s_smf.n_tracks  = 0;
    if (s_midi_len < 14) return -1;
    if (s_midi_buf[0] != 'M' || s_midi_buf[1] != 'T'
     || s_midi_buf[2] != 'h' || s_midi_buf[3] != 'd') return -1;

    uint32_t mthd_len = smf_be32(s_midi_buf, 4);
    if (mthd_len < 6 || 8 + mthd_len > s_midi_len) return -1;
    s_smf.division = smf_be16(s_midi_buf, 12);
    if (s_smf.division == 0 || (s_smf.division & 0x8000u)) {
        /* SMPTE divisions: not used by DOOM music; fall back to 96. */
        s_smf.division = 96;
    }
    s_smf.us_per_tick = 500000u / s_smf.division;  /* default 120 BPM */

    /* Walk every MTrk chunk, install a cursor per track. */
    uint32_t off = 8 + mthd_len;
    while (off + 8 <= s_midi_len && s_smf.n_tracks < SMF_MAX_TRACKS) {
        uint32_t chunk_len = smf_be32(s_midi_buf, off + 4);
        uint32_t after = off + 8 + chunk_len;
        if (after > s_midi_len) break;
        if (s_midi_buf[off] == 'M' && s_midi_buf[off+1] == 'T'
         && s_midi_buf[off+2] == 'r' && s_midi_buf[off+3] == 'k') {
            smf_track_t *t = &s_smf.tracks[s_smf.n_tracks++];
            t->pos            = off + 8;
            t->end            = after;
            t->running_status = 0;
            t->ended          = 0;
            uint32_t delta = smf_vlq(s_midi_buf, &t->pos, t->end);
            t->wait_us = delta * s_smf.us_per_tick;
        }
        off = after;
    }
    s_smf.all_ended = (s_smf.n_tracks == 0);
    return s_smf.all_ended ? -1 : 0;
}

static void smf_reset(void) {
    midiopl_reset();
    smf_init();
}

/* Fire one event from the given track. Returns 0 on success, 1 on
 * end-of-track. Does not read the next delta-time (caller handles it). */
static int smf_track_fire(smf_track_t *t) {
    if (t->pos >= t->end) { t->ended = 1; return 1; }
    uint8_t b = s_midi_buf[t->pos];

    /* Meta event: 0xFF NN LL data... */
    if (b == 0xFFu) {
        if (t->pos + 2 > t->end) { t->ended = 1; return 1; }
        uint8_t meta_type = s_midi_buf[t->pos + 1];
        uint32_t p = t->pos + 2;
        uint32_t mlen = smf_vlq(s_midi_buf, &p, t->end);
        if (p + mlen > t->end) { t->ended = 1; return 1; }
        if (meta_type == 0x2Fu) { t->ended = 1; return 1; }
        if (meta_type == 0x51u && mlen == 3) {
            uint32_t tempo = ((uint32_t)s_midi_buf[p]   << 16)
                           | ((uint32_t)s_midi_buf[p+1] <<  8)
                           |  (uint32_t)s_midi_buf[p+2];
            if (tempo > 0) {
                s_smf.us_per_tick = tempo / s_smf.division;
                if (s_smf.us_per_tick == 0) s_smf.us_per_tick = 1;
            }
        }
        t->pos = p + mlen;
        return 0;
    }

    /* SysEx (0xF0 / 0xF7): SMF stores wrapped length, skip. */
    if (b == 0xF0u || b == 0xF7u) {
        uint32_t p = t->pos + 1;
        uint32_t slen = smf_vlq(s_midi_buf, &p, t->end);
        if (p + slen > t->end) { t->ended = 1; return 1; }
        t->pos = p + slen;
        return 0;
    }

    /* Channel voice / mode message — running status applies. */
    uint8_t status;
    uint32_t p;
    if (b & 0x80u) { status = b; p = t->pos + 1; t->running_status = status; }
    else           { status = t->running_status; p = t->pos; }

    if (status == 0) { t->pos = p + 1; return 0; }

    uint8_t kind = (uint8_t)(status & 0xF0u);
    uint8_t needs_two = (kind != 0xC0u && kind != 0xD0u);
    uint32_t need = needs_two ? 2 : 1;
    if (p + need > t->end) { t->ended = 1; return 1; }

    uint8_t msg[3];
    msg[0] = status;
    msg[1] = s_midi_buf[p];
    if (needs_two) msg[2] = s_midi_buf[p + 1];
    midiopl_feed(msg, 1u + need);
    (void)kind;
    t->pos = p + need;
    return 0;
}

/* Streaming-source pull: advances the SMF clock by the audio duration
 * of this buffer, fires every event whose delta has elapsed, then
 * renders that many frames of OPL audio. */
static void music_pull(int16_t *out, uint32_t frames, void *ctx)
{
    (void)ctx;
    if (s_midi_buf && !s_smf.all_ended) {
        uint32_t pull_us = (frames * 1000000u) / 22050u;
        while (pull_us > 0u) {
            /* Find the live track with the smallest wait_us. */
            uint32_t min_wait = 0xFFFFFFFFu;
            int min_idx = -1;
            for (int i = 0; i < s_smf.n_tracks; i++) {
                smf_track_t *t = &s_smf.tracks[i];
                if (t->ended) continue;
                if (t->wait_us < min_wait) { min_wait = t->wait_us; min_idx = i; }
            }
            if (min_idx < 0) { s_smf.all_ended = 1; break; }
            if (min_wait > pull_us) {
                for (int i = 0; i < s_smf.n_tracks; i++) {
                    if (!s_smf.tracks[i].ended) s_smf.tracks[i].wait_us -= pull_us;
                }
                pull_us = 0u;
                break;
            }
            for (int i = 0; i < s_smf.n_tracks; i++) {
                if (!s_smf.tracks[i].ended) s_smf.tracks[i].wait_us -= min_wait;
            }
            pull_us -= min_wait;
            smf_track_t *t = &s_smf.tracks[min_idx];
            if (smf_track_fire(t) != 0) continue;
            if (!t->ended) {
                uint32_t delta = smf_vlq(s_midi_buf, &t->pos, t->end);
                t->wait_us = delta * s_smf.us_per_tick;
            }
        }
        if (s_smf.all_ended && s_music_loop) smf_reset();
    }
    midiopl_render(out, frames);
}

/* Lazy GENMIDI loader — called on first I_InitMusic */
static int load_genmidi_from_wad(void)
{
    int gn = W_GetNumForName("GENMIDI");
    if (gn < 0) {
        serial_write_string("[i_sound] no GENMIDI in WAD\n");
        return -1;
    }
    int len = W_LumpLength((unsigned int)gn);
    uint8_t *gm = (uint8_t *)W_CacheLumpNum(gn, 1); /* PU_STATIC = 1 */
    if (!gm || len <= 0) { return -1; }
    midiopl_init(gm, (uint32_t)len);
    return 0;
}

/* music_module_t implementations */
static boolean cup_music_init(void)
{
    if (load_genmidi_from_wad() == 0) {
        s_music_inited = 1;
        serial_write_string("[i_music] init: GENMIDI loaded\n");
        return true;
    }
    serial_write_string("[i_music] init: NO GENMIDI; music disabled\n");
    return false;
}

static void cup_music_shutdown(void) { mixer_stop(8); }

static void cup_music_set_volume(int volume)
{
    /* DOOM passes 0..127 per music_module_t comment; clamp defensively */
    if (volume < 0)   { volume = 0; }
    if (volume > 127) { volume = 127; }
    midiopl_set_volume((uint8_t)volume);
}

static void cup_music_pause(void)   { mixer_stop(8); }

static void cup_music_resume(void)
{
    mixer_play_stream(8, music_pull, 0, 100, 100);
}

static void *cup_music_register(void *data, int len)
{
    if (!s_music_inited) {
        if (load_genmidi_from_wad() != 0) { return 0; }
        s_music_inited = 1;
    }
    if (s_midi_buf) {
        kfree(s_midi_buf);
        s_midi_buf = 0;
        s_midi_len = 0;
    }
    {
        const uint8_t *bytes = (const uint8_t *)data;
        /* Freedoom ships MIDI lumps directly (MThd...). Vanilla DOOM ships
         * MUS lumps (MUS\x1A...). Detect format by magic and only run
         * mus2midi on real MUS data; for MIDI, pass the lump straight
         * through to the OPL synth. */
        int is_mus  = (len >= 4
                       && bytes[0] == 'M' && bytes[1] == 'U'
                       && bytes[2] == 'S' && bytes[3] == 0x1A);
        int is_midi = (len >= 4
                       && bytes[0] == 'M' && bytes[1] == 'T'
                       && bytes[2] == 'h' && bytes[3] == 'd');

        if (is_midi) {
            s_midi_buf = (uint8_t *)kmalloc((uint32_t)len);
            if (!s_midi_buf) { return 0; }
            memcpy(s_midi_buf, bytes, (size_t)len);
            s_midi_len = (uint32_t)len;
            (void)smf_init();
            return s_midi_buf;
        }
        if (!is_mus) {
            serial_write_string("[i_music] unknown song format; skipping\n");
            return 0;
        }
    }
    int rc = mus2midi_convert((const uint8_t *)data, (uint32_t)len,
                              &s_midi_buf, &s_midi_len);
    if (rc != 0) {
        serial_write_string("[i_music] mus2midi failed\n");
        return 0;
    }
    (void)smf_init();
    return s_midi_buf;
}

static void cup_music_unregister(void *handle)
{
    (void)handle;
    if (s_midi_buf) {
        kfree(s_midi_buf);
        s_midi_buf = 0;
        s_midi_len = 0;
    }
    mixer_stop(8);
}

static void cup_music_play(void *handle, boolean looping)
{
    (void)handle;
    s_music_loop = looping ? 1 : 0;
    smf_reset();
    mixer_play_stream(8, music_pull, 0, 100, 100);
}

static void cup_music_stop(void) { mixer_stop(8); }

static boolean cup_music_is_playing(void)
{
    return mixer_active(8) ? true : false;
}

static void cup_music_poll(void) {}

music_module_t cupidos_music_module = {
    .sound_devices    = cup_devices,
    .num_sound_devices = 1,
    .Init             = cup_music_init,
    .Shutdown         = cup_music_shutdown,
    .SetMusicVolume   = cup_music_set_volume,
    .PauseMusic       = cup_music_pause,
    .ResumeMusic      = cup_music_resume,
    .RegisterSong     = cup_music_register,
    .UnRegisterSong   = cup_music_unregister,
    .PlaySong         = cup_music_play,
    .StopSong         = cup_music_stop,
    .MusicIsPlaying   = cup_music_is_playing,
    .Poll             = cup_music_poll,
};

/* Top-level I_*Music — DOOM calls these directly. */
void I_InitMusic(void)    { cupidos_music_module.Init(); }
void I_ShutdownMusic(void) { cupidos_music_module.Shutdown(); }

void I_SetMusicVolume(int volume)
{
    cup_music_set_volume(volume);
}

void I_PauseSong(void)    { cup_music_pause(); }
void I_ResumeSong(void)   { cup_music_resume(); }

void *I_RegisterSong(void *data, int len)
{
    return cup_music_register(data, len);
}

void I_UnRegisterSong(void *handle) { cup_music_unregister(handle); }

void I_PlaySong(void *handle, boolean looping)
{
    cup_music_play(handle, looping);
}

void I_StopSong(void)             { cup_music_stop(); }
boolean I_MusicIsPlaying(void)    { return cup_music_is_playing(); }
void I_UpdateMusic(void)          { cup_music_poll(); }
