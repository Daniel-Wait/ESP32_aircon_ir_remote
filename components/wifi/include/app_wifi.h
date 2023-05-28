#ifndef __APP_WIFI_TASKS_H__
#define __APP_WIFI_TASKS_H__

#ifdef __cplusplus
extern "C" {
#endif

#define STA_WIFI_SSID "DESKTOP-DBY"
#define STA_WIFI_PASS "8m1-N023"

#define AP_WIFI_SSID "esp32_aircon_remote"
#define AP_WIFI_PASS "AC_ir_1234"
#define AP_WIFI_CHANNEL 0
#define AP_MAX_STA_CONN 1

void app_wifi_tasks_init(void);

#ifdef __cplusplus
}
#endif

#endif // __APP_WIFI_TASKS_H__