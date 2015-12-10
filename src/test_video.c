#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL.h>

#ifdef __cplusplus
};
#endif

#define SFM_REFRESH_EVENT (SDL_USEREVENT + 1)
#define FRAME_PER_SECOND 15

extern int global_exit;
static int pause = 0;

static int event_poster(void *opaque)
{
    global_exit = 0;
    pause = 0;

    while(!global_exit)
    {
        if(!pause)
        {
            SDL_Event event;
            event.type = SFM_REFRESH_EVENT;
            SDL_PushEvent(&event);
        }
        SDL_Delay(FRAME_PER_SECOND);
    }
    global_exit = 0;
    pause = 0;
    return 0;
}

static int test(int argc, char* argv[])
{
    char filepath[] = "quicksort.mp4";
    //-------------------ffmpeg---------------------
    AVFormatContext *pFormatCtx;
    int i, videoindex;
    AVCodecContext *pCodecCtx;
    AVCodec *pCodec;
    AVFrame *pFrame, *pFrameYUV;
    uint8_t *out_buffer;
    AVPacket *packet;
    int got_picture;

    //-------------------SDL---------------------
    int screen_w, screen_h;
    SDL_Window *sdlWin;
    SDL_Renderer *sdlRen;
    SDL_Texture *sdlTex;
    SDL_Thread *sdlThread;
    SDL_Event sdlEvent;

    struct SwsContext *swsCtx;

    //-------------------------------------------
    av_register_all();
    avformat_network_init();
    pFormatCtx = avformat_alloc_context();

    //读取文件头部并把信息保存到AVFormatContext结构体中
    if(avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0)
    {
        printf("avformat_open_input() eror.\n");
        return 1;
    }
    av_dump_format(pFormatCtx, 0, filepath, 0);//output info

    //读取文件中"流"的信息
    if(avformat_find_stream_info(pFormatCtx, NULL) < 0)
    {
        printf("avformat_find_stream_info() error.\n");
        return 1;
    }

    //find video stream index
    videoindex = -1;
    for(i=0; i<pFormatCtx->nb_streams; ++i)
    {
        if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoindex = i;
            break;
        }
    }
    if(videoindex == -1)
    {
        printf("video stream not found.\n");
        return 1;
    }

    //find & open decoder
    pCodecCtx = pFormatCtx->streams[videoindex]->codec;
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if(pCodec == NULL)
    {
        printf("avcodec_find_decoder() error.\n");
        return 1;
    }
    if(avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
    {
        printf("avcodec_open2() error.\n");
        return 1;
    }

    //allocate frame pre/post decoding
    pFrame = av_frame_alloc();

    pFrameYUV = av_frame_alloc();
    out_buffer = (uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height)); //buffer allocated for YUV image
    avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);//mount the buffer to YUV frame

    //swsCtx used for generating YUV
    swsCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
                            pCodecCtx->pix_fmt,
                            pCodecCtx->width, pCodecCtx->height,
                            PIX_FMT_YUV420P, SWS_BICUBIC,
                            NULL, NULL, NULL);

    //init window
    //note: SDL 2.0 supports multiple windows
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        printf("SDL_Init() error: %s", SDL_GetError());
        return 1;
    }
    screen_w = pCodecCtx->width;
    screen_h = pCodecCtx->height;
    sdlWin = SDL_CreateWindow("player(ffmpeg + SDL2.0-event driven)", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              screen_w, screen_h,SDL_WINDOW_OPENGL);
    if(!sdlWin)
    {
        printf("SDL_CreateWindow() error: %s", SDL_GetError());
        return 1;
    }

    //sdl renderer
    sdlRen = SDL_CreateRenderer(sdlWin, -1, 0);

    //sdl texture
    //note: (1)IYUV: Y+U+V (2)YV12: Y+V+U
    sdlTex = SDL_CreateTexture(sdlRen, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,pCodecCtx->width,pCodecCtx->height);

    //event loop
    packet = (AVPacket *)av_malloc(sizeof(AVPacket));
    sdlThread = SDL_CreateThread(event_poster, NULL, NULL);
    for(;;)
    {
        SDL_WaitEvent(&sdlEvent);
        switch(sdlEvent.type)
        {
        case SFM_REFRESH_EVENT: //每一帧频收到一个刷新消息
            if(av_read_frame(pFormatCtx, packet) == 0){
                    if(packet->stream_index == videoindex){
                            //解码：packet --> 原始帧
                            //note: 解码一帧失败，立即暂停视频
                            if(avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet) < 0){
                                printf("decode error.\n");
                                pause = 1;
                                break;
                            }else{
                                if(got_picture){
                                    //“像素格式转换”: 原始帧 --> YUV帧
                                    sws_scale(swsCtx,
                                              (const uint8_t* const *)pFrame->data, pFrame->linesize,
                                              0, pCodecCtx->height,
                                              pFrameYUV->data, pFrameYUV->linesize);

                                    //此时得到一段内存用来存储YUV帧，同样也可用glut显示替代SDL ???
                                    SDL_UpdateTexture(sdlTex, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
                                    SDL_RenderClear(sdlRen);
                                    //SDL_RenderCopy(sdlRen, sdlTex, &sdlRect, &sdlRect);
                                    SDL_RenderCopy(sdlRen, sdlTex, NULL, NULL);
                                    SDL_RenderPresent(sdlRen);
                                }
                            }
                    }
                    av_free_packet(packet);
            }else {
                //读取一帧失败，立即暂停视频
                pause = 1;
                break;
            }
            break;
        case SDL_KEYDOWN: //按下"空格键"暂停或继续
            if(sdlEvent.key.keysym.sym==SDLK_SPACE)
            {
                pause = !pause;
            }
            break;
        case SDL_QUIT:
            global_exit = 1;
            break;
        }

        if(global_exit) break;
    }

    sws_freeContext(swsCtx);
    SDL_Quit();
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);
    return 0;
}
