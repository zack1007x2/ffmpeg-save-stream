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
#include <libswresample/swresample.h>

static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL, *audio_dec_ctx;
static int width, height;
static enum AVPixelFormat pix_fmt;
static AVStream *video_stream = NULL, *audio_stream = NULL;
static const char *src_filename = NULL;
static const char *video_dst_filename = NULL;
static const char *audio_dst_filename = NULL;
static const char *out_filename = NULL;
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
SwrContext *swr;

static AVOutputFormat *ofmt = NULL;
static AVFormatContext *ofmt_ctx = NULL;


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


static int decode_packet(int *got_frame)
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
            
            int got_picture=0;
            
            
            //Encode
            ret = avcodec_encode_video2(pCodecCtx, &epkt,pFrame, &got_picture);
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
        int retuu;
        
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
            
            ret = swr_convert(swr,
                              AudFrame->data, frame->nb_samples,
                              (const uint8_t **)frame->extended_data, frame->nb_samples);
            if (ret < 0) {
                fprintf(stderr, "Error while converting\n");
                exit(1);
            }
            
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
        
        if ((ret = avcodec_open2(dec_ctx, dec, &opts)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
        *stream_idx = stream_index;
    }
    
    return 0;
}


static int init_video_out_context(){
    //init output format
    pFormatCtx = avformat_alloc_context();
    fmt = av_guess_format(NULL, video_dst_filename, NULL);
    pFormatCtx->oformat = fmt;
    
    //Open output
    if (avio_open(&pFormatCtx->pb,video_dst_filename, AVIO_FLAG_READ_WRITE) < 0){
        printf("Failed to open output file! \n");
        return 1;
    }
    
    video_st = avformat_new_stream(pFormatCtx, 0);
    video_st->time_base.num = 1;
    video_st->time_base.den = 25;
    
    if (video_st==NULL){
        return 1;
    }
    //Param that must set
    pCodecCtx = video_st->codec;
    pCodecCtx->codec_id = fmt->video_codec;
    pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    pCodecCtx->pix_fmt = PIX_FMT_YUV420P;
    pCodecCtx->width = width;
    pCodecCtx->height = height;
    pCodecCtx->time_base.num = 1;
    pCodecCtx->time_base.den = 24;
    // pCodecCtx->time_base = video_dec_ctx->time_base;
    pCodecCtx->bit_rate = video_dec_ctx->bit_rate;
    pCodecCtx->gop_size=10;
    //H264
    //pCodecCtx->me_range = 16;
    //pCodecCtx->max_qdiff = 4;
    //pCodecCtx->qcompress = 0.6;
    pCodecCtx->qmin = 10;
    pCodecCtx->qmax = 51;
    
    //Optional Param
    pCodecCtx->max_b_frames=video_dec_ctx->max_b_frames;
    
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
    
    //Show some Information
    av_dump_format(pFormatCtx, 0, video_dst_filename, 1);
    
    pCodec = avcodec_find_encoder(pCodecCtx->codec_id);
    if (!pCodec){
        printf("Can not find encoder! \n");
        return 1;
    }
    if (avcodec_open2(pCodecCtx, pCodec,&param) < 0){
        printf("Failed to open encoder! \n");
        return 1;
    }
    
    y_size = pCodecCtx->width * pCodecCtx->height;
    
    pFrame = av_frame_alloc();
    pFrame->width = 240;
    pFrame->height = 160;
    pFrame->format = pCodecCtx->pix_fmt;
    
    picture_size = avpicture_get_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);
    picture_buf = (uint8_t *)av_malloc(picture_size);
    avpicture_fill((AVPicture *)pFrame, picture_buf, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);
    
    return 0;
}

