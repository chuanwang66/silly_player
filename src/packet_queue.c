#include "packet_queue.h"

extern int global_exit;

void packet_queue_init(PacketQueue *q){
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt){
    //wrap AVPacket in a AVPacketList, which is a element of the list
    AVPacketList *pktList;
    if(av_dup_packet(pkt) != 0){
        return -1;
    }

    pktList = av_malloc(sizeof(AVPacketList));
    if(!pktList) return -1;
    pktList->pkt = *pkt;
    pktList->next = NULL;

    //
    SDL_LockMutex(q->mutex);

    if(!q->last_pkt) q->first_pkt = pktList;
    else q->last_pkt->next = pktList;

    q->last_pkt = pktList;
    q->nb_packets++;
    q->size += pktList->pkt.size;

    SDL_CondSignal(q->cond);
    SDL_UnlockMutex(q->mutex);

    return 0;
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block){
    AVPacketList *pktList;
    int ret;

    SDL_LockMutex(q->mutex);

    for(;;){
        //if(quit_get_from_queue){
        if(global_exit){
            ret = -1;
            break;
        }

        //
        pktList = q->first_pkt;
        if(pktList){ //read all elements from the list
            q->first_pkt = pktList->next;
            if(!q->first_pkt){
                q->last_pkt = NULL;
            }
            q->nb_packets--;
            q->size -= pktList->pkt.size;
            *pkt = pktList->pkt;
            av_free(pktList);

            ret = 1;
            break;
        }else {
            if(!block){ //return if non-blocking
                ret = 0;
                break;
            }else{
                SDL_CondWait(q->cond, q->mutex);
            }
        }
    }//end for(;;)

    SDL_UnlockMutex(q->mutex);
    return ret;
}
