#include <stdio.h>

#include <stdio.h>
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_err.h"
#include "esp_log.h"


#include "driver/rmt.h"
#include "driver/gpio.h"

#include "ir_tools.h"

static const char *TAG = "aircon";

static rmt_channel_t tx_rmt_chan = RMT_CHANNEL_0;

static void localTxEndCallback(rmt_channel_t channel, void *arg)
{
    ESP_LOGI(TAG, "rmt_tx_cmplt : chan %d", channel);
}

/**
 * @brief RMT Transmit Task
 *
 */
static void ir_tx_task(void *arg)
{
    uint32_t addr = 0xB24D;
    uint32_t cmd = (0xBF40 << 16) | 0x00FF;
    rmt_item32_t *items = NULL;
    size_t length = 0;

    rmt_config_t rmt_tx_config = RMT_DEFAULT_CONFIG_TX(GPIO_NUM_2, tx_rmt_chan);
    rmt_tx_config.tx_config.carrier_en = true;
    rmt_tx_config.tx_config.carrier_freq_hz = 37900;
    rmt_config(&rmt_tx_config);
    rmt_driver_install(tx_rmt_chan, 0, 0);

    // __unused rmt_tx_end_callback_t previous = rmt_register_tx_end_callback(localTxEndCallback, (void *)0xABCD);
    

    ir_builder_config_t ir_builder_config = IR_BUILDER_DEFAULT_CONFIG((ir_dev_t)tx_rmt_chan);
    ir_builder_config.flags |= IR_TOOLS_FLAGS_PROTO_EXT; // Using extended IR protocols (both NEC and RC5 have extended version)

    ir_builder_t* ir_builder = ir_builder_rmt_new_samsung(&ir_builder_config);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_LOGI(TAG, "Send command 0x%x to address 0x%x", cmd, addr);
        vTaskDelay(pdMS_TO_TICKS(500));
        // Send new key code
        ESP_ERROR_CHECK(ir_builder->build_frame(ir_builder, addr, cmd));
        ESP_ERROR_CHECK(ir_builder->get_result(ir_builder, &items, &length));
        //To send data according to the waveform items.
        rmt_write_items(tx_rmt_chan, items, length, false);
    }
    ir_builder->del(ir_builder);
    rmt_driver_uninstall(tx_rmt_chan);
    vTaskDelete(NULL);
}
void app_main(void)
{
    xTaskCreate(ir_tx_task, "ir_tx_task", 2048, NULL, 10, NULL);
}