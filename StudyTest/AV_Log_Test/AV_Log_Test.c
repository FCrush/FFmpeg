#include <stdio.h>
#include "libavutil/log.h"

int main()
{
    av_log_set_level(AV_LOG_INFO);
    av_log_set_flags(AV_LOG_PRINT_LEVEL);
    av_log(0, AV_LOG_INFO, "Hello World\n");
    av_log(0, AV_LOG_INFO, "Hello World\n");
    av_log(0, AV_LOG_INFO, "Hello World\n");
    av_log(0, AV_LOG_INFO, "Hello World\n");
    av_log(0, AV_LOG_INFO, "Hello World\n");
    av_log(0, AV_LOG_INFO, "Hello World\n");
    av_log(0, AV_LOG_INFO, "Hello World4\n");
    av_log(0, AV_LOG_INFO, "Hello World3\n");
    av_log(0, AV_LOG_INFO, "Hello World2\n");
    printf("AWH\n");
}