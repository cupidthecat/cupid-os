#include "mixer.h"
#include "../../drivers/serial.h"

typedef struct {
    const int16_t *pcm;
    uint32_t       len_frames;
    uint32_t       pos_frames;
    uint8_t        channels;
    uint8_t        loop;
    uint8_t        vol_l;
    uint8_t        vol_r;
    uint8_t        active;
    uint8_t        is_stream;
    mixer_pull_fn  pull;
    void          *ctx;
} slot_t;

static slot_t s_slots[MIXER_SLOTS];

int mixer_init(void) {
    for (int i = 0; i < MIXER_SLOTS; i++) {
        s_slots[i].active    = 0;
        s_slots[i].is_stream = 0;
    }
    return 0;
}

static int slot_valid(int slot) { return slot >= 0 && slot < MIXER_SLOTS; }

int mixer_play(int slot, const int16_t *pcm, uint32_t frames,
               uint8_t channels, uint8_t loop,
               uint8_t vol_l, uint8_t vol_r) {
    if (!slot_valid(slot) || !pcm || (channels != 1 && channels != 2)) return -1;
    s_slots[slot].pcm        = pcm;
    s_slots[slot].len_frames = frames;
    s_slots[slot].pos_frames = 0;
    s_slots[slot].channels   = channels;
    s_slots[slot].loop       = loop;
    s_slots[slot].vol_l      = vol_l;
    s_slots[slot].vol_r      = vol_r;
    s_slots[slot].is_stream  = 0;
    s_slots[slot].active     = 1;
    return 0;
}

int mixer_play_stream(int slot, mixer_pull_fn pull, void *ctx,
                      uint8_t vol_l, uint8_t vol_r) {
    if (!slot_valid(slot) || !pull) return -1;
    s_slots[slot].pull      = pull;
    s_slots[slot].ctx       = ctx;
    s_slots[slot].vol_l     = vol_l;
    s_slots[slot].vol_r     = vol_r;
    s_slots[slot].is_stream = 1;
    s_slots[slot].active    = 1;
    return 0;
}

void mixer_stop(int slot) {
    if (slot_valid(slot)) s_slots[slot].active = 0;
}

int mixer_active(int slot) {
    return slot_valid(slot) ? (int)s_slots[slot].active : 0;
}

void mixer_set_volume(int slot, uint8_t vol_l, uint8_t vol_r) {
    if (slot_valid(slot)) {
        s_slots[slot].vol_l = vol_l;
        s_slots[slot].vol_r = vol_r;
    }
}

static int16_t clamp16(int32_t v) {
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

void mixer_fill(int16_t *out, uint32_t frames) {
    /* zero output */
    for (uint32_t f = 0; f < frames; f++) {
        out[f * 2u + 0u] = 0;
        out[f * 2u + 1u] = 0;
    }
    static int16_t scratch[1024 * 2];   /* per-slot stereo */
    for (int s = 0; s < MIXER_SLOTS; s++) {
        slot_t *sl = &s_slots[s];
        if (!sl->active) continue;
        if (sl->is_stream) {
            sl->pull(scratch, frames, sl->ctx);
            for (uint32_t f = 0; f < frames; f++) {
                int32_t l = (int32_t)scratch[f * 2u + 0u] * (int32_t)sl->vol_l;
                int32_t r = (int32_t)scratch[f * 2u + 1u] * (int32_t)sl->vol_r;
                out[f * 2u + 0u] = clamp16((int32_t)out[f * 2u + 0u] + (l >> 7));
                out[f * 2u + 1u] = clamp16((int32_t)out[f * 2u + 1u] + (r >> 7));
            }
        } else {
            for (uint32_t f = 0; f < frames; f++) {
                if (sl->pos_frames >= sl->len_frames) {
                    if (sl->loop) sl->pos_frames = 0u;
                    else { sl->active = 0; break; }
                }
                int16_t lv, rv;
                if (sl->channels == 1u) {
                    int16_t v = sl->pcm[sl->pos_frames];
                    lv = v; rv = v;
                } else {
                    lv = sl->pcm[sl->pos_frames * 2u + 0u];
                    rv = sl->pcm[sl->pos_frames * 2u + 1u];
                }
                int32_t li = (int32_t)lv * (int32_t)sl->vol_l;
                int32_t ri = (int32_t)rv * (int32_t)sl->vol_r;
                out[f * 2u + 0u] = clamp16((int32_t)out[f * 2u + 0u] + (li >> 7));
                out[f * 2u + 1u] = clamp16((int32_t)out[f * 2u + 1u] + (ri >> 7));
                sl->pos_frames++;
            }
        }
    }
}
