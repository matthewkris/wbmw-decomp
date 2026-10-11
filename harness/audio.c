/* Sound out through SDL, for the playable windows (play.c, native/wbmw.c): each frame's samples
 * from sound.c go into SDL's audio queue. The queue is kept short (under about 150 ms) so the
 * sound stays with the picture: if it grows (the frame rate ran ahead, or fast-forward), samples
 * are dropped instead of queued. */
#include <SDL.h>
#include "md.h"

static SDL_AudioDeviceID dev;
static int started;

void audio_open(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { fprintf(stderr, "audio: %s\n", SDL_GetError()); return; }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SOUND_RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);   /* SDL converts to the device's own rate */
    if (!dev) fprintf(stderr, "audio: %s (playing without sound)\n", SDL_GetError());
}

/* after each frame: queue its sound, or throw it away when muted or too far ahead */
void audio_frame(int mute)
{
    static int16_t buf[4096 * 2];
    int n = sound_take(buf, 4096);
    if (!dev || mute) return;
    Uint32 queued = SDL_GetQueuedAudioSize(dev), limit = SOUND_RATE * 4 * 15 / 100;
    if (queued < limit) SDL_QueueAudio(dev, buf, (Uint32)n * 4);
    if (!started && queued >= SOUND_RATE * 4 / 20) { SDL_PauseAudioDevice(dev, 0); started = 1; }   /* 50 ms in hand */
}
