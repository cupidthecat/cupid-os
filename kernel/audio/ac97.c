#include "ac97.h"
#include "../pci.h"
#include "../ports.h"
#include "../irq.h"
#include "../memory.h"
#include "../kernel.h"
#include "../../drivers/serial.h"

/* Known AC97 PCI device IDs (Intel ICH family) */
static const uint16_t AC97_VENDOR = 0x8086u;
static const uint16_t AC97_DEVICES[] = {
    0x2415u, 0x2425u, 0x2445u, 0x2485u, 0x24C5u, 0x24D5u, 0x266Eu
};
#define AC97_DEVICE_COUNT (sizeof(AC97_DEVICES) / sizeof(AC97_DEVICES[0]))

/* NAM (mixer) registers */
#define NAM_RESET           0x00u
#define NAM_MASTER_VOL      0x02u
#define NAM_PCM_OUT_VOL     0x18u
#define NAM_EXT_AUDIO_ID    0x28u
#define NAM_EXT_AUDIO_CTRL  0x2Au
#define NAM_PCM_FRONT_RATE  0x2Cu

/* NABM PCM-out (PO) — offset from bar_nabm */
#define NABM_PO_BDBAR       0x10u  /* dword */
#define NABM_PO_CIV         0x14u  /* byte  — current index value */
#define NABM_PO_LVI         0x15u  /* byte  — last valid index */
#define NABM_PO_SR          0x16u  /* word  — status */
#define NABM_PO_PICB        0x18u  /* word  — position in current buffer */
#define NABM_PO_CR          0x1Bu  /* byte  — control */

#define NABM_GLOB_CNT       0x2Cu
#define NABM_GLOB_STA       0x30u

/* PO_SR bits */
#define POSR_BCIS  (1u << 3)
#define POSR_LVBCI (1u << 2)
#define POSR_FIFOE (1u << 4)
/* PO_CR bits */
#define POCR_RPBM  (1u << 0)
#define POCR_RR    (1u << 1)
#define POCR_LVBIE (1u << 2)
#define POCR_FEIE  (1u << 3)
#define POCR_IOCE  (1u << 4)

/* BDL configuration */
#define AC97_BDL_ENTRIES        32
#define AC97_FRAMES_PER_BUF     1024u
#define AC97_BYTES_PER_BUF      (AC97_FRAMES_PER_BUF * 2u * 2u)  /* stereo s16 */

typedef struct __attribute__((packed)) {
    uint32_t buf_phys;
    uint16_t samples;   /* s16 count == frames * 2 */
    uint16_t flags;     /* bit 14 = IOC, bit 15 = BUP */
} bdl_entry_t;

static bdl_entry_t *s_bdl;
static int16_t     *s_dma_pool;

static struct {
    bool         present;
    pci_device_t *pci;
    uint16_t     bar_nam;
    uint16_t     bar_nabm;
    uint8_t      irq_line;
    void       (*fill)(int16_t *, uint32_t);
} s_ac97;

/* Static helper: print a 16-bit value as "0xABCD" to serial */
static void hex16(uint16_t v) {
    static const char H[] = "0123456789ABCDEF";
    char buf[7];
    buf[0] = '0'; buf[1] = 'x';
    buf[2] = H[(v >> 12) & 0xFu];
    buf[3] = H[(v >>  8) & 0xFu];
    buf[4] = H[(v >>  4) & 0xFu];
    buf[5] = H[ v        & 0xFu];
    buf[6] = '\0';
    serial_write_string(buf);
}

bool ac97_is_present(void) { return s_ac97.present; }

int  ac97_is_present_int(void) { return s_ac97.present ? 1 : 0; }

void ac97_set_fill_callback(void (*fill)(int16_t *, uint32_t)) {
    s_ac97.fill = fill;
}

