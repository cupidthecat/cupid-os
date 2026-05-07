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
    uint8_t     fixed_note;        /* used for fixed-pitch percussion only */
    int16_t     base_note_offset;  /* per-instrument tuning (LE16, signed) */
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

/* Frequency table from chocolate-doom's i_oplmusic.c (GPL-2).
 *
 * Indexed by `freq_index = 64 + 32 * note + pitch_bend`. The first
 * 284 entries are used directly; beyond 284 the table loops in 384-
 * entry chunks (one per octave) and the octave is OR'd into bit 10+
 * of the result (block field for OPL3 register 0xB0). This gives a
 * smooth pitch-bend-aware mapping from MIDI note to OPL FNUM/block,
 * which the previous 12-entry chromatic table couldn't do — and is
 * the reason real DOOM/Freedoom music plays as recognisable melodies
 * here instead of buzzy beeps.
 */
static const uint16_t s_freq_curve[] = {
    0x133, 0x133, 0x134, 0x134, 0x135, 0x136, 0x136, 0x137,
    0x137, 0x138, 0x138, 0x139, 0x139, 0x13a, 0x13b, 0x13b,
    0x13c, 0x13c, 0x13d, 0x13d, 0x13e, 0x13f, 0x13f, 0x140,
    0x140, 0x141, 0x142, 0x142, 0x143, 0x143, 0x144, 0x144,
    0x145, 0x146, 0x146, 0x147, 0x147, 0x148, 0x149, 0x149,
    0x14a, 0x14a, 0x14b, 0x14c, 0x14c, 0x14d, 0x14d, 0x14e,
    0x14f, 0x14f, 0x150, 0x150, 0x151, 0x152, 0x152, 0x153,
    0x153, 0x154, 0x155, 0x155, 0x156, 0x157, 0x157, 0x158,
    0x158, 0x159, 0x15a, 0x15a, 0x15b, 0x15b, 0x15c, 0x15d,
    0x15d, 0x15e, 0x15f, 0x15f, 0x160, 0x161, 0x161, 0x162,
    0x162, 0x163, 0x164, 0x164, 0x165, 0x166, 0x166, 0x167,
    0x168, 0x168, 0x169, 0x16a, 0x16a, 0x16b, 0x16c, 0x16c,
    0x16d, 0x16e, 0x16e, 0x16f, 0x170, 0x170, 0x171, 0x172,
    0x172, 0x173, 0x174, 0x174, 0x175, 0x176, 0x176, 0x177,
    0x178, 0x178, 0x179, 0x17a, 0x17a, 0x17b, 0x17c, 0x17c,
    0x17d, 0x17e, 0x17e, 0x17f, 0x180, 0x181, 0x181, 0x182,
    0x183, 0x183, 0x184, 0x185, 0x185, 0x186, 0x187, 0x188,
    0x188, 0x189, 0x18a, 0x18a, 0x18b, 0x18c, 0x18d, 0x18d,
    0x18e, 0x18f, 0x18f, 0x190, 0x191, 0x192, 0x192, 0x193,
    0x194, 0x194, 0x195, 0x196, 0x197, 0x197, 0x198, 0x199,
    0x19a, 0x19a, 0x19b, 0x19c, 0x19d, 0x19d, 0x19e, 0x19f,
    0x1a0, 0x1a0, 0x1a1, 0x1a2, 0x1a3, 0x1a3, 0x1a4, 0x1a5,
    0x1a6, 0x1a6, 0x1a7, 0x1a8, 0x1a9, 0x1a9, 0x1aa, 0x1ab,
    0x1ac, 0x1ad, 0x1ad, 0x1ae, 0x1af, 0x1b0, 0x1b0, 0x1b1,
    0x1b2, 0x1b3, 0x1b4, 0x1b4, 0x1b5, 0x1b6, 0x1b7, 0x1b8,
    0x1b8, 0x1b9, 0x1ba, 0x1bb, 0x1bc, 0x1bc, 0x1bd, 0x1be,
    0x1bf, 0x1c0, 0x1c0, 0x1c1, 0x1c2, 0x1c3, 0x1c4, 0x1c4,
    0x1c5, 0x1c6, 0x1c7, 0x1c8, 0x1c9, 0x1c9, 0x1ca, 0x1cb,
    0x1cc, 0x1cd, 0x1ce, 0x1ce, 0x1cf, 0x1d0, 0x1d1, 0x1d2,
    0x1d3, 0x1d3, 0x1d4, 0x1d5, 0x1d6, 0x1d7, 0x1d8, 0x1d8,
    0x1d9, 0x1da, 0x1db, 0x1dc, 0x1dd, 0x1de, 0x1de, 0x1df,
    0x1e0, 0x1e1, 0x1e2, 0x1e3, 0x1e4, 0x1e5, 0x1e5, 0x1e6,
    0x1e7, 0x1e8, 0x1e9, 0x1ea, 0x1eb, 0x1ec, 0x1ed, 0x1ed,
    0x1ee, 0x1ef, 0x1f0, 0x1f1, 0x1f2, 0x1f3, 0x1f4, 0x1f5,
    0x1f6, 0x1f6, 0x1f7, 0x1f8, 0x1f9, 0x1fa, 0x1fb, 0x1fc,
    0x1fd, 0x1fe, 0x1ff, 0x200, 0x201, 0x201, 0x202, 0x203,
    0x204, 0x205, 0x206, 0x207, 0x208, 0x209, 0x20a, 0x20b,
    0x20c, 0x20d, 0x20e, 0x20f, 0x210, 0x210, 0x211, 0x212,
    0x213, 0x214, 0x215, 0x216, 0x217, 0x218, 0x219, 0x21a,
    0x21b, 0x21c, 0x21d, 0x21e, 0x21f, 0x220, 0x221, 0x222,
    0x223, 0x224, 0x225, 0x226, 0x227, 0x228, 0x229, 0x22a,
    0x22b, 0x22c, 0x22d, 0x22e, 0x22f, 0x230, 0x231, 0x232,
    0x233, 0x234, 0x235, 0x236, 0x237, 0x238, 0x239, 0x23a,
    0x23b, 0x23c, 0x23d, 0x23e, 0x23f, 0x240, 0x241, 0x242,
    0x244, 0x245, 0x246, 0x247, 0x248, 0x249, 0x24a, 0x24b,
    0x24c, 0x24d, 0x24e, 0x24f, 0x250, 0x251, 0x252, 0x253,
    0x254, 0x256, 0x257, 0x258, 0x259, 0x25a, 0x25b, 0x25c,
    0x25d, 0x25e, 0x25f, 0x260, 0x262, 0x263, 0x264, 0x265,
    0x266, 0x267, 0x268, 0x269, 0x26a, 0x26c, 0x26d, 0x26e,
    0x26f, 0x270, 0x271, 0x272, 0x273, 0x275, 0x276, 0x277,
    0x278, 0x279, 0x27a, 0x27b, 0x27d, 0x27e, 0x27f, 0x280,
    0x281, 0x282, 0x284, 0x285, 0x286, 0x287, 0x288, 0x289,
    0x28b, 0x28c, 0x28d, 0x28e, 0x28f, 0x290, 0x292, 0x293,
    0x294, 0x295, 0x296, 0x298, 0x299, 0x29a, 0x29b, 0x29c,
    0x29e, 0x29f, 0x2a0, 0x2a1, 0x2a2, 0x2a4, 0x2a5, 0x2a6,
    0x2a7, 0x2a9, 0x2aa, 0x2ab, 0x2ac, 0x2ae, 0x2af, 0x2b0,
    0x2b1, 0x2b2, 0x2b4, 0x2b5, 0x2b6, 0x2b7, 0x2b9, 0x2ba,
    0x2bb, 0x2bd, 0x2be, 0x2bf, 0x2c0, 0x2c2, 0x2c3, 0x2c4,
    0x2c5, 0x2c7, 0x2c8, 0x2c9, 0x2cb, 0x2cc, 0x2cd, 0x2ce,
    0x2d0, 0x2d1, 0x2d2, 0x2d4, 0x2d5, 0x2d6, 0x2d8, 0x2d9,
    0x2da, 0x2dc, 0x2dd, 0x2de, 0x2e0, 0x2e1, 0x2e2, 0x2e4,
    0x2e5, 0x2e6, 0x2e8, 0x2e9, 0x2ea, 0x2ec, 0x2ed, 0x2ee,
    0x2f0, 0x2f1, 0x2f2, 0x2f4, 0x2f5, 0x2f6, 0x2f8, 0x2f9,
    0x2fb, 0x2fc, 0x2fd, 0x2ff, 0x300, 0x302, 0x303, 0x304,
    0x306, 0x307, 0x309, 0x30a, 0x30b, 0x30d, 0x30e, 0x310,
    0x311, 0x312, 0x314, 0x315, 0x317, 0x318, 0x31a, 0x31b,
    0x31c, 0x31e, 0x31f, 0x321, 0x322, 0x324, 0x325, 0x327,
    0x328, 0x329, 0x32b, 0x32c, 0x32e, 0x32f, 0x331, 0x332,
    0x334, 0x335, 0x337, 0x338, 0x33a, 0x33b, 0x33d, 0x33e,
    0x340, 0x341, 0x343, 0x344, 0x346, 0x347, 0x349, 0x34a,
    0x34c, 0x34d, 0x34f, 0x350, 0x352, 0x353, 0x355, 0x357,
    0x358, 0x35a, 0x35b, 0x35d, 0x35e, 0x360, 0x361, 0x363,
    0x365, 0x366, 0x368, 0x369, 0x36b, 0x36c, 0x36e, 0x370,
    0x371, 0x373, 0x374, 0x376, 0x378, 0x379, 0x37b, 0x37c,
    0x37e, 0x380, 0x381, 0x383, 0x384, 0x386, 0x388, 0x389,
    0x38b, 0x38d, 0x38e, 0x390, 0x392, 0x393, 0x395, 0x397,
    0x398, 0x39a, 0x39c, 0x39d, 0x39f, 0x3a1, 0x3a2, 0x3a4,
    0x3a6, 0x3a7, 0x3a9, 0x3ab, 0x3ac, 0x3ae, 0x3b0, 0x3b1,
    0x3b3, 0x3b5, 0x3b7, 0x3b8, 0x3ba, 0x3bc, 0x3bd, 0x3bf,
    0x3c1, 0x3c3, 0x3c4, 0x3c6, 0x3c8, 0x3ca, 0x3cb, 0x3cd,
    0x3cf, 0x3d1, 0x3d2, 0x3d4, 0x3d6, 0x3d8, 0x3da, 0x3db,
    0x3dd, 0x3df, 0x3e1, 0x3e3, 0x3e4, 0x3e6, 0x3e8, 0x3ea,
    0x3ec, 0x3ed, 0x3ef, 0x3f1, 0x3f3, 0x3f5, 0x3f6, 0x3f8,
    0x3fa, 0x3fc, 0x3fe, 0x36c
};
#define FREQ_CURVE_LEN ((int)(sizeof(s_freq_curve) / sizeof(s_freq_curve[0])))

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
static void key_on_freq(int oplc, uint32_t freq);
static void key_off(int oplc);
static uint32_t freq_for_note(int note, int bend, int finetune_voice2);

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
        s_patches[i].fixed_note  = p[3];

        /* DOOM GENMIDI voice layout (16 bytes):
         *   [0..5]  modulator op (tremolo, attack, sustain, waveform, scale, level)
         *   [6]     feedback / connection
         *   [7..12] carrier op (same six fields)
         *   [13]    unused
         *   [14..15] base_note_offset (LE16, signed)
         *
         * Earlier code put the carrier at [8..13], shifted one byte
         * forward, so every carrier register saw the wrong field
         * (attack-byte programmed as tremolo, etc). That made every
         * patch sound like a buzzy beep. */
        v0 = p + 4u;
        s_patches[i].v0.mod.mult           = v0[0];
        s_patches[i].v0.mod.atk_dec        = v0[1];
        s_patches[i].v0.mod.sus_rel        = v0[2];
        s_patches[i].v0.mod.waveform       = v0[3];
        s_patches[i].v0.mod.ksl_lvl        = v0[4];
        s_patches[i].v0.mod.ksr_eg_vib_am  = v0[5];
        s_patches[i].v0.feedback_conn      = v0[6];
        s_patches[i].v0.car.mult           = v0[7];
        s_patches[i].v0.car.atk_dec        = v0[8];
        s_patches[i].v0.car.sus_rel        = v0[9];
        s_patches[i].v0.car.waveform       = v0[10];
        s_patches[i].v0.car.ksl_lvl        = v0[11];
        s_patches[i].v0.car.ksr_eg_vib_am  = v0[12];
        /* v0[13] unused */
        s_patches[i].base_note_offset = (int16_t)((uint16_t)v0[14]
                                       | (uint16_t)((uint16_t)v0[15] << 8));
        /* voice1 (p+20..p+35) ignored in v1 — single voice per patch. */

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
/* MIDI 0..127 → DOOM volume_mapping_table (chocolate-doom). Used for
 * scaling carrier output level by note velocity * channel volume. */
