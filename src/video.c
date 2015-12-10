#ifdef __cplusplus
extern "C"
{
#endif

/*
#include <stdio.h>
#include <assert.h>
#include <math.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
*/
#include <SDL.h>


#include "video.h"
#include "player.h"

#ifdef __cplusplus
};
#endif

static SDL_Window *sdlWin;
static SDL_mutex *sdlWinMutex;

static SDL_Renderer *sdlRen;
static SDL_Texture *sdlTex;

int video_init(VideoState *is){
    sdlWin = SDL_CreateWindow("silly player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              is->video_ctx->width, is->video_ctx->height,
                              SDL_WINDOW_OPENGL);
    if(!sdlWin){
        fprintf(stderr, "SDL_CreateWindow() error: %s", SDL_GetError());
        return 1;
    }
    sdlRen = SDL_CreateRenderer(sdlWin, -1, 0);
    sdlTex = SDL_CreateTexture(sdlRen, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,is->video_ctx->width,is->video_ctx->height);
}

static double synchronize_video(VideoState *is, AVFrame *src_frame, double pts)
{
    double frame_delay;

    if(pts != 0)
    {
        /* if we have pts, set video clock to it */
        is->video_clock = pts;
    }
    else
    {
        /* if we aren't given a pts, set it to the clock */
        pts = is->video_clock;
    }
    /* update the video clock */
    frame_delay = av_q2d(is->video_ctx->time_base);
    /* if we are repeating a frame, adjust clock accordingly */
    frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
    is->video_clock += frame_delay;
    return pts;
}

static int queue_picture(VideoState *is, AVFrame *pFrame, double pts){
    VideoPicture *vp; //exactly the same as is->pictq

    //wait for finishing displaying the last frame
    SDL_LockMutex(is->pictq_mutex);
    while(is->pictq_size >= 1 && !is->quit){
        SDL_CondWait(is->pictq_cond, is->pictq_mutex);
    }
    SDL_UnlockMutex(is->pictq_mutex);

    if(is->quit) return -1;

    //allocate space for "YUV image" on demand
    vp = &is->pictq;
    if(vp->allocated != 1){ //not allocated yet
        SDL_LockMutex(sdlWinMutex);
        vp->pFrameYUV = av_frame_alloc();
        uint8_t *out_buffer = (uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, is->video_ctx->width, is->video_ctx->height));
        avpicture_fill((AVPicture *)vp->pFrameYUV, out_buffer, PIX_FMT_YUV420P, is->video_ctx->width, is->video_ctx->height);
        SDL_UnlockMutex(sdlWinMutex);

        vp->width = is->video_ctx->width;
        vp->height = is->video_ctx->height;
        vp->allocated = 1;
    }

    if(is->quit) return -1;

    //conversion: video frame --> YUV image
    if(vp->pFrameYUV){
        vp->pts = pts;
        sws_scale(is->sws_ctx,
                  (const uint8_t* const *)pFrame->data, pFrame->linesize,
                  0, is->video_ctx->height,
                  vp->pFrameYUV->data, vp->pFrameYUV->linesize);

        //inform video-display thread(main thread)
        SDL_LockMutex(is->pictq_mutex);
        ++is->pictq_size;
        SDL_UnlockMutex(is->pictq_mutex);
    }
    return 0;
}

int video_thread(void *arg)
{
    VideoState *is = (VideoState *)arg;
    AVPacket pkt1, *packet = &pkt1;
    int frameFinished;
    AVFrame *pFrame;
    double pts;

    pFrame = av_frame_alloc();

    for(;;)
    {
        if(packet_queue_get(&is->videoq, packet, 1) < 0)
        {
            break; //means quitting getting packets
        }
        pts = 0;

        //decoding: packet --> frame
        avcodec_decode_video2(is->video_ctx, pFrame, &frameFinished, packet);
        av_free_packet(packet);

        if((pts = av_frame_get_best_effort_timestamp(pFrame)) == AV_NOPTS_VALUE)
        {
            pts = 0;
        }
        pts *= av_q2d(is->video_st->time_base);

        //frame --> YUV image
        if(frameFinished)
        {
            pts = synchronize_video(is, pFrame, pts);
            if(queue_picture(is, pFrame, pts) < 0) break;
        }
    }

    avcodec_free_frame(&pFrame);
    return 0;
}

void video_display(VideoState *is){
    VideoPicture vp;

    vp = is->pictq;
    if(vp.pFrameYUV){
        SDL_LockMutex(sdlWinMutex);

        SDL_UpdateTexture(sdlTex, NULL, vp.pFrameYUV->data[0], vp.pFrameYUV->linesize[0]);
        SDL_RenderClear(sdlRen);
        SDL_RenderCopy(sdlRen, sdlTex, NULL, NULL);
        SDL_RenderPresent(sdlRen);

        SDL_UnlockMutex(sdlWinMutex);
    }
}
