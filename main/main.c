#include "sdkconfig.h"

#include "nvs_flash.h"

#include "app_ir_tasks.h"
#include "app_wifi.h"

static const char *TAG = "aircon_main";

void app_main(void)
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    app_ir_tasks_init();
    // app_wifi_tasks_init();
}