#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
// #include "lwip/lwip_napt.h"

#include "app_wifi.h"
#include "main.h"

// static const char* TAG = "wifi app";
static const char* TAG_STA = "wifi STA";
static const char* TAG_AP = "wifi AP";

void wifi_event_handler(void* event_handler_arg,
                        esp_event_base_t event_base,
                        int32_t event_id,
                        void* event_data)
{
    if (event_base == WIFI_EVENT)
    {
        // ESP_LOGI(TAG_STA, "Event ID : %u", event_id);
    }
    else
    {
        // ESP_LOGW(TAG_STA, "WIFI event expected, but got %s", (char*)event_base);
    }
}

void ip_event_handler(void* event_handler_arg,
                        esp_event_base_t event_base,
                        int32_t event_id,
                        void* event_data)
{
    if (event_base == IP_EVENT)
    {
        // ESP_LOGI(TAG_AP, "Event ID : %u", event_id);
    }
    else
    {
        // ESP_LOGW(TAG_AP, "IP event expected, but got %s", (char*)event_base);
    }
}


static void app_wifi_task(void *arg)
{
    /* Station Wifi Config*/
    wifi_config_t sta_wifi_config = {
        .sta = {
            .ssid = STA_WIFI_SSID,
            .password = STA_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	        .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    /* Access Point Wifi Config*/
    wifi_config_t ap_wifi_config = {
        .ap = {
            .ssid = AP_WIFI_SSID,
            .ssid_len = strlen(AP_WIFI_SSID),
            .channel = AP_WIFI_CHANNEL,
            .password = AP_WIFI_PASS,
            .max_connection = AP_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(AP_WIFI_PASS) == 0) {
        ap_wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_event_handler_instance_t instance_wifi_event;
    esp_event_handler_instance_t instance_ip_event;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &instance_wifi_event);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, ip_event_handler, NULL, &instance_ip_event);

    __unused esp_netif_t* netif_sta = esp_netif_create_default_wifi_sta();
    __unused esp_netif_t* netif_ap = esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    // ESP_ERROR_CHECK(esp_wifi_set_protocol());
    // ESP_ERROR_CHECK(esp_wifi_set_bandwidth());
    // ESP_ERROR_CHECK(esp_wifi_set_ps());
    ESP_ERROR_CHECK(esp_wifi_start());

    while (1)
    {
    }
}

void app_wifi_tasks_init(void)
{
    xTaskCreatePinnedToCore(app_wifi_task, "app_wifi_task", 4096, NULL, TASK_WIFI, NULL, 0);
}
