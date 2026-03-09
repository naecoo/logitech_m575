# m575-scroll

A lightweight macOS command-line tool written in C that lets you customize the scroll wheel speed of a **Logitech M575** (or any HID-compatible mouse). It intercepts scroll events at the CoreGraphics level and multiplies them by a configurable factor, without needing any third-party driver.

## Features

- 🖱️ Real-time scroll speed adjustment (1.0×–10.0×)
- 🔧 HID-level tuning via `hidutil` (disables acceleration, sets scaling)
- 🔁 Optional **launchd** integration for automatic startup on login
- 🛡️ Graceful signal handling (`SIGINT` / `SIGTERM`) for clean shutdown
- 🧵 Thread-safe event tap using CoreGraphics + POSIX threads

## Requirements

| Requirement | Details                                                                                      |
| ----------- | -------------------------------------------------------------------------------------------- |
| OS          | macOS (any version with CoreGraphics)                                                        |
| Compiler    | GCC or Clang with `-framework CoreGraphics`                                                  |
| Permissions | **Accessibility** permission required (System Settings → Privacy & Security → Accessibility) |
| Optional    | `sudo` recommended for full HID configuration access                                         |

## Building

```bash
make
```

The compiled binary will be placed at `./m575-scroll`.

### Install system-wide

```bash
sudo make install
# Installs to /usr/local/bin/m575-scroll
```

### Uninstall

```bash
sudo make uninstall
```

## Usage

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
  -s, --speed <倍数>    速度倍数 (1.0–10.0，默认：2.0)
  -d, --daemon          后台守护进程模式
  -h, --help            显示帮助
  -v, --version         显示版本
```

### Examples

```bash
# Start with default speed (2×)
sudo ./m575-scroll start

# Start with 3× speed
sudo ./m575-scroll start -s 3.0

# Start as a background daemon at 2.5×
sudo ./m575-scroll start -s 2.5 -d

# Check current status
./m575-scroll status

# Stop the running process
./m575-scroll stop

# Set up auto-start on login at 3× speed
sudo ./m575-scroll install-launchd -s 3.0

# Remove auto-start
./m575-scroll uninstall-launchd
```

## Architecture

```
m575-scroll
├── main.c                 — CLI entry point, command dispatch
├── scroll_interceptor.c   — CoreGraphics event tap (scroll multiplier)
├── hid_config.c           — hidutil wrappers (acceleration, scaling)
└── daemon.c               — PID file management, launchd plist creation
```

### Component Overview

| Module                 | Responsibility                                                                                                                                                                       |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `main.c`               | Parses command-line arguments (`getopt_long`), dispatches to command handlers, manages the main run loop, handles `SIGINT`/`SIGTERM` for clean shutdown                              |
| `scroll_interceptor.c` | Creates a `CGEventTap` on `kCGEventScrollWheel`, multiplies both vertical and horizontal scroll deltas by the configured factor, runs in a dedicated POSIX thread with a `CFRunLoop` |
| `hid_config.c`         | Uses `hidutil property --set` to disable mouse acceleration (`UserAcceleration=0`) and set a base scaling value; resets on stop                                                      |
| `daemon.c`             | Writes/reads a PID file in `/tmp/`, generates a launchd `.plist` in `~/Library/LaunchAgents/`, loads/unloads it via `launchctl`                                                      |

## launchd Auto-Start

`install-launchd` writes a property list to:

```
~/Library/LaunchAgents/com.m575.scroll.plist
```

The plist is configured with `RunAtLoad: true` and `KeepAlive: true`, so the daemon automatically starts on login and restarts if it crashes.

To remove it completely:

```bash
./m575-scroll uninstall-launchd
```

This unloads the job from `launchctl` and deletes the plist file.

## Permissions

macOS requires **Accessibility** permission for `CGEventTap` to intercept input events. On first run you may see:

```
创建事件监听失败，请检查辅助功能权限
```

Go to **System Settings → Privacy & Security → Accessibility** and add your terminal or the `m575-scroll` binary to the allow list, then try again.

## Makefile Targets

| Target              | Description                          |
| ------------------- | ------------------------------------ |
| `make` / `make all` | Build the binary                     |
| `make clean`        | Remove object files and binary       |
| `make install`      | Copy binary to `/usr/local/bin/`     |
| `make uninstall`    | Remove binary from `/usr/local/bin/` |
| `make test`         | Build and run `m575-scroll status`   |

## License

MIT
