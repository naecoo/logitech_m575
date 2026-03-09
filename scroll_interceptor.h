#ifndef SCROLL_INTERCEPTOR_H
#define SCROLL_INTERCEPTOR_H

#include <stdbool.h>

// 初始化拦截器
int interceptor_init(double multiplier);

// 启动拦截器
int interceptor_start(void);

// 停止拦截器
void interceptor_stop(void);

// 检查是否运行
bool interceptor_is_running(void);

// 设置速度倍数
void interceptor_set_multiplier(double multiplier);

// 获取当前倍数
double interceptor_get_multiplier(void);

// 清理资源
void interceptor_cleanup(void);

#endif