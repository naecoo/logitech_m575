#include "hid_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_command(const char *cmd) {
    int ret = system(cmd);
    return WEXITSTATUS(ret);
}

int hid_apply_config(double speed) {
    char cmd[256];

    // 禁用加速度
    snprintf(cmd, sizeof(cmd), "hidutil property --set '{\"UserAcceleration\": 0}' 2>/dev/null");
    if (run_command(cmd) != 0) {
        fprintf(stderr, "⚠️  设置加速度失败\n");
    }

    // 设置速度
    snprintf(cmd, sizeof(cmd), "hidutil property --set '{\"Scaling\": %.2f}' 2>/dev/null", speed);
    if (run_command(cmd) != 0) {
        fprintf(stderr, "⚠️  设置速度失败\n");
    }

    printf("✅ HID 配置已应用 (速度：%.2f)\n", speed);
    return 0;
}

int hid_reset_config(void) {
    run_command("hidutil property --set '{\"UserAcceleration\": 1}' 2>/dev/null");
    run_command("hidutil property --set '{\"Scaling\": 1}' 2>/dev/null");
    printf("✅ HID 配置已重置\n");
    return 0;
}

void hid_show_config(void) {
    printf("=== 当前 HID 配置 ===\n");
    system("hidutil property --get '{\"UserAcceleration\"}' 2>/dev/null");
    system("hidutil property --get '{\"Scaling\"}' 2>/dev/null");
    printf("\n=== 系统鼠标设置 ===\n");
    system("defaults read -g com.apple.mouse.scaling 2>/dev/null || echo '未设置'");
}