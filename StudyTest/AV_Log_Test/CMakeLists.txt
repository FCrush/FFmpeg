cmake_minimum_required(VERSION 3.10)

project(AV_Log_Test C)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 可以动态调整export FFMPEG_PATH=/path/to/your/ffmpeg
set(FFMPEG_CUSTOM_PATH $ENV{FFMPEG_PATH})
if(NOT FFMPEG_CUSTOM_PATH)
    set(FFMPEG_CUSTOM_PATH "/usr/local/ffmpeg")
endif()
#set(FFMPEG_PATH /usr/local/ffmpeg)

include_directories(${FFMPEG_CUSTOM_PATH}/include) # 头文件

# 源文件目录
aux_source_directory(. SRC_LIST)

# 添加可执行文件
add_executable(AV_Log_Test ${SRC_LIST})

# 设置库文件搜索路径
target_link_directories(AV_Log_Test PRIVATE ${FFMPEG_CUSTOM_PATH}/lib)

target_link_libraries(AV_Log_Test PRIVATE libavutil.so)