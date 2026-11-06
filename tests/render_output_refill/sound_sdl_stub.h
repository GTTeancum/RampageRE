#ifndef REFILL_TEST_SDL_STUB_H
#define REFILL_TEST_SDL_STUB_H
/* No live device is opened. The fixture below implements this SDL boundary. */
typedef unsigned SDL_AudioDeviceID;
void SDL_PauseAudioDevice(SDL_AudioDeviceID device, int paused);
unsigned SDL_GetQueuedAudioSize(SDL_AudioDeviceID device);
int SDL_QueueAudio(SDL_AudioDeviceID device, const void *pcm, unsigned bytes);
void SDL_ClearQueuedAudio(SDL_AudioDeviceID device);
const char *SDL_GetError(void);
#endif
