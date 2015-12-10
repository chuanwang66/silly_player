#ifndef VIDEO_H
#define VIDEO_H

#include "player.h"

int video_init(VideoState *is);
int video_thread(void *arg);
void video_display(VideoState *is);

#endif // VIDEO_H
