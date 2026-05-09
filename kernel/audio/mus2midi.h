/* Vendored from https://github.com/chocolate-doom/chocolate-doom
 * (HEAD as of 2026-05-06)
 * License: GPL-2
 *
 * Local modifications:
 *  - Replaced #include "doomtype.h" with local typedefs (boolean, byte,
 *    PACKED_STRUCT) compatible with kernel/types.h.
 *  - Replaced #include "memio.h" with the local vendored path.
 *  - Added mus2midi_convert() wrapper declaration.
 */

//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2006 Ben Ryves 2006
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
//
// mus2mid.h - Ben Ryves 2006 - http://benryves.com - benryves@benryves.com
// Use to convert a MUS file into a single track, type 0 MIDI file.

#ifndef MUS2MIDI_H
#define MUS2MIDI_H

#include "types.h"
#include "memio.h"

/* Local replacements for doomtype.h types not present in kernel/types.h */
typedef int boolean;
typedef uint8_t byte;

/* PACKED_STRUCT: GCC packed attribute wrapper used by musheader */
#define PACKED_STRUCT(...) struct __VA_ARGS__ __attribute__((packed))

/* SHORT: little-endian 16-bit identity on x86 (always LE) */
#define SHORT(x) ((signed short)(x))

boolean mus2mid(MEMFILE *musinput, MEMFILE *midioutput);

/* Convert MUS lump to MIDI bytes in a kmalloc'd buffer.
 * Caller must kfree(*out_midi). Returns 0 on success, negative on
 * malformed input or OOM.
 */
int mus2midi_convert(const uint8_t *mus, uint32_t mus_len,
                     uint8_t **out_midi, uint32_t *out_len);

#endif /* #ifndef MUS2MIDI_H */
