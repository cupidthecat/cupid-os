#include "godspeak.h"

#include "string.h"
#include "timer.h"

extern const char _binary_god_Vocab_DD_start[];
extern const char _binary_god_Vocab_DD_end[];

#define GODSPEAK_MAX_WORDS 8192
#define GODSPEAK_MAX_WORD_LEN 96

static uint32_t gs_word_start[GODSPEAK_MAX_WORDS];
static uint16_t gs_word_len[GODSPEAK_MAX_WORDS];
static int gs_word_count = 0;
static bool gs_init_done = false;
static uint32_t gs_rng_state = 0xC0DEF00Du;

static uint32_t gs_xorshift32(void) {
  uint32_t x = gs_rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  if (x == 0) {
    x = 0xA5A5A5A5u;
  }
  gs_rng_state = x;
  return x;
}

static void gs_seed_rng(void) {
  uint32_t t = timer_get_uptime_ms();
  gs_rng_state ^= (t << 16) ^ (t >> 3) ^ 0x9E3779B9u;
  if (gs_rng_state == 0) {
    gs_rng_state = 0xC0DEF00Du;
  }
}

static void gs_scan_vocab(void) {
  const char *start = _binary_god_Vocab_DD_start;
  const char *end = _binary_god_Vocab_DD_end;
  uint32_t total = (uint32_t)(end - start);
  uint32_t i = 0;

  gs_word_count = 0;

  while (i < total && gs_word_count < GODSPEAK_MAX_WORDS) {
    while (i < total && (start[i] == '\n' || start[i] == '\r' || start[i] == ' ' ||
                         start[i] == '\t')) {
      i++;
    }
    if (i >= total) {
      break;
    }

    uint32_t s = i;
    while (i < total && start[i] != '\n' && start[i] != '\r') {
      i++;
    }

    if (i > s) {
      uint32_t len = i - s;
      if (len > (uint32_t)GODSPEAK_MAX_WORD_LEN) {
        len = (uint32_t)GODSPEAK_MAX_WORD_LEN;
      }
      gs_word_start[gs_word_count] = s;
      gs_word_len[gs_word_count] = (uint16_t)len;
      gs_word_count++;
    }
  }
}

static void gs_init_once(void) {
  if (gs_init_done) {
    return;
  }
  gs_seed_rng();
  gs_scan_vocab();
  gs_init_done = true;
}

bool godspeak_get_word(char *out, int out_len) {
  if (!out || out_len < 2) {
    return false;
  }

  gs_init_once();
  if (gs_word_count <= 0) {
    out[0] = '\0';
    return false;
  }

  uint32_t idx = gs_xorshift32() % (uint32_t)gs_word_count;
  uint32_t off = gs_word_start[idx];
  uint16_t len = gs_word_len[idx];

  if ((int)len > out_len - 1) {
    len = (uint16_t)(out_len - 1);
  }

  memcpy(out, _binary_god_Vocab_DD_start + off, len);
  out[len] = '\0';
  return true;
}

bool godspeak_get_insert_text(char *out, int out_len) {
  char word[GODSPEAK_MAX_WORD_LEN + 1];
  const char *prefix = "Cupid says: ";
  int p = 0;
  int i = 0;

  if (!out || out_len < 8) {
    return false;
  }

  if (!godspeak_get_word(word, (int)sizeof(word))) {
    return false;
  }

  while (prefix[i] && p < out_len - 1) {
    out[p++] = prefix[i++];
  }

  i = 0;
  while (word[i] && p < out_len - 2) {
    out[p++] = word[i++];
  }

  if (p < out_len - 1) {
    out[p++] = ' ';
  }
  out[p] = '\0';
  return true;
}
