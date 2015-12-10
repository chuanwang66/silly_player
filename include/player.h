#ifndef PLAYER_H
#define PLAYER_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#define MAX_AUDIO_FRAME_SIZE 192000

#include <packet_queue.h>

//ffmpeg
#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

//SDL2
#define SDL_AUDIO_BUFFER_SIZE 1024

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0

//note: allocated once
typedef struct VideoPicture{
    //SDL_Overlay *bmp; //SDL2 counterpart ???
    //int width, height;
    AVFrame *pFrameYUV;
    int width, height;
    int allocated;
    double pts;
}VideoPicture;

typedef struct VideoState{
    AVFormatContext *pFormatCtx;
    struct SwsContext *sws_ctx;

    /** ************** audio related ************** */
    int audio_stream_index;
    AVStream *audio_st;
    AVCodecContext *audio_ctx;

    double audio_clock;

    //注: 从音频流中读出的audio packet放入队列audioq，但一个audio packet可能被解为多个audio frame并放入audio buffer
    //    因此audio_pkt就是上一次没完全解完的audio packet，audio_pkt_data[0, ... , audio_pkt_size-1]是其中的剩余部分

    //(1) packets are read from audio stream, appending to the audioq.
    PacketQueue audioq; //list of AVPacketList(a AVPacket Wrapper)

    //(2) a single packet is picked out, waiting for decoding into one or more frames.
    AVPacket *audio_pkt_ptr;
    //1 "audio packet" may be decoded into multiple "audio frames", that is,
    //audio_pkt_data[0, ... , audio_pkt_size-1] is the remaining part waiting for decoding
    uint8_t *audio_pkt_data;
    int audio_pkt_size;
    int audio_hw_buf_size; //the actual capacity of audio buffer

    //(3) copy frames into audio_buf[], which would be consumed by
    // the audio device later.
    AVFrame audio_frame;
    //audio_buf[audio_index, ... , audio_buf_size-1] is a bunch of audio frame
    uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) >> 1]; //why???
    unsigned int audio_buf_index;
    unsigned int audio_buf_size;

    SwrContext *swr_ctx; //to convert audio frame from AV_SAMPLE_FMT_FLTP to AV_SAMPLE_FMT_S16
    uint8_t *out_buffer; //to contain the conversion result

    /** ************** video related ************** */
    int video_stream_index;
    AVStream *video_st;
    AVCodecContext *video_ctx;

    double video_clock;
    double frame_timer; //predicted pts of the next video frame.
    double frame_last_pts; //(actual) pts of the last video frame.
    double frame_last_delay; //last delay of two adjacent video frames.

    //(1)video packet queue
    PacketQueue videoq;

    //(2)AVPacket allocated within "video decoding thread"

    //(3)AVFrame allocated within "video decoding thread"
    //AVFrame
    //  --(YUV conversion)--> VideoPicture
    //  --(copy into back buffer)--> pictq[0]
    VideoPicture pictq;
    int pictq_size;
    SDL_mutex *pictq_mutex;
    SDL_cond *pictq_cond;


    char filename[1024];

    /** ************** process terminator ************** **/
    int quit;
}VideoState;

#endif // PLAYER_H
