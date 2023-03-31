//
// Created by Ethan Gouveia on 30/3/2023.
//

#ifndef FFMPEGTESTBED_INGESTINATORFUNCTIONS_H
#define FFMPEGTESTBED_INGESTINATORFUNCTIONS_H

#endif //FFMPEGTESTBED_INGESTINATORFUNCTIONS_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include "libavutil/md5.h"
#include "libavutil/mem.h"
#include <stdio.h>
#include <stdlib.h>

AVFormatContext *inputFormatContext;
AVFormatContext *outputFormatContext;

typedef struct FilteringContext {
    AVFilterContext *buffersinkContext;
    AVFilterContext *buffersrcContext;
    AVFilterGraph *filterGraph;

    AVPacket *encodePacket;
    AVFrame *filteredFrame;
} FilteringContext;
FilteringContext *filterContext;


typedef struct StreamContext {
    AVCodecContext *decodeContext;
    AVCodecContext *encodeContext;

    AVFrame *decodeFrame;
} StreamContext;
StreamContext *streamContext;

typedef struct EncodingParams {
    int copyVideo;
    int copyAudio;
    char outputExtension;
    char *muxerOptKey;
    char *muxerOptValue;
    enum AVCodecID videoCodec;
    enum AVCodecID audioCodec;
    int audioStreams;
    int audioChannels;
    int audioSampleRate;
    enum AVSampleFormat audioSampleFormat;
    int audioOutputBitRate;
    AVChannelLayout audioOutputChannelLayout;
    char *codecPrivKey;
    char *codecPrivValue;
    int frameHeight;
    int frameWidth;
    int outputBitRate;
    int bitstreamBufferSize;
    int minBitRate;
    int maxBitRate;
    AVRational pixelAspectRatio;
    AVRational frameRate;
    enum AVPixelFormat videoPixelFormat;
    int profile;
    enum AVColorSpace colourSpace;
    enum AVColorPrimaries colourPrimaries;
    enum AVColorTransferCharacteristic colourTRC;
} EncodingParams;


int open_input_file(char *inputFilename);

int open_output_file(const char *filename, EncodingParams *encodingPreset);

int init_filter(FilteringContext *filterContext, AVCodecContext *decodeContext, AVCodecContext *encodeContext, const char *filterSpec);

int init_filters(EncodingParams *encodingPreset);

int encode_write_frame(unsigned int streamIndex, int flush);

int filter_encode_write_frame(AVFrame *frame, unsigned int streamIndex);

int flush_encoder(unsigned int streamIndex);