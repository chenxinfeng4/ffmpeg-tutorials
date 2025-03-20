#include <iostream>
#include <string>
#include "something.h"
#include "ffmpegcv.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
}

std::tuple<Size_wh, Size_wh, std::string> get_videofilter_cpu(
        Size_wh originsize, std::string pix_fmt, std::tuple<int, int, int, int> crop_xywh, Size_wh resize) {
    static const std::vector<std::string> allowed_pix_fmts = {"rgb24", "bgr24", "yuv420p", "yuvj420p", "nv12", "gray"};
    assert(std::find(allowed_pix_fmts.begin(), allowed_pix_fmts.end(), pix_fmt) != allowed_pix_fmts.end());
    int origin_width = originsize.width;
    int origin_height = originsize.height;
    int crop_x = std::get<0>(crop_xywh);
    int crop_y = std::get<1>(crop_xywh);
    int crop_w = std::get<2>(crop_xywh);
    int crop_h = std::get<3>(crop_xywh);
    int resize_width = resize.width;
    int resize_height = resize.height;

    std::string cropopt;
    if (crop_w != 0 && crop_h != 0) {
        assert(crop_x % 2 == 0 && crop_y % 2 == 0 && crop_w % 2 == 0 && crop_h % 2 == 0);
        assert(crop_w <= origin_width && crop_h <= origin_height);
        cropopt = "crop=" + std::to_string(crop_w) + ":" + std::to_string(crop_h) +
                  ":" + std::to_string(crop_x) + ":" + std::to_string(crop_y);
    } else {
        crop_w = origin_width;
        crop_h = origin_height;
        cropopt = "";
    }
    Size_wh cropsize = {crop_w, crop_h};
    Size_wh final_size_wh = cropsize;

    std::string scaleopt="";
    std::string padopt="";
    if (!resize.empty() && (resize_width != 0 || resize_height != 0)) {
        assert (resize_width % 2 == 0 && resize_height % 2 == 0);
        final_size_wh = resize;
        scaleopt = "scale=" + std::to_string(resize_width) + "x" + std::to_string(resize_height);
    }

    std::string pix_fmt_opt = (pix_fmt == "gray") ? "extractplanes=y" : "";
    std::string filterstr = "";
    if (!cropopt.empty() || !scaleopt.empty() || !pix_fmt_opt.empty()) {
        filterstr = "-vf ";
        if (!cropopt.empty()) filterstr += cropopt + ",";
        if (!scaleopt.empty()) filterstr += scaleopt + ",";
        if (!pix_fmt_opt.empty()) filterstr += pix_fmt_opt + ",";
        filterstr = filterstr.substr(0, filterstr.size() - 1);
    }
    return std::make_tuple(cropsize, final_size_wh, filterstr);
}

std::vector<int> get_outnumpyshape(Size_wh size_wh, std::string pix_fmt) {
    if (pix_fmt == "bgr24" || pix_fmt == "rgb24") {
        return {size_wh.height, size_wh.width, 3};
    } else if (pix_fmt == "gray") {
        return {size_wh.height, size_wh.width};
    } else if (pix_fmt == "yuv420p" || pix_fmt == "yuvj420p" || pix_fmt == "nv12") {
        return {size_wh.height * 3 / 2, size_wh.width};
    } else {
        assert(false && "pix_fmt not supported");
        return {0, 0};
    }
}


class FFmpegVideoCapture {
private:
    AVFormatContext* decoderFmtCtx = nullptr;
    int videoStreamIndex = -1;

    AVCodecContext* codecContext = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;

public:
    int width = 0;
    int height = 0;
    int origin_width = 0;
    int origin_height = 0;
    int count = 0;
    int iframe = -1;
    double fps = 0;
    float duration = 0;
    AVRational fps_r = {60, 1};
    std::string codecName = "";
    std::string filename = "";
    std::string src_pix_fmt = "";
    std::string tgt_pix_fmt = "";
    std::tuple<int, int, int, int> crop_xywh = {0, 0, 0, 0};
    Size_wh size_wh = Size_wh(0, 0);
    Size_wh resize = Size_wh(0, 0);
    std::vector<int> outnumpyshape;
    int bytes_per_frame = 0;

public:
    // 拷贝视频信息
    void __copy_videoinfo(VideoInfo &videoInfo){
        filename = videoInfo.filename;
        height = origin_height = videoInfo.height;
        width = origin_width = videoInfo.width;
        count = videoInfo.count;
        fps_r = videoInfo.fps_r;
        fps = videoInfo.fps;
        duration = videoInfo.duration;
        codecName = videoInfo.codec;
        src_pix_fmt = videoInfo.src_pix_fmt;
        decoderFmtCtx = videoInfo.decoderFmtCtx;
        videoStreamIndex = videoInfo.videoStreamIndex;
    }

