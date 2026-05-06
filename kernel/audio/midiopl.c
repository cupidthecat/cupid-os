/* midiopl.c — MIDI → OPL3 synthesiser
 *
 * Parses a DOOM-format GENMIDI lump (175 patches × 36 bytes), maintains
 * a MIDI running-status parser, allocates 9 OPL3 melodic channels via
 * round-robin / LRU steal, and renders s16 stereo @ 22050 Hz.
 *
 * Internal OPL3 rate: 49716 Hz (Nuked-OPL3 native).  midiopl_render
 * generates at 49716 then linear-resamples to 22050.
 *
 * Not wired into the audio chain yet — Task 17 does that.
 */

#include "midiopl.h"
#include "nuked_opl3.h"
#include "../../drivers/serial.h"
#include "../string.h"

/* =========================================================================
 * GENMIDI lump structures (DMX format)
 *
 * Header:  8 bytes — "#OPL_II#"
 * 175 patches × 36 bytes (128 melodic + 47 percussion)
 * 175 × 32 bytes patch names (after the patch data)
 *
 * Each 36-byte patch:
 *   [0..1]  uint16  flags
 *   [2]     int8    finetune
 *   [3]     uint8   note (percussion note override)
 *   [4..19] 16-byte voice0: mod(6B) + car(6B) + feedback_conn(1B) + pad(3B)
 *   [20..35] 16-byte voice1: same layout (4-op; ignored in v1)
 *
 * The 6-byte operator layout inside each voice:
 *   [0] mult        (AM/VIB/EG/KSR/MULT)
 *   [1] atk_dec     (ATTACK/DECAY)
 *   [2] sus_rel     (SUSTAIN/RELEASE)
 *   [3] waveform    (WS)
 *   [4] ksl_lvl     (KSL/TL — total level)
 *   [5] ksr_eg_vib_am (flags byte)
 *   [6] feedback_conn (for the voice, after both ops)
 * =========================================================================
 */

typedef struct {
    uint8_t mult;
    uint8_t atk_dec;
    uint8_t sus_rel;
    uint8_t waveform;
    uint8_t ksl_lvl;
    uint8_t ksr_eg_vib_am;
} opl_op_t;

typedef struct {
    opl_op_t mod;
    opl_op_t car;
    uint8_t  feedback_conn;
} opl_voice_t;

typedef struct {
    uint16_t    flags;
    int8_t      finetune;
    uint8_t     note_offset;
    opl_voice_t v0;
    opl_voice_t v1;
} genmidi_patch_t;

#define NUM_PATCHES 175
static genmidi_patch_t s_patches[NUM_PATCHES];
static int             s_patches_loaded = 0;

/* =========================================================================
 * Single global OPL3 chip state.
 * opl3_chip is large (~32KB); lives in BSS (zero-init, no flash cost).
 * =========================================================================
 */
static opl3_chip g_chip;

/* =========================================================================
 * OPL3 9-channel allocator
 * =========================================================================
 */
#define NUM_OPL_CH 9

typedef struct {
    int      in_use;
    uint8_t  midi_ch;
    uint8_t  midi_note;
    uint32_t age;   /* monotonic counter — smaller = older = steal first */
} opl_ch_state_t;

static opl_ch_state_t s_opl_ch[NUM_OPL_CH];
static uint32_t       s_age = 0u;

/* =========================================================================
 * MIDI channel state (16 channels)
 * =========================================================================
 */
typedef struct {
    uint8_t  program;
    uint8_t  volume;       /* CC 7  — channel volume 0..127 */
    uint8_t  pan;          /* CC 10 — pan 0..127 (64=centre) */
    uint16_t pitch_bend;   /* 0..16383 (0x2000=centre) */
} midi_ch_state_t;

static midi_ch_state_t s_midi[16];

/* =========================================================================
 * Master volume (0..127) applied at render time.
 * =========================================================================
 */
static uint8_t s_master_vol = 100u;

/* =========================================================================
 * MIDI running-status parser state
 * =========================================================================
 */
static uint8_t s_running_status = 0u;
static uint8_t s_msg_buf[3];
static uint8_t s_msg_have = 0u;
static uint8_t s_msg_need = 0u;

