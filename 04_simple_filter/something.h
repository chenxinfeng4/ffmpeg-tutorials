#pragma once
#include <fstream>
#include <iostream>
#include <cstdio>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
}


inline void CHECK(bool condition, const char* msg="Assert fail."){
    if (!condition){
        std::cerr << "Error: " << msg << std::endl;
        throw std::runtime_error(msg);
    }
}





bool file_exsits(const std::string& filename) {
    std::ifstream file(filename);
    return file.good();
}

std::string get_file_extension(const std::string& filename) {
    const size_t pos = filename.find_last_of('.');
    if (pos != std::string::npos) {
        std::string ext = filename.substr(pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return ext;
    }
    return "";
}

class VideoInfo {
public:
    std::string filename;
    std::string codec;
    std::string src_pix_fmt;
    float duration = 0.0f;
    AVRational fps_r = {60, 1};
    float fps = 0.0f;
    int width = 0;
    int height = 0;
    int count = 0;
    bool is_complex = false;

    AVFormatContext* decoderFmtCtx = nullptr;
    int videoStreamIndex = -1;

public:
    VideoInfo();
    VideoInfo(const char* filename);
    void show() const;
};


struct Size_wh {
    int width = 0;
    int height = 0;

    Size_wh(int w, int h) : width(w), height(h) {}
    Size_wh() {}
    Size_wh(std::initializer_list<int> wh) : width(*wh.begin()), height(*(wh.begin() + 1)) {}
    Size_wh(const VideoInfo& vi) : width(vi.width), height(vi.height) {}

    bool empty() const { return width == 0 || height == 0; }
};


VideoInfo::VideoInfo(){;}

VideoInfo::VideoInfo(const char* filename) {
    CHECK (file_exsits(filename), "File does not exist");
    this->filename = std::string(filename);
    static const std::vector<std::string> complex_formats = {"mkv", "flv", "ts"};
    is_complex = std::find(complex_formats.begin(), complex_formats.end(),
                                get_file_extension(filename)) != complex_formats.end();
    CHECK (avformat_open_input(&decoderFmtCtx, this->filename.c_str(), nullptr, nullptr) >= 0,
           "Failed to open input file.");

    CHECK (avformat_find_stream_info(decoderFmtCtx, nullptr) >= 0,
           "Failed to find stream info.");

    // 找到视频流
    videoStreamIndex = av_find_best_stream(decoderFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    CHECK(videoStreamIndex>=0, "No video stream found.");

    AVCodecParameters* codecParameters = decoderFmtCtx->streams[videoStreamIndex]->codecpar;
    const AVCodec* codecObj = avcodec_find_decoder(codecParameters->codec_id);
    AVCodecContext* codecContext = avcodec_alloc_context3(codecObj);
    CHECK(avcodec_parameters_to_context(codecContext, decoderFmtCtx->streams[videoStreamIndex]->codecpar) >= 0);

    // 获取基本信息
    codec = codecObj->name;
    width = codecContext->width;
    height = codecContext->height;
    src_pix_fmt = av_get_pix_fmt_name(codecContext->pix_fmt);
    count = int(decoderFmtCtx->streams[videoStreamIndex]->nb_frames);
    duration = decoderFmtCtx->duration / (float)AV_TIME_BASE;
    fps_r = decoderFmtCtx->streams[videoStreamIndex]->avg_frame_rate;
    fps = float(av_q2d(fps_r));

    // 释放
    avcodec_free_context(&codecContext);
}

void VideoInfo::show() const{
    // Please print the information
    std::cout << "Video Information:" << std::endl
              << "Codec Name: " << codec << std::endl
              << "Original Width: " << width << std::endl
              << "Original Height: " << height << std::endl
              << "Pixel Format: " << src_pix_fmt << std::endl
              << "Frame Rate: " << fps << " fps" << std::endl
              << "Duration: " << duration << " seconds" << std::endl
              << "Total Frames: " << count << std::endl;
}