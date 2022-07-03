/**
 * Copyright (c) 2008, Ruben "angryzor" Tytgat
 * 
 * Permission to use, copy, modify, and/or distribute this
 * software for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice and this permission
 * notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL,
 * DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH
 * THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bass_vgmstream.h"

#include <vgmstream.h>
#include <stdlib.h>

/**
 * Callback for BASS. Called when it needs more data.
 */
DWORD CALLBACK vgmStreamProc(
    HSTREAM handle,
    void *buffer,
    DWORD length,
    void *user
)
{
	VGMSTREAM* stream = (VGMSTREAM*)user;                         // We passed the VGMSTREAM as user data.
	BOOL ended = FALSE;                                           // Used to signal end of stream.
	int max_samples = length / sizeof(sample) / stream->channels; // Calculate the maximum amount of samples from max buffer size.
	int samples_to_do;                                            // Will hold the amount of samples to be copied.
	
	// If this is a looping VGM stream, we handle it as an infinite stream and read out the
	// next `max_samples` of data.
	//
	// Otherwise, we read out at most `max_samples`. If there is less data available,
	// we consider the stream ended and signal this to BASS
	if (!stream->loop_flag && stream->current_sample + max_samples > stream->num_samples) {
		samples_to_do = stream->num_samples - stream->current_sample;
		ended = TRUE;
	}
	else
		samples_to_do = max_samples;

	// Render the stream.
	render_vgmstream((sample*)buffer, samples_to_do, stream);
	// BASS expects you to return the amount of data read in bytes, so multiply by the sample size
	samples_to_do *= sizeof(sample) * stream->channels;

	// If we reached the end of a non-looping VGM stream, we'll check BASS' loop flag.
	// If it is set, we restart from the beginning. Otherwise, we signal the end of stream
	// to BASS.
	if(ended) {
		if(BASS_ChannelFlags(handle,0,0) & BASS_SAMPLE_LOOP)
			reset_vgmstream(stream);
		else
			samples_to_do |= BASS_STREAMPROC_END;
	}

	return samples_to_do;
}

/**
 * Called when the BASS handle is closed
 */
void CALLBACK vgmStreamOnFree(
    HSYNC handle,
    DWORD channel,
    DWORD data,
    void *user
)
{
	VGMSTREAM* stream = (VGMSTREAM*)user;
	close_vgmstream(stream);
}

// Read from memory: https://github.com/vgmstream/vgmstream/issues/662
STREAMFILE* open_memory_streamfile(uint8_t* buf, size_t bufsize, const char* name);

typedef struct {
	STREAMFILE sf; /* pre-alloc'd part */

	uint8_t* buf;
	size_t bufsize;
	const char* name;
	off_t offset;
} MEMORY_STREAMFILE;

static size_t memory_read(MEMORY_STREAMFILE* sf, uint8_t* dst, off_t offset, size_t length) {
	if (!dst || length <= 0 || offset < 0 || offset >= sf->bufsize)
		return 0;
	if (offset + length > sf->bufsize)
		length = sf->bufsize - offset; /* clamp */

	memcpy(dst, sf->buf + offset, length);
	sf->offset = offset;
	return length;
}

static size_t memory_get_size(MEMORY_STREAMFILE* sf) {
	return sf->bufsize;
}

static size_t memory_get_offset(MEMORY_STREAMFILE* sf) {
	return sf->offset;
}

static void memory_get_name(MEMORY_STREAMFILE* sf, char* buffer, size_t length) {
	strncpy(buffer, sf->name, length);
	buffer[length - 1] = '\0';
}

static STREAMFILE* memory_open(MEMORY_STREAMFILE* sf, const char* const filename, size_t buffersize) {
	/* Some formats need to open companion files though, maybe pass N bufs+name per file */
	/* also must detect "reopens", as internal processes need to clone this streamfile at times */
	if (strcmp(filename, sf->name) == 0)
		return open_memory_streamfile(sf->buf, sf->bufsize, sf->name); /* reopen */
	return NULL;
}

static void memory_close(MEMORY_STREAMFILE* sf) {
	free(sf);
}

STREAMFILE* open_memory_streamfile(uint8_t* buf, size_t bufsize, const char* name) {
	MEMORY_STREAMFILE* this_sf = NULL;

	this_sf = calloc(1, sizeof(MEMORY_STREAMFILE));
	if (!this_sf) goto fail;

	this_sf->sf.read = (void*)memory_read;
	this_sf->sf.get_size = (void*)memory_get_size;
	this_sf->sf.get_offset = (void*)memory_get_offset;
	this_sf->sf.get_name = (void*)memory_get_name;
	this_sf->sf.open = (void*)memory_open;
	this_sf->sf.close = (void*)memory_close;

	/* assumes bufs live externally during decode, otherwise malloc/memcpy and free on close */
	this_sf->buf = buf;
	this_sf->bufsize = bufsize;
	this_sf->name = name;

	return &this_sf->sf;
fail:
	free(this_sf);
	return NULL;
}

BASS_VGMSTREAM_API HSTREAM BASS_VGMSTREAM_StreamCreate(const char* file, DWORD flags)
{
	HSTREAM h;
	VGMSTREAM* stream = init_vgmstream(file);
	if(!stream)
		return 0;

	h = BASS_StreamCreate(stream->sample_rate, stream->channels, flags, &vgmStreamProc, stream);
	if(!h)
		return 0;

	BASS_ChannelSetSync(h, BASS_SYNC_FREE|BASS_SYNC_MIXTIME, 0, &vgmStreamOnFree, stream);
	return h;
}

BASS_VGMSTREAM_API HSTREAM BASS_VGMSTREAM_StreamCreateFromMemory(unsigned char* buf, int bufsize, const char* name, DWORD flags)
{
	HSTREAM h;
	if (!buf)
		return 0;

	STREAMFILE* sf = open_memory_streamfile(buf, bufsize, name);
	VGMSTREAM* vgmstream = init_vgmstream_from_STREAMFILE(sf);

	h = BASS_StreamCreate(vgmstream->sample_rate, vgmstream->channels, flags, &vgmStreamProc, vgmstream);
	if (!h)
		return 0;

	BASS_ChannelSetSync(h, BASS_SYNC_FREE | BASS_SYNC_MIXTIME, 0, &vgmStreamOnFree, vgmstream);
	return h;
}