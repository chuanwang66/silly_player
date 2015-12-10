#include <assert.h>
#include <libswresample/swresample.h>
#include "libavutil/time.h"

#include "player.h"
#include "audio.h"

#define min(x,y) ((x)<(y)?(x):(y))

#define CONVERT_FMT_SWR
//#define SHOW_AUDIO_FRAME

static float cmid(float x, float min, float max){
    return (x<min) ? min : ((x>max) ? max: x);
}

static int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size, double *pts_ptr){
    int pkt_consumed, data_size = 0;
    double pts;

#ifndef CONVERT_FMT_SWR
    int16_t out_buffer[SDL_AUDIO_BUFFER_SIZE << 1]; //2048
    float sample0, sample1;
    int i;
#endif

    for(;;){
        while(is->audio_pkt_size > 0){
            int got_frame = 0;
            //packet --> frame
            //pkt_consumed: how many bytes of packet consumed
            pkt_consumed = avcodec_decode_audio4(is->audio_ctx, &is->audio_frame, &got_frame, is->audio_pkt_ptr);

            if(pkt_consumed < 0){
                is->audio_pkt_size = 0;
                break;
            }
            is->audio_pkt_data += pkt_consumed;
            is->audio_pkt_size -= pkt_consumed;

            //frame --> audio_buf
            //data_size: how many bytes of frame generated
            data_size = 0;
            if(got_frame){
                //data_size : now 8192 bytes in this frame
                //but only "half" of the frame is utilized in new-version ffmpeg
                data_size = av_samples_get_buffer_size(NULL,
                                                       is->audio_ctx->channels, //2 ==> 2 channels
                                                       is->audio_frame.nb_samples, //1024 ==> 1024 samples per channel
                                                       is->audio_frame.format, //FLTP ==> a float(4 bytes) per sample
                                                       1);
                /*
                  https://trac.ffmpeg.org/ticket/3525

                  "Well I figure it out. comparing to the old ffmpeg, It seems that in new ffmpeg,
                  each frame is twice larger than old one, so I guess that the output size could
                  be larger estimated in new version. So I tried devided the size of the resampled
                   data by 2, well I got the perfact audio."
                */
                data_size >>= 1;

                assert(data_size <= buf_size);

#ifndef CONVERT_FMT_SWR
                //convert from AV_SAMPLE_FMT_FLTP to AV_SAMPLE_FMT_S16SYS
                //http://stackoverflow.com/questions/14989397/how-to-convert-sample-rate-from-av-sample-fmt-fltp-to-av-sample-fmt-s16
                float *inputChannel0 = is->audio_frame.data[0];
                if(is->audio_frame.channels == 1){ //mono
                    for(i=0; i<is->audio_frame.nb_samples; ++i){
                        sample0 = cmid(*inputChannel0, -1.0f, 1.0f);
                        out_buffer[i] = (int16_t)(sample0 * 32767.0);

                        ++inputChannel0;
                    }
                }else{ //stereo
                    //fprintf(stderr, "\n(start)");
                    float *inputChannel1 = is->audio_frame.data[1];
                    for(i=0; i<is->audio_frame.nb_samples; ++i){
                        sample0 = cmid(*inputChannel0, -1.0f, 1.0f);
                        sample1 = cmid(*inputChannel1, -1.0f, 1.0f);
                        //fprintf(stderr, "%f:%f ", sample0, sample1);
                        out_buffer[i*2] = (int16_t) (sample0 * 32767.0f);
                        out_buffer[i*2+1] = (int16_t) (sample1 * 32767.0f);
                        ++inputChannel0;
                        ++inputChannel1;
                    }
                    //fprintf(stderr, "(end)\n");
                }
                //memcpy(audio_buf, is->audio_frame.data[0], data_size);
                memcpy(audio_buf, out_buffer, is->audio_frame.nb_samples * 2);
#else

                /*ATTENTION:
                    swr_convert(..., in_count)
                    in_count: number of input samples available in one channel
                    so half of data_size should be provided here.
                    HOLY SHIT!!!
                */
                if(swr_convert(is->swr_ctx, &is->out_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)is->audio_frame.data, data_size>>1) < 0){
                    fprintf(stderr, "swr_convert: Error while converting.\n");
                    return -1;
                }
                memcpy(audio_buf, is->out_buffer, data_size);
#endif
            }
            if(data_size <= 0) {
                continue;
            }

            //
            pts = is->audio_clock;
            *pts_ptr = pts;
            is->audio_clock += (double)data_size / (double)(2 * is->audio_ctx->channels * is->audio_ctx->sample_rate);
            //we have a little in audio buffer, return it and come back for more later

            return data_size;
        }

        //重新从PacketQueue中取
        //1. decoding失败
        //2. 没有部分解码的is->audio_pkt_ptr
        if(is->audio_pkt_ptr->data){
            av_free_packet(is->audio_pkt_ptr); //free on destroy ???
        }
        if(is->quit){
            return -1;
        }

        if(packet_queue_get(&is->audioq, is->audio_pkt_ptr, 1) < 0){
            return -1;
        }
        is->audio_pkt_data = is->audio_pkt_ptr->data;
        is->audio_pkt_size = is->audio_pkt_ptr->size;

        if(is->audio_pkt_ptr->pts != AV_NOPTS_VALUE){ //???why
            //fprintf(stderr, "is->audio_clock=%f\n", is->audio_clock);
            is->audio_clock = av_q2d(is->audio_st->time_base) * is->audio_pkt_ptr->pts;
        }
    }
}

