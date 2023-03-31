//
// Created by Ethan Gouveia on 30/3/2023.
//

#include "ingestinatorFunctions.h"

//AVFormatContext *inputFormatContext;
//AVFormatContext *outputFormatContext;

int open_input_file(char *inputFilename) {
    int ret;
    unsigned int i;
    inputFormatContext = NULL;

    if ((ret = avformat_open_input(&inputFormatContext, inputFilename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(inputFormatContext, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    streamContext = av_calloc(inputFormatContext->nb_streams, sizeof(*streamContext));
    if (!streamContext)
        return AVERROR(ENOMEM);

    for (i = 0; i < inputFormatContext->nb_streams; i++) {
        AVStream *stream = inputFormatContext->streams[i];
        const AVCodec *dec = avcodec_find_decoder(stream->codecpar->codec_id);
        AVCodecContext *codecContext;
        if (!dec) {
            av_log(NULL, AV_LOG_ERROR, "Failed to find decoder for stream #%u\n", i);
            return AVERROR_DECODER_NOT_FOUND;
        }
        codecContext = avcodec_alloc_context3(dec);
        if (!codecContext) {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate the decoder context for stream #%u\n", i);
            return AVERROR(ENOMEM);
        }
        ret = avcodec_parameters_to_context(codecContext, stream->codecpar);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to copy decoder parameters to input decoder context "
                                       "for stream #%u\n", i);
            return ret;
        }
        /* Re-encode video & audio and re-mux subtitles etc. */
        if (codecContext->codec_type == AVMEDIA_TYPE_VIDEO
            || codecContext->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (codecContext->codec_type == AVMEDIA_TYPE_VIDEO)
                codecContext->framerate = av_guess_frame_rate(inputFormatContext, stream, NULL);
            /* Open decoder */
            ret = avcodec_open2(codecContext, dec, NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", i);
                return ret;
            }
        }
        streamContext[i].decodeContext = codecContext;

        streamContext[i].decodeFrame = av_frame_alloc();
        if (!streamContext[i].decodeFrame)
            return AVERROR(ENOMEM);
    }

    av_dump_format(inputFormatContext, 0, inputFilename, 0);
    return 0;
}


int open_output_file(const char *filename, EncodingParams *encodingPreset) {
    AVStream *outputStream;
    AVStream *inputStream;
    AVCodecContext *decoderContext, *encoderContext;
    const AVCodec *encoder;
    int ret;
    unsigned int i;

    outputFormatContext = NULL;
    avformat_alloc_output_context2(&outputFormatContext, NULL, NULL, filename);
    if (!outputFormatContext) {
        av_log(NULL, AV_LOG_ERROR, "Could not create output context\n");
        return AVERROR_UNKNOWN;
    }


    for (i = 0; i < inputFormatContext->nb_streams; i++) {
        outputStream = avformat_new_stream(outputFormatContext, NULL);
        if (!outputStream) {
            av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
            return AVERROR_UNKNOWN;
        }

        inputStream = (AVStream *) &inputFormatContext->streams[i];
        decoderContext = streamContext[i].decodeContext;

        if (decoderContext->codec_type == AVMEDIA_TYPE_VIDEO
            || decoderContext->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* In this example, we transcode to same properties (picture size,
             * sample rate etc.). These properties can be changed for output
             * streams easily using filters */
            if (decoderContext->codec_type == AVMEDIA_TYPE_VIDEO) {
                encoder = avcodec_find_encoder(encodingPreset->videoCodec);
                if (!encoder) {
                    av_log(NULL, AV_LOG_FATAL, "Necessary encoder not found\n");
                    return AVERROR_INVALIDDATA;
                }
                encoderContext = avcodec_alloc_context3(encoder);
                if (!encoderContext) {
                    av_log(NULL, AV_LOG_FATAL, "Failed to allocate the encoder context\n");
                    return AVERROR(ENOMEM);
                }
                encoderContext->height = encodingPreset->frameHeight;
                encoderContext->width = encodingPreset->frameWidth;
                encoderContext->sample_aspect_ratio = encodingPreset->pixelAspectRatio;
                encoderContext->pix_fmt = encodingPreset->videoPixelFormat;
                encoderContext->time_base = av_inv_q(encodingPreset->frameRate);
                encoderContext->sample_aspect_ratio = encodingPreset->pixelAspectRatio;
                encoderContext->bit_rate = encodingPreset->outputBitRate;
                encoderContext->rc_buffer_size = encodingPreset->bitstreamBufferSize;
                encoderContext->rc_max_rate = encodingPreset->maxBitRate;
                encoderContext->rc_min_rate = encodingPreset->maxBitRate;
                encoderContext->flags += AV_CODEC_FLAG_INTERLACED_ME;
                encoderContext->flags += AV_CODEC_FLAG_INTERLACED_DCT;
                encoderContext->profile = encodingPreset->profile;
                encoderContext->colorspace = encodingPreset->colourSpace;
                encoderContext->color_primaries = encodingPreset->colourPrimaries;
                encoderContext->color_trc = encodingPreset->colourTRC;
                if (encodingPreset->codecPrivKey) {
                    av_opt_set(encoderContext->priv_data, encodingPreset->codecPrivKey, encodingPreset->codecPrivValue,
                               0);
                }
            } else {
                encoder = avcodec_find_encoder(encodingPreset->audioCodec);
                if (!encoder) {
                    av_log(NULL, AV_LOG_FATAL, "Necessary encoder not found\n");
                    return AVERROR_INVALIDDATA;
                }
                encoderContext = avcodec_alloc_context3(encoder);
                if (!encoderContext) {
                    av_log(NULL, AV_LOG_FATAL, "Failed to allocate the encoder context\n");
                    return AVERROR(ENOMEM);
                }
                encoderContext->sample_rate = encodingPreset->audioSampleRate;
                ret = av_channel_layout_copy(&encoderContext->ch_layout, &decoderContext->ch_layout);
                if (ret < 0)
                    return ret;
                /* take first format from list of supported formats */
                encoderContext->bits_per_raw_sample = encodingPreset->audioOutputBitRate;
                encoderContext->sample_fmt = encodingPreset->audioSampleFormat;
                encoderContext->time_base = (AVRational){1, encoderContext->sample_rate};
            }

            if (outputFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
                encoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            /* Third parameter can be used to pass settings to encoder */
            ret = avcodec_open2(encoderContext, encoder, NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream #%u\n", i);
                return ret;
            }
            ret = avcodec_parameters_from_context(outputStream->codecpar, encoderContext);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Failed to copy encoder parameters to output stream #%u\n", i);
                return ret;
            }

            outputStream->time_base = encoderContext->time_base;
            streamContext[i].encodeContext = encoderContext;
        } else if (decoderContext->codec_type == AVMEDIA_TYPE_UNKNOWN) {
            av_log(NULL, AV_LOG_FATAL, "Elementary stream #%d is of unknown type, cannot proceed\n", i);
            return AVERROR_INVALIDDATA;
        } else {
            /* if this stream must be remuxed */
            ret = avcodec_parameters_copy(outputStream->codecpar, inputStream->codecpar);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Copying parameters for stream #%u failed\n", i);
                return ret;
            }
            outputStream->time_base = inputStream->time_base;
        }

    }
    av_dump_format(outputFormatContext, 0, filename, 1);

    if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outputFormatContext->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s'", filename);
            return ret;
        }
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(outputFormatContext, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
        return ret;
    }

    return 0;
}