/* =========================================================================
 * OPL3 operator slot offsets (register base) for channels 0..8.
 *
 * OPL3 operator register map (2-op melodic channels):
 *   ch 0: op0=0x00, op3=0x03
 *   ch 1: op1=0x01, op4=0x04
 *   ch 2: op2=0x02, op5=0x05
 *   ch 3: op6=0x08, op9=0x0B
 *   ch 4: op7=0x09, op10=0x0C
 *   ch 5: op8=0x0A, op11=0x0D
 *   ch 6: op12=0x10, op15=0x13
 *   ch 7: op13=0x11, op16=0x14
 *   ch 8: op14=0x12, op17=0x15
 * =========================================================================
 */
static const uint8_t OP_OFFSET[9][2] = {
    {0x00u, 0x03u}, {0x01u, 0x04u}, {0x02u, 0x05u},
    {0x08u, 0x0Bu}, {0x09u, 0x0Cu}, {0x0Au, 0x0Du},
    {0x10u, 0x13u}, {0x11u, 0x14u}, {0x12u, 0x15u}
};

/* F-number table for 12 chromatic semitones (octave 0 reference).
 * Source: OPL3 datasheet, Table 1 — octave/block applied separately. */
static const uint16_t FNUM[12] = {
    0x158u, 0x16Du, 0x183u, 0x19Au, 0x1B2u, 0x1CCu,
    0x1E7u, 0x204u, 0x223u, 0x244u, 0x267u, 0x28Du
};

/* =========================================================================
 * Forward declarations (strict -Wmissing-prototypes compliance)
 * =========================================================================
 */
static int  midiopl_load_genmidi(const uint8_t *lump, uint32_t len);
static void midiopl_handle_event(const uint8_t *msg, uint8_t msglen);
static void note_on(uint8_t ch, uint8_t note, uint8_t vel);
static void note_off(uint8_t ch, uint8_t note);
static void program_change(uint8_t ch, uint8_t prog);
static void controller(uint8_t ch, uint8_t cc, uint8_t v);
static void pitch_bend_change(uint8_t ch, uint16_t bend);
static int  alloc_opl_ch(uint8_t midi_ch, uint8_t note);
static void free_opl_ch(int oplc);
static void program_patch(int oplc, const genmidi_patch_t *p);
static void key_on(int oplc, uint16_t fnum, uint8_t block);
static void key_off(int oplc);

/* =========================================================================
 * Public API
 * =========================================================================
 */

int midiopl_init(const uint8_t *genmidi_lump, uint32_t lump_len)
{
    int i;

    OPL3_Reset(&g_chip, 49716u);
    /* Enable OPL3 mode (new-register access) */
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)0x105u, (uint8_t)0x01u);

    for (i = 0; i < NUM_OPL_CH; i++) {
        s_opl_ch[i].in_use   = 0;
        s_opl_ch[i].midi_ch  = 0u;
        s_opl_ch[i].midi_note = 0u;
        s_opl_ch[i].age      = 0u;
    }
    for (i = 0; i < 16; i++) {
        s_midi[i].program    = 0u;
        s_midi[i].volume     = 100u;
        s_midi[i].pan        = 64u;
        s_midi[i].pitch_bend = 0x2000u;
    }
    s_running_status = 0u;
    s_msg_have       = 0u;
    s_msg_need       = 0u;
    s_age            = 0u;

    if (midiopl_load_genmidi(genmidi_lump, lump_len) != 0) {
        serial_write_string("[midiopl] WARN: GENMIDI parse failed; using defaults\n");
        /* Not fatal — synth still works, just no patch data */
    }
    return 0;
}

void midiopl_reset(void)
{
    int i;
    for (i = 0; i < NUM_OPL_CH; i++) {
        if (s_opl_ch[i].in_use) {
            free_opl_ch(i);
        }
    }
    s_running_status = 0u;
    s_msg_have       = 0u;
    s_msg_need       = 0u;
}

void midiopl_set_volume(uint8_t vol_0_127)
{
    s_master_vol = (vol_0_127 > 127u) ? 127u : vol_0_127;
}