//bytes of length 'len' need to be fed to 'stream'
void audio_callback(void *userdata, uint8_t *stream, int len){
    VideoState *is = (VideoState *)userdata;
    int len1, audio_size;
    double pts;

#ifdef SHOW_AUDIO_FRAME
    uint8_t *offset, one_byte, flag;
    int i;
#endif

    //fprintf(stderr, "audio_callback(): av_time()=%lf, len=%d\n", (double)av_gettime() / 1000.0, len);

    //SDL 2.0
    SDL_memset(stream, 0, len);

    while(len > 0){
        if(is->audio_buf_index >= is->audio_buf_size){
            //we have sent all our data(in audio buf), decode more
            audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf), &pts);
            if(audio_size < 0){
                //error, output silence
                is->audio_buf_size = SDL_AUDIO_BUFFER_SIZE;
                memset(is->audio_buf, 0, is->audio_buf_size);
            }else{
                is->audio_buf_size = audio_size;
            }
            is->audio_buf_index = 0;
        }

        //there're data left in audio buf, feed to stream
        len1 = is->audio_buf_size - is->audio_buf_index;
        len1 = min(len1, len);
        //memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);

#ifdef SHOW_AUDIO_FRAME
        //print decoded data
        flag = 0;
        offset = (uint8_t *)is->audio_buf + is->audio_buf_index;
        for(i=0; i<len1; ++i){
            one_byte = *(offset+i);
            if(one_byte != 0x00){
                fprintf(stderr, "%X", one_byte);
            }
        }
        if(flag == 1) fprintf(stderr, "\n");
#endif

        SDL_MixAudio(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1, SDL_MIX_MAXVOLUME);

        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
}

double get_audio_clock(VideoState *is) {
  double pts;
#if 1
  int hw_buf_size, bytes_per_sec, n;

  pts = is->audio_clock; /* maintained in the audio thread */
  hw_buf_size = is->audio_buf_size - is->audio_buf_index;
  bytes_per_sec = 0;
  n = is->audio_ctx->channels * 2;
  if(is->audio_st) {
    bytes_per_sec = is->audio_ctx->sample_rate * n;
  }
  if(bytes_per_sec) {
    pts -= (double)hw_buf_size / bytes_per_sec;
  }
#else
  pts = (av_gettime() / 1000000.0);
#endif
  return pts;
}