static const uint8_t s_vmt[128] = {
      0,  1,  3,  5,  6,  8, 10, 11,
     13, 14, 16, 17, 19, 20, 22, 23,
     25, 26, 27, 29, 30, 32, 33, 34,
     36, 37, 39, 41, 43, 45, 47, 49,
     50, 52, 54, 55, 57, 59, 60, 61,
     63, 64, 66, 67, 68, 69, 71, 72,
     73, 74, 75, 76, 77, 79, 80, 81,
     82, 83, 84, 84, 85, 86, 87, 88,
     89, 90, 91, 92, 92, 93, 94, 95,
     96, 96, 97, 98, 99, 99,100,101,
    101,102,103,103,104,105,105,106,
    107,107,108,109,109,110,110,111,
    112,112,113,113,114,114,115,115,
    116,117,117,118,118,119,119,120,
    120,121,121,122,122,123,123,123,
    124,124,125,125,126,126,127,127
};

/* Program a voice for an instrument. Modulator gets its real level
 * applied if the patch uses modulating feedback (bit 0 of feedback
 * byte = 0); the carrier is silenced (level=0x3F) and the real
 * volume is applied per-note in note_on via velocity + channel vol.
 * This matches chocolate-doom's LoadOperatorData semantics. */
static void program_patch(int oplc, const genmidi_patch_t *p)
{
    uint8_t mod_off = OP_OFFSET[oplc][0];
    uint8_t car_off = OP_OFFSET[oplc][1];
    uint8_t fb      = p->v0.feedback_conn;
    int     modulating = ((fb & 0x01u) == 0u);
    uint8_t mod_lvl;

    /* In modulating mode the modulator's own level shapes the FM index;
     * in additive mode the modulator output is summed and we want it
     * silent at first. */
    mod_lvl = (uint8_t)(p->v0.mod.ksl_lvl
              | (modulating ? p->v0.mod.ksr_eg_vib_am : 0x3Fu));

    /* Modulator */
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x20u + (uint16_t)mod_off), p->v0.mod.mult);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x40u + (uint16_t)mod_off), mod_lvl);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x60u + (uint16_t)mod_off), p->v0.mod.atk_dec);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x80u + (uint16_t)mod_off), p->v0.mod.sus_rel);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0xE0u + (uint16_t)mod_off), p->v0.mod.waveform);

    /* Carrier — silence first; note_on applies velocity-scaled volume. */
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x20u + (uint16_t)car_off), p->v0.car.mult);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x40u + (uint16_t)car_off),
                          (uint8_t)(p->v0.car.ksl_lvl | 0x3Fu));
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x60u + (uint16_t)car_off), p->v0.car.atk_dec);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x80u + (uint16_t)car_off), p->v0.car.sus_rel);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0xE0u + (uint16_t)car_off), p->v0.car.waveform);

    /* Feedback / connection. OR 0x30 enables both L+R outputs (OPL3). */
    OPL3_WriteRegBuffered(&g_chip,
                          (uint16_t)(0xC0u + (uint16_t)oplc),
                          (uint8_t)(fb | 0x30u));
}

