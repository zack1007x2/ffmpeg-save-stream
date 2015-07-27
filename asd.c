/*
 * Copyright (c) 2012 Stefano Sabatini
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * Demuxing and decoding example.
 *
 * Show how to use the libavformat and libavcodec API to demux and
 * decode audio and video data.
 * @example demuxing_decoding.c
 */

#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>


static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL, *audio_dec_ctx;
static int width, height;
static enum AVPixelFormat pix_fmt;
static AVStream *video_stream = NULL, *audio_stream = NULL;
static const char *src_filename = NULL;
static const char *video_dst_filename = NULL;
static const char *audio_dst_filename = NULL;
static FILE *video_dst_file = NULL;
static FILE *audio_dst_file = NULL;

static uint8_t *video_dst_data[4] = {NULL};
static int      video_dst_linesize[4];
static int video_dst_bufsize;

static int video_stream_idx = -1, audio_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket pkt;
static int video_frame_count = 0;
static int audio_frame_count = 0;

static AVFormatContext* pFormatCtx;
static AVOutputFormat* fmt;
static AVStream* video_st;
static AVCodecContext* pCodecCtx;
static AVCodec* pCodec;
static AVPacket epkt;
static uint8_t* picture_buf;
static AVFrame* pFrame;
static int picture_size;
static int y_size;

static AVCodec *AudCodec;
static AVCodecContext *AudCodecCtx= NULL;
static AVFormatContext* AudFormatCtx;
static AVOutputFormat* Audofmt;
static AVStream* audio_st;
static AVFrame *AudFrame;
static AVPacket AudPkt;
static int aud_buffer_size;
static uint8_t *sample_buf;


/* The different ways of decoding and managing data memory. You are not
 * supposed to support all the modes in your application but pick the one most
 * appropriate to your needs. Look for the use of api_mode in this example to
 * see what are the differences of API usage between them */
enum {
    API_MODE_OLD                  = 0, /* old method, deprecated */
    API_MODE_NEW_API_REF_COUNT    = 1, /* new method, using the frame reference counting */
    API_MODE_NEW_API_NO_REF_COUNT = 2, /* new method, without reference counting */
};

static int api_mode = API_MODE_OLD;



/* check that a given sample format is supported by the encoder */
static int check_sample_fmt(AVCodec *codec, enum AVSampleFormat sample_fmt)
{
    const enum AVSampleFormat *p = codec->sample_fmts;

    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}

/* just pick the highest supported samplerate */
static int select_sample_rate(AVCodec *codec)
{
    const int *p;
    int best_samplerate = 0;

    if (!codec->supported_samplerates)
        return 44100;

    p = codec->supported_samplerates;
    while (*p) {
        best_samplerate = FFMAX(*p, best_samplerate);
        p++;
    }
    return best_samplerate;
}

/* select layout with the highest channel count */
static int select_channel_layout(AVCodec *codec)
{
    const uint64_t *p;
    uint64_t best_ch_layout = 0;
    int best_nb_channels   = 0;

    if (!codec->channel_layouts)
        return AV_CH_LAYOUT_STEREO;

    p = codec->channel_layouts;
    while (*p) {
        int nb_channels = av_get_channel_layout_nb_channels(*p);

        if (nb_channels > best_nb_channels) {
            best_ch_layout    = *p;
            best_nb_channels = nb_channels;
        }
        p++;
    }
    return best_ch_layout;
}


void print_hex(uint8_t *s, size_t len) {
    int counter=0;
    for(int i = 0; i < len; i++) {    
        if(i%4!=0){
            printf("%02x", s[i]);
        }else{
            if(counter==4){
                printf("\n");
                counter=0;
            }
            printf("      %02x", s[i]);
            counter++;
        }
        
    }
    printf("\n");
}




