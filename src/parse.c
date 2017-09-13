#include "player.h"
#include "parse.h"

extern int global_exit;
extern int global_exit_parse;

int parse_thread(void *arg)
{
    VideoState *is = (VideoState *)arg;
    AVPacket pkt1, *packet = &pkt1;

    //seek to position (audio seeking supported ONLY)
    packet_queue_clear(&is->audioq);

    AVRational time_base = is->audio_st->time_base;
    //printf("seek_target==%d/%d\n", time_base.num, time_base.den);
    //printf("start_time==%d\n", is->audio_st->start_time);
    //printf("0===%d\n", av_rescale(0, time_base.den, time_base.num));
    //printf("1===%d\n", av_rescale(1, time_base.den, time_base.num));
    //printf("2===%d\n", av_rescale(2, time_base.den, time_base.num));
    int64_t seek_time = is->audio_st->start_time + av_rescale(is->seek_pos_sec, time_base.den, time_base.num);
    //printf("seek_time=%d, cur_dts=%d\n", seek_time, is->audio_st->cur_dts);
    if(seek_time > is->audio_st->cur_dts) {
        av_seek_frame(is->pFormatCtx, is->audio_stream_index, seek_time, AVSEEK_FLAG_ANY);
    } else {
        av_seek_frame(is->pFormatCtx, is->audio_stream_index, seek_time, AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD);
    }


    for(;;)
    {
        if(global_exit_parse) break;
        //seek stuff goes here ???

        //reading too fast, slow down!
        if(is->audioq.size > MAX_AUDIOQ_SIZE || is->videoq.size > MAX_VIDEOQ_SIZE)
        {
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

        if(packet->stream_index == is->audio_stream_index)
        {
            packet_queue_put(&is->audioq, packet);
        }
        else if(packet->stream_index == is->video_stream_index)
        {
            packet_queue_put(&is->videoq, packet);
        }
        else
        {
            av_free_packet(packet);
        }
    }

    /* wait for quitting */
    while(!global_exit_parse)
    {
        SDL_Delay(100);
    }

    /* free facilities for audio/video playing */
    if(global_exit)
    {
        av_free(is->out_buffer);
        swr_free(&is->swr_ctx);
    }

    fprintf(stderr, "parse thread breaks\n");
    return 0;
}