/* Apply the real carrier output level after program_patch silenced
 * it. Mirrors chocolate-doom SetVoiceVolume: combines note velocity
 * and channel volume CC#7 into a 0..63 attenuation for the carrier
 * TL bits, preserving the patch's KSL bits in the upper two. */
static void set_voice_volume(int oplc, const genmidi_patch_t *p,
                             uint8_t channel_volume, uint8_t velocity)
{
    uint8_t car_off = OP_OFFSET[oplc][1];
    if (channel_volume > 127u) channel_volume = 127u;
    if (velocity > 127u)       velocity       = 127u;
    unsigned midi_volume = 2u * (unsigned)(s_vmt[channel_volume] + 1u);
    unsigned full_volume = ((unsigned)s_vmt[velocity] * midi_volume) >> 9u;
    unsigned car_vol     = 0x3Fu - full_volume;
    uint8_t  reg = (uint8_t)((p->v0.car.ksl_lvl & 0xC0u) | (car_vol & 0x3Fu));
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0x40u + (uint16_t)car_off), reg);
}

/* =========================================================================
 * Key-on / Key-off helpers
 * =========================================================================
 */
/* Take a packed (block << 10 | fnum) frequency from s_freq_curve and
 * write the OPL3 0xA0 / 0xB0 registers, asserting the key-on bit. */
