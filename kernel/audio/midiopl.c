/* midiopl.c - MIDI -> OPL3 synthesiser
 *
 * Parses a DOOM-format GENMIDI lump (175 patches x 36 bytes), maintains
 * a MIDI running-status parser, allocates 9 OPL3 melodic channels via
 * round-robin / LRU steal, and renders s16 stereo @ 22050 Hz.
 *
 * Internal OPL3 rate: 49716 Hz (Nuked-OPL3 native).  midiopl_render
 * generates at 49716 then linear-resamples to 22050.
 *
 * Not wired into the audio chain yet - Task 17 does that.
*/

#include "midiopl.h"
#include "nuked_opl3.h"
#include "serial.h"
#include "string.h"

/* GENMIDI lump structures (DMX format)
 *
 * Header:  8 bytes - "#OPL_II#"
 * 175 patches x 36 bytes (128 melodic + 47 percussion)
 * 175 x 32 bytes patch names (after the patch data)
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
 *   [4] ksl_lvl     (KSL/TL - total level)
 *   [5] ksr_eg_vib_am (flags byte)
 *   [6] feedback_conn (for the voice, after both ops)
*/

/* OPL operator. Field names match chocolate-doom's genmidi_op_t - these
 * are the *bytes* read from the GENMIDI lump, not the OPL3 register
 * names. tremolo->$20, attack->$60, sustain->$80, waveform->$E0; scale and
 * level together form $40 (KSL=upper 2 bits, TL=lower 6 bits).*/
typedef struct {
    uint8_t tremolo;       /* $20: AM/VIB/EG/KSR/MULT */
    uint8_t attack;        /* $60: AR/DR */
    uint8_t sustain;       /* $80: SL/RR */
    uint8_t waveform;      /* $E0: WS */
    uint8_t scale;         /* $40 upper bits (KSL) */
    uint8_t level;         /* $40 lower bits (TL) - patch loudness */
} opl_op_t;

/* GENMIDI voice (16 bytes per chocolate-doom genmidi_voice_t). One
 * patch carries two voices; if the patch flags include GM_FLAG_2VOICE
 * the synth layers both.*/
typedef struct {
    opl_op_t mod;
    uint8_t  feedback;        /* $C0 lower 4 bits (FB|connection) */
    opl_op_t car;
    uint8_t  unused;
    int16_t  base_note_offset; /* signed LE16, per-voice tuning offset */
} opl_voice_t;

typedef struct {
    uint16_t    flags;
    int8_t      finetune;          /* signed; voice 1 detune amount */
    uint8_t     fixed_note;        /* used for percussion / GM_FLAG_FIXED */
    opl_voice_t voices[2];
} genmidi_patch_t;

#define NUM_PATCHES 175
static genmidi_patch_t s_patches[NUM_PATCHES];
static int             s_patches_loaded = 0;

/* Single global OPL3 chip state.
 * opl3_chip is large (~32KB); lives in BSS (zero-init, no flash cost).
*/
static opl3_chip g_chip;

/* OPL3 18-voice allocator. Bank 0 (registers 0x000..0x0FF) holds voices
 * 0..8; bank 1 (registers 0x100..0x1FF) holds voices 9..17.
 * voice_index is 0 for the primary (voice0) layer and 1 for the
 * secondary (voice1) layer of GM_FLAG_2VOICE patches; both share the
 * same midi_ch + midi_note so note-off frees them together.
*/
#define NUM_OPL_CH 18

typedef struct {
    int      in_use;
    uint8_t  midi_ch;
    uint8_t  midi_note;
    uint8_t  voice_index;   /* 0 = primary, 1 = secondary (2-voice patches) */
    uint8_t  prog;          /* patch number used (for level updates) */
    uint8_t  velocity;      /* note-on velocity (0..127) - kept so that
                             * I_SetMusicVolume can rescale live voices*/
    uint32_t age;           /* monotonic counter - smaller = older = steal first */
} opl_ch_state_t;

static opl_ch_state_t s_opl_ch[NUM_OPL_CH];
static uint32_t       s_age = 0u;

/* MIDI channel state (16 channels)
*/
typedef struct {
    uint8_t  program;
    uint8_t  volume;       /* CC 7  - channel volume 0..127 */
    uint8_t  pan;          /* CC 10 - pan 0..127 (64=centre) */
    uint8_t  sustain;      /* CC 64 - 0..63 off, 64..127 on (release deferred) */
    uint16_t pitch_bend;   /* 0..16383 (0x2000=centre) */
} midi_ch_state_t;