/* =========================================================================
 * GENMIDI lump parser
 *
 * Layout verified against DOOM source (w_wad.c / sounds.c):
 *   [0..7]   "#OPL_II#"
 *   [8..]    175 × 36-byte patch records
 *   [8+6300..] 175 × 32-byte ASCII names (ignored here)
 *
 * 36-byte record:
 *   [0..1]  flags (LE16)
 *   [2]     finetune (signed)
 *   [3]     note_offset (unsigned)
 *   [4..19] voice0 (16 bytes):
 *             [0]  mod.mult
 *             [1]  mod.atk_dec
 *             [2]  mod.sus_rel
 *             [3]  mod.waveform
 *             [4]  mod.ksl_lvl
 *             [5]  mod.ksr_eg_vib_am
 *             [6]  feedback_conn
 *             [7]  (padding / unused output level byte)
 *             [8]  car.mult
 *             [9]  car.atk_dec
 *             [10] car.sus_rel
 *             [11] car.waveform
 *             [12] car.ksl_lvl
 *             [13] car.ksr_eg_vib_am
 *             [14..15] (padding)
 *   [20..35] voice1 (same layout; ignored for v1 2-op)
 * =========================================================================
 */
static int midiopl_load_genmidi(const uint8_t *lump, uint32_t len)
{
    const uint8_t *p;
    int i;

    if (lump == NULL) return -1;
    /* Minimum: 8-byte header + 175*36 bytes of patch data */
    if (len < 8u + (uint32_t)(NUM_PATCHES * 36)) return -1;

    /* Verify magic "#OPL_II#" */
    if (lump[0] != (uint8_t)'#' || lump[1] != (uint8_t)'O' ||
        lump[2] != (uint8_t)'P' || lump[3] != (uint8_t)'L' ||
        lump[4] != (uint8_t)'_' || lump[5] != (uint8_t)'I' ||
        lump[6] != (uint8_t)'I' || lump[7] != (uint8_t)'#') {
        return -2;
    }

    p = lump + 8u;
    for (i = 0; i < NUM_PATCHES; i++) {
        const uint8_t *v0;

        s_patches[i].flags       = (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
        s_patches[i].finetune    = (int8_t)p[2];
        s_patches[i].note_offset = p[3];

        /* voice0 starts at p+4 */
        v0 = p + 4u;
        s_patches[i].v0.mod.mult           = v0[0];
        s_patches[i].v0.mod.atk_dec        = v0[1];
        s_patches[i].v0.mod.sus_rel        = v0[2];
        s_patches[i].v0.mod.waveform       = v0[3];
        s_patches[i].v0.mod.ksl_lvl        = v0[4];
        s_patches[i].v0.mod.ksr_eg_vib_am  = v0[5];
        s_patches[i].v0.feedback_conn      = v0[6];
        /* v0[7] is padding/unused */
        s_patches[i].v0.car.mult           = v0[8];
        s_patches[i].v0.car.atk_dec        = v0[9];
        s_patches[i].v0.car.sus_rel        = v0[10];
        s_patches[i].v0.car.waveform       = v0[11];
        s_patches[i].v0.car.ksl_lvl        = v0[12];
        s_patches[i].v0.car.ksr_eg_vib_am  = v0[13];
        /* v0[14..15] padding; voice1 [20..35] ignored in v1 */

        p += 36u;
    }

    s_patches_loaded = 1;
    return 0;
}

/* =========================================================================
 * MIDI byte-stream parser
 *
 * Handles:
 *  0xF8..0xFF (realtime) — skipped inline
 *  0xFF       (meta)     — VLQ-length body skipped
 *  0xF0/0xF7  (sysex)    — body skipped up to 0xF7
 *  0x80..0xEF (channel)  — running-status buffered, dispatched
 *  data bytes            — accumulated into s_msg_buf[]
 * =========================================================================
 */
void midiopl_feed(const uint8_t *bytes, uint32_t len)
{
    uint32_t i;

    for (i = 0u; i < len; i++) {
        uint8_t b = bytes[i];

        if (b & 0x80u) {
            /* --- status byte --- */

            /* Realtime messages (0xF8..0xFF): single-byte, no state change */
            if (b >= 0xF8u) {
                /* 0xFF is also realtime in a raw MIDI stream.
                 * In a MIDI *file* SMF stream 0xFF is meta — but midiopl_feed
                 * processes raw MIDI output from mus2midi which does not embed
                 * SMF meta events in the live byte stream. Skip. */
                continue;
            }

            /* SysEx: skip until 0xF7 end-of-exclusive */
            if (b == 0xF0u || b == 0xF7u) {
                if (b == 0xF0u) {
                    /* scan for terminating 0xF7 */
                    i++;
                    while (i < len && bytes[i] != 0xF7u) {
                        i++;
                    }
                }
                s_running_status = 0u;
                s_msg_have = 0u;
                continue;
            }

            /* Channel / mode messages (0x80..0xEF) */
            {
                uint8_t kind = (uint8_t)(b & 0xF0u);
                s_running_status = b;
                s_msg_buf[0]     = b;
                s_msg_have       = 1u;
                /* Program Change (0xC0) and Channel Pressure (0xD0) take 1 data byte */
                s_msg_need = ((kind == 0xC0u) || (kind == 0xD0u)) ? 2u : 3u;
            }

        } else {
            /* --- data byte --- */

            /* Running status: re-prime from saved status if buffer empty */
            if (s_msg_have == 0u) {
                if (s_running_status == 0u) {
                    /* No running status — discard orphan data byte */
                    continue;
                }
                {
                    uint8_t kind = (uint8_t)(s_running_status & 0xF0u);
                    s_msg_buf[0] = s_running_status;
                    s_msg_have   = 1u;
                    s_msg_need   = ((kind == 0xC0u) || (kind == 0xD0u)) ? 2u : 3u;
                }
            }

            if (s_msg_have < s_msg_need) {
                s_msg_buf[s_msg_have] = b;
                s_msg_have++;
                if (s_msg_have == s_msg_need) {
                    midiopl_handle_event(s_msg_buf, s_msg_need);
                    /* Keep status byte in buf[0] for next running-status message */
                    s_msg_have = 1u;
                }
            }
        }
    }
}

/* =========================================================================
 * Event dispatcher
 * =========================================================================
 */
static void midiopl_handle_event(const uint8_t *msg, uint8_t msglen)
{
    uint8_t kind = (uint8_t)(msg[0] & 0xF0u);
    uint8_t ch   = (uint8_t)(msg[0] & 0x0Fu);

    if (kind == 0x90u && msglen == 3u) {
        if (msg[2] != 0u) {
            note_on(ch, msg[1], msg[2]);
        } else {
            /* Note-On with velocity 0 = Note-Off */
            note_off(ch, msg[1]);
        }
    } else if (kind == 0x80u && msglen == 3u) {
        note_off(ch, msg[1]);
    } else if (kind == 0xC0u && msglen == 2u) {
        program_change(ch, msg[1]);
    } else if (kind == 0xB0u && msglen == 3u) {
        controller(ch, msg[1], msg[2]);
    } else if (kind == 0xE0u && msglen == 3u) {
        /* Pitch bend: two 7-bit data bytes, LSB first */
        uint16_t bend = (uint16_t)((uint16_t)msg[1] | (uint16_t)((uint16_t)msg[2] << 7));
        pitch_bend_change(ch, bend);
    }
    /* All other statuses (0xA0 aftertouch, 0xD0 channel pressure) ignored */
}

/* =========================================================================
 * OPL3 patch programming
 *
 * Register map for 2-op melodic channel `oplc` (0..8):
 *   Modulator registers: base = OP_OFFSET[oplc][0]
 *     0x20+base  mult / KSR / EG / VIB / AM
 *     0x40+base  KSL / TL (total level = attenuation)
 *     0x60+base  AR / DR
 *     0x80+base  SL / RR
 *     0xE0+base  waveform select
 *   Carrier registers:   base = OP_OFFSET[oplc][1]
 *     same offsets
 *   Channel registers:
 *     0xA0+oplc  F-number low 8 bits
 *     0xB0+oplc  key-on, block, F-number high 2 bits
 *     0xC0+oplc  feedback / connection / L+R output enable
 * =========================================================================
 */
static void program_patch(int oplc, const genmidi_patch_t *p)
{
    uint8_t mod_off = OP_OFFSET[oplc][0];
    uint8_t car_off = OP_OFFSET[oplc][1];

    /* Modulator */
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x20u + (uint16_t)mod_off), p->v0.mod.mult);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x40u + (uint16_t)mod_off), p->v0.mod.ksl_lvl);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x60u + (uint16_t)mod_off), p->v0.mod.atk_dec);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x80u + (uint16_t)mod_off), p->v0.mod.sus_rel);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0xE0u + (uint16_t)mod_off), p->v0.mod.waveform);

    /* Carrier */
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x20u + (uint16_t)car_off), p->v0.car.mult);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x40u + (uint16_t)car_off), p->v0.car.ksl_lvl);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x60u + (uint16_t)car_off), p->v0.car.atk_dec);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x80u + (uint16_t)car_off), p->v0.car.sus_rel);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0xE0u + (uint16_t)car_off), p->v0.car.waveform);

    /* Channel: feedback/connection — OR in 0x30 to enable both L+R outputs */
    OPL3_WriteRegBuffered(&g_chip,
                          (uint16_t)(0xC0u + (uint16_t)oplc),
                          (uint8_t)(p->v0.feedback_conn | 0x30u));
}

