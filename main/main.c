#include <stdio.h>

#include <stdio.h>
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_err.h"
#include "esp_log.h"


#include "driver/rmt.h"
#include "driver/gpio.h"
#include "driver/timer.h"

#include "ir_tools.h"

static const char *TAG = "aircon";

static const rmt_channel_t tx_rmt_chan = RMT_CHANNEL_0;
static const rmt_channel_t rx_rmt_chan = RMT_CHANNEL_1;

SemaphoreHandle_t xSemaphoreRmtTx;
SemaphoreHandle_t xSemaphoreRmtRx;


static void localTxEndCallback(rmt_channel_t channel, void *arg)
{
    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xSemaphoreGiveFromISR(xSemaphoreRmtTx, &xHigherPriorityTaskWoken);
}

/**
 * @brief RMT Transmit Task
 *
 */
static void ir_tx_task(void *arg)
{
    uint32_t addr = 0xB24D;
    // uint32_t cmd = (0xBF40 << 16) | 0x00FF;
    uint32_t arr_cmd[2] = {0xdd2207f8, 0xf80721de};
    rmt_item32_t *items = NULL;
    size_t length = 0;

    rmt_config_t rmt_tx_config = RMT_DEFAULT_CONFIG_TX(GPIO_NUM_2, tx_rmt_chan);
    rmt_tx_config.tx_config.carrier_en = true;
    rmt_tx_config.tx_config.carrier_freq_hz = 37900;
    rmt_config(&rmt_tx_config);
    rmt_driver_install(tx_rmt_chan, 0, 0);

    __unused rmt_tx_end_callback_t previous = rmt_register_tx_end_callback(localTxEndCallback, (void *)&addr);

    ir_builder_config_t ir_builder_config = IR_BUILDER_DEFAULT_CONFIG((ir_dev_t)tx_rmt_chan);
    ir_builder_config.flags |= IR_TOOLS_FLAGS_PROTO_EXT; // Using extended IR protocols (both NEC and RC5 have extended version)

    ir_builder_t* ir_builder = ir_builder_rmt_new_samsung(&ir_builder_config);

    uint8_t cmd_num = 0;
    while (1) {
        uint32_t cmd = arr_cmd[cmd_num];
        vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_LOGI(TAG, "Send command 0x%lx to address 0x%lx", cmd, addr);
        vTaskDelay(pdMS_TO_TICKS(500));
        // Send new key code
        ESP_ERROR_CHECK(ir_builder->build_frame(ir_builder, addr, cmd));
        ESP_ERROR_CHECK(ir_builder->get_result(ir_builder, &items, &length));
        //To send data according to the waveform items.
        rmt_write_items(tx_rmt_chan, items, length, true);
#ifdef HACK_DELAY_5500US
        // Plan here was to delay for requisite 5500us for repeat send
        // Rather opted to make the ending code high ticks = 5500us
        vTaskDelay(pdMS_TO_TICKS(5));
        taskDISABLE_INTERRUPTS();
        ets_delay_us(500);
        taskENABLE_INTERRUPTS();
#endif
        rmt_write_items(tx_rmt_chan, items, length, false);
        cmd_num += 1;
        cmd_num %= 2;

        if (0) {break;}
    }
    ir_builder->del(ir_builder);
    rmt_driver_uninstall(tx_rmt_chan);
    vTaskDelete(NULL);
}


static void ir_rx_task(void *arg)
{
    uint32_t addr = 0;
    uint32_t cmd = 0;
    size_t length = 0;
    bool repeat = false;
    RingbufHandle_t rb = NULL;
    rmt_item32_t *items = NULL;

    rmt_config_t rmt_rx_config = RMT_DEFAULT_CONFIG_RX(GPIO_NUM_5, rx_rmt_chan);
    rmt_rx_config.rx_config.idle_threshold = 5100;
    rmt_config(&rmt_rx_config);
    rmt_driver_install(rx_rmt_chan, 1000, 0);

    ir_parser_config_t ir_parser_config = IR_PARSER_DEFAULT_CONFIG((ir_dev_t)rx_rmt_chan);
    ir_parser_config.margin_us = 200;
    ir_parser_config.flags |= IR_TOOLS_FLAGS_PROTO_EXT; // Using extended IR protocols
    ir_parser_t *ir_parser = NULL;
    ir_parser = ir_parser_rmt_new_samsung(&ir_parser_config);

    //get RMT RX ringbuffer
    rmt_get_ringbuf_handle(rx_rmt_chan, &rb);
    assert(rb != NULL);
    // Start receive
    rmt_rx_start(rx_rmt_chan, true);
    while (1) 
    {
        items = (rmt_item32_t *) xRingbufferReceive(rb, &length, portMAX_DELAY);
        if (items)
        {
            length /= 4; // one RMT = 4 Bytes
            if (ir_parser->input(ir_parser, items, length) == ESP_OK) 
            {
                // xSemaphoreGive(xSemaphoreRmtRx);
                if (ir_parser->get_scan_code(ir_parser, &addr, &cmd, &repeat) == ESP_OK)
                {
                    ESP_LOGI(TAG, "Scan Code %s --- addr: 0x%lx cmd: 0x%lx", repeat ? "(repeat)" : "", addr, cmd);
                }
            }
            //after parsing the data, return spaces to ringbuffer.
            vRingbufferReturnItem(rb, (void *) items);
            if (0) {break;}
        }
    }
    ir_parser->del(ir_parser);
    rmt_driver_uninstall(rx_rmt_chan);
    vTaskDelete(NULL);
}

static void debug_print_task(void *arg)
{
    while (1)
    {
        if (xSemaphoreTake(xSemaphoreRmtTx, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI(TAG, "rmt_tx_cmplt");
        }
        if (xSemaphoreTake(xSemaphoreRmtRx, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI(TAG, "rmt_rx_line_active");
        }
    }
    vTaskDelete(NULL);
}


void app_main(void)
{
    xSemaphoreRmtTx = xSemaphoreCreateBinary();
    xSemaphoreRmtRx = xSemaphoreCreateBinary();
    xTaskCreate(debug_print_task, "debug_print_task", 2048, NULL, 9, NULL);
    xTaskCreate(ir_tx_task, "ir_tx_task", 2048, NULL, 10, NULL);
    xTaskCreate(ir_rx_task, "ir_rx_task", 2048, NULL, 11, NULL);
}