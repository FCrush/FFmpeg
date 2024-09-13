# 关键函数
### 1. int `av_log_set_level`(int level)
* 设置全局变量 `av_log_level` ，在av_log输出时对等级做判断，如果等级比其大则直接返回不输出；
```c
if (level > av_log_level)
    return;
```

### 2. void `av_log`(void* avcl, int level, const char *fmt, ...)
* 调用回调函数`av_log_callback`来实现日志输出，如果需要自定义日志输出函数，则修改该函数即可；（`av_log_set_callback()`）
* 默认函数 `av_log_default_callback()`；


### 3. int `av_log_set_flag`(int flag)
* 如果要打印的日志和上一行重复的话通过该参数来控制打印是否输出；
* `AV_LOG_SKIP_REPEATED`表示跳过中间重复的打印，通过一句特殊的数字来说明中间重复的次数；
* `AV_LOG_PRINT_LEVEL`表示不跳过，正常打印输出；






#### 使用 `style` 属性修改文字的背景色

<font style="background: red">红色</font>
<font style="background: green">绿色</font>
<font style="background: blue">蓝色</font>

<font style="background: rgb(200,100,100)">使用 rgb 颜色值</font>

<font style="background: #FF00BB">使用十六进制颜色值</font>
