#ifndef HID_CONFIG_H
#define HID_CONFIG_H

// 应用 HID 配置
int hid_apply_config(double speed);

// 重置 HID 配置
int hid_reset_config(void);

// 显示当前配置
void hid_show_config(void);

#endif