static midi_ch_state_t s_midi[16];

/* Master volume (0..127) applied at render time.
*/
static uint8_t s_master_vol = 100u;

/* MIDI running-status parser state
*/
static uint8_t s_running_status = 0u;
static uint8_t s_msg_buf[3];
static uint8_t s_msg_have = 0u;
static uint8_t s_msg_need = 0u;

/* OPL3 operator slot offsets (register base) for channels 0..8.
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
*/
static const uint8_t OP_OFFSET[9][2] = {
    {0x00u, 0x03u}, {0x01u, 0x04u}, {0x02u, 0x05u},
    {0x08u, 0x0Bu}, {0x09u, 0x0Cu}, {0x0Au, 0x0Du},
    {0x10u, 0x13u}, {0x11u, 0x14u}, {0x12u, 0x15u}
};

/* Voice -> register-bank helpers.
 * Voices 0..8 live in OPL3 bank 0 (registers 0x000..0x0FF); voices 9..17
 * live in bank 1 (registers 0x100..0x1FF). The operator-offset table
 * above is the same for both banks; only the register-address high bit
 * changes.*/
static uint16_t voice_bank(int v)    { return (v >= 9) ? (uint16_t)0x100u : (uint16_t)0x000u; }
static uint8_t  voice_ch(int v)      { return (uint8_t)(v % 9); }
static uint8_t  voice_op_mod(int v)  { return OP_OFFSET[voice_ch(v)][0]; }
static uint8_t  voice_op_car(int v)  { return OP_OFFSET[voice_ch(v)][1]; }

/* Frequency table from chocolate-doom's i_oplmusic.c (GPL-2).
 *
 * Indexed by `freq_index = 64 + 32 * note + pitch_bend`. The first
 * 284 entries are used directly; beyond 284 the table loops in 384-
 * entry chunks (one per octave) and the octave is OR'd into bit 10+
 * of the result (block field for OPL3 register 0xB0). This gives a
 * smooth pitch-bend-aware mapping from MIDI note to OPL FNUM/block,
 * which the previous 12-entry chromatic table couldn't do - and is
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

/* Forward declarations (strict -Wmissing-prototypes compliance)
*/
static int  midiopl_load_genmidi(const uint8_t *lump, uint32_t len);
static void midiopl_handle_event(const uint8_t *msg, uint8_t msglen);
static void note_on(uint8_t ch, uint8_t note, uint8_t vel);
static void note_off(uint8_t ch, uint8_t note);
static void program_change(uint8_t ch, uint8_t prog);
static void controller(uint8_t ch, uint8_t cc, uint8_t v);
static void pitch_bend_change(uint8_t ch, uint16_t bend);
static int  alloc_opl_voice(uint8_t midi_ch, uint8_t note, uint8_t voice_idx);
static void free_opl_ch(int v);
static void program_voice(int oplv, const opl_voice_t *vc);
static void set_voice_volume(int oplv, const opl_voice_t *vc,
                             uint8_t channel_volume, uint8_t velocity);
static void set_voice_pan(int oplv, const opl_voice_t *vc, uint8_t pan);
static void key_on_freq(int oplv, uint32_t freq);
static void key_off(int oplv);
static uint32_t freq_for_note(int note, int bend, int finetune);

/* Public API
*/

