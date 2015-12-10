#ifndef PACKET_QUEUE_H
#define PACKET_QUEUE_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <SDL.h>

#define MAX_AUDIOQ_SIZE (5*16*1024)
#define MAX_VIDEOQ_SIZE (5*256*1024)

typedef struct PacketQueue{
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets; //number of all elements
    int size; //total size of all elements
    SDL_mutex *mutex;
    SDL_cond *cond;
}PacketQueue;

//int quit_get_from_queue; //stop getting AVPacket from the queue

/** initialize a queue */
void packet_queue_init(PacketQueue *q);

/** append "one" AVPacket to the end of the queue */
int packet_queue_put(PacketQueue *q, AVPacket *pkt);

/** get "one" AVPacket from the queue in blocking/non-blocking manner*/
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block);

#endif // _PACKET_QUEUE_H