    FFmpegVideoCapture(const std::string& filename, std::string pix_fmt,
                       std::tuple<int, int, int, int> crop_xywh = {0, 0, 0, 0},
                       Size_wh resize = Size_wh(0,0)):
                       filename(filename), tgt_pix_fmt(pix_fmt), crop_xywh(crop_xywh), resize(resize){
        // 打开输入文件
        VideoInfo videoInfo(filename.c_str());
        videoInfo.show();
        __copy_videoinfo(videoInfo);

        CHECK (width % 2 == 0, "Height must be even");
        CHECK ( height % 2 == 0, "Width must be even");

        std::tuple<Size_wh, Size_wh, std::string> filter_options = get_videofilter_cpu(
                {width, height}, tgt_pix_fmt, crop_xywh, resize);
        size_wh = std::get<1>(filter_options);
        std::string filterstr = std::get<2>(filter_options);
        width = size_wh.width;
        height = size_wh.height;

        // 计算每帧的位数
        outnumpyshape = get_outnumpyshape(size_wh, tgt_pix_fmt);
        bytes_per_frame = 1;
        for (int num : outnumpyshape) {bytes_per_frame *= num;}

        // 初始化解码器
        AVCodecParameters* codecParameters = decoderFmtCtx->streams[videoStreamIndex]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);
        CHECK (codec,"Unsupported codec!");

        codecContext = avcodec_alloc_context3(codec);
        CHECK (codecContext,"Failed to allocate codec context.");

        CHECK (avcodec_parameters_to_context(codecContext, codecParameters) >= 0,
            "Failed to copy codec parameters to context.");

        CHECK (avcodec_open2(codecContext, codec, nullptr) >= 0, "Failed to open codec.");

        packet = av_packet_alloc();
        frame = av_frame_alloc();
    }

    // 析构函数：释放资源
    ~FFmpegVideoCapture() {
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codecContext);
        avformat_close_input(&decoderFmtCtx);
    }

    // 读取下一帧
    bool read(AVFrame*& outFrame) {
        while (true) {
            int ret = av_read_frame(decoderFmtCtx, packet);
            if (ret < 0) {
                // 如果没有更多数据，则发送 NULL 包来触发解码剩余帧
                avcodec_send_packet(codecContext, nullptr);
            } else if (packet->stream_index == videoStreamIndex) {
                // 发送视频流数据包到解码器
                avcodec_send_packet(codecContext, packet);
            } else if (packet->stream_index != videoStreamIndex) {
                continue;
            }

            av_packet_unref(packet);

            // 接收解码的帧
            ret = avcodec_receive_frame(codecContext, frame);
            if (ret == 0) {
                outFrame = frame;
                iframe++;
                return true;
            } else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                if (ret == AVERROR_EOF) return false; // 数据读取完成
                continue;
            } else {
                return false;
            }
        }
    }

    bool read(uint8_t*& framebuf){ //allocated frame
        AVFrame* avframe = nullptr;
        bool ret = read(avframe);
        if(!ret){return ret;}
//        memcpy(framebuf, avframe->data[0], width*height);
        framebuf = avframe->data[0];
        return ret;
    }
};

class FFmpegVideoWriter {
public:
    std::string filename;
    std::string codec_name;
    float fps;
    int width;
    int height;
    Size_wh size_wh;
    std::string pix_fmt;