int midiopl_init(const uint8_t *genmidi_lump, uint32_t lump_len)
{
    int i;

    /* Reset the chip with our final output rate (22050 Hz). Nuked-OPL3
     * runs its DSP at 49716 Hz internally and uses a continuous linear
     * resampler (`rateratio` = samplerate/49716, applied per output
     * sample with phase carried across calls) to deliver samples at
     * the requested rate. The previous code reset at native 49716 and
     * then linear-resampled in midiopl_render - same interpolation
     * but with a phase reset at every buffer boundary, which produced
     * audible buzz at the music-pull cadence.*/
    OPL3_Reset(&g_chip, 22050u);
    /* Enable OPL3 mode (new-register access + bank 1 + CHA/CHB stereo) */
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)0x105u, (uint8_t)0x01u);

    for (i = 0; i < NUM_OPL_CH; i++) {
        s_opl_ch[i].in_use      = 0;
        s_opl_ch[i].midi_ch     = 0u;
        s_opl_ch[i].midi_note   = 0u;
        s_opl_ch[i].voice_index = 0u;
        s_opl_ch[i].prog        = 0u;
        s_opl_ch[i].velocity    = 0u;
        s_opl_ch[i].age         = 0u;
    }
    for (i = 0; i < 16; i++) {
        s_midi[i].program    = 0u;
        s_midi[i].volume     = 100u;
        s_midi[i].pan        = 64u;
        s_midi[i].sustain    = 0u;
        s_midi[i].pitch_bend = 0x2000u;
    }
    s_running_status = 0u;
    s_msg_have       = 0u;
    s_msg_need       = 0u;
    s_age            = 0u;

    if (midiopl_load_genmidi(genmidi_lump, lump_len) != 0) {
        serial_write_string("[midiopl] WARN: GENMIDI parse failed; using defaults\n");
        /* Not fatal - synth still works, just no patch data */
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
    int i;
    s_master_vol = (vol_0_127 > 127u) ? 127u : vol_0_127;

    /* Re-level any currently sounding voices so the change takes
     * effect immediately rather than only on the next note-on.
     * Mirrors chocolate-doom's SetMusicVolume.*/
    if (!s_patches_loaded) return;
    for (i = 0; i < NUM_OPL_CH; i++) {
        const opl_ch_state_t *cs = &s_opl_ch[i];
        const genmidi_patch_t *p;
        const opl_voice_t *vc;
        if (!cs->in_use) continue;
        if (cs->prog >= NUM_PATCHES) continue;
        p  = &s_patches[cs->prog];
        vc = &p->voices[cs->voice_index & 1u];
        set_voice_volume(i, vc, s_midi[cs->midi_ch].volume, cs->velocity);
    }
}

/* GENMIDI lump parser
 *
 * Layout verified against DOOM source (w_wad.c / sounds.c):
 *   [0..7]   "#OPL_II#"
 *   [8..]    175 x 36-byte patch records
 *   [8+6300..] 175 x 32-byte ASCII names (ignored here)
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
        int v;

        s_patches[i].flags       = (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
        s_patches[i].finetune    = (int8_t)p[2];
        s_patches[i].fixed_note  = p[3];

        /* DOOM GENMIDI voice layout (16 bytes), repeated for voice 0
         * (offset 4..19) and voice 1 (offset 20..35):
         *   [0]  modulator tremolo  -> OPL3 reg $20
         *   [1]  modulator attack   -> OPL3 reg $60
         *   [2]  modulator sustain  -> OPL3 reg $80
         *   [3]  modulator waveform -> OPL3 reg $E0
         *   [4]  modulator scale    -> OPL3 reg $40 (KSL bits 6..7)
         *   [5]  modulator level    -> OPL3 reg $40 (TL bits 0..5)
         *   [6]  feedback/connection -> OPL3 reg $C0 lower 4 bits
         *   [7..12] carrier op (same six fields)
         *   [13] unused
         *   [14..15] base_note_offset (LE16 signed)*/
        for (v = 0; v < 2; v++) {
            const uint8_t *vp = p + 4u + (uint32_t)(v * 16);
            s_patches[i].voices[v].mod.tremolo   = vp[0];
            s_patches[i].voices[v].mod.attack    = vp[1];
            s_patches[i].voices[v].mod.sustain   = vp[2];
            s_patches[i].voices[v].mod.waveform  = vp[3];
            s_patches[i].voices[v].mod.scale     = vp[4];
            s_patches[i].voices[v].mod.level     = vp[5];
            s_patches[i].voices[v].feedback      = vp[6];
            s_patches[i].voices[v].car.tremolo   = vp[7];
            s_patches[i].voices[v].car.attack    = vp[8];
            s_patches[i].voices[v].car.sustain   = vp[9];
            s_patches[i].voices[v].car.waveform  = vp[10];
            s_patches[i].voices[v].car.scale     = vp[11];
            s_patches[i].voices[v].car.level     = vp[12];
            s_patches[i].voices[v].unused        = vp[13];
            s_patches[i].voices[v].base_note_offset =
                (int16_t)((uint16_t)vp[14] | (uint16_t)((uint16_t)vp[15] << 8));
        }

        p += 36u;
    }

    s_patches_loaded = 1;
    return 0;
}

