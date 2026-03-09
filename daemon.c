#include "daemon.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PID_FILE "/tmp/m575-scroll.pid"
#define LAUNCHD_PLIST_SUFFIX "/Library/LaunchAgents/com.m575.scroll.plist"

static void get_plist_path(char *buf, size_t size) {
  const char *home = getenv("HOME");
  if (!home)
    home = "/tmp";
  snprintf(buf, size, "%s%s", home, LAUNCHD_PLIST_SUFFIX);
}

int daemon_save_pid(int pid) {
  FILE *f = fopen(PID_FILE, "w");
  if (!f) {
    perror("fopen");
    return -1;
  }
  fprintf(f, "%d", pid);
  fclose(f);
  return 0;
}

int daemon_read_pid(void) {
  FILE *f = fopen(PID_FILE, "r");
  if (!f) {
    return -1;
  }
  int pid;
  if (fscanf(f, "%d", &pid) != 1) {
    fclose(f);
    return -1;
  }
  fclose(f);
  return pid;
}

void daemon_remove_pid(void) { remove(PID_FILE); }

bool daemon_is_running(void) {
  int pid = daemon_read_pid();
  if (pid <= 0) {
    return false;
  }
  return (kill(pid, 0) == 0);
}

int daemon_create_launchd(const char *exec_path, double speed) {
  char plist_path[512];
  get_plist_path(plist_path, sizeof(plist_path));
  FILE *f = fopen(plist_path, "w");
  if (!f) {
    perror("fopen");
    return -1;
  }

  fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  fprintf(f, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\"\n");
  fprintf(f, "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
  fprintf(f, "<plist version=\"1.0\">\n");
  fprintf(f, "<dict>\n");
  fprintf(f, "    <key>Label</key>\n");
  fprintf(f, "    <string>com.m575.scroll</string>\n");
  fprintf(f, "    <key>ProgramArguments</key>\n");
  fprintf(f, "    <array>\n");
  fprintf(f, "        <string>%s</string>\n", exec_path);
  fprintf(f, "        <string>start</string>\n");
  fprintf(f, "        <string>--speed</string>\n");
  fprintf(f, "        <string>%.2f</string>\n", speed);
  fprintf(f, "        <string>--daemon</string>\n");
  fprintf(f, "    </array>\n");
  fprintf(f, "    <key>RunAtLoad</key>\n");
  fprintf(f, "    <true/>\n");
  fprintf(f, "    <key>KeepAlive</key>\n");
  fprintf(f, "    <true/>\n");
  fprintf(f, "</dict>\n");
  fprintf(f, "</plist>\n");

  fclose(f);
  printf("✅ launchd 配置已创建：%s\n", plist_path);
  return 0;
}

int daemon_load_launchd(void) {
  char plist_path[512];
  get_plist_path(plist_path, sizeof(plist_path));
  char cmd[600];
  snprintf(cmd, sizeof(cmd), "launchctl load %s 2>/dev/null", plist_path);
  if (system(cmd) != 0) {
    fprintf(stderr, "⚠️  加载 launchd 配置失败\n");
    return -1;
  }
  printf("✅ launchd 配置已加载\n");
  return 0;
}

int daemon_unload_launchd(void) {
  char plist_path[512];
  get_plist_path(plist_path, sizeof(plist_path));
  char cmd[600];
  snprintf(cmd, sizeof(cmd), "launchctl unload %s 2>/dev/null", plist_path);
  if (system(cmd) != 0) {
    fprintf(stderr, "⚠️  卸载 launchd 配置失败\n");
    return -1;
  }
  remove(plist_path);
  printf("✅ launchd 配置已卸载\n");
  return 0;
}