extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavdevice/avdevice.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
#include "something.h"
#include "defer.h"
#include "fmt/format.h"
#include "ffmpegcv.hpp"


int main(int argc, char* argv[])
{

//    CHECK(argc >= 3);
//
//    const char * in_filename = argv[1];
//    const char * out_filename = argv[2];
    const char * in_filename = "/Users/chenxinfeng/ml-project/ffmpegcv-cpp/examples/input.mp4";
    const char * out_filename = "/Users/chenxinfeng/ml-project/ffmpegcv-cpp/examples/output_filter.mp4";

    AVFormatContext* decoder_fmt_ctx = nullptr;
    CHECK(avformat_open_input(&decoder_fmt_ctx, in_filename, nullptr, nullptr) >= 0);
    defer(avformat_close_input(&decoder_fmt_ctx));

    CHECK(avformat_find_stream_info(decoder_fmt_ctx, nullptr) >= 0);
    int video_stream_idx = av_find_best_stream(decoder_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    CHECK(video_stream_idx >= 0);

    // decoder
    auto decoder = avcodec_find_decoder(decoder_fmt_ctx->streams[video_stream_idx]->codecpar->codec_id);
    CHECK_NOTNULL(decoder);

    // decoder context
    AVCodecContext* decoder_ctx = avcodec_alloc_context3(decoder);
    defer(avcodec_free_context(&decoder_ctx));
    CHECK_NOTNULL(decoder_ctx);

    CHECK(avcodec_parameters_to_context(decoder_ctx, decoder_fmt_ctx->streams[video_stream_idx]->codecpar) >= 0);

    AVDictionary * decoder_options = nullptr;
    av_dict_set(&decoder_options, "threads", "auto", AV_DICT_DONT_OVERWRITE);
    defer(av_dict_free(&decoder_options));

    CHECK(avcodec_open2(decoder_ctx, decoder, &decoder_options) >= 0);

    av_dump_format(decoder_fmt_ctx, 0, in_filename, 0);
    std::cout << fmt::format("[ INPUT] {:>3d}x{:>3d}, fps = {}/{}, tbr = {}/{}, tbc = {}/{}, tbn = {}/{}",
                             decoder_ctx->width, decoder_ctx->height,
                             decoder_fmt_ctx->streams[video_stream_idx]->avg_frame_rate.num, decoder_fmt_ctx->streams[video_stream_idx]->avg_frame_rate.den,
                             decoder_fmt_ctx->streams[video_stream_idx]->r_frame_rate.num, decoder_fmt_ctx->streams[video_stream_idx]->r_frame_rate.den,
                             decoder_ctx->time_base.num, decoder_ctx->time_base.den,
                             decoder_fmt_ctx->streams[video_stream_idx]->time_base.num, decoder_fmt_ctx->streams[video_stream_idx]->time_base.den);

    // filters @{
    AVFilterGraph * filter_graph = avfilter_graph_alloc();
    CHECK_NOTNULL(filter_graph);
    defer(avfilter_graph_free(&filter_graph));

    const AVFilter *buffersrc = avfilter_get_by_name("buffer");
    CHECK_NOTNULL(buffersrc);
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    CHECK_NOTNULL(buffersink);
    const AVFilter *vflip_filter = avfilter_get_by_name("vflip");
    CHECK_NOTNULL(vflip_filter);
    const AVFilter* scale_filter = avfilter_get_by_name("scale");

    AVStream* video_stream = decoder_fmt_ctx->streams[video_stream_idx];
    AVRational fr = av_guess_frame_rate(decoder_fmt_ctx, video_stream, nullptr);
    std::string args = fmt::format("video_size={}x{}:pix_fmt={}:time_base={}/{}:pixel_aspect={}/{}:frame_rate={}/{}",
                                   decoder_ctx->width, decoder_ctx->height, av_get_pix_fmt_name(decoder_ctx->pix_fmt),
                                   video_stream->time_base.num, video_stream->time_base.den,
                                   video_stream->codecpar->sample_aspect_ratio.num, std::max<int>(1, video_stream->codecpar->sample_aspect_ratio.den),
                                   fr.num, fr.den);

    std::cout << "\nbuffersrc args: " << args << std::endl;

    AVFilterContext *src_filter_ctx = nullptr;
    AVFilterContext *sink_filter_ctx = nullptr;
    AVFilterContext *vflip_filter_ctx = nullptr;
    AVFilterContext *scale_filter_ctx = nullptr;

    CHECK(avfilter_graph_create_filter(&src_filter_ctx, buffersrc, "src", args.c_str(), nullptr, filter_graph) >= 0);
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
    CHECK(avfilter_graph_create_filter(&sink_filter_ctx, buffersink, "sink", nullptr, nullptr, filter_graph) >= 0);
    CHECK(av_opt_set_int_list(sink_filter_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN) >= 0);
    CHECK(avfilter_graph_create_filter(&vflip_filter_ctx, vflip_filter, "vflip", nullptr, nullptr, filter_graph) >= 0);
    CHECK(avfilter_graph_create_filter(&scale_filter_ctx, scale_filter, "scale", "320:240", nullptr, filter_graph) >= 0);

    CHECK(avfilter_link(src_filter_ctx, 0, vflip_filter_ctx, 0) == 0);
    CHECK(avfilter_link(vflip_filter_ctx, 0, scale_filter_ctx, 0) == 0);
    CHECK(avfilter_link(scale_filter_ctx, 0, sink_filter_ctx, 0) == 0);

    CHECK(avfilter_graph_config(filter_graph, nullptr) >= 0);
    char * graph = avfilter_graph_dump(filter_graph, nullptr);
    CHECK_NOTNULL(graph);
    std::cout << "\nfilter graph >>>> \n" << graph << std::endl;
    // @}

    std::cout << fmt::format("[FILTER] {:>3d}x{:>3d}, framerate = {}/{}, timebase = {}/{}",
                             av_buffersink_get_w(sink_filter_ctx), av_buffersink_get_h(sink_filter_ctx),
                             av_buffersink_get_frame_rate(sink_filter_ctx).num, av_buffersink_get_frame_rate(sink_filter_ctx).den,
                             av_buffersink_get_time_base(sink_filter_ctx).num, av_buffersink_get_time_base(sink_filter_ctx).den);

    //
    // output
    //

    // encoder codec params
    int out_h = av_buffersink_get_h(sink_filter_ctx);
    int out_w = av_buffersink_get_w(sink_filter_ctx);
    const char* dec_pix_fmt = "rgb24";
    AVPixelFormat dstFormat = av_get_pix_fmt(dec_pix_fmt);

    SwsContext* sws_context = sws_getContext(
            out_w, out_w, decoder_ctx->pix_fmt, // 输入格式
            out_w, out_w, dstFormat,       // 输出格式
            SWS_BILINEAR, nullptr, nullptr, nullptr);
    CHECK_NOTNULL(sws_context);


    AVPacket * in_packet = av_packet_alloc();
    AVFrame * in_frame = av_frame_alloc();
    AVPacket* out_packet = av_packet_alloc();
    AVFrame * filtered_frame = av_frame_alloc();

    AVFrame * rgb_frame = av_frame_alloc();
    rgb_frame->height = out_h;
    rgb_frame->width = out_w;
    rgb_frame->format = dstFormat;
    av_frame_get_buffer(rgb_frame, 0);

    while(av_read_frame(decoder_fmt_ctx, in_packet) >= 0 && decoder_ctx->frame_num < 200) {
        if (in_packet->stream_index != video_stream_idx) {
            continue;
        }

        in_packet->pts -= decoder_fmt_ctx->streams[video_stream_idx]->start_time;

        int ret = avcodec_send_packet(decoder_ctx, in_packet);
        while(ret >= 0) {
            av_frame_unref(in_frame);
            ret = avcodec_receive_frame(decoder_ctx, in_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                fprintf(stderr, "encoder: avcodec_receive_frame() \n");
                return ret;
            }

            // filtering
            if((ret = av_buffersrc_add_frame_flags(src_filter_ctx, in_frame, AV_BUFFERSRC_FLAG_PUSH)) < 0) {
                fprintf(stderr, "av_buffersrc_add_frame_flags()\n");
                break;
            }
            while(ret >= 0) {
                av_frame_unref(filtered_frame);
                ret = av_buffersink_get_frame_flags(sink_filter_ctx, filtered_frame, AV_BUFFERSINK_FLAG_NO_REQUEST);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                else if (ret < 0) {
                    fprintf(stderr, "av_buffersink_get_frame_flags()\n");
                    return ret;
                }

                //
                filtered_frame->pict_type = AV_PICTURE_TYPE_NONE;

                // 将帧转换为 RGB 格式
                sws_scale(sws_context, filtered_frame->data, filtered_frame->linesize,
                          0, filtered_frame->height,
                          rgb_frame->data, rgb_frame->linesize);

                // 打印 RGB 帧数据
                std::cout << "Decoded RGB frame: " << codec_context->width << "x" << codec_context->height
                          << ", Data size: " << rgb_buffer.size() << std::endl;

            }
        }
        av_packet_unref(in_packet);
    }

    av_packet_free(&in_packet);
    av_frame_free(&in_frame);
    av_packet_free(&out_packet);
    av_frame_free(&filtered_frame);

    return 0;
}