/* MIDI byte-stream parser
 *
 * Handles:
 *  0xF8..0xFF (realtime) - skipped inline
 *  0xFF       (meta)     - VLQ-length body skipped
 *  0xF0/0xF7  (sysex)    - body skipped up to 0xF7
 *  0x80..0xEF (channel)  - running-status buffered, dispatched
 *  data bytes            - accumulated into s_msg_buf[]
*/
void midiopl_feed(const uint8_t *bytes, uint32_t len)
{
    uint32_t i;

    for (i = 0u; i < len; i++) {
        uint8_t b = bytes[i];

        if (b & 0x80u) {
            /* status byte */

            /* Realtime messages (0xF8..0xFF): single-byte, no state change */
            if (b >= 0xF8u) {
                /* 0xFF is also realtime in a raw MIDI stream.
                 * In a MIDI *file* SMF stream 0xFF is meta - but midiopl_feed
                 * processes raw MIDI output from mus2midi which does not embed
                 * SMF meta events in the live byte stream. Skip.*/
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
            /* data byte */

            /* Running status: re-prime from saved status if buffer empty */
            if (s_msg_have == 0u) {
                if (s_running_status == 0u) {
                    /* No running status - discard orphan data byte */
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

/* Event dispatcher
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

/* OPL3 patch programming
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
*/
/* GENMIDI flag bits (LE16, taken from chocolate-doom's i_oplmusic.c). */
#define GM_FLAG_FIXED   0x0001u   /* fixed-pitch patch (drums, sfx) */
#define GM_FLAG_2VOICE  0x0004u   /* layer voice0 + voice1 */

/* MIDI 0..127 -> DOOM volume_mapping_table (chocolate-doom). Used for
 * scaling carrier output level by note velocity * channel volume.*/
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

/* Program one OPL3 voice from a GENMIDI voice descriptor. Modulator
 * gets its real level if the voice is in FM (modulating) mode; in
 * additive mode the modulator is silenced so only the carrier is
 * audible. Carrier level is silenced here and applied later in
 * set_voice_volume after velocity + channel volume scaling. Matches
 * chocolate-doom's LoadOperatorData / LoadVoiceData.*/
static void program_voice(int oplv, const opl_voice_t *vc)
{
    uint16_t bank    = voice_bank(oplv);
    uint8_t  mod_off = voice_op_mod(oplv);
    uint8_t  car_off = voice_op_car(oplv);
    uint8_t  ch      = voice_ch(oplv);
    uint8_t  fb      = vc->feedback;
    int      modulating = ((fb & 0x01u) == 0u);
    uint8_t  mod_lvl;

    mod_lvl = (uint8_t)((vc->mod.scale & 0xC0u)
              | ((modulating ? vc->mod.level : 0x3Fu) & 0x3Fu));

    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0x20u + (uint16_t)mod_off)), vc->mod.tremolo);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0x40u + (uint16_t)mod_off)), mod_lvl);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0x60u + (uint16_t)mod_off)), vc->mod.attack);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0x80u + (uint16_t)mod_off)), vc->mod.sustain);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0xE0u + (uint16_t)mod_off)), vc->mod.waveform);

    /* Carrier - silence first; set_voice_volume applies velocity-scaled
     * volume after.*/
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0x20u + (uint16_t)car_off)), vc->car.tremolo);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0x40u + (uint16_t)car_off)),
                          (uint8_t)((vc->car.scale & 0xC0u) | 0x3Fu));
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0x60u + (uint16_t)car_off)), vc->car.attack);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0x80u + (uint16_t)car_off)), vc->car.sustain);
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0xE0u + (uint16_t)car_off)), vc->car.waveform);

    /* Feedback / connection. OR 0x30 = both L+R output enable (OPL3
     * stereo). set_voice_pan rewrites this register if the MIDI
     * channel pan CC has biased the note off-centre.*/
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0xC0u + (uint16_t)ch)),
                          (uint8_t)((fb & 0x0Fu) | 0x30u));
}