/* =========================================================================
 * Key-on / Key-off helpers
 * =========================================================================
 */
static void key_on(int oplc, uint16_t fnum, uint8_t block)
{
    /* A0: F-number low 8 bits */
    uint8_t fl = (uint8_t)(fnum & 0xFFu);
    /* B0: Key-On bit | block[2:0]<<2 | F-num[9:8] */
    uint8_t fh = (uint8_t)((fnum >> 8u) & 0x03u);
    uint8_t b_kon_fh = (uint8_t)(0x20u | (uint8_t)((uint8_t)(block & 0x07u) << 2u) | fh);

    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0xA0u + (uint16_t)oplc), fl);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0xB0u + (uint16_t)oplc), b_kon_fh);
}

static void key_off(int oplc)
{
    /* Clear key-on bit, preserve block/fnum (write 0 — channel silenced) */
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0xB0u + (uint16_t)oplc), (uint8_t)0x00u);
}

/* =========================================================================
 * OPL3 channel allocator
 * =========================================================================
 */
static int alloc_opl_ch(uint8_t midi_ch, uint8_t note)
{
    int i;
    int oldest;

    /* Scan for a free slot */
    for (i = 0; i < NUM_OPL_CH; i++) {
        if (!s_opl_ch[i].in_use) {
            s_opl_ch[i].in_use    = 1;
            s_opl_ch[i].midi_ch   = midi_ch;
            s_opl_ch[i].midi_note = note;
            s_opl_ch[i].age       = ++s_age;
            return i;
        }
    }

    /* All channels busy — steal the oldest (LRU) */
    oldest = 0;
    for (i = 1; i < NUM_OPL_CH; i++) {
        if (s_opl_ch[i].age < s_opl_ch[oldest].age) {
            oldest = i;
        }
    }
    free_opl_ch(oldest);
    s_opl_ch[oldest].in_use    = 1;
    s_opl_ch[oldest].midi_ch   = midi_ch;
    s_opl_ch[oldest].midi_note = note;
    s_opl_ch[oldest].age       = ++s_age;
    return oldest;
}

