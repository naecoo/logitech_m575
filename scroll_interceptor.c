#include "scroll_interceptor.h"
#include <CoreGraphics/CoreGraphics.h>
#include <pthread.h>
#include <semaphore.h>
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
  sem_t *run_loop_ready; // 用于同步：等待线程 RunLoop 启动
} g_interceptor = {.multiplier = 2.0,
                   .running = false,
                   .run_loop = NULL,
                   .event_tap = NULL,
                   .mutex = PTHREAD_MUTEX_INITIALIZER,
                   .run_loop_ready = NULL};

// 事件计数（用于日志）
static volatile long long g_event_count = 0;

// 事件回调函数
CGEventRef scroll_callback(CGEventTapProxy proxy, CGEventType type,
                           CGEventRef event, void *refcon) {
  (void)proxy;
  (void)refcon;

  // 处理 tap 被系统禁用的情况（通常是权限问题）
  if (type == kCGEventTapDisabledByTimeout ||
      type == kCGEventTapDisabledByUserInput) {
    fprintf(stderr, "[WARN] Event tap 被系统禁用 (type=%d)，尝试重新启用...\n",
            type);
    if (g_interceptor.event_tap) {
      CGEventTapEnable(g_interceptor.event_tap, true);
    }
    return event;
  }

  if (type != kCGEventScrollWheel) {
    return event;
  }

  pthread_mutex_lock(&g_interceptor.mutex);
  if (!g_interceptor.running) {
    pthread_mutex_unlock(&g_interceptor.mutex);
    fprintf(stderr, "[WARN] 收到事件但拦截器未在运行状态\n");
    return event;
  }

  double multiplier = g_interceptor.multiplier;
  pthread_mutex_unlock(&g_interceptor.mutex);

  // 获取原始滚动值
  int64_t orig_y =
      CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1);
  int64_t orig_x =
      CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis2);

  // 应用速度倍数
  int64_t new_y = (int64_t)((double)orig_y * multiplier);
  int64_t new_x = (int64_t)((double)orig_x * multiplier);

  // 设置新值
  CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1, new_y);
  CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis2, new_x);

  // 日志：每10次打印一次，避免刷屏
  g_event_count++;
  if (g_event_count % 10 == 1) {
    printf("[LOG] 滚轮事件 #%lld: Y轴 %lld -> %lld, X轴 %lld -> %lld "
           "(倍数=%.2f)\n",
           g_event_count, orig_y, new_y, orig_x, new_x, multiplier);
    fflush(stdout);
  }

  return event;
}

// 拦截器线程函数
void *interceptor_thread(void *arg) {
  (void)arg;

  pthread_mutex_lock(&g_interceptor.mutex);
  g_interceptor.run_loop = CFRunLoopGetCurrent();
  pthread_mutex_unlock(&g_interceptor.mutex);

  printf("[LOG] 拦截器线程已启动，RunLoop = %p\n",
         (void *)g_interceptor.run_loop);
  fflush(stdout);

  // 通知主线程：RunLoop 已就绪
  if (g_interceptor.run_loop_ready) {
    sem_post(g_interceptor.run_loop_ready);
  }

  CFRunLoopRun();

  printf("[LOG] 拦截器线程已退出\n");
  fflush(stdout);
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

  // 创建信号量，用于等待线程 RunLoop 就绪
  g_interceptor.run_loop_ready =
      sem_open("/m575_runloop_ready", O_CREAT | O_EXCL, 0644, 0);
  if (g_interceptor.run_loop_ready == SEM_FAILED) {
    // 信号量可能残留，尝试删除后重建
    sem_unlink("/m575_runloop_ready");
    g_interceptor.run_loop_ready =
        sem_open("/m575_runloop_ready", O_CREAT | O_EXCL, 0644, 0);
    if (g_interceptor.run_loop_ready == SEM_FAILED) {
      pthread_mutex_unlock(&g_interceptor.mutex);
      fprintf(stderr, "[ERROR] 创建信号量失败\n");
      return -1;
    }
  }

  // 创建事件监听
  // ⚠️ 必须用 kCGEventTapOptionDefault（而非 ListenOnly）才能修改事件！
  CGEventMask event_mask = (1 << kCGEventScrollWheel);
  printf("[LOG] 正在创建 CGEventTap (mask=0x%llx)...\n",
         (unsigned long long)event_mask);
  fflush(stdout);

  g_interceptor.event_tap = CGEventTapCreate(
      kCGSessionEventTap, kCGHeadInsertEventTap,
      kCGEventTapOptionDefault, // ✅ 修复：Default 模式才能修改事件值
      event_mask, scroll_callback, NULL);

  if (!g_interceptor.event_tap) {
    pthread_mutex_unlock(&g_interceptor.mutex);
    sem_close(g_interceptor.run_loop_ready);
    sem_unlink("/m575_runloop_ready");
    g_interceptor.run_loop_ready = NULL;
    fprintf(stderr, "[ERROR] 创建事件监听失败，请检查辅助功能权限\n");
    fprintf(
        stderr,
        "        请前往：系统设置 -> 隐私与安全性 -> 辅助功能，授权本程序\n");
    return -1;
  }
  printf("[LOG] CGEventTap 创建成功: %p\n", (void *)g_interceptor.event_tap);
  fflush(stdout);

  g_interceptor.running = true;
  pthread_mutex_unlock(&g_interceptor.mutex);

  // 创建并启动线程
  if (pthread_create(&g_interceptor.thread, NULL, interceptor_thread, NULL) !=
      0) {
    fprintf(stderr, "[ERROR] 创建线程失败\n");
    interceptor_stop();
    return -1;
  }

  // ✅ 修复竞态条件：等待线程 RunLoop 就绪后再添加 source
  printf("[LOG] 等待拦截器线程 RunLoop 就绪...\n");
  fflush(stdout);
  sem_wait(g_interceptor.run_loop_ready);
  sem_close(g_interceptor.run_loop_ready);
  sem_unlink("/m575_runloop_ready");
  g_interceptor.run_loop_ready = NULL;

  // 添加事件源到运行循环
  CFRunLoopSourceRef source =
      CFMachPortCreateRunLoopSource(NULL, g_interceptor.event_tap, 0);
  pthread_mutex_lock(&g_interceptor.mutex);
  if (g_interceptor.run_loop) {
    CFRunLoopAddSource(g_interceptor.run_loop, source, kCFRunLoopCommonModes);
    CGEventTapEnable(g_interceptor.event_tap, true);
    printf("[LOG] EventTap 已注册到 RunLoop 并已启用\n");
    fflush(stdout);
  } else {
    fprintf(stderr, "[ERROR] RunLoop 为空，EventTap 注册失败！\n");
  }
  CFRelease(source);
  pthread_mutex_unlock(&g_interceptor.mutex);

  printf("✅ 事件拦截器已启动 (倍数=%.2f)\n", g_interceptor.multiplier);
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