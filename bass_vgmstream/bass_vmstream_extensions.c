#include "bass_vgmstream.h"

#include <vgmstream.h>
#include <stdlib.h>

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

// Conversion
int Convert(VGMSTREAM* vgmstream, const char* outputdata)
{
	int result = 1; // 0 if there is no error
	sample* buf = NULL;
	int32_t len;
	const int BUFSIZE = 4000; // PCM sample size
	int position = 0x2C; // Buffer seeking

	// Init
	//printf("Start\n");
	if (!vgmstream)
	{
		//printf("Dead\n");
		return 1;
	}

	// Allocate buffer
	buf = malloc(BUFSIZE * sizeof(sample) * vgmstream->channels);
	len = get_vgmstream_play_samples(0, 0, 0, vgmstream);

	// Add WAV header
	make_wav_header((uint8_t*)buf, len, vgmstream->sample_rate, vgmstream->channels);
	memcpy(outputdata, buf, 0x2C);

	// Decode data
	for (int i = 0; i < len; i += BUFSIZE)
	{
		int toget = BUFSIZE;
		if (i + BUFSIZE > len)
			toget = len - i;

		render_vgmstream(buf, toget, vgmstream);
		swap_samples_le(buf, vgmstream->channels * toget);
		//printf("Step: %u\n", i);
		memcpy(&outputdata[position], buf, sizeof(sample) * vgmstream->channels * toget);
		position += sizeof(sample) * vgmstream->channels * toget;
	}

	// Cleanup
	free(buf);
	buf = NULL;
	return 0;
}

// API functions
BASS_VGMSTREAM_API void* BASS_VGMSTREAM_InitVGMStreamFromMemory(void* data, int size, const char* name)
{
	STREAMFILE* sf = open_memory_streamfile(data, size, name);
	return (void*)init_vgmstream_from_STREAMFILE(sf);
}

BASS_VGMSTREAM_API void BASS_VGMSTREAM_CloseVGMStream(void* vgmstream)
{
	close_vgmstream((VGMSTREAM*)vgmstream);
}

BASS_VGMSTREAM_API int BASS_VGMSTREAM_GetVGMStreamOutputSize(void* vgmstream)
{
	VGMSTREAM* stream = (VGMSTREAM*)vgmstream;
	int numsample = get_vgmstream_play_samples(0, 0, 0, stream);
	return numsample * sizeof(sample) * stream->channels + 0x2C; // + WAV header
}

BASS_VGMSTREAM_API int BASS_VGMSTREAM_ConvertVGMStreamToWav(void* vgmstream, const char* outputdata)
{
	return Convert((VGMSTREAM*)vgmstream, outputdata);
}

#ifdef  DEBUG
int main(int argc, char* argv[])
{
	const char* srcFile = "test.adx";
	const char* dstFile = "test.wav";
	int inputSize;
	int outputSize = 0;
	void* InputBuffer;
	const char* OutputBuffer = NULL;

	// Read source file
	FILE* f;
	fopen_s(&f, srcFile, "rb");
	fseek(f, 0L, SEEK_END);
	inputSize = ftell(f);
	fseek(f, 0L, SEEK_SET);
	InputBuffer = malloc(inputSize);
	fread(InputBuffer, 1, inputSize, f);
	fclose(f);

	// Create vgmstream
	VGMSTREAM* vgmstream = BASS_VGMSTREAM_InitVGMStreamFromMemory(InputBuffer, inputSize, srcFile);

	// Get output size
	outputSize = BASS_VGMSTREAM_GetVGMStreamOutputSize(vgmstream);
	OutputBuffer = malloc(outputSize);

	// Convert data
	Convert(vgmstream, OutputBuffer);
	printf("Output size 2: %u\n", outputSize);
	printf("Output data: %u\n", OutputBuffer[0]);

	// Write output file
	fopen_s(&f, dstFile, "wb");
	fwrite(OutputBuffer, outputSize, 1, f);
	fclose(f);
	return 0;
}
#endif