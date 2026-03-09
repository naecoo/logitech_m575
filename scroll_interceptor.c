#include "scroll_interceptor.h"
#include <CoreGraphics/CoreGraphics.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 全局状态
static struct {
    double multiplier;
    bool running;
    CFRunLoopRef run_loop;
    CFMachPortRef event_tap;
    pthread_t thread;
    pthread_mutex_t mutex;
} g_interceptor = {
    .multiplier = 2.0,
    .running = false,
    .run_loop = NULL,
    .event_tap = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

// 事件回调函数
CGEventRef scroll_callback(CGEventTapProxy proxy, CGEventType type,
                           CGEventRef event, void *refcon) {
    (void)proxy;
    (void)refcon;

    if (type != kCGEventScrollWheel) {
        return event;
    }

    pthread_mutex_lock(&g_interceptor.mutex);
    if (!g_interceptor.running) {
        pthread_mutex_unlock(&g_interceptor.mutex);
        return event;
    }

    double multiplier = g_interceptor.multiplier;
    pthread_mutex_unlock(&g_interceptor.mutex);

    // 获取滚动值
    int64_t scroll_y = CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1);
    int64_t scroll_x = CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis2);

    // 应用速度倍数
    scroll_y = (int64_t)((double)scroll_y * multiplier);
    scroll_x = (int64_t)((double)scroll_x * multiplier);

    // 设置新值
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1, scroll_y);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis2, scroll_x);

    return event;
}

// 拦截器线程函数
void* interceptor_thread(void *arg) {
    (void)arg;

    pthread_mutex_lock(&g_interceptor.mutex);
    g_interceptor.run_loop = CFRunLoopGetCurrent();
    pthread_mutex_unlock(&g_interceptor.mutex);

    CFRunLoopRun();

    return NULL;
}

int interceptor_init(double multiplier) {
    pthread_mutex_lock(&g_interceptor.mutex);
    g_interceptor.multiplier = multiplier;
    g_interceptor.running = false;
    pthread_mutex_unlock(&g_interceptor.mutex);

    return 0;
}

int interceptor_start(void) {
    pthread_mutex_lock(&g_interceptor.mutex);
    if (g_interceptor.running) {
        pthread_mutex_unlock(&g_interceptor.mutex);
        fprintf(stderr, "拦截器已在运行\n");
        return -1;
    }

    // 创建事件监听
    CGEventMask event_mask = (1 << kCGEventScrollWheel);
    g_interceptor.event_tap = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,
        event_mask,
        scroll_callback,
        NULL
    );

    if (!g_interceptor.event_tap) {
        pthread_mutex_unlock(&g_interceptor.mutex);
        fprintf(stderr, "创建事件监听失败，请检查辅助功能权限\n");
        return -1;
    }

    g_interceptor.running = true;
    pthread_mutex_unlock(&g_interceptor.mutex);

    // 创建并启动线程
    if (pthread_create(&g_interceptor.thread, NULL, interceptor_thread, NULL) != 0) {
        fprintf(stderr, "创建线程失败\n");
        interceptor_stop();
        return -1;
    }

    // 添加事件源到运行循环
    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(NULL, g_interceptor.event_tap, 0);
    pthread_mutex_lock(&g_interceptor.mutex);
    if (g_interceptor.run_loop) {
        CFRunLoopAddSource(g_interceptor.run_loop, source, kCFRunLoopCommonModes);
        CGEventTapEnable(g_interceptor.event_tap, true);
    }
    pthread_mutex_unlock(&g_interceptor.mutex);

    printf("✅ 事件拦截器已启动\n");
    return 0;
}

void interceptor_stop(void) {
    pthread_mutex_lock(&g_interceptor.mutex);
    if (!g_interceptor.running) {
        pthread_mutex_unlock(&g_interceptor.mutex);
        return;
    }

    g_interceptor.running = false;

    if (g_interceptor.event_tap) {
        CGEventTapEnable(g_interceptor.event_tap, false);
    }

    pthread_mutex_unlock(&g_interceptor.mutex);

    // 停止运行循环
    if (g_interceptor.run_loop) {
        CFRunLoopStop(g_interceptor.run_loop);
    }

    // 等待线程结束
    pthread_join(g_interceptor.thread, NULL);

    pthread_mutex_lock(&g_interceptor.mutex);
    if (g_interceptor.event_tap) {
        CFMachPortInvalidate(g_interceptor.event_tap);
        CFRelease(g_interceptor.event_tap);
        g_interceptor.event_tap = NULL;
    }
    g_interceptor.run_loop = NULL;
    pthread_mutex_unlock(&g_interceptor.mutex);

    printf("✅ 事件拦截器已停止\n");
}

bool interceptor_is_running(void) {
    pthread_mutex_lock(&g_interceptor.mutex);
    bool running = g_interceptor.running;
    pthread_mutex_unlock(&g_interceptor.mutex);
    return running;
}

void interceptor_set_multiplier(double multiplier) {
    pthread_mutex_lock(&g_interceptor.mutex);
    g_interceptor.multiplier = multiplier;
    pthread_mutex_unlock(&g_interceptor.mutex);
}

double interceptor_get_multiplier(void) {
    pthread_mutex_lock(&g_interceptor.mutex);
    double multiplier = g_interceptor.multiplier;
    pthread_mutex_unlock(&g_interceptor.mutex);
    return multiplier;
}

void interceptor_cleanup(void) {
    interceptor_stop();
    pthread_mutex_destroy(&g_interceptor.mutex);
}