    AVFormatContext* encoderFmtCtx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVCodec* codec = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    int stream_index = 0;

public:
    FFmpegVideoWriter(const std::string& filename, const std::string& codec_name, double fps, Size_wh size_wh, std::string pix_fmt)
            : filename(filename), codec_name(codec_name), fps(fps), size_wh(size_wh),
              width(size_wh.width), height(size_wh.height), pix_fmt(pix_fmt) {
        // 打开输出文件
        CHECK (avformat_alloc_output_context2(&encoderFmtCtx, nullptr, nullptr, filename.c_str()) >= 0,
                "Could not allocate output context");

        // 创建stream
        CHECK (avformat_new_stream(encoderFmtCtx, nullptr), "Could not create a video stream");

        // 查找编码器
        codec = const_cast<AVCodec*>(avcodec_find_encoder_by_name(codec_name.c_str())); //"libx264"
        CHECK(codec, "Codec not found");

        // 创建编码器上下文
        codec_ctx = avcodec_alloc_context3(codec);
        CHECK(codec_ctx, "Could not allocate video codec context");
        packet = av_packet_alloc();

        // 设置编码器参数
//        codec_ctx->bit_rate = 400000;
        codec_ctx->width = width;
        codec_ctx->height = height;
        codec_ctx->time_base = (AVRational){1, int(fps)};
//        codec_ctx->gop_size = 10;
//        codec_ctx->max_b_frames = 1;
        codec_ctx->pix_fmt = av_get_pix_fmt("gray"); // 假设输入是灰度图像

        // some options
        AVDictionary *encoder_options = nullptr;
        av_dict_set(&encoder_options, "crf", "23", AV_DICT_DONT_OVERWRITE);
        av_dict_set(&encoder_options, "threads", "auto", AV_DICT_DONT_OVERWRITE);
        CHECK (avcodec_open2(codec_ctx, codec, &encoder_options) >= 0, "Can not open the encoder.");

        // 将编码器参数复制到流中
        CHECK (avcodec_parameters_from_context(encoderFmtCtx->streams[stream_index]->codecpar, codec_ctx) >= 0,
               "Could not copy codec parameters to stream");

        // 打开输出文件
        CHECK (!(!(encoderFmtCtx->oformat->flags & AVFMT_NOFILE) && (avio_open(&encoderFmtCtx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0)),
            "Could not open output file");

        // 写入文件头
        CHECK (avformat_write_header(encoderFmtCtx, nullptr) >= 0, "Could not write header");
        av_dump_format(encoderFmtCtx, 0, filename.c_str(), 1);

        // 分配 AVFrame
        frame = av_frame_alloc();
        frame->format = codec_ctx->pix_fmt;
        frame->width = width;
        frame->height = height;

        // 分配帧缓冲区
        CHECK (av_image_alloc(frame->data, frame->linesize, width, height, codec_ctx->pix_fmt, 32) >= 0,
               "Could not allocate raw picture buffer");
    }

    void write(uint8_t* framearray){
        // 将输入数据复制到 AVFrame
        memcpy(frame->data[0], framearray, av_image_get_buffer_size(codec_ctx->pix_fmt, width, height, 32));
        frame->pts = av_rescale_q(codec_ctx->frame_num, codec_ctx->time_base, encoderFmtCtx->streams[stream_index]->time_base);

        // 编码帧
        int ret = avcodec_send_frame(codec_ctx, frame);
        CHECK(ret>=0, "Error sending frame to encoder");
        while (ret >= 0) {
            ret = avcodec_receive_packet(codec_ctx, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {break;}
            CHECK (ret >=0, "Error during encoding");

            // 写入数据包到输出文件
            packet->stream_index = stream_index;
            CHECK(av_interleaved_write_frame(encoderFmtCtx, packet) >= 0,
                  "Error writing packet to output file");
            av_packet_unref(packet);
        }
    }

    void release(){
        // 发送 nullptr 帧以触发编码器输出所有缓存的帧
        int ret = avcodec_send_frame(codec_ctx, nullptr);
        CHECK (ret >= 0, "Error sending a NULL frame to the encoder");

        // 接收并写入所有剩余的数据包
        while (true) {
            ret = avcodec_receive_packet(codec_ctx, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {break;}
            ; CHECK (ret >=0, "Error during encoding");
            if (ret < 0) {break;}

            // 写入数据包到输出文件
            packet->stream_index = stream_index;
            ret = av_interleaved_write_frame(encoderFmtCtx, packet);
            if (ret < 0) {break;}
            av_packet_unref(packet);
        }

        // 写入尾部信息
        av_write_trailer(encoderFmtCtx);

        // 释放资源
        av_frame_free(&frame);
        avcodec_free_context(&codec_ctx);
        if (encoderFmtCtx && !(encoderFmtCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&encoderFmtCtx->pb);
        }
        avformat_free_context(encoderFmtCtx);
    }
};

int main(int argc, char* argv[]) {
    std::string inputFilename = "/Users/chenxinfeng/ml-project/ffmpegcv-cpp/examples/input.mp4";
    std::string outputFilename = "/Users/chenxinfeng/ml-project/ffmpegcv-cpp/examples/output_gray.mp4";

    FFmpegVideoCapture cap(inputFilename, "rgb24", {16, 32, 600, 400}, {400, 300});
    FFmpegVideoWriter writer(outputFilename,
                                 "libx264",             // codec
                                 cap.fps,                          // float, frame rate
                                 {cap.width, cap.height},  // frame size
                                 "gray"                    // source pix_fmt
    );

    uint8_t* framearray = nullptr;
    while (cap.read(framearray)) {
        //framearray: uint8_t[cap.width*cap.height];
        writer.write(framearray);
    }

    std::cout << "Video decoding completed. Frames read = "
              << cap.iframe + 1 << std::endl;
    writer.release();

    return 0;
}