/* Apply velocity + channel-volume + master-volume scaled carrier
 * level. Mirrors chocolate-doom's SetVoiceVolume formula:
 *
 *   op_vol     = 0x3F - patch_carrier_level                       (0..63)
 *   full_vol   = vmt[vel] * vmt[ch_vol] * vmt[music_vol] / 127^2  (0..127)
 *   reg_vol    = op_vol * full_vol / 128                          (scaled)
 *   carrier TL = (0x3F - reg_vol) | (KSL bits)                    (final)
 *
 * Including music_vol in the formula pre-attenuates each voice so the
 * sum of 18 OPL3 voices doesn't saturate the chip's s16 mixbuffer.
 * Saturation inside the chip is what made the previous pass sound
 * harsh and "dirty" - once we hit ±32767 internally the clipping is
 * irreversible no matter how we scale at render time.
*/
static void set_voice_volume(int oplv, const opl_voice_t *vc,
                             uint8_t channel_volume, uint8_t velocity)
{
    uint16_t bank    = voice_bank(oplv);
    uint8_t  car_off = voice_op_car(oplv);
    unsigned op_vol;
    unsigned full_vol;
    unsigned reg_vol;
    uint8_t  reg;

    if (channel_volume > 127u) channel_volume = 127u;
    if (velocity > 127u)       velocity       = 127u;

    op_vol   = (unsigned)(0x3Fu - (vc->car.level & 0x3Fu));
    full_vol = ((unsigned)s_vmt[velocity]
              * (unsigned)s_vmt[channel_volume]
              * (unsigned)s_vmt[s_master_vol]) / (127u * 127u);
    if (full_vol > 127u) full_vol = 127u;
    reg_vol  = (op_vol * full_vol) >> 7u;
    if (reg_vol > 0x3Fu) reg_vol = 0x3Fu;

    reg = (uint8_t)((vc->car.scale & 0xC0u) | ((0x3Fu - reg_vol) & 0x3Fu));
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0x40u + (uint16_t)car_off)), reg);
}

/* Apply pan: write the channel's $C0 register with CHA / CHB output
 * enable bits derived from the MIDI pan CC (0..127, 64=centre). The
 * lower nibble (feedback/connection) is preserved from the voice
 * patch. Hard-pan thresholds match chocolate-doom's three-zone pan.*/
static void set_voice_pan(int oplv, const opl_voice_t *vc, uint8_t pan)
{
    uint16_t bank = voice_bank(oplv);
    uint8_t  ch   = voice_ch(oplv);
    uint8_t  cha_chb;

    if (pan < 32u)       cha_chb = 0x10u;   /* hard left  (CHA only) */
    else if (pan > 96u)  cha_chb = 0x20u;   /* hard right (CHB only) */
    else                 cha_chb = 0x30u;   /* centred (both) */

    OPL3_WriteRegBuffered(&g_chip,
                          (uint16_t)(bank | (0xC0u + (uint16_t)ch)),
                          (uint8_t)((vc->feedback & 0x0Fu) | cha_chb));
}

/* Key-on / Key-off helpers - bank-aware (voices 0..17)
*/
static void key_on_freq(int oplv, uint32_t freq)
{
    uint16_t bank = voice_bank(oplv);
    uint8_t  ch   = voice_ch(oplv);

    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0xA0u + (uint16_t)ch)),
                          (uint8_t)(freq & 0xFFu));
    /* $B0: bit 5 = key-on, bits 4..2 = block, bits 1..0 = FNUM upper. */
    OPL3_WriteRegBuffered(&g_chip, (uint16_t)(bank | (0xB0u + (uint16_t)ch)),
                          (uint8_t)(((freq >> 8) & 0x1Fu) | 0x20u));
}

static void key_off(int oplv)
{
    uint16_t bank = voice_bank(oplv);
    uint8_t  ch   = voice_ch(oplv);

    OPL3_WriteRegBuffered(&g_chip,
                          (uint16_t)(bank | (0xB0u + (uint16_t)ch)),
                          (uint8_t)0x00u);
}

/* Frequency lookup matching chocolate-doom FrequencyForVoice. Returns
 * a packed value with FNUM in low 10 bits and OPL block in bits 10+
 * (so the high byte after >>8 already has block in bits 5..2 + fnum
 * upper in 1..0, ready for register 0xB0 OR'd with the key-on bit).*/
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

