# M575 滚轮速度调整工具 — 技术文档

> 本文档详细介绍 `m575-scroll` 的工作原理、系统架构与各模块机制。

---

## 目录

1. [整体架构](#1-整体架构)
2. [双层速度控制机制](#2-双层速度控制机制)
3. [模块详解](#3-模块详解)
   - [main.c — 入口与命令调度](#31-mainc--入口与命令调度)
   - [hid_config.c — HID 硬件层配置](#32-hid_configc--hid-硬件层配置)
   - [scroll_interceptor.c — 事件拦截层](#33-scroll_interceptorc--事件拦截层)
   - [daemon.c — 进程生命周期管理](#34-daemonc--进程生命周期管理)
4. [完整启动流程](#4-完整启动流程)
5. [CGEventTap 深度解析](#5-cgeventtap-深度解析)
6. [线程模型与同步机制](#6-线程模型与同步机制)
7. [macOS 权限机制说明](#7-macos-权限机制说明)
8. [已知问题与排查](#8-已知问题与排查)

---

## 1. 整体架构

```
┌─────────────────────────────────────────────────────┐
│                    m575-scroll CLI                  │
│                      main.c                         │
└──────────┬──────────────┬──────────────┬────────────┘
           │              │              │
           ▼              ▼              ▼
   ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
   │  hid_config  │ │   scroll_    │ │    daemon    │
   │      .c      │ │ interceptor  │ │      .c      │
   │              │ │     .c       │ │              │
   │ hidutil CLI  │ │ CGEventTap   │ │ PID 文件管理 │
   │ 硬件层配置   │ │ 事件拦截修改 │ │ launchd 集成 │
   └──────────────┘ └──────────────┘ └──────────────┘
           │              │
           ▼              ▼
   ┌──────────────────────────────┐
   │       macOS 系统层           │
   │  IOHIDManager  /  CoreGraph  │
   │  (硬件驱动)   /  ics (事件)  │
   └──────────────────────────────┘
```

该工具通过**两个独立的系统接口**共同作用，实现滚轮速度控制：

| 层次   | 接口                     | 作用                            | 权限要求     |
| ------ | ------------------------ | ------------------------------- | ------------ |
| 硬件层 | `hidutil` / IOHIDManager | 修改设备属性（加速度、Scaling） | 推荐 root    |
| 事件层 | `CGEventTap`             | 拦截并乘以滚轮事件数值          | 辅助功能授权 |

---

## 2. 双层速度控制机制

### 第一层：HID 硬件层（hidutil）

在事件进入 macOS 事件队列**之前**，直接修改 HID 设备的上报参数：

```
鼠标硬件 → HID 驱动 → [hidutil 修改 Scaling/UserAcceleration] → 事件队列
```

- **`UserAcceleration: 0`** — 禁用系统鼠标加速度曲线，使滚轮速度与物理移动线性对应
- **`Scaling: N`** — 在驱动层对原始 HID 数据乘以 N 倍

**特点**：作用于底层驱动，对所有应用生效，但精度有限（浮点 Scaling 可能被取整）。

### 第二层：事件拦截层（CGEventTap）

在事件已进入系统事件队列后，通过 `CGEventTap` 在事件传递路径上拦截：

```
事件队列 → [CGEventTap 回调：数值 × multiplier] → 目标应用
```

- 读取 `kCGScrollWheelEventPointDeltaAxis1`（Y 轴）和 `kCGScrollWheelEventPointDeltaAxis2`（X 轴）的整数值
- 乘以用户指定的 `multiplier` 倍数
- 写回修改后的值，目标应用收到的是已放大的事件

**特点**：精确可控，支持实时调整，但需要辅助功能授权。

### 为什么需要两层？

- 单独 `hidutil`：Scaling 精度低，且某些应用（如浏览器）有自己的加速曲线，效果不一致
- 单独 `CGEventTap`：在 `UserAcceleration` 开启时，系统加速曲线会导致非线性放大，结果难以预测
- **两层组合**：先用 `hidutil` 关闭加速、建立基准线，再用 `CGEventTap` 精确线性放大

---

## 3. 模块详解

### 3.1 `main.c` — 入口与命令调度

**职责**：CLI 参数解析、命令路由、信号处理。

使用 POSIX `getopt_long` 解析参数，支持长选项（`--speed`、`--daemon`）和短选项（`-s`、`-d`）。

**命令路由表**：

| 命令                | 函数                      | 描述                                |
| ------------------- | ------------------------- | ----------------------------------- |
| `start`             | `cmd_start()`             | 启动 HID 配置 + 事件拦截器          |
| `stop`              | `cmd_stop()`              | 发送 SIGTERM 给运行中进程，重置 HID |
| `status`            | `cmd_status()`            | 读取 PID 文件，显示运行状态         |
| `config`            | `cmd_config()`            | 保存配置                            |
| `install-launchd`   | `cmd_install_launchd()`   | 创建并加载 launchd plist            |
| `uninstall-launchd` | `cmd_uninstall_launchd()` | 卸载 launchd plist                  |

**信号处理**：

注册 `SIGINT` 和 `SIGTERM` 处理器，收到信号后设置 `g_running = 0`，主循环退出，触发 `interceptor_stop()` 和 `daemon_remove_pid()` 清理。

```
main loop: while(g_running) { sleep(1); }
    ↑
  SIGINT/SIGTERM → signal_handler → g_running = 0
```

---

### 3.2 `hid_config.c` — HID 硬件层配置

**职责**：通过调用系统 `hidutil` 命令行工具修改鼠标 HID 属性。

#### 核心实现

```c
// 禁用加速度
hidutil property --set '{"UserAcceleration": 0}'

// 设置速度 Scaling
hidutil property --set '{"Scaling": 1.50}'
```

使用 `system()` 调用子进程执行，并通过 `WEXITSTATUS(ret)` 获取退出码判断成功与否。

#### hidutil 工作原理

`hidutil` 是 macOS 自带的 HID（Human Interface Device）配置工具，底层通过 **IOKit / IOHIDManager** 框架与 HID 驱动通信。它修改的是内核级别的设备属性，在当次会话（重启前）持续有效。

| 属性               | 说明                               |
| ------------------ | ---------------------------------- |
| `UserAcceleration` | 0 = 线性，1 = 系统加速曲线（默认） |
| `Scaling`          | 原始 HID delta 的倍率系数          |

**重置**（`stop` 命令时调用）：恢复 `UserAcceleration: 1`、`Scaling: 1`。

---

### 3.3 `scroll_interceptor.c` — 事件拦截层

这是整个工具最核心的模块。

#### 全局状态结构

```c
static struct {
    double multiplier;        // 速度倍数
    bool running;             // 是否运行中
    CFRunLoopRef run_loop;    // 拦截器线程的 RunLoop
    CFMachPortRef event_tap;  // CGEventTap 句柄
    pthread_t thread;         // 拦截器线程
    pthread_mutex_t mutex;    // 保护多线程访问
    sem_t *run_loop_ready;    // 同步信号量：等待 RunLoop 就绪
} g_interceptor;
```

#### CGEventTap 创建参数解析

```c
g_interceptor.event_tap = CGEventTapCreate(
    kCGSessionEventTap,        // 1. 监听会话级别事件（当前登录用户的所有事件）
    kCGHeadInsertEventTap,     // 2. 在事件处理链头部插入（最先执行）
    kCGEventTapOptionDefault,  // 3. ⚠️ 关键：Default 模式才能修改事件值
                               //    ListenOnly 模式只能观察，无法修改
    (1 << kCGEventScrollWheel),// 4. 只监听滚轮事件（mask = 0x400000）
    scroll_callback,           // 5. 事件处理回调函数
    NULL                       // 6. 用户数据（不需要）
);
```

> **为什么必须用 `kCGEventTapOptionDefault` 而非 `ListenOnly`？**
> `ListenOnly` 是只读观察，回调里修改事件值无效。`Default` 模式是拦截修改模式，修改后的事件值会被传递给下游。

#### 事件回调函数

```c
CGEventRef scroll_callback(CGEventTapProxy proxy, CGEventType type,
                            CGEventRef event, void *refcon) {
    // 处理 tap 被系统禁用（超时保护机制）
    if (type == kCGEventTapDisabledByTimeout ||
        type == kCGEventTapDisabledByUserInput) {
        CGEventTapEnable(g_interceptor.event_tap, true); // 重新启用
        return event;
    }

    // 获取原始滚轮 delta 值
    int64_t orig_y = CGEventGetIntegerValueField(event,
                         kCGScrollWheelEventPointDeltaAxis1); // Y轴
    int64_t orig_x = CGEventGetIntegerValueField(event,
                         kCGScrollWheelEventPointDeltaAxis2); // X轴

    // 应用倍数并写回
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1,
                                (int64_t)(orig_y * multiplier));
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis2,
                                (int64_t)(orig_x * multiplier));

    return event; // 返回修改后的事件，继续传递
}
```

---

### 3.4 `daemon.c` — 进程生命周期管理

**职责**：PID 文件管理 + launchd 开机启动集成。

#### PID 文件机制

- 路径：`/tmp/m575-scroll.pid`
- 启动时写入当前 PID，停止时删除
- `daemon_is_running()` 通过 `kill(pid, 0)` 检测进程是否存活（信号 0 不发送实际信号，只检测进程存在性）

#### launchd 集成

生成标准 Apple Property List（plist）文件到 `~/Library/LaunchAgents/com.m575.scroll.plist`：

```xml
<dict>
    <key>Label</key>
    <string>com.m575.scroll</string>
    <key>ProgramArguments</key>
    <array>
        <string>/path/to/m575-scroll</string>
        <string>start</string>
        <string>--speed</string>
        <string>2.00</string>
        <string>--daemon</string>
    </array>
    <key>RunAtLoad</key><true/>
    <key>KeepAlive</key><true/>
</dict>
```

`KeepAlive: true` 意味着 launchd 会在程序崩溃后自动重启它。

---

## 4. 完整启动流程

```
用户执行: ./m575-scroll start -s 1.5
          │
          ▼
    ┌─────────────────────────────────┐
    │ main() 解析参数                 │
    │  speed = 1.5, daemon_mode = 0  │
    └────────────┬────────────────────┘
                 │
                 ▼
    ┌─────────────────────────────────┐
    │ hid_apply_config(1.5)           │
    │  ├─ hidutil set UserAccel=0    │
    │  └─ hidutil set Scaling=1.5    │
    └────────────┬────────────────────┘
                 │
                 ▼
    ┌─────────────────────────────────┐
    │ interceptor_init(1.5)           │
    │  └─ 设置 multiplier = 1.5      │
    └────────────┬────────────────────┘
                 │
                 ▼
    ┌─────────────────────────────────┐
    │ interceptor_start()             │
    │  ├─ 创建 POSIX 信号量           │
    │  ├─ CGEventTapCreate(...)       │  ← 需要辅助功能权限
    │  ├─ pthread_create(拦截器线程)  │
    │  ├─ sem_wait(等待 RunLoop 就绪) │  ← 同步点
    │  └─ CFRunLoopAddSource(...)     │
    │     CGEventTapEnable(true)      │
    └────────────┬────────────────────┘
                 │
                 ▼
    ┌─────────────────────────────────┐
    │ daemon_save_pid(getpid())       │
    └────────────┬────────────────────┘
                 │
                 ▼
    ┌─────────────────────────────────┐
    │ 主线程: while(g_running)sleep() │  ← 阻塞等待 Ctrl+C
    │                                 │
    │ 拦截器线程: CFRunLoopRun()      │  ← 事件循环处理滚轮事件
    └────────────┬────────────────────┘
                 │ Ctrl+C / SIGTERM
                 ▼
    ┌─────────────────────────────────┐
    │ interceptor_stop()              │
    │  ├─ CGEventTapEnable(false)    │
    │  ├─ CFRunLoopStop(...)          │
    │  └─ pthread_join(等待线程退出) │
    │ daemon_remove_pid()             │
    └─────────────────────────────────┘
```

---

## 5. CGEventTap 深度解析

### 事件流路径

```
鼠标物理滚动
     │
     ▼
HID 驱动（内核态）
     │  已应用 Scaling 和 UserAcceleration
     ▼
Window Server（系统事件服务进程）
     │
     ▼ ← CGEventTap 插入点（kCGSessionEventTap + kCGHeadInsertEventTap）
[scroll_callback 执行：数值 × multiplier]
     │
     ▼
事件派发给目标应用（Finder, Chrome, 终端...）
```

### Tap 类型对比

| 参数                          | 说明                                     |
| ----------------------------- | ---------------------------------------- |
| `kCGSessionEventTap`          | 监听当前用户会话的所有事件               |
| `kCGAnnotatedSessionEventTap` | 同上，但包含附加的窗口信息               |
| `kCGHIDEventTap`              | 更底层，拦截所有 HID 设备事件（需 root） |

本工具使用 `kCGSessionEventTap`，无需 root 但需要**辅助功能**（Accessibility）权限。

### RunLoop 集成

`CGEventTap` 必须挂载到一个 `CFRunLoop` 上才能激活，工作方式类似于事件驱动的回调注册：

```c
CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(NULL, event_tap, 0);
CFRunLoopAddSource(run_loop, source, kCFRunLoopCommonModes);
CGEventTapEnable(event_tap, true);
```

这就是为什么需要专用线程运行 `CFRunLoopRun()` —— 主线程的 RunLoop 被 `sleep()` 占用，无法处理事件。

### 系统超时保护

macOS 有一个保护机制：如果 `CGEventTap` 的回调处理时间**过长**，系统会自动禁用该 tap（以防恶意或卡死的程序阻塞输入）。

代码中的处理：

```c
if (type == kCGEventTapDisabledByTimeout ||
    type == kCGEventTapDisabledByUserInput) {
    CGEventTapEnable(g_interceptor.event_tap, true); // 立即重新启用
}
```

---

## 6. 线程模型与同步机制

### 两个线程的分工

```
主线程                          拦截器线程
  │                                │
  │ interceptor_start()            │
  │   ├─ CGEventTapCreate          │
  │   ├─ pthread_create ──────────►│ interceptor_thread()
  │   │                            │   ├─ CFRunLoopGetCurrent()
  │   │                            │   ├─ sem_post(run_loop_ready)
  │   ◄── sem_wait(run_loop_ready) │   └─ CFRunLoopRun() ← 阻塞于此
  │   ├─ CFRunLoopAddSource        │       处理滚轮回调...
  │   └─ CGEventTapEnable          │
  │                                │
  │ while(g_running) sleep(1)      │
  │ (等待 Ctrl+C)                  │
  │                                │
  │ interceptor_stop()             │
  │   ├─ CGEventTapEnable(false)  │
  │   ├─ CFRunLoopStop() ─────────►│ CFRunLoopRun() 返回
  │   └─ pthread_join() ◄─────────┘ 线程退出
```

### 信号量同步的必要性

`CFRunLoopAddSource` 必须在目标 RunLoop **已经启动**后才能安全调用。早期版本存在竞态条件：主线程可能在子线程 RunLoop 就绪前就调用 `AddSource`，导致 EventTap 无法触发。

通过 POSIX 具名信号量（`sem_open("/m575_runloop_ready", ...)`）解决：子线程在 `CFRunLoopRun()` 前先 `sem_post` 通知主线程，主线程 `sem_wait` 后再进行 Source 注册。

### 互斥锁保护的资源

`g_interceptor.mutex` 保护：

- `multiplier`（可能被 `interceptor_set_multiplier()` 并发修改）
- `running` 状态标志
- `run_loop` 指针（跨线程读写）

---

## 7. macOS 权限机制说明

### TCC（Transparency, Consent, and Control）

macOS 通过 `TCC.db` 数据库管理应用权限。辅助功能权限存储于：

- 用户级：`~/Library/Application Support/com.apple.TCC/TCC.db`
- 系统级：`/var/db/tcc/TCC.db`（root 进程查此处）

### `sudo` 与 CGEventTap 的冲突

**问题**：以 `sudo` 运行时，进程 euid = 0（root）。系统检查辅助功能权限时查询**系统级 TCC.db**，而用户在「系统设置 → 辅助功能」中的授权写入**用户级 TCC.db**，两者不同，导致权限验证失败。

```
sudo 运行时:
  进程 euid = 0 → 查 /var/db/tcc/TCC.db (系统级) → 未授权 → CGEventTap 失败

普通用户运行时:
  进程 euid = 501 → 查 ~/Library/.../TCC.db (用户级) → 已授权 → CGEventTap 成功
```

**权限矩阵**：

| 运行方式        | HID 配置（hidutil）  | CGEventTap          |
| --------------- | -------------------- | ------------------- |
| 普通用户        | ⚠️ 部分功能受限      | ✅ 需辅助功能授权   |
| sudo            | ✅ 完整权限          | ❌ 辅助功能权限失效 |
| 降权（seteuid） | ✅（先用 root 配置） | ✅ 降权后可获取     |

---

## 8. 已知问题与排查

### 问题：授权后仍然报 CGEventTap 创建失败

**原因**：使用了 `sudo` 运行（参见第 7 节）

**解决**：

```bash
# 方案 A：不使用 sudo
./m575-scroll start -s 1.5

# 方案 B：代码中创建 EventTap 前降权
seteuid(getuid());  // 切换回真实用户身份
// 然后 CGEventTapCreate(...)
```

### 问题：程序退出后 HID 配置未恢复

**原因**：程序被 SIGKILL 强制杀死（跳过了清理代码）

**解决**：

```bash
./m575-scroll stop  # 手动执行 stop 命令，会调用 hid_reset_config()
```

### 问题：开机启动后不生效

**检查 launchd 日志**：

```bash
log show --predicate 'subsystem == "com.apple.launchd"' --last 5m
```

**重新加载**：

```bash
launchctl unload ~/Library/LaunchAgents/com.m575.scroll.plist
launchctl load ~/Library/LaunchAgents/com.m575.scroll.plist
```

---

_文档版本：v1.0 | 最后更新：2026-03-10_