static void key_on_freq(int oplc, uint32_t freq)
{
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0xA0u + (uint16_t)oplc),
                          (uint8_t)(freq & 0xFFu));
    /* High register: 0x20 = key-on, then bits 5..2 = block, bits 1..0 =
     * fnum upper. The packed freq already places block at >>10, which
     * is exactly the layout we need after the >>8 below. */
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0xB0u + (uint16_t)oplc),
                          (uint8_t)(((freq >> 8) & 0x1Fu) | 0x20u));
}

static void key_off(int oplc)
{
    /* Clear key-on bit, preserve block/fnum (write 0 — channel silenced) */
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(0xB0u + (uint16_t)oplc), (uint8_t)0x00u);
}

/* Frequency lookup matching chocolate-doom FrequencyForVoice. Returns
 * a packed value with FNUM in low 10 bits and OPL block in bits 10+
 * (so the high byte after >>8 already has block in bits 5..2 + fnum
 * upper in 1..0, ready for register 0xB0 OR'd with the key-on bit). */
static uint32_t freq_for_note(int note, int bend, int finetune_voice2)
{
    int freq_index;
    int sub_index;
    int octave;

    if (note < 0)  note = 0;
    if (note > 95) note = 95;
    while (note < 0)  note += 12;
    while (note > 95) note -= 12;

    freq_index = 64 + 32 * note + bend;
    if (finetune_voice2) {
        freq_index += (finetune_voice2 / 2) - 64;
    }
    if (freq_index < 0) freq_index = 0;

    if (freq_index < 284) return s_freq_curve[freq_index];

    sub_index = (freq_index - 284) % (12 * 32);
    octave    = (freq_index - 284) / (12 * 32);
    if (octave > 7) octave = 7;
    return (uint32_t)s_freq_curve[sub_index + 284]
         | ((uint32_t)octave << 10);
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
    uint8_t  prog;
    int      oplc;
    int      eff_note;
    int      bend;
    uint32_t freq;

    if (!s_patches_loaded) return;

    prog = s_midi[ch].program;
    if (prog >= (uint8_t)NUM_PATCHES) prog = 0u;

    oplc = alloc_opl_ch(ch, note);
    program_patch(oplc, &s_patches[prog]);

    /* Apply per-instrument note offset (signed). For percussion in DOOM
     * the GENMIDI flags include a fixed-note bit; that's not handled
     * here yet, so percussion may sound wrong. */
    eff_note = (int)note + (int)s_patches[prog].base_note_offset;

    /* Pitch-bend: 14-bit unsigned with 0x2000 = neutral. The frequency
     * table indexes in 1/32-semitone units, so divide bend delta by
     * 64 to get a ±~2-semitone bend at full deflection. */
    bend = (int)s_midi[ch].pitch_bend - 0x2000;
    bend /= 64;

    freq = freq_for_note(eff_note, bend, (int)s_patches[prog].finetune);

    /* Apply velocity-scaled carrier level then key-on. Order matters:
     * the level register write must precede key-on so the envelope
     * starts at the right amplitude rather than ramping from full
     * attenuation. */
    set_voice_volume(oplc, &s_patches[prog], s_midi[ch].volume, vel);
    key_on_freq(oplc, freq);
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