/* OPL3 voice allocator (18 voices: bank0 0..8, bank1 9..17)
 *
 * Returns a voice index 0..17 to use for the new note. If all voices
 * are busy, steals the LRU one (oldest age). voice_index encodes
 * whether this is the primary (0) or secondary (1) layer of a
 * 2-voice patch - both share the same midi_ch + midi_note so that
 * note-off can release them as a unit.
*/
static int alloc_opl_voice(uint8_t midi_ch, uint8_t note, uint8_t voice_idx)
{
    int i;
    int oldest;

    for (i = 0; i < NUM_OPL_CH; i++) {
        if (!s_opl_ch[i].in_use) {
            s_opl_ch[i].in_use      = 1;
            s_opl_ch[i].midi_ch     = midi_ch;
            s_opl_ch[i].midi_note   = note;
            s_opl_ch[i].voice_index = voice_idx;
            s_opl_ch[i].age         = ++s_age;
            return i;
        }
    }

    oldest = 0;
    for (i = 1; i < NUM_OPL_CH; i++) {
        if (s_opl_ch[i].age < s_opl_ch[oldest].age) {
            oldest = i;
        }
    }
    free_opl_ch(oldest);
    s_opl_ch[oldest].in_use      = 1;
    s_opl_ch[oldest].midi_ch     = midi_ch;
    s_opl_ch[oldest].midi_note   = note;
    s_opl_ch[oldest].voice_index = voice_idx;
    s_opl_ch[oldest].age         = ++s_age;
    return oldest;
}

static void free_opl_ch(int v)
{
    key_off(v);
    s_opl_ch[v].in_use = 0;
}

/* MIDI event handlers
*/
/* Allocate, program, freq + level + pan, key-on a single OPL voice
 * for one layer of a note. Used twice when a patch has GM_FLAG_2VOICE.*/
static void start_one_layer(const genmidi_patch_t *p, int prog,
                            int patch_voice_idx,
                            uint8_t ch, uint8_t key, int base_note,
                            int bend, uint8_t vel)
{
    const opl_voice_t *vc = &p->voices[patch_voice_idx];
    int      oplv;
    int      eff_note;
    int      finetune;
    uint32_t freq;
    uint8_t  pan;

    oplv = alloc_opl_voice(ch, key, (uint8_t)patch_voice_idx);
    s_opl_ch[oplv].prog     = (uint8_t)prog;
    s_opl_ch[oplv].velocity = vel;
    program_voice(oplv, vc);

    eff_note = base_note + (int)vc->base_note_offset;

    /* Voice 1 of a 2-voice patch is detuned by the patch's `finetune`
     * byte (signed offset around 0x80 = neutral) so it produces a
     * subtle chorus when layered with voice 0. Voice 0 itself is
     * never detuned.*/
    finetune = (patch_voice_idx == 1) ? (int)p->finetune : 0;

    freq = freq_for_note(eff_note, bend, finetune);

    /* Order: level + pan before key-on so the envelope starts at the
     * right amplitude / output enable, not ramping from silence.*/
    set_voice_volume(oplv, vc, s_midi[ch].volume, vel);
    pan = (ch == 9u) ? 64u : s_midi[ch].pan;
    set_voice_pan(oplv, vc, pan);
    key_on_freq(oplv, freq);
}

static void note_on(uint8_t ch, uint8_t note, uint8_t vel)
{
    int   prog;
    int   base_note;
    int   bend;
    const genmidi_patch_t *p;

    if (!s_patches_loaded) return;

    /* Channel 9 = GM percussion. Note number selects which drum, not
     * the pitch. GENMIDI ships 47 drum patches at indices 128..174;
     * drum keys are 35..81 (kick=35). Out-of-range keys ignored,
     * matching chocolate-doom.*/
    if (ch == 9u) {
        if (note < 35u || note > 81u) return;
        prog = 128 + ((int)note - 35);
    } else {
        prog = (int)s_midi[ch].program;
        if (prog < 0 || prog >= NUM_PATCHES) prog = 0;
    }
    p = &s_patches[prog];

    /* Pick the *played* note. Percussion and any patch flagged FIXED
     * use the patch's hard-coded fixed_note (e.g. a snare always plays
     * at one specific frequency, independent of the MIDI key that
     * triggered it). All other notes follow the MIDI key.*/
    if (ch == 9u || (p->flags & GM_FLAG_FIXED)) {
        base_note = (int)p->fixed_note;
    } else {
        base_note = (int)note;
    }

    /* Pitch-bend: 14-bit unsigned, 0x2000 = neutral. Frequency table
     * indexes 1/32-semitone steps, so divide bend delta by 64 for
     * ±2-semitone bend at full deflection. Percussion ignores pitch
     * bend (drums are always fixed pitch).*/
    if (ch == 9u) {
        bend = 0;
    } else {
        bend = (int)s_midi[ch].pitch_bend - 0x2000;
        bend /= 64;
    }

    /* Voice 0 - always present. */
    start_one_layer(p, prog, 0, ch, note, base_note, bend, vel);

    /* Voice 1 - only if the patch is a 2-voice instrument (flag 0x04).
     * Adds chorus / fullness; ~30% of the vanilla GENMIDI bank uses
     * this. The two layers share (midi_ch, midi_note) so note-off
     * releases them together.*/
    if (p->flags & GM_FLAG_2VOICE) {
        start_one_layer(p, prog, 1, ch, note, base_note, bend, vel);
    }
}