static int init_audio_out_context(uint8_t *sample_buf){

    int aud_buffer_size;
    

    AudFormatCtx = avformat_alloc_context();
    Audofmt = av_guess_format(NULL, audio_dst_filename, NULL);
    AudFormatCtx->oformat = Audofmt;
    
    if (avio_open(&AudFormatCtx->pb,audio_dst_filename, AVIO_FLAG_READ_WRITE) < 0){
        printf("Failed to open output file!\n");
        return 1;
    }
    
    audio_st = avformat_new_stream(AudFormatCtx, 0);
    if (audio_st==NULL){
        return 1;
    }
    
    
    AudCodecCtx = audio_st->codec;
    AudCodecCtx->codec_id = Audofmt->audio_codec;
    AudCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
    AudCodecCtx->sample_fmt = AV_SAMPLE_FMT_S16;
    AudCodecCtx->sample_rate= audio_dec_ctx->sample_rate;
    AudCodecCtx->channel_layout=AV_CH_LAYOUT_STEREO;
    AudCodecCtx->channels = av_get_channel_layout_nb_channels(AudCodecCtx->channel_layout);
    AudCodecCtx->bit_rate           = audio_dec_ctx->bit_rate;
    AudCodec = avcodec_find_encoder(AudCodecCtx->codec_id);
    if (!AudCodec){
        printf("Can not find encoder!\n");
        return 1;
    }
    if (avcodec_open2(AudCodecCtx, AudCodec,NULL) < 0){
        printf("Failed to open encoder!\n");
        return 1;
    }
    
    //Show some information
    av_dump_format(AudFormatCtx, 0, audio_dst_filename, 1);
    
    AudFrame = av_frame_alloc();
    AudFrame->nb_samples     = AudCodecCtx->frame_size;
    AudFrame->format         = AudCodecCtx->sample_fmt;
    // AudFrame->channel_layout = AudCodecCtx->channel_layout;
    
    aud_buffer_size = av_samples_get_buffer_size(NULL, AudCodecCtx->channels,AudCodecCtx->frame_size,AudCodecCtx->sample_fmt, 1);
    sample_buf = (uint8_t *)av_malloc(aud_buffer_size);
    avcodec_fill_audio_frame(AudFrame, AudCodecCtx->channels, AudCodecCtx->sample_fmt,(const uint8_t*)sample_buf, aud_buffer_size, 1);
    av_new_packet(&AudPkt,aud_buffer_size);
    
    return 0;
}

static int init_input(){
    int ret;
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
            return 1;
        }
        
        /* allocate image where the decoded image will be put */
        width = video_dec_ctx->width;
        height = video_dec_ctx->height;
        pix_fmt = video_dec_ctx->pix_fmt;
        ret = av_image_alloc(video_dst_data, video_dst_linesize,
                             width, height, pix_fmt, 1);
        if (ret < 0) {
            fprintf(stderr, "Could not allocate raw video buffer\n");
            return 1;
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
            return 1;
        }
    }
    
    
    /* dump input information to stderr */
    av_dump_format(fmt_ctx, 0, src_filename, 0);
    return 0;
    
}

