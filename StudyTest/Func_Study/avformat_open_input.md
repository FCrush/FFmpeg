# 一. avformat_open_input
### 1. 函数定义：
初始化输入媒体文件，__读取文件头信息__，并初始化AVFormatContext结构体；
```c
int avformat_open_input(AVFormatContext **ps, const char *filename, const AVInputFormat *fmt, AVDictionary **options)
```

### 2. 函数入参：
* `AVFormatContext **ps`：一个包含媒体文件格式信息的结构体；
* `const char *url`：输入文件名，可以是本地文件路径，也可以是网络流的URL；
* `const AVInputFormat *fmt`：指定输入格式，为NULL则由FFmpeg自动检测，指定格式可以加快速度；
* `AVDictionary **options`：一个包含媒体文件格式信息的结构体,用于传递额外的选项给解复用器；

### 3. 函数流程图：

* AVFormatContext **ps：

* 设置全局变量 `av_log_level` ，在av_log输出时对等级做判断，如果等级比其大则直接返回不输出
```c
if (level > av_log_level)
    return;
```
### 2. void `av_log`(void* avcl, int level, const char *fmt, ...)
* 调用回调函数`av_log_callback`来实现日志输出，如果需要自定义日志输出函数，则修改该函数即可

#### 2. av_log_set_level


# 一. avformat_close_input
### 1. 函数定义：
```c
void avformat_close_input(AVFormatContext **ps)
```