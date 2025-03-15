cmake_minimum_required(VERSION 3.30)

set(CMAKE_CXX_STANDARD 17)

project(04_simple_filter)

include_directories(
        /Users/chenxinfeng/ml-project/ffmpeg-tutorials/utils
        /opt/homebrew/Cellar/fmt/11.1.4/include
        /opt/homebrew/Cellar/ffmpeg/7.1_4/include
        /opt/homebrew/Cellar/glog/0.6.0/include
        /opt/homebrew/Cellar/gflags/2.2.2/include
)

link_directories(
        /opt/homebrew/Cellar/fmt/11.1.4/lib
        /opt/homebrew/Cellar/ffmpeg/7.1_4/lib
        /opt/homebrew/Cellar/glog/0.6.0/lib
        /opt/homebrew/Cellar/gflags/2.2.2/lib
)

set(CMAKE_CXX_STANDARD 14)

add_executable(04_simple_filter
        filter.cpp
        something.h)

target_link_libraries(04_simple_filter glog fmt avformat avcodec avutil swscale avfilter)
