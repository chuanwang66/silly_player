#include "player.h"
#include "parse.h"

int parse_thread(void *arg)
{
    VideoState *is = (VideoState *)arg;
    AVPacket pkt1, *packet = &pkt1;

    for(;;)
    {
        if(is->quit) break;
        //seek stuff goes here ???

        //reading too fast, slow down!
        if(is->audioq.size > MAX_AUDIOQ_SIZE || is->videoq.size > MAX_VIDEOQ_SIZE)
        {
            //if(is->audioq.size > MAX_AUDIOQ_SIZE){
            //    fprintf(stderr, "{block audio dec}");
            //}else if(is->videoq.size > MAX_VIDEOQ_SIZE){
            //    fprintf(stderr, "{block video dec}");
            //}

            SDL_Delay(10);
            continue;
        }
        if(av_read_frame(is->pFormatCtx, packet) < 0)
        {
            if(is->pFormatCtx->pb->error == 0)
            {
                SDL_Delay(100); /* no error; wait for user input */
                continue;
            }
            else
            {
                break;
            }
        }

        if(packet->stream_index == is->video_stream_index)
        {
            packet_queue_put(&is->videoq, packet);
        }
        else if(packet->stream_index == is->audio_stream_index)
        {
            packet_queue_put(&is->audioq, packet);
        }
        else
        {
            av_free_packet(packet);
        }
    }

    /* wait for quitting */
    while(!is->quit)
    {
        SDL_Delay(100);
    }

    /* free facilities for audio/video playing */
    av_free(is->out_buffer);
    swr_free(&is->swr_ctx);
    return 0;
}