static void free_opl_ch(int oplc)
{
    key_off(oplc);
    s_opl_ch[oplc].in_use = 0;
}

/* =========================================================================
 * MIDI event handlers
 * =========================================================================
 */
static void note_on(uint8_t ch, uint8_t note, uint8_t vel)
{
    uint8_t prog;
    int oplc;
    int oct;
    uint16_t fnum;

    if (!s_patches_loaded) return;

    prog = s_midi[ch].program;
    if (prog >= (uint8_t)NUM_PATCHES) prog = 0u;

    oplc = alloc_opl_ch(ch, note);
    program_patch(oplc, &s_patches[prog]);

    /* Map MIDI note to F-num / block (octave).
     * Middle C = MIDI note 60 = C4.
     * OPL3 block 0..7 ~ octaves 0..7. */
    oct = (int)note / 12;
    if (oct < 0) oct = 0;
    if (oct > 7) oct = 7;

    fnum = FNUM[(int)note % 12];
    key_on(oplc, fnum, (uint8_t)oct);

    (void)vel; /* TODO v2: scale carrier TL by velocity */
}

static void note_off(uint8_t ch, uint8_t note)
{
    int i;
    for (i = 0; i < NUM_OPL_CH; i++) {
        if (s_opl_ch[i].in_use &&
            s_opl_ch[i].midi_ch   == ch &&
            s_opl_ch[i].midi_note == note) {
            free_opl_ch(i);
            return;
        }
    }
}