int init_filter(FilteringContext *filterContext, AVCodecContext *decodeContext, AVCodecContext *encodeContext, const char *filterSpec) {
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc = NULL;
    const AVFilter *buffersink = NULL;
    AVFilterContext *buffersrcContext = NULL;
    AVFilterContext *buffersinkContext = NULL;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVFilterGraph *filterGraph = avfilter_graph_alloc();

    if (!outputs || !inputs || !filterGraph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (decodeContext->codec_type == AVMEDIA_TYPE_VIDEO) {
        buffersrc = avfilter_get_by_name("buffer");
        buffersink = avfilter_get_by_name("buffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        snprintf(args, sizeof(args),
                 "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                 decodeContext->width, decodeContext->height, decodeContext->pix_fmt,
                 decodeContext->time_base.num, decodeContext->time_base.den,
                 decodeContext->sample_aspect_ratio.num,
                 decodeContext->sample_aspect_ratio.den);

        ret = avfilter_graph_create_filter(&buffersrcContext, buffersrc, "in",
                                           args, NULL, filterGraph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersinkContext, buffersink, "out",
                                           NULL, NULL, filterGraph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersinkContext, "pix_fmts",
                             (uint8_t*)&encodeContext->pix_fmt, sizeof(encodeContext->pix_fmt),
                             AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
            goto end;
        }
    } else if (decodeContext->codec_type == AVMEDIA_TYPE_AUDIO) {
        char buf[64];
        buffersrc = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        if (decodeContext->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&decodeContext->ch_layout, decodeContext->ch_layout.nb_channels);
        av_channel_layout_describe(&decodeContext->ch_layout, buf, sizeof(buf));
        snprintf(args, sizeof(args),
                 "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                 decodeContext->time_base.num, decodeContext->time_base.den, decodeContext->sample_rate,
                 av_get_sample_fmt_name(decodeContext->sample_fmt),
                 buf);
        ret = avfilter_graph_create_filter(&buffersrcContext, buffersrc, "in",
                                           args, NULL, filterGraph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersinkContext, buffersink, "out",
                                           NULL, NULL, filterGraph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersinkContext, "sample_fmts",
                             (uint8_t*)&encodeContext->sample_fmt, sizeof(encodeContext->sample_fmt),
                             AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
            goto end;
        }

        av_channel_layout_describe(&encodeContext->ch_layout, buf, sizeof(buf));
        ret = av_opt_set(buffersinkContext, "ch_layouts",
                         buf, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersinkContext, "sample_rates",
                             (uint8_t*)&encodeContext->sample_rate, sizeof(encodeContext->sample_rate),
                             AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
            goto end;
        }
    } else {
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    /* Endpoints for the filter graph. */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrcContext;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersinkContext;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if (!outputs->name || !inputs->name) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if ((ret = avfilter_graph_parse_ptr(filterGraph, filterSpec,
                                        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filterGraph, NULL)) < 0)
        goto end;

    /* Fill FilteringContext */
    filterContext->buffersrcContext = buffersrcContext;
    filterContext->buffersinkContext = buffersinkContext;
    filterContext->filterGraph = filterGraph;

    end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}


int init_filters(EncodingParams *encodingPreset) {
    const char *filterSpec;
    unsigned int i;
    int ret;
    filterContext = av_malloc_array(inputFormatContext->nb_streams, sizeof(*filterContext));
    if (!filterContext)
        return AVERROR(ENOMEM);

    for (i = 0; i < inputFormatContext->nb_streams; i++) {
        filterContext[i].buffersrcContext  = NULL;
        filterContext[i].buffersinkContext = NULL;
        filterContext[i].filterGraph   = NULL;
        if (!(inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO
              || inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO))
            continue;


        if (inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            filterSpec = "scale=w=1920:h=1080";
        } else {
            filterSpec = "aformat=sample_fmts=s16:sample_rates=48000"; /* passthrough (dummy) filter for audio */
        }
        ret = init_filter(&filterContext[i], streamContext[i].decodeContext,
                          streamContext[i].encodeContext, filterSpec);
        if (ret)
            return ret;

        filterContext[i].encodePacket = av_packet_alloc();
        if (!filterContext[i].encodePacket)
            return AVERROR(ENOMEM);

        filterContext[i].filteredFrame = av_frame_alloc();
        if (!filterContext[i].filteredFrame)
            return AVERROR(ENOMEM);
    }
    return 0;
}


int encode_write_frame(unsigned int streamIndex, int flush) {
    StreamContext *stream = &streamContext[streamIndex];
    FilteringContext *filter = &filterContext[streamIndex];
    AVFrame *filt_frame = flush ? NULL : filter->filteredFrame;
    AVPacket *enc_pkt = filter->encodePacket;
    int ret;

    av_log(NULL, AV_LOG_INFO, "Encoding frame\n");
    /* encode filtered frame */
    av_packet_unref(enc_pkt);

    ret = avcodec_send_frame(stream->encodeContext, filt_frame);

    if (ret < 0)
        return ret;

    while (ret >= 0) {
        ret = avcodec_receive_packet(stream->encodeContext, enc_pkt);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;

        /* prepare packet for muxing */
        enc_pkt->stream_index = streamIndex;
        av_packet_rescale_ts(enc_pkt,
                             stream->encodeContext->time_base,
                             outputFormatContext->streams[streamIndex]->time_base);

        av_log(NULL, AV_LOG_DEBUG, "Muxing frame\n");
        /* mux encoded frame */
        ret = av_interleaved_write_frame(outputFormatContext, enc_pkt);
    }

    return ret;
}


int filter_encode_write_frame(AVFrame *frame, unsigned int streamIndex)
{
    FilteringContext *filter = &filterContext[streamIndex];
    int ret;

    av_log(NULL, AV_LOG_INFO, "Pushing decoded frame to filters\n");
    /* push the decoded frame into the filtergraph */
    ret = av_buffersrc_add_frame_flags(filter->buffersrcContext,
                                       frame, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
        return ret;
    }

    /* pull filtered frames from the filtergraph */
    while (1) {
        av_log(NULL, AV_LOG_INFO, "Pulling filtered frame from filters\n");
        ret = av_buffersink_get_frame(filter->buffersinkContext,
                                      filter->filteredFrame);
        if (ret < 0) {
            /* if no more frames for output - returns AVERROR(EAGAIN)
             * if flushed and no more frames for output - returns AVERROR_EOF
             * rewrite retcode to 0 to show it as normal procedure completion
             */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            break;
        }

        filter->filteredFrame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = encode_write_frame(streamIndex, 0);
        av_frame_unref(filter->filteredFrame);
        if (ret < 0)
            break;
    }

    return ret;
}


int flush_encoder(unsigned int streamIndex) {
    if (!(streamContext[streamIndex].encodeContext->codec->capabilities &
          AV_CODEC_CAP_DELAY))
        return 0;

    av_log(NULL, AV_LOG_INFO, "Flushing stream #%u encoder\n", streamIndex);
    return encode_write_frame(streamIndex, 1);
}