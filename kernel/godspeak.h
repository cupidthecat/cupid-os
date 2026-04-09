#ifndef GODSPEAK_H
#define GODSPEAK_H

#include "types.h"

/* TempleOS-inspired random vocabulary picker backed by god/Vocab.DD. */
bool godspeak_get_word(char *out, int out_len);
bool godspeak_get_insert_text(char *out, int out_len);

#endif
