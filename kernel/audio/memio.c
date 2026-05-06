/* Vendored from https://github.com/chocolate-doom/chocolate-doom
 * (HEAD as of 2026-05-06)
 * License: GPL-2
 *
 * Local modifications:
 *  - Dropped <stdio.h>, <stdlib.h>, <string.h> — replaced with kernel headers.
 *  - Dropped #include "z_zone.h" (chocolate-doom zone allocator).
 *  - Replaced Z_Malloc(..., PU_STATIC, 0) with kmalloc(size).
 *  - Replaced Z_Free(ptr) with kfree(ptr).
 *  - Removed printf() calls in error paths (no stdio in kernel); those
 *    paths now just return the error value silently.
 */

//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
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
// Emulates the IO functions in C stdio.h reading and writing to
// memory.
//

#include "../types.h"
#include "../string.h"
#include "../memory.h"

#include "memio.h"

typedef enum {
	MODE_READ,
	MODE_WRITE,
} memfile_mode_t;

struct _MEMFILE {
	unsigned char *buf;
	size_t buflen;
	size_t alloced;
	unsigned int position;
	memfile_mode_t mode;
};

// Open a memory area for reading

MEMFILE *mem_fopen_read(void *buf, size_t buflen)
{
	MEMFILE *file;

	file = (MEMFILE *)kmalloc(sizeof(MEMFILE));
	if (!file)
		return (void *)0;

	file->buf = (unsigned char *) buf;
	file->buflen = buflen;
	file->position = 0;
	file->alloced = 0;
	file->mode = MODE_READ;

	return file;
}

// Read bytes

size_t mem_fread(void *buf, size_t size, size_t nmemb, MEMFILE *stream)
{
	size_t items;

	if (stream->mode != MODE_READ)
	{
		return (size_t)-1;
	}

	// Trying to read more bytes than we have left?

	items = nmemb;

	if (items * size > stream->buflen - stream->position)
	{
		items = (stream->buflen - stream->position) / size;
	}

	// Copy bytes to buffer

	memcpy(buf, stream->buf + stream->position, items * size);

	// Update position

	stream->position += (unsigned int)(items * size);

	return items;
}

// Open a memory area for writing

MEMFILE *mem_fopen_write(void)
{
	MEMFILE *file;

	file = (MEMFILE *)kmalloc(sizeof(MEMFILE));
	if (!file)
		return (void *)0;

	file->alloced = 1024;
	file->buf = (unsigned char *)kmalloc(file->alloced);
	if (!file->buf) {
		kfree(file);
		return (void *)0;
	}
	file->buflen = 0;
	file->position = 0;
	file->mode = MODE_WRITE;

	return file;
}

// Write bytes to stream

size_t mem_fwrite(const void *ptr, size_t size, size_t nmemb, MEMFILE *stream)
{
	size_t bytes;

	if (stream->mode != MODE_WRITE)
	{
		return (size_t)-1;
	}

	// More bytes than can fit in the buffer?
	// If so, reallocate bigger.

	bytes = size * nmemb;

	while (bytes > stream->alloced - stream->position)
	{
		unsigned char *newbuf;
		size_t newsz = stream->alloced * 2;

		newbuf = (unsigned char *)kmalloc((uint32_t)newsz);
		if (!newbuf)
			return (size_t)-1;
		memcpy(newbuf, stream->buf, stream->alloced);
		kfree(stream->buf);
		stream->buf = newbuf;
		stream->alloced = newsz;
	}

	// Copy into buffer

	memcpy(stream->buf + stream->position, ptr, bytes);
	stream->position += (unsigned int)bytes;

	if (stream->position > stream->buflen)
		stream->buflen = stream->position;

	return nmemb;
}

void mem_get_buf(MEMFILE *stream, void **buf, size_t *buflen)
{
	*buf = stream->buf;
	*buflen = stream->buflen;
}

void mem_fclose(MEMFILE *stream)
{
	if (stream->mode == MODE_WRITE)
	{
		kfree(stream->buf);
	}

	kfree(stream);
}

long mem_ftell(MEMFILE *stream)
{
	return (long)stream->position;
}

int mem_fseek(MEMFILE *stream, signed long position, mem_rel_t whence)
{
	unsigned int newpos;

	switch (whence)
	{
		case MEM_SEEK_SET:
			newpos = (unsigned int) position;
			break;

		case MEM_SEEK_CUR:
			newpos = (unsigned int) (stream->position + position);
			break;

		case MEM_SEEK_END:
			newpos = (unsigned int) (stream->buflen + position);
			break;
		default:
			return -1;
	}

	if (newpos <= stream->buflen)
	{
		stream->position = newpos;
		return 0;
	}
	else
	{
		return -1;
	}
}
