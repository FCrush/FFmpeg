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

## 基本流程