static int init_output(){
    int ret;
    //output
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
    if (!ofmt_ctx) {
        printf( "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        return 1;
    }
    ofmt = ofmt_ctx->oformat;
    
    if (!audio_stream && !video_stream) {
        fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
        ret = 1;
        return 1;
    }
    
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        
        AVStream *in_stream = fmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
        if (!out_stream) {
            printf( "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            return 1;
        }
        //copy codec_context from input
        ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
        if (ret < 0) {
            printf( "Failed to copy context from input to output stream codec context\n");
            return 1;
        }
        out_stream->codec->codec_tag = 0;
        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }
    
    printf("==========Output Information==========\n");
    av_dump_format(ofmt_ctx, 0, out_filename, 1);
    printf("======================================\n");
    return 0;
}


int remux_packet(AVFormatContext *ofmt_ctx,AVPacket *pkt){
    int ret;
    //figure out audio stream or video stream to rescale
    AVStream *in_stream, *out_stream;
    in_stream  = fmt_ctx->streams[pkt->stream_index];
    out_stream = ofmt_ctx->streams[pkt->stream_index];

    
    //convert pts,dts
    pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
    pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
    pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
    pkt->pos = -1;
    //remux into certain format video
    ret = av_interleaved_write_frame(ofmt_ctx, pkt);
    return 0;
}

int main (int argc, char **argv){
    int ret = 0, got_frame;



    uint8_t *sample_buf;
    
    
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "input  1.source file:%s\n"
                "2.output_video\n"
                "3.output_audio\n"
                "4.mux video file(Optional)\n"
                "\n", argv[0]);
        exit(1);
    }
    
    src_filename = argv[1];
    video_dst_filename = argv[2];
    audio_dst_filename = argv[3];
    //optional mux to any type video
    if(argc == 5){
        out_filename = argv[4];
    }
    
    /* register all formats and codecs */
    av_register_all();
    //for network stream
    avformat_network_init();
    
    ret = init_input();
    if(ret){
        goto end;
    }
    ret = init_video_out_context();
    if(ret){
        goto end;
    }
    ret = init_audio_out_context(sample_buf);
    if(ret){
        goto end;
    }
    
    if(argc == 5){
        ret = init_output();
        if(ret){
            goto end;
        }
    }
    
    
    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            printf( "Could not open output file '%s'", out_filename);
            goto end;
        }
    }
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        printf( "Error occurred when opening output file\n");
        goto end;
    }
    
    //this will fill up by decoder(|read frame|->packet->|decoder|->frame)
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }
    
    if (video_stream)
        printf("Demuxing video from file '%s' into '%s'\n", src_filename, video_dst_filename);
    if (audio_stream)
        printf("Demuxing audio from file '%s' into '%s'\n", src_filename, audio_dst_filename);
    
    
    //Write video Header
    avformat_write_header(pFormatCtx,NULL);
    //Write audio Header
    avformat_write_header(AudFormatCtx,NULL);
    
    //alloc packet to get copy from pkt
    
    av_new_packet(&epkt,picture_size);
    
    /*setup the convert parameter
     *due to input sample format AV_SAMPLE_FMT_FLTP
     *can't be converted to AV_SAMPLE_FMT_S16
     *which only accepted by the aac encoder
     */
    swr = swr_alloc();
    av_opt_set_int(swr, "in_channel_layout",  audio_dec_ctx->channel_layout, 0);
    av_opt_set_int(swr, "out_channel_layout", AudCodecCtx->channel_layout,  0);
    av_opt_set_int(swr, "in_sample_rate",     audio_dec_ctx->sample_rate, 0);
    av_opt_set_int(swr, "out_sample_rate",    AudCodecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt",  AV_SAMPLE_FMT_FLTP, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16,  0);
    swr_init(swr);
    
    
    
    
    /*start read frames from the file */
    while (av_read_frame(fmt_ctx, &pkt) >= 0) {
        //do demux & decode -> encode -> output h264 & aac file
        ret = decode_packet(&got_frame);
        if (ret < 0)
            break;
        if(argc == 5){
            printf("***************************\n");
            remux_packet(ofmt_ctx,&pkt);
        }
        
        av_free_packet(&pkt);
    }
    
    /* flush cached frames */
    pkt.data = NULL;
    pkt.size = 0;
    
    
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
    
    //Write audio Trailer
    av_write_trailer(AudFormatCtx);
    
    //Write remux Trailer
    if(argc == 5){
        av_write_trailer(ofmt_ctx);
    }
    
    
    printf("Output succeeded!!!!\n");
    
    
    
    
    
    
    
    
    
end:
    //free remux
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_close(ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    
    //free audio
    if (audio_st){
        avcodec_close(audio_st->codec);
        av_free(AudFrame);
        av_free(sample_buf);
    }
    avio_close(AudFormatCtx->pb);
    avformat_free_context(AudFormatCtx);
    
    //free video
    if (video_st){
        avcodec_close(video_st->codec);
        av_free(pFrame);
        av_free(picture_buf);
    }
    avio_close(pFormatCtx->pb);  
    avformat_free_context(pFormatCtx);
    
    //free decode
    avcodec_close(video_dec_ctx);
    avcodec_close(audio_dec_ctx);
    avformat_close_input(&fmt_ctx);
    if (video_dst_file)
        fclose(video_dst_file);
    if (audio_dst_file)
        fclose(audio_dst_file);    
    av_frame_free(&frame);
    return ret < 0;
}
