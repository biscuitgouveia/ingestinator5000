
#include "ingestinator.h"

int main(int argc, char **argv) {

    EncodingParams AVCi50 = {0};
    AVCi50.videoCodec = AV_CODEC_ID_H264;
    AVCi50.audioCodec = AV_CODEC_ID_PCM_S16LE;
    AVCi50.frameHeight = 1080;
    AVCi50.frameWidth = 1440;
    AVCi50.pixelAspectRatio = (AVRational){4, 3};
    AVCi50.frameRate = (AVRational){25, 1};
    AVCi50.videoPixelFormat = AV_PIX_FMT_YUV420P10LE;
    AVCi50.outputBitRate = 50000000;
    AVCi50.minBitRate = 50000000;
    AVCi50.maxBitRate = 50000000;
    AVCi50.bitstreamBufferSize = 60000000;
    AVCi50.codecPrivKey = "x264-params";
    AVCi50.codecPrivValue = "avcintra-class=50:colorprim=bt709:transfer=bt709:colormatrix=bt709:interlaced=1:force-cfr=1:keyint=1:min-keyint=1:scenecut=0";
    AVCi50.audioSampleRate = 48000;
    AVCi50.audioSampleFormat = AV_SAMPLE_FMT_S16P;
    AVCi50.audioOutputBitRate = 160000;
    AVCi50.profile = FF_PROFILE_H264_HIGH_10_INTRA;
    AVCi50.colourPrimaries = AVCOL_PRI_BT709;
    AVCi50.colourTRC = AVCOL_TRC_BT709;
    AVCi50.colourSpace = AVCOL_SPC_BT709;

    EncodingParams HD422 = {0};
    HD422.videoCodec = AV_CODEC_ID_MPEG2VIDEO;
    HD422.audioCodec = AV_CODEC_ID_PCM_S16LE;
    HD422.frameHeight = 1080;
    HD422.frameWidth = 1920;
    HD422.pixelAspectRatio = (AVRational){1, 1};
    HD422.frameRate = (AVRational){25, 1};
    HD422.videoPixelFormat = AV_PIX_FMT_YUV422P;
    HD422.outputBitRate = 35000000;
    HD422.minBitRate = 35000000;
    HD422.maxBitRate = 35000000;
    HD422.bitstreamBufferSize = 3500000;
    HD422.audioSampleRate = 48000;
    HD422.audioSampleFormat = AV_SAMPLE_FMT_S16;
    HD422.audioOutputBitRate = 160000;
    HD422.profile = FF_PROFILE_MPEG2_422;
    HD422.colourPrimaries = AVCOL_PRI_BT709;
    HD422.colourTRC = AVCOL_TRC_BT709;
    HD422.colourSpace = AVCOL_SPC_BT709;

    EncodingParams *paramsPointer = &HD422;

    int ret;
    AVPacket *packet = NULL;
    unsigned int streamIndex;
    unsigned int i;

    if (argc != 3) {
        av_log(NULL, AV_LOG_ERROR, "Usage: %s <input file> <output file>\n", argv[0]);
        return 1;
    }

    if ((ret = open_input_file(argv[1])) < 0)
        goto end;
    if ((ret = open_output_file(argv[2], paramsPointer)) < 0)
        goto end;
    if ((ret = init_filters(paramsPointer)) < 0)
        goto end;
    if (!(packet = av_packet_alloc()))
        goto end;

    /* read all packets */
    while (1) {
        if ((ret = av_read_frame(inputFormatContext, packet)) < 0)
            break;
        streamIndex = packet->stream_index;
        av_log(NULL, AV_LOG_DEBUG, "Demuxer gave frame of streamIndex %u\n",
               streamIndex);

        if (filterContext[streamIndex].filterGraph) {
            StreamContext *stream = &streamContext[streamIndex];

            av_log(NULL, AV_LOG_DEBUG, "Going to reencode&filter the frame\n");

            av_packet_rescale_ts(packet,
                                 inputFormatContext->streams[streamIndex]->time_base,
                                 stream->decodeContext->time_base);
            ret = avcodec_send_packet(stream->decodeContext, packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(stream->decodeContext, stream->decodeFrame);
                if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                    break;
                else if (ret < 0)
                    goto end;

                stream->decodeFrame->pts = stream->decodeFrame->best_effort_timestamp;
                ret = filter_encode_write_frame(stream->decodeFrame, streamIndex);
                if (ret < 0)
                    goto end;
            }
        } else {
            /* remux this frame without reencoding */
            av_packet_rescale_ts(packet,
                                 inputFormatContext->streams[streamIndex]->time_base,
                                 outputFormatContext->streams[streamIndex]->time_base);

            ret = av_interleaved_write_frame(outputFormatContext, packet);
            if (ret < 0)
                goto end;
        }
        av_packet_unref(packet);
    }

    /* flush filters and encoders */
    for (i = 0; i < inputFormatContext->nb_streams; i++) {
        /* flush filter */
        if (!filterContext[i].filterGraph)
            continue;
        ret = filter_encode_write_frame(NULL, i);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing filter failed\n");
            goto end;
        }

        /* flush encoder */
        ret = flush_encoder(i);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing encoder failed\n");
            goto end;
        }
    }

    av_write_trailer(outputFormatContext);
    end:
    av_packet_free(&packet);
    for (i = 0; i < inputFormatContext->nb_streams; i++) {
        avcodec_free_context(&streamContext[i].decodeContext);
        if (outputFormatContext && outputFormatContext->nb_streams > i && outputFormatContext->streams[i] && streamContext[i].encodeContext)
            avcodec_free_context(&streamContext[i].encodeContext);
        if (filterContext && filterContext[i].filterGraph) {
            avfilter_graph_free(&filterContext[i].filterGraph);
            av_packet_free(&filterContext[i].encodePacket);
            av_frame_free(&filterContext[i].filteredFrame);
        }

        av_frame_free(&streamContext[i].decodeFrame);
    }
    av_free(filterContext);
    av_free(streamContext);
    avformat_close_input(&inputFormatContext);
    if (outputFormatContext && !(outputFormatContext->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outputFormatContext->pb);
    avformat_free_context(outputFormatContext);

    if (ret < 0)
        av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));

    return ret ? 1 : 0;
}