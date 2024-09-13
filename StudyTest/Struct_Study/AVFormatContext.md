# 一. AVFormatContext
### 1.1、 结构体定义定义：
用于封装媒体文件或流的格式信息，存储和管理与媒体格式相关的所有信息，包括输入/输出格式、流信息、元数据等；

### 1.2、 AVFormatContext和FFFormatContext：
FFFormatContext是AVFormatContext的超集，包含了AVFormatContext的所有字段，并在其基础上添加部分内部使用的扩展字段；这种分离有助于保持API的稳定性和内部实现的灵活性
```c
FFFormatContext *si = ffformatcontext(s);
```
### 1.3、 AVFormatContext结构体变量含义：
* `io_open, io_close2`: 用于打开和关闭 I/O 流的回调函数;
* `item_name`：函数指针，用于返回与该类关联的特定上下文实例的名称；
* `iformat, oformat`: 输入输出文件格式类型
* `pb`: I/O 上下文，用于实际的读写操作，它定义了如何读取或写入媒体数据；
# 二. FFFormatContext
### 2.1、 FFFormatContext结构体变量含义：
* `pkt`: 用于在通用解复用代码中暂时存放数据包;



## 关键函数
### 1. int `av_log_set_flags`(int arg)
* 设置全局变量 `av_log_level` ，在av_log输出时对等级做判断，如果等级比其大则直接返回不输出
```c
if (level > av_log_level)
    return;
```
### 2. void `av_log`(void* avcl, int level, const char *fmt, ...)
* 调用回调函数`av_log_callback`来实现日志输出，如果需要自定义日志输出函数，则修改该函数即可

#### 2. av_log_set_level




AVFormatContext 是 FFmpeg 库中的一个核心结构体，用于封装媒体文件或流的格式信息。它的主要作用是存储和管理与媒体格式相关的所有信息，包括输入/输出格式、流信息、元数据等。以下是该结构体的一些主要组成部分及其作用：
格式信息：
iformat: 输入格式（用于解复用）
oformat: 输出格式（用于复用）
priv_data: 格式私有数据
I/O 操作：
pb: I/O 上下文，用于实际的读写操作
io_open, io_close2: 用于打开和关闭 I/O 流的回调函数
流信息：
nb_streams: 流的数量
streams: 流数组，每个元素是一个 AVStream 指针
nb_stream_groups, stream_groups: 流组相关信息
4. 元数据：
metadata: 整个文件的元数据
时间信息：
start_time: 组件的开始时间
duration: 流的持续时间
bit_rate: 总比特率
章节信息：
nb_chapters, chapters: 章节相关信息
程序信息：
nb_programs, programs: 程序相关信息
控制标志：
flags: 用于修改（解）复用器行为的标志
debug: 用于启用调试的标志
event_flags: 指示文件上发生的事件的标志
编解码器相关：
video_codec_id, audio_codec_id, subtitle_codec_id, data_codec_id: 强制使用的编解码器 ID
video_codec, audio_codec, subtitle_codec, data_codec: 强制使用的编解码器
探测和分析相关：
probesize: 用于确定流属性的最大读取字节数
max_analyze_duration: 用于分析流的最大持续时间
format_probesize: 用于识别输入格式的最大读取字节数
其他控制参数：
max_index_size: 每个流的索引的最大内存使用量
max_picture_buffer: 实时捕获设备缓冲帧的最大内存使用量
max_delay: 最大延迟
avoid_negative_ts: 在复用时避免负时间戳
这个结构体是 FFmpeg 中处理媒体文件的核心，它提供了丰富的参数和选项，允许用户精细地控制媒体处理过程。在进行媒体文件的读写、转码等操作时，AVFormatContext 都扮演着关键角色。