static void program_change(uint8_t ch, uint8_t prog)
{
    s_midi[ch].program = prog;
}

static void controller(uint8_t ch, uint8_t cc, uint8_t v)
{
    int i;
    if (cc == 7u) {
        s_midi[ch].volume = v;
    } else if (cc == 10u) {
        s_midi[ch].pan = v;
    } else if (cc == 123u) {
        /* All Notes Off */
        for (i = 0; i < NUM_OPL_CH; i++) {
            if (s_opl_ch[i].in_use && s_opl_ch[i].midi_ch == ch) {
                free_opl_ch(i);
            }
        }
    }
}

static void pitch_bend_change(uint8_t ch, uint16_t bend)
{
    s_midi[ch].pitch_bend = bend;
    /* TODO v2: re-key ringing notes on this channel with detuned fnum */
}

/* =========================================================================
 * midiopl_render — pull stereo s16 @ 22050 Hz
 *
 * Strategy:
 *   1. Compute how many OPL3-native samples (@ 49716 Hz) are needed to
 *      cover `frames` output samples via ceiling division.
 *   2. Call OPL3_GenerateStream to fill a static native buffer (stereo).
 *   3. Linear-interpolate native → output, applying master volume.
 *
 * Buffer: 2048 native frames × 2 channels = 4096 int16_t.
 * At 49716 Hz, 2048 native frames ≈ 41 ms which comfortably covers
 * ≈ 23 ms of output at 22050 Hz (512 output frames is a typical call).
 * =========================================================================
 */

#define RENDER_NATIVE_BUF 2048u

void midiopl_render(int16_t *out_stereo, uint32_t frames)
{
    static int16_t s_native[RENDER_NATIVE_BUF * 2u];
    uint32_t need;
    uint32_t f;
    int32_t  att;

    /* How many native samples do we need? */
    need = (frames * 49716u + 22049u) / 22050u;
    if (need > RENDER_NATIVE_BUF) {
        need = RENDER_NATIVE_BUF;
    }

    OPL3_GenerateStream(&g_chip, s_native, need);

    att = (int32_t)s_master_vol; /* 0..127 */

    for (f = 0u; f < frames; f++) {
        uint32_t src_q16;
        uint32_t idx;
        uint32_t fr;
        int32_t l0, l1, r0, r1;
        int32_t l, r;

        /* Fixed-point source position in native buffer */
        src_q16 = (uint32_t)((f * 49716u * 65536u) / 22050u);
        idx     = src_q16 >> 16u;
        fr      = src_q16 & 0xFFFFu;

        /* Guard against running off the end of the native buffer */
        if (idx >= need - 1u) {
            idx = need - 2u;
        }

        l0 = (int32_t)s_native[idx * 2u + 0u];
        l1 = (int32_t)s_native[(idx + 1u) * 2u + 0u];
        r0 = (int32_t)s_native[idx * 2u + 1u];
        r1 = (int32_t)s_native[(idx + 1u) * 2u + 1u];

        /* Linear interpolate then scale by master volume */
        l = (l0 + (((l1 - l0) * (int32_t)fr) >> 16)) * att / 127;
        r = (r0 + (((r1 - r0) * (int32_t)fr) >> 16)) * att / 127;

        out_stereo[f * 2u + 0u] = (int16_t)l;
        out_stereo[f * 2u + 1u] = (int16_t)r;
    }
}