static void note_off(uint8_t ch, uint8_t note)
{
    int i;

    /* Sustain pedal held: defer the actual key-off until the pedal is
     * released. We mark the voice ended-but-sustained by setting age
     * to 0 so it's the first stolen if all voices fill up. Channels
     * with sustain off release immediately.*/
    if (s_midi[ch].sustain >= 64u) {
        for (i = 0; i < NUM_OPL_CH; i++) {
            if (s_opl_ch[i].in_use &&
                s_opl_ch[i].midi_ch   == ch &&
                s_opl_ch[i].midi_note == note) {
                s_opl_ch[i].age = 0u;     /* eligible for first-steal */
            }
        }
        return;
    }

    /* Free *every* voice matching (ch, note). 2-voice patches occupy
     * two OPL voices per note - the previous early-`return` after the
     * first match left the second layer ringing forever.*/
    for (i = 0; i < NUM_OPL_CH; i++) {
        if (s_opl_ch[i].in_use &&
            s_opl_ch[i].midi_ch   == ch &&
            s_opl_ch[i].midi_note == note) {
            free_opl_ch(i);
        }
    }
}

static void program_change(uint8_t ch, uint8_t prog)
{
    s_midi[ch].program = prog;
}

/* Release every voice on `ch` that is in the sustain-deferred state
 * (marked with age=0 by note_off while the pedal was held).*/
static void release_sustained(uint8_t ch)
{
    int i;
    for (i = 0; i < NUM_OPL_CH; i++) {
        if (s_opl_ch[i].in_use &&
            s_opl_ch[i].midi_ch == ch &&
            s_opl_ch[i].age     == 0u) {
            free_opl_ch(i);
        }
    }
}

static void controller(uint8_t ch, uint8_t cc, uint8_t v)
{
    int i;

    switch (cc) {
    case 7u:    /* Channel volume MSB */
        s_midi[ch].volume = v;
        break;
    case 10u:   /* Pan */
        s_midi[ch].pan = v;
        break;
    case 64u:   /* Sustain pedal */
        s_midi[ch].sustain = v;
        if (v < 64u) { release_sustained(ch); }
        break;
    case 120u:  /* All sound off - kill all voices on this channel now */
    case 123u:  /* All notes off */
        for (i = 0; i < NUM_OPL_CH; i++) {
            if (s_opl_ch[i].in_use && s_opl_ch[i].midi_ch == ch) {
                free_opl_ch(i);
            }
        }
        break;
    case 121u:  /* Reset all controllers */
        s_midi[ch].volume     = 100u;
        s_midi[ch].pan        = 64u;
        s_midi[ch].sustain    = 0u;
        s_midi[ch].pitch_bend = 0x2000u;
        break;
    default:
        break;
    }
}

static void pitch_bend_change(uint8_t ch, uint16_t bend)
{
    s_midi[ch].pitch_bend = bend;
    /* TODO: re-key ringing notes on this channel with detuned fnum.
     * Most DOOM songs only use bend at note-on, so this is rarely
     * audible in practice.*/
}

/* midiopl_render - pull stereo s16 @ 22050 Hz
 *
 * Nuked-OPL3 runs its DSP at 49716 Hz and resamples to whatever rate
 * was passed to OPL3_Reset (we use 22050). OPL3_GenerateStream emits
 * stereo s16 at that target rate with phase carried between calls.
 * Master volume is applied per-voice via set_voice_volume so the
 * chip's internal mixbuffer never saturates - at this point we just
 * stream the resampled output straight to the caller.
*/
void midiopl_render(int16_t *out_stereo, uint32_t frames)
{
    OPL3_GenerateStream(&g_chip, out_stereo, frames);
}

