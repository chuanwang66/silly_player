#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <player.h>

void audio_callback(void *userdata, uint8_t *stream, int len);
double get_audio_clock(VideoState *is);

#endif // AUDIO_H
