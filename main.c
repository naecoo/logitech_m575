#include "daemon.h"
#include "hid_config.h"
#include "scroll_interceptor.h"
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define VERSION "1.0.0"

static volatile sig_atomic_t g_running = 1;

void signal_handler(int sig) {
  (void)sig;
  g_running = 0;
}

void print_usage(const char *prog) {
  printf("用法：%s [命令] [选项]\n\n", prog);
  printf("命令:\n");
  printf("  start       启动滚轮速度调整\n");
  printf("  stop        停止滚轮速度调整\n");
  printf("  status      查看当前状态\n");
  printf("  config      管理配置\n");
  printf("  help        显示帮助信息\n\n");
  printf("选项:\n");
  printf("  -s, --speed <倍数>    滚轮速度倍数 (1.0-10.0, 默认：2.0)\n");
  printf("  -d, --daemon          以守护进程模式运行\n");
  printf("  -h, --help            显示帮助信息\n");
  printf("  -v, --version         显示版本信息\n\n");
  printf("示例:\n");
  printf("  %s start -s 2.5\n", prog);
  printf("  %s start -s 3.0 -d\n", prog);
  printf("  %s stop\n", prog);
  printf("  %s status\n", prog);
}

void print_version(void) {
  printf("m575-scroll-c version %s\n", VERSION);
  printf("macOS 罗技 M575 滚轮速度自定义工具\n");
}

int cmd_start(double speed, int daemon_mode) {
  printf("🖱️  启动 M575 滚轮速度调整 (倍数：%.2f)\n", speed);

  if (geteuid() != 0) {
    printf("⚠️  警告：建议使用 sudo 运行以获得完整功能\n");
  }

  // 应用 HID 配置
  hid_apply_config(speed);

  // 初始化拦截器
  if (interceptor_init(speed) != 0) {
    fprintf(stderr, "初始化拦截器失败\n");
    return -1;
  }

  // 启动拦截器
  if (interceptor_start() != 0) {
    fprintf(stderr, "启动拦截器失败\n");
    return -1;
  }

  // 保存 PID
  daemon_save_pid(getpid());

  if (daemon_mode) {
    printf("🔧 以守护进程模式运行\n");
    printf("按 Ctrl+C 停止\n");
  } else {
    printf("按 Ctrl+C 停止\n");
  }

  // 设置信号处理
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // 等待退出信号
  while (g_running) {
    sleep(1);
  }

  // 清理
  interceptor_stop();
  daemon_remove_pid();
  printf("✅ 已停止\n");

  return 0;
}

int cmd_stop(void) {
  printf("🛑 停止滚轮速度调整\n");

  if (daemon_is_running()) {
    int pid = daemon_read_pid();
    kill(pid, SIGTERM);
    sleep(1);
    if (daemon_is_running()) {
      kill(pid, SIGKILL);
    }
    printf("✅ 进程已终止 (PID: %d)\n", pid);
  } else {
    printf("ℹ️  没有运行中的进程\n");
  }

  hid_reset_config();
  daemon_remove_pid();
  printf("✅ 已恢复默认设置\n");

  return 0;
}

int cmd_status(void) {
  printf("=== M575 滚轮速度状态 ===\n\n");

  if (daemon_is_running()) {
    printf("状态：🟢 运行中\n");
    printf("PID: %d\n", daemon_read_pid());
    printf("速度倍数：%.2f\n", interceptor_get_multiplier());
  } else {
    printf("状态：🔴 未运行\n");
  }

  printf("\n");
  hid_show_config();

  return 0;
}

int cmd_config(double speed) {
  printf("✅ 配置已保存 (速度：%.2f)\n", speed);
  // 这里可以保存到配置文件
  return 0;
}

int cmd_install_launchd(const char *exec_path, double speed) {
  printf("🔧 创建开机启动配置\n");

  if (daemon_create_launchd(exec_path, speed) != 0) {
    return -1;
  }

  if (daemon_load_launchd() != 0) {
    return -1;
  }

  printf("✅ 开机启动配置完成\n");
  return 0;
}

int cmd_uninstall_launchd(void) {
  printf("🔧 移除开机启动配置\n");
  daemon_unload_launchd();
  printf("✅ 开机启动配置已移除\n");
  return 0;
}

int main(int argc, char *argv[]) {
  static struct option long_options[] = {{"speed", required_argument, 0, 's'},
                                         {"daemon", no_argument, 0, 'd'},
                                         {"help", no_argument, 0, 'h'},
                                         {"version", no_argument, 0, 'v'},
                                         {0, 0, 0, 0}};

  double speed = 2.0;
  int daemon_mode = 0;
  int opt;
  int option_index = 0;

  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  // 解析全局选项
  while ((opt = getopt_long(argc, argv, "s:dhv", long_options,
                            &option_index)) != -1) {
    switch (opt) {
    case 's':
      speed = atof(optarg);
      if (speed < 1.0 || speed > 10.0) {
        fprintf(stderr, "错误：速度倍数必须在 1.0-10.0 之间\n");
        return 1;
      }
      break;
    case 'd':
      daemon_mode = 1;
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    case 'v':
      print_version();
      return 0;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  const char *command = argv[optind];

  if (strcmp(command, "start") == 0) {
    return cmd_start(speed, daemon_mode);
  } else if (strcmp(command, "stop") == 0) {
    return cmd_stop();
  } else if (strcmp(command, "status") == 0) {
    return cmd_status();
  } else if (strcmp(command, "config") == 0) {
    return cmd_config(speed);
  } else if (strcmp(command, "install-launchd") == 0) {
    return cmd_install_launchd(argv[0], speed);
  } else if (strcmp(command, "uninstall-launchd") == 0) {
    return cmd_uninstall_launchd();
  } else if (strcmp(command, "help") == 0) {
    print_usage(argv[0]);
    return 0;
  } else {
    fprintf(stderr, "未知命令：%s\n", command);
    print_usage(argv[0]);
    return 1;
  }

  return 0;
}