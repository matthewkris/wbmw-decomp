/* A WAV file of the game's sound (16-bit stereo), for headless runs: --wav FILE. */
#include <stdio.h>
#include <stdint.h>

static FILE *wav; static uint32_t wav_frames, wav_rate;

static void put32(uint32_t v) { fputc(v & 255, wav); fputc(v >> 8 & 255, wav); fputc(v >> 16 & 255, wav); fputc(v >> 24, wav); }
static void put16(unsigned v) { fputc(v & 255, wav); fputc(v >> 8 & 255, wav); }

static void header(void)
{
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav); put32(36 + wav_frames * 4); fwrite("WAVEfmt ", 1, 8, wav);
    put32(16); put16(1); put16(2); put32(wav_rate); put32(wav_rate * 4); put16(4); put16(16);
    fwrite("data", 1, 4, wav); put32(wav_frames * 4);
}

int wav_open(const char *path, uint32_t rate)
{
    wav = fopen(path, "wb");
    if (!wav) { perror(path); return -1; }
    wav_rate = rate; wav_frames = 0;
    header();
    return 0;
}

void wav_write(const int16_t *stereo, int n)
{
    if (!wav) return;
    for (int i = 0; i < 2 * n; i++) put16((uint16_t)stereo[i]);
    wav_frames += (uint32_t)n;
}

void wav_close(void)
{
    if (!wav) return;
    header();
    fclose(wav); wav = NULL;
}
