#ifndef DAEMON_H
#define DAEMON_H

#include <stdbool.h>

// 保存 PID 文件
int daemon_save_pid(int pid);

// 读取 PID 文件
int daemon_read_pid(void);

// 删除 PID 文件
void daemon_remove_pid(void);

// 检查进程是否运行
bool daemon_is_running(void);

// 创建 launchd 配置
int daemon_create_launchd(const char *exec_path, double speed);

// 加载 launchd 配置
int daemon_load_launchd(void);

// 卸载 launchd 配置
int daemon_unload_launchd(void);

#endif