int flush_encoder(AVFormatContext *fmt_ctx,unsigned int stream_index){
    int ret;
    int got_frame;
    AVPacket enc_pkt;
    if (!(fmt_ctx->streams[stream_index]->codec->codec->capabilities &
        CODEC_CAP_DELAY))
        return 0;
    while (1) {
        enc_pkt.data = NULL;
        enc_pkt.size = 0;
        av_init_packet(&enc_pkt);
        ret = avcodec_encode_video2 (fmt_ctx->streams[stream_index]->codec, &enc_pkt,
            NULL, &got_frame);
        av_frame_free(NULL);
        if (ret < 0)
            break;
        if (!got_frame){
            ret=0;
            break;
        }
        printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n",enc_pkt.size);
        /* mux encoded frame */
        ret = av_write_frame(fmt_ctx, &enc_pkt);
        if (ret < 0)
            break;
    }
    return ret;
}


static int decode_packet(int *got_frame, int cached)
{
    int ret = 0;
    int decoded = pkt.size;

    *got_frame = 0;

    if (pkt.stream_index == video_stream_idx) {
        /* decode video frame */
        ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding video frame (%s)\n", av_err2str(ret));
            return ret;
        }

        if (*got_frame) {

            if (frame->width != width || frame->height != height ||
                frame->format != pix_fmt) {
                /* To handle this change, one could call av_image_alloc again and
                 * decode the following frames into another rawvideo file. */
                fprintf(stderr, "Error: Width, height and pixel format have to be "
                        "constant in a rawvideo file, but the width, height or "
                        "pixel format of the input video changed:\n"
                        "old: width = %d, height = %d, format = %s\n"
                        "new: width = %d, height = %d, format = %s\n",
                        width, height, av_get_pix_fmt_name(pix_fmt),
                        frame->width, frame->height,
                        av_get_pix_fmt_name(frame->format));
                return -1;
            }

            printf("video_frame%s n:%d coded_n:%d pts:%s\n",
                   cached ? "(cached)" : "",
                   video_frame_count, frame->coded_picture_number,
                   av_ts2timestr(frame->pts, &video_dec_ctx->time_base));

            
            // print_hex(frame->data[0],strlen((char*)frame->data[0]));

            /* copy decoded frame to destination buffer:
             * this is required since rawvideo expects non aligned data */
            av_image_copy(video_dst_data, video_dst_linesize,
                          (const uint8_t **)(frame->data), frame->linesize,
                          pix_fmt, width, height);

            //pFrame data 跟video_dst_data不一樣

            picture_buf = video_dst_data[0];

            /* write to rawvideo file */
            // fwrite(video_dst_data[0], 1, video_dst_bufsize, video_dst_file);
            pFrame->data[0] = picture_buf;              // Y
            pFrame->data[1] = picture_buf+ y_size;      // U 
            pFrame->data[2] = picture_buf+ y_size*5/4;  // V

            
            //PTS
            pFrame->pts=video_frame_count;
            int got_picture=0;
            //Encode
            int ret = avcodec_encode_video2(pCodecCtx, &epkt,pFrame, &got_picture);
            if(ret < 0){
                printf("Failed to encode! \n");
                return -1;
            }
            if (got_picture==1){
                printf("Succeed to encode frame: %5d\tsize:%5d\n",video_frame_count,epkt.size);
                video_frame_count++;
                epkt.stream_index = video_st->index;
                ret = av_interleaved_write_frame(pFormatCtx, &epkt);
                av_free_packet(&epkt);
            }
        }
    } else if (pkt.stream_index == audio_stream_idx) {
        int got_output,retuu;
        /* decode audio frame */
        ret = avcodec_decode_audio4(audio_dec_ctx, frame, got_frame, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding audio frame (%s)\n", av_err2str(ret));
            return ret;
        }
        /* Some audio decoders decode only part of the packet, and have to be
         * called again with the remainder of the packet data.
         * Sample: fate-suite/lossless-audio/luckynight-partial.shn
         * Also, some decoders might over-read the packet. */
        decoded = FFMIN(ret, pkt.size);

        if (*got_frame) {
            size_t unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(frame->format);
            printf("audio_frame%s n:%d nb_samples:%d pts:%s\n",
                   cached ? "(cached)" : "",
                   audio_frame_count, frame->nb_samples,
                   av_ts2timestr(frame->pts, &audio_dec_ctx->time_base));
            printf("################################\n");

            /* Write the raw audio data samples of the first plane. This works
             * fine for packed formats (e.g. AV_SAMPLE_FMT_S16). However,
             * most audio decoders output planar audio, which uses a separate
             * plane of audio samples for each channel (e.g. AV_SAMPLE_FMT_S16P).
             * In other words, this code will write only the first audio channel
             * in these cases.
             * You should use libswresample or libavfilter to convert the frame
             * to packed data. */
            // printf("@@@@@@@@@@@@@@@@@@@@@@ AUDIO farme data @@@@@@@@@@@@@@@@@@\n");
            // print_hex(frame->extended_data[0],strlen((char*)frame->extended_data[0]));
            // fwrite(frame->extended_data[0], 1, unpadded_linesize, audio_dst_file);

            
            sample_buf = frame->extended_data[0];

            AudFrame->data[0] = sample_buf;//raw audio data

            // print_hex(AudFrame->data[0],strlen((char*)AudFrame->data[0]));
            
            int got_audio_frame=0;
            //Encode  
            retuu = avcodec_encode_audio2(AudCodecCtx, &AudPkt,AudFrame, &got_audio_frame);  
            if(retuu < 0){  
                printf("Failed to encode!\n");  
                return -1;  
            }  
            if (got_audio_frame==1){  
                printf("audio_frame_count = %d\n", audio_frame_count);
                printf("SUCCESS to encode 1 audio frame! \tsize:%5d\n",AudPkt.size); 
                
                audio_frame_count++;
                AudPkt.stream_index = audio_st->index;  
                ret = av_interleaved_write_frame(AudFormatCtx, &AudPkt);  
                av_free_packet(&AudPkt);  
            } 
        }
    }

    /* If we use the new API with reference counting, we own the data and need
     * to de-reference it when we don't use it anymore */
    if (*got_frame && api_mode == API_MODE_NEW_API_REF_COUNT)
        av_frame_unref(frame);

    return decoded;
}

static int open_codec_context(int *stream_idx,
                              AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(type), src_filename);
        return ret;
    } else {
        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* find decoder for the stream */
        dec_ctx = st->codec;
        dec = avcodec_find_decoder(dec_ctx->codec_id);
        if (!dec) {
            fprintf(stderr, "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Init the decoders, with or without reference counting */
        if (api_mode == API_MODE_NEW_API_REF_COUNT)
            av_dict_set(&opts, "refcounted_frames", "1", 0);
        if ((ret = avcodec_open2(dec_ctx, dec, &opts)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
        *stream_idx = stream_index;
    }

    return 0;
}

static int get_format_from_sample_fmt(const char **fmt,
                                      enum AVSampleFormat sample_fmt)
{
    int i;
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt; const char *fmt_be, *fmt_le;
    } sample_fmt_entries[] = {
        { AV_SAMPLE_FMT_U8,  "u8",    "u8"    },
        { AV_SAMPLE_FMT_S16, "s16be", "s16le" },
        { AV_SAMPLE_FMT_S32, "s32be", "s32le" },
        { AV_SAMPLE_FMT_FLT, "f32be", "f32le" },
        { AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
    };
    *fmt = NULL;

    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }

    fprintf(stderr,
            "sample format %s is not supported as output format\n",
            av_get_sample_fmt_name(sample_fmt));
    return -1;
}

int main (int argc, char **argv)
{
    int ret = 0, got_frame;
    int Audret = 0;

    if (argc != 4 && argc != 5) {
        fprintf(stderr, "usage: %s [-refcount=<old|new_norefcount|new_refcount>] "
                "input_file video_output_file audio_output_file\n"
                "API example program to show how to read frames from an input file.\n"
                "This program reads frames from a file, decodes them, and writes decoded\n"
                "video frames to a rawvideo file named video_output_file, and decoded\n"
                "audio frames to a rawaudio file named audio_output_file.\n\n"
                "If the -refcount option is specified, the program use the\n"
                "reference counting frame system which allows keeping a copy of\n"
                "the data for longer than one decode call. If unset, it's using\n"
                "the classic old method.\n"
                "\n", argv[0]);
        exit(1);
    }
    if (argc == 5) {
        const char *mode = argv[1] + strlen("-refcount=");
        if      (!strcmp(mode, "old"))            api_mode = API_MODE_OLD;
        else if (!strcmp(mode, "new_norefcount")) api_mode = API_MODE_NEW_API_NO_REF_COUNT;
        else if (!strcmp(mode, "new_refcount"))   api_mode = API_MODE_NEW_API_REF_COUNT;
        else {
            fprintf(stderr, "unknow mode '%s'\n", mode);
            exit(1);
        }
        argv++;
    }
    src_filename = argv[1];
    video_dst_filename = argv[2];
    audio_dst_filename = argv[3];

    /* register all formats and codecs */
    av_register_all();
    avformat_network_init();

    //init output format
    pFormatCtx = avformat_alloc_context();
    fmt = av_guess_format(NULL, video_dst_filename, NULL);
    pFormatCtx->oformat = fmt;


    AudFormatCtx = avformat_alloc_context();
    Audofmt = av_guess_format(NULL, audio_dst_filename, NULL);
    AudFormatCtx->oformat = Audofmt;




    //get input format
    /* open input file, and allocate format context */
    if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        exit(1);
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    //get input codec and stream
    if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
        video_stream = fmt_ctx->streams[video_stream_idx];
        video_dec_ctx = video_stream->codec;
        pCodecCtx = video_stream->codec;

        video_dst_file = fopen(video_dst_filename, "wb");
        if (!video_dst_file) {
            fprintf(stderr, "Could not open destination file %s\n", video_dst_filename);
            ret = 1;
            goto end;
        }

        /* allocate image where the decoded image will be put */
        width = video_dec_ctx->width;
        height = video_dec_ctx->height;
        pix_fmt = video_dec_ctx->pix_fmt;
        ret = av_image_alloc(video_dst_data, video_dst_linesize,
                             width, height, pix_fmt, 1);
        if (ret < 0) {
            fprintf(stderr, "Could not allocate raw video buffer\n");
            goto end;
        }
        video_dst_bufsize = ret;
    }

    if (open_codec_context(&audio_stream_idx, fmt_ctx, AVMEDIA_TYPE_AUDIO) >= 0) {
        audio_stream = fmt_ctx->streams[audio_stream_idx];
        audio_dec_ctx = audio_stream->codec;
        AudCodecCtx = audio_stream->codec;
        audio_dst_file = fopen(audio_dst_filename, "wb");
        if (!audio_dst_file) {
            fprintf(stderr, "Could not open destination file %s\n", audio_dst_filename);
            ret = 1;
            goto end;
        }
    }


    //Open output 
    if (avio_open(&pFormatCtx->pb,video_dst_filename, AVIO_FLAG_READ_WRITE) < 0){
        printf("Failed to open output file! \n");
        return -1;
    }

    if (avio_open(&AudFormatCtx->pb,audio_dst_filename, AVIO_FLAG_READ_WRITE) < 0){  
        printf("Failed to open output file!\n");  
        return -1;  
    }  

    

    video_st = avformat_new_stream(pFormatCtx, 0);
    video_st->time_base.num = 1; 
    video_st->time_base.den = 25;  

    if (video_st==NULL){
        return -1;
    }

    audio_st = avformat_new_stream(AudFormatCtx, 0);  
    if (audio_st==NULL){  
        return -1;  
    }

    AudCodecCtx = audio_st->codec;
    AudCodecCtx->codec_id = Audofmt->audio_codec;
    AudCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
    AudCodecCtx->sample_fmt = AV_SAMPLE_FMT_S16;
    AudCodecCtx->sample_rate= 12000;
    AudCodecCtx->channel_layout=AV_CH_LAYOUT_STEREO;
    AudCodecCtx->channels = av_get_channel_layout_nb_channels(AudCodecCtx->channel_layout);
    // AudCodecCtx->bit_rate = 64000; 
    
    // AudCodecCtx->sample_fmt         = audio_dec_ctx->sample_fmt;  
    // AudCodecCtx->sample_rate        = audio_dec_ctx->sample_rate;  
    // AudCodecCtx->channel_layout     = audio_dec_ctx->channel_layout;  
    // AudCodecCtx->channels           = av_get_channel_layout_nb_channels(audio_dec_ctx->channel_layout);  
    AudCodecCtx->bit_rate           = audio_dec_ctx->bit_rate;

    // AudCodecCtx->sample_rate    = select_sample_rate(AudCodec);
    // AudCodecCtx->channel_layout = select_channel_layout(AudCodec);
    // AudCodecCtx->channels       = av_get_channel_layout_nb_channels(AudCodecCtx->channel_layout);  

    AudCodec = avcodec_find_encoder(AudCodecCtx->codec_id);  
    if (!AudCodec){  
        printf("Can not find encoder!\n");  
        return -1;  
    }  
    if (avcodec_open2(AudCodecCtx, AudCodec,NULL) < 0){  
        printf("Failed to open encoder!\n");  
        return -1;  
    } 

    AudFrame = av_frame_alloc();  
    AudFrame->nb_samples     = AudCodecCtx->frame_size;
    AudFrame->format         = AudCodecCtx->sample_fmt;
    // AudFrame->channel_layout = AudCodecCtx->channel_layout;

    aud_buffer_size = av_samples_get_buffer_size(NULL, AudCodecCtx->channels,AudCodecCtx->frame_size,AudCodecCtx->sample_fmt, 1);  
    sample_buf = (uint8_t *)av_malloc(aud_buffer_size);  
    avcodec_fill_audio_frame(AudFrame, AudCodecCtx->channels, AudCodecCtx->sample_fmt,(const uint8_t*)sample_buf, aud_buffer_size, 1);


    ///////////////////////////////////////////////////////////////////
    


    printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
    /* dump input information to stderr */
    av_dump_format(fmt_ctx, 0, src_filename, 0);

    //Param that must set
    pCodecCtx = video_st->codec;
    pCodecCtx->codec_id = fmt->video_codec;
    pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    pCodecCtx->pix_fmt = PIX_FMT_YUV420P;
    pCodecCtx->width = width;  
    pCodecCtx->height = height;
    pCodecCtx->time_base.num = 1;  
    pCodecCtx->time_base.den = 25;  
    pCodecCtx->bit_rate = 400000;  
    pCodecCtx->gop_size=10;
    //H264
    //pCodecCtx->me_range = 16;
    //pCodecCtx->max_qdiff = 4;
    //pCodecCtx->qcompress = 0.6;
    pCodecCtx->qmin = 10;
    pCodecCtx->qmax = 51;

    //Optional Param
    pCodecCtx->max_b_frames=3;

    // Set Option
    AVDictionary *param = 0;
    //H.264
    if(pCodecCtx->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&param, "preset", "slow", 0);
        av_dict_set(&param, "tune", "zerolatency", 0);
        //av_dict_set(&param, "profile", "main", 0);
    }
    //H.265
    if(pCodecCtx->codec_id == AV_CODEC_ID_H265){
        av_dict_set(&param, "preset", "ultrafast", 0);
        av_dict_set(&param, "tune", "zero-latency", 0);
    }

    if (!audio_stream && !video_stream) {
        fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
        ret = 1;
        goto end;
    }

    /* When using the new API, you need to use the libavutil/frame.h API, while
     * the classic frame management is available in libavcodec */
    if (api_mode == API_MODE_OLD)
        frame = avcodec_alloc_frame();
    else
        frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* initialize packet, set data to NULL, let the demuxer fill it */
    // av_init_packet(&pkt);
    // pkt.data = NULL;
    // pkt.size = 0;

    if (video_stream)
        printf("Demuxing video from file '%s' into '%s'\n", src_filename, video_dst_filename);
    if (audio_stream)
        printf("Demuxing audio from file '%s' into '%s'\n", src_filename, audio_dst_filename);


    printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
    //Show some Information
    av_dump_format(pFormatCtx, 0, video_dst_filename, 1);

    printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
    //Show some information  
    av_dump_format(AudFormatCtx, 0, audio_dst_filename, 1);



    pCodec = avcodec_find_encoder(pCodecCtx->codec_id);
    if (!pCodec){
        printf("Can not find encoder! \n");
        return -1;
    }
    if (avcodec_open2(pCodecCtx, pCodec,&param) < 0){
        printf("Failed to open encoder! \n");
        return -1;
    }

    pFrame = av_frame_alloc();
    picture_size = avpicture_get_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);
    picture_buf = (uint8_t *)av_malloc(picture_size);
    avpicture_fill((AVPicture *)pFrame, picture_buf, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);

    printf("picture_buf = %d\n", picture_size);

    //Write video Header
    avformat_write_header(pFormatCtx,NULL);
    //Write audio Header
    avformat_write_header(AudFormatCtx,NULL);
    
    av_new_packet(&AudPkt,aud_buffer_size);  
    av_new_packet(&epkt,picture_size);

    y_size = pCodecCtx->width * pCodecCtx->height;
    pFrame->width = 240;
    pFrame->height = 160;
    pFrame->format = pCodecCtx->pix_fmt;




    /* read frames from the file */
    while (av_read_frame(fmt_ctx, &pkt) >= 0) {
        AVPacket orig_pkt = pkt;
        do {
            ret = decode_packet(&got_frame, 0);
            if (ret < 0)
                break;
            pkt.data += ret;
            pkt.size -= ret;
        } while (pkt.size > 0);
        av_free_packet(&orig_pkt);
    }

    /* flush cached frames */
    pkt.data = NULL;
    pkt.size = 0;
    do {
        decode_packet(&got_frame, 1);
    } while (got_frame);


    //Flush Encoder
    int retfe = flush_encoder(pFormatCtx,0);
    if (retfe < 0) {
        printf("Flushing encoder failed\n");
        return -1;
    }

    //Flush Encoder  
    ret = flush_encoder(pFormatCtx,0);  
    if (ret < 0) {  
        printf("Flushing encoder failed\n");  
        return -1;  
    }  

    //Write video trailer
    av_write_trailer(pFormatCtx);
  
    //Writeaudio Trailer  
    av_write_trailer(AudFormatCtx);  
  
    

    printf("Demuxing succeeded.\n");

    if (video_stream) {
        printf("Play the output video file with the command:\n"
               "ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
               av_get_pix_fmt_name(pix_fmt), width, height,
               video_dst_filename);
    }

    if (audio_stream) {
        enum AVSampleFormat sfmt = audio_dec_ctx->sample_fmt;
        int n_channels = audio_dec_ctx->channels;
        const char *fmt;

        if (av_sample_fmt_is_planar(sfmt)) {
            const char *packed = av_get_sample_fmt_name(sfmt);
            printf("Warning: the sample format the decoder produced is planar "
                   "(%s). This example will output the first channel only.\n",
                   packed ? packed : "?");
            sfmt = av_get_packed_sample_fmt(sfmt);
            n_channels = 1;
        }

        if ((ret = get_format_from_sample_fmt(&fmt, sfmt)) < 0)
            goto end;

        printf("Play the output audio file with the command:\n"
               "ffplay -f %s -ac %d -ar %d %s\n",
               fmt, n_channels, audio_dec_ctx->sample_rate,
               audio_dst_filename);
    }




end:
    //Clean  
    if (audio_st){  
        avcodec_close(audio_st->codec);  
        av_free(AudFrame);  
        av_free(sample_buf);  
    }  
    avio_close(AudFormatCtx->pb);  
    avformat_free_context(AudFormatCtx);

    //Clean
    if (video_st){
        avcodec_close(video_st->codec);
        av_free(pFrame);
        av_free(picture_buf);
    }
    avcodec_close(video_dec_ctx);
    avcodec_close(audio_dec_ctx);
    avformat_close_input(&fmt_ctx);
    if (video_dst_file)
        fclose(video_dst_file);
    if (audio_dst_file)
        fclose(audio_dst_file);
    if (api_mode == API_MODE_OLD)
        avcodec_free_frame(&frame);
    else
        av_frame_free(&frame);
    av_free(video_dst_data[0]);

    return ret < 0;
}
