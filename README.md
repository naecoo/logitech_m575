# m575-scroll

一款用 C 语言编写的轻量级 macOS 命令行工具，用于自定义 **Logitech M575**（或任何兼容 HID 的鼠标）的滚轮速度。它在 CoreGraphics 层面拦截滚动事件，并将其乘以可配置的倍数，无需任何第三方驱动。

## 功能特性

- 🖱️ 实时滚轮速度调整（1.0×–10.0×）
- 🔧 通过 `hidutil` 进行 HID 级别调节（禁用加速度、设置缩放比例）
- 🔁 可选的 **launchd** 集成，支持登录时自动启动
- 🛡️ 优雅的信号处理（`SIGINT` / `SIGTERM`），确保干净退出
- 🧵 基于 CoreGraphics + POSIX 线程的线程安全事件监听

## 系统要求

| 要求     | 说明                                                     |
| -------- | -------------------------------------------------------- |
| 操作系统 | macOS（任何支持 CoreGraphics 的版本）                    |
| 编译器   | GCC 或 Clang，需支持 `-framework CoreGraphics`           |
| 权限     | 需要**辅助功能**权限（系统设置 → 隐私与安全 → 辅助功能） |
| 可选     | 建议使用 `sudo` 以获得完整的 HID 配置访问权限            |

## 构建

```bash
make
```

编译后的二进制文件将输出到 `./m575-scroll`。

### 全局安装

```bash
sudo make install
# 安装至 /usr/local/bin/m575-scroll
```

### 卸载

```bash
sudo make uninstall
```

## 使用说明

```
用法：m575-scroll [命令] [选项]

命令:
  start             启动滚轮速度调整
  stop              停止滚轮速度调整
  status            查看当前状态
  config            管理配置
  install-launchd   设置开机自动启动
  uninstall-launchd 移除开机自动启动
  help              显示帮助信息

选项:
  -s, --speed <倍数>    速度倍数（1.0–10.0，默认：2.0）
  -d, --daemon          后台守护进程模式
  -h, --help            显示帮助
  -v, --version         显示版本
```

### 示例

```bash
# 以默认速度（2×）启动
sudo ./m575-scroll start

# 以 3× 速度启动
sudo ./m575-scroll start -s 3.0

# 以 2.5× 速度在后台守护进程模式启动
sudo ./m575-scroll start -s 2.5 -d

# 查看当前状态
./m575-scroll status

# 停止运行中的进程
./m575-scroll stop

# 设置登录时以 3× 速度自动启动
sudo ./m575-scroll install-launchd -s 3.0

# 移除自动启动
./m575-scroll uninstall-launchd
```

## 项目架构

```
m575-scroll
├── main.c                 — CLI 入口，命令分发
├── scroll_interceptor.c   — CoreGraphics 事件监听（滚动倍数处理）
├── hid_config.c           — hidutil 封装（加速度、缩放比例）
└── daemon.c               — PID 文件管理，launchd plist 生成
```

### 模块说明

| 模块                   | 职责                                                                                                                     |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `main.c`               | 使用 `getopt_long` 解析命令行参数，分发命令处理函数，管理主运行循环，处理 `SIGINT`/`SIGTERM` 信号以干净退出              |
| `scroll_interceptor.c` | 在 `kCGEventScrollWheel` 上创建 `CGEventTap`，将垂直和水平滚动增量乘以配置倍数，运行在独立的 POSIX 线程和 `CFRunLoop` 中 |
| `hid_config.c`         | 使用 `hidutil property --set` 禁用鼠标加速（`UserAcceleration=0`）并设置基础缩放值，停止时重置                           |
| `daemon.c`             | 在 `/tmp/` 中写入/读取 PID 文件，在 `~/Library/LaunchAgents/` 中生成 launchd `.plist`，通过 `launchctl` 加载/卸载        |

## launchd 开机自动启动

`install-launchd` 命令会将配置文件写入：

```
~/Library/LaunchAgents/com.m575.scroll.plist
```

plist 配置了 `RunAtLoad: true` 和 `KeepAlive: true`，因此守护进程会在登录时自动启动，崩溃后也会自动重启。

如需完全移除，执行：

```bash
./m575-scroll uninstall-launchd
```

此命令会从 `launchctl` 中卸载任务并删除 plist 文件。

## 权限说明

macOS 要求应用具有**辅助功能**权限，`CGEventTap` 才能拦截输入事件。首次运行时可能会看到：

```
创建事件监听失败，请检查辅助功能权限
```

请前往**系统设置 → 隐私与安全 → 辅助功能**，将终端或 `m575-scroll` 二进制文件添加到允许列表，然后重试。

## Makefile 目标

| 目标                | 说明                                 |
| ------------------- | ------------------------------------ |
| `make` / `make all` | 构建二进制文件                       |
| `make clean`        | 删除目标文件和二进制文件             |
| `make install`      | 将二进制文件复制到 `/usr/local/bin/` |
| `make uninstall`    | 从 `/usr/local/bin/` 移除二进制文件  |
| `make test`         | 构建并运行 `m575-scroll status`      |

## 许可证

MIT