/* AC97 IRQ handler — refills the buffer that just completed */
static void ac97_isr(struct registers *r) {
    (void)r;
    uint16_t sr = inw((uint16_t)(s_ac97.bar_nabm + NABM_PO_SR));
    if (sr & (uint16_t)POSR_BCIS) {
        uint8_t civ  = inb((uint16_t)(s_ac97.bar_nabm + NABM_PO_CIV));
        uint8_t done = (uint8_t)((civ - 1u) & (uint8_t)(AC97_BDL_ENTRIES - 1u));
        if (s_ac97.fill) {
            int16_t *buf = &s_dma_pool[(uint32_t)done * AC97_FRAMES_PER_BUF * 2u];
            s_ac97.fill(buf, AC97_FRAMES_PER_BUF);
        }
        outb((uint16_t)(s_ac97.bar_nabm + NABM_PO_LVI), done);
    }
    /* ack all status bits */
    outw((uint16_t)(s_ac97.bar_nabm + NABM_PO_SR), sr);
}

int ac97_init(void) {
    pci_device_t *dev = NULL;
    uint32_t i;
    for (i = 0; i < AC97_DEVICE_COUNT; i++) {
        dev = pci_find_by_vendor_device(AC97_VENDOR, AC97_DEVICES[i]);
        if (dev) { break; }
    }
    if (!dev) {
        serial_write_string("[ac97] no AC97 device found\n");
        return -1;
    }

    uint32_t bar0 = pci_config_read_dword(dev->bus, dev->device, dev->function, 0x10u);
    uint32_t bar1 = pci_config_read_dword(dev->bus, dev->device, dev->function, 0x14u);
    s_ac97.bar_nam  = (uint16_t)(bar0 & 0xFFFCu);
    s_ac97.bar_nabm = (uint16_t)(bar1 & 0xFFFCu);
    s_ac97.irq_line = pci_config_read_byte(dev->bus, dev->device, dev->function, 0x3Cu);
    s_ac97.pci      = dev;
    s_ac97.present  = true;

    pci_enable_bus_master(dev);

    /* Allocate BDL + DMA pool */
    s_bdl      = (bdl_entry_t *)kmalloc(sizeof(bdl_entry_t) * AC97_BDL_ENTRIES);
    s_dma_pool = (int16_t *)kmalloc(AC97_BYTES_PER_BUF * AC97_BDL_ENTRIES);
    if (!s_bdl || !s_dma_pool) {
        serial_write_string("[ac97] OOM allocating BDL/DMA pool\n");
        return -1;
    }
    /* Zero the DMA pool (silence) */
    {
        uint32_t total_samples = (AC97_BYTES_PER_BUF * AC97_BDL_ENTRIES) / 2u;
        for (uint32_t j = 0; j < total_samples; j++) {
            s_dma_pool[j] = 0;
        }
    }
    /* Populate BDL entries */
    for (int j = 0; j < AC97_BDL_ENTRIES; j++) {
        s_bdl[j].buf_phys = (uint32_t)&s_dma_pool[(uint32_t)j * AC97_FRAMES_PER_BUF * 2u];
        s_bdl[j].samples  = (uint16_t)(AC97_FRAMES_PER_BUF * 2u);
        s_bdl[j].flags    = (uint16_t)(1u << 14);  /* IOC */
    }

    /* Cold reset codec via NABM GLOB_CNT bit 1 */
    outl((uint16_t)(s_ac97.bar_nabm + NABM_GLOB_CNT),
         inl((uint16_t)(s_ac97.bar_nabm + NABM_GLOB_CNT)) | (1u << 1));
    for (volatile int delay = 0; delay < 1000; delay++) { (void)delay; }

    /* NAM RESET */
    outw((uint16_t)(s_ac97.bar_nam + NAM_RESET), 0u);

    /* Master + PCM-out volume (unmute) */
    outw((uint16_t)(s_ac97.bar_nam + NAM_MASTER_VOL), 0x0000u);
    outw((uint16_t)(s_ac97.bar_nam + NAM_PCM_OUT_VOL), 0x0808u);

    /* Variable-rate audio @ 22050 Hz */
    uint16_t ext = inw((uint16_t)(s_ac97.bar_nam + NAM_EXT_AUDIO_CTRL));
    outw((uint16_t)(s_ac97.bar_nam + NAM_EXT_AUDIO_CTRL), (uint16_t)(ext | 0x1u));
    outw((uint16_t)(s_ac97.bar_nam + NAM_PCM_FRONT_RATE), 22050u);

    /* Reset PO channel */
    outb((uint16_t)(s_ac97.bar_nabm + NABM_PO_CR), (uint8_t)POCR_RR);
    while (inb((uint16_t)(s_ac97.bar_nabm + NABM_PO_CR)) & (uint8_t)POCR_RR) { /* spin */ }

    /* Program BDL */
    outl((uint16_t)(s_ac97.bar_nabm + NABM_PO_BDBAR), (uint32_t)s_bdl);
    outb((uint16_t)(s_ac97.bar_nabm + NABM_PO_LVI),
         (uint8_t)(AC97_BDL_ENTRIES - 1));

    /* Install IRQ handler */
    irq_install_handler((int)s_ac97.irq_line, ac97_isr);

    serial_write_string("[ac97] present: NAM=");
    hex16(s_ac97.bar_nam);
    serial_write_string(" NABM=");
    hex16(s_ac97.bar_nabm);
    serial_write_string("\n");

    return 0;
}

void ac97_start(void) {
    if (!s_ac97.present) { return; }
    outb((uint16_t)(s_ac97.bar_nabm + NABM_PO_CR),
         (uint8_t)(POCR_IOCE | POCR_LVBIE | POCR_FEIE | POCR_RPBM));
}

void ac97_stop(void) {
    if (!s_ac97.present) { return; }
    outb((uint16_t)(s_ac97.bar_nabm + NABM_PO_CR), 0u);
}

void ac97_set_master_volume(uint8_t pct) {
    if (!s_ac97.present) { return; }
    if (pct > 100u) { pct = 100u; }
    uint8_t att = (uint8_t)(((uint32_t)(100u - pct) * 0x3Fu) / 100u);
    uint16_t v  = (uint16_t)(((uint16_t)att << 8) | (uint16_t)att);
    outw((uint16_t)(s_ac97.bar_nam + NAM_MASTER_VOL), v);
}

/* ── Smoke-test kernel helpers ─────────────────────────────────────────── */

/* forward declaration — defined below */
static void ac97_tsc_sleep_ms(uint32_t ms);

/* Generate a triangle-wave mono PCM into a freshly kmalloc'd buffer.
 * Caller frees. Returns NULL on alloc failure. */
static int16_t *gen_triangle_mono(uint32_t hz, uint32_t ms, uint32_t *out_frames) {
    const uint32_t SR = 22050u;
    uint32_t frames = (SR * ms) / 1000u;
    int16_t *p = (int16_t *)kmalloc(frames * 2u);
    if (!p) { return (int16_t *)0; }
    int32_t inc = (int32_t)((65536u * hz) / SR);
    int32_t phase = 0;
    for (uint32_t i = 0; i < frames; i++) {
        int32_t t = phase & 0xFFFF;
        int32_t s = (t < 0x8000) ? (t - 0x4000) : (0xC000 - t);
        p[i] = (int16_t)(s * 2);
        phase += inc;
    }
    *out_frames = frames;
    return p;
}

void ac97_smoke_sweep(void) {
    if (!s_ac97.present) {
        serial_write_string("[SKIP] audiotest sweep: no AC97\n");
        return;
    }

    extern int  mixer_play(int, const int16_t *, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t);
    extern void mixer_stop(int);

    static const uint32_t F[8] = { 50u, 100u, 200u, 400u, 800u, 1600u, 3200u, 8000u };
    for (int i = 0; i < 8; i++) {
        uint32_t frames = 0u;
        int16_t *t = gen_triangle_mono(F[(uint32_t)i], 500u, &frames);
        if (!t) { continue; }
        mixer_play(9, t, frames, (uint8_t)1, (uint8_t)0, (uint8_t)100, (uint8_t)100);
        ac97_tsc_sleep_ms(500u);
        mixer_stop(9);
        kfree(t);
    }
    serial_write_string("[PASS] audiotest sweep\n");
}

void ac97_smoke_pan(void) {
    if (!s_ac97.present) {
        serial_write_string("[SKIP] audiotest pan: no AC97\n");
        return;
    }

    extern int  mixer_play(int, const int16_t *, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t);
    extern void mixer_stop(int);
    extern void mixer_set_volume(int, uint8_t, uint8_t);

    uint32_t frames = 0u;
    int16_t *t = gen_triangle_mono(1000u, 4000u, &frames);   /* 4s @ 1kHz */
    if (!t) {
        serial_write_string("[FAIL] audiotest pan: kmalloc\n");
        return;
    }
    mixer_play(9, t, frames, (uint8_t)1, (uint8_t)0, (uint8_t)100, (uint8_t)0);
    /* 8 ramp segments over 4 s — each segment is 500 ms = 10 × 50 ms steps */
    for (int seg = 0; seg < 8; seg++) {
        for (int j = 0; j <= 100; j += 10) {
            uint8_t l = (uint8_t)((seg & 1) ? j : 100 - j);
            uint8_t r = (uint8_t)((seg & 1) ? 100 - j : j);
            mixer_set_volume(9, l, r);
            ac97_tsc_sleep_ms(50u);
        }
    }
    mixer_stop(9);
    kfree(t);
    serial_write_string("[PASS] audiotest pan\n");
}



/* TSC-based busy-wait for ms milliseconds.
 * Uses RDTSC — advances regardless of IF/IRQ state.
 * Suitable for smoke tests where hlt/timer may be unreliable from JIT context. */
static void ac97_tsc_sleep_ms(uint32_t ms) {
    uint64_t freq = get_cpu_freq();          /* Hz */
    uint64_t cycles = (freq / 1000u) * (uint64_t)ms;
    uint32_t lo0, hi0, lo1, hi1;
    __asm__ volatile("rdtsc" : "=a"(lo0), "=d"(hi0));
    uint64_t t0 = ((uint64_t)hi0 << 32) | lo0;
    uint64_t elapsed;
    do {
        __asm__ volatile("rdtsc" : "=a"(lo1), "=d"(hi1));
        elapsed = (((uint64_t)hi1 << 32) | lo1) - t0;
        __asm__ volatile("pause");
    } while (elapsed < cycles);
}

/* Generate a 1-second 440 Hz triangle-wave PCM buffer (mono, 22050 frames) */
static int16_t *make_triangle_pcm(uint32_t *out_frames) {
    static const uint32_t RATE   = 22050u;
    static const uint32_t PERIOD = 50u;   /* 22050 / 440 ≈ 50 */
    static const int16_t  AMP    = 8000;

    uint32_t frames = RATE;   /* 1 second */
    int16_t *pcm = (int16_t *)kmalloc(frames * sizeof(int16_t));
    if (!pcm) { *out_frames = 0; return (int16_t *)0; }

    for (uint32_t i = 0; i < frames; i++) {
        uint32_t ph = i % PERIOD;
        int32_t val;
        if (ph < PERIOD / 2u) {
            val = ((int32_t)ph * (int32_t)AMP * 2) / (int32_t)(PERIOD / 2u) - (int32_t)AMP;
        } else {
            uint32_t half_off = ph - (PERIOD / 2u);
            val = (int32_t)AMP - ((int32_t)half_off * (int32_t)AMP * 2) / (int32_t)(PERIOD / 2u);
        }
        pcm[i] = (int16_t)val;
    }
    *out_frames = frames;
    return pcm;
}

int ac97_smoke_sine(void) {
    if (!s_ac97.present) {
        serial_write_string("[SKIP] audiotest sine: no AC97 device\n");
        return 0;
    }

    extern int  mixer_play(int, const int16_t *, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t);
    extern void mixer_stop(int);

    uint32_t frames = 0u;
    int16_t *pcm = make_triangle_pcm(&frames);
    if (!pcm) {
        serial_write_string("[FAIL] audiotest sine: OOM\n");
        return -1;
    }

    /* Play via mixer slot 9, looping, half-scale volume */
    mixer_play(9, pcm, frames, (uint8_t)1, (uint8_t)1, (uint8_t)100, (uint8_t)100);
    ac97_tsc_sleep_ms(2000u);
    mixer_stop(9);

    kfree(pcm);
    serial_write_string("[PASS] audiotest sine\n");
    return 0;
}
