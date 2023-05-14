// Copyright 2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.#include <stdlib.h>
#include <sys/cdefs.h>
#include "esp_log.h"
#include "ir_tools.h"
#include "ir_timings.h"
#include "driver/rmt.h"

static const char *TAG = "samsung_builder";
#define SAMSUNG_CHECK(a, str, goto_tag, ret_value, ...)                               \
    do                                                                            \
    {                                                                             \
        if (!(a))                                                                 \
        {                                                                         \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            ret = ret_value;                                                      \
            goto goto_tag;                                                        \
        }                                                                         \
    } while (0)

typedef struct {
    ir_builder_t parent;
    uint32_t buffer_size;
    uint32_t cursor;
    uint32_t flags;
    uint32_t leading_code_high_ticks;
    uint32_t leading_code_low_ticks;
    uint32_t payload_logic0_high_ticks;
    uint32_t payload_logic0_low_ticks;
    uint32_t payload_logic1_high_ticks;
    uint32_t payload_logic1_low_ticks;
    uint32_t ending_code_high_ticks;
    uint32_t ending_code_low_ticks;
    bool inverse;
    rmt_item32_t buffer[0];
} samsung_builder_t;

static esp_err_t samsung_builder_make_head(ir_builder_t *builder)
{
    samsung_builder_t *samsung_builder = __containerof(builder, samsung_builder_t, parent);
    samsung_builder->cursor = 0;
    samsung_builder->buffer[samsung_builder->cursor].level0 = !samsung_builder->inverse;
    samsung_builder->buffer[samsung_builder->cursor].duration0 = samsung_builder->leading_code_high_ticks;
    samsung_builder->buffer[samsung_builder->cursor].level1 = samsung_builder->inverse;
    samsung_builder->buffer[samsung_builder->cursor].duration1 = samsung_builder->leading_code_low_ticks;
    samsung_builder->cursor += 1;
    return ESP_OK;
}

static esp_err_t samsung_builder_make_logic0(ir_builder_t *builder)
{
    samsung_builder_t *samsung_builder = __containerof(builder, samsung_builder_t, parent);
    samsung_builder->buffer[samsung_builder->cursor].level0 = !samsung_builder->inverse;
    samsung_builder->buffer[samsung_builder->cursor].duration0 = samsung_builder->payload_logic0_high_ticks;
    samsung_builder->buffer[samsung_builder->cursor].level1 = samsung_builder->inverse;
    samsung_builder->buffer[samsung_builder->cursor].duration1 = samsung_builder->payload_logic0_low_ticks;
    samsung_builder->cursor += 1;
    return ESP_OK;
}

static esp_err_t samsung_builder_make_logic1(ir_builder_t *builder)
{
    samsung_builder_t *samsung_builder = __containerof(builder, samsung_builder_t, parent);
    samsung_builder->buffer[samsung_builder->cursor].level0 = !samsung_builder->inverse;
    samsung_builder->buffer[samsung_builder->cursor].duration0 = samsung_builder->payload_logic1_high_ticks;
    samsung_builder->buffer[samsung_builder->cursor].level1 = samsung_builder->inverse;
    samsung_builder->buffer[samsung_builder->cursor].duration1 = samsung_builder->payload_logic1_low_ticks;
    samsung_builder->cursor += 1;
    return ESP_OK;
}

static esp_err_t samsung_builder_make_end(ir_builder_t *builder)
{
    samsung_builder_t *samsung_builder = __containerof(builder, samsung_builder_t, parent);
    samsung_builder->buffer[samsung_builder->cursor].level0 = !samsung_builder->inverse;
    samsung_builder->buffer[samsung_builder->cursor].duration0 = samsung_builder->ending_code_high_ticks;
    samsung_builder->buffer[samsung_builder->cursor].level1 = samsung_builder->inverse;
    samsung_builder->buffer[samsung_builder->cursor].duration1 = samsung_builder->ending_code_low_ticks;
    samsung_builder->cursor += 1;
    samsung_builder->buffer[samsung_builder->cursor].val = 0;
    samsung_builder->cursor += 1;
    return ESP_OK;
}

static esp_err_t samsung_build_frame(ir_builder_t *builder, uint32_t address, uint32_t command)
{
    esp_err_t ret = ESP_OK;
    samsung_builder_t *samsung_builder = __containerof(builder, samsung_builder_t, parent);
    if (!samsung_builder->flags & IR_TOOLS_FLAGS_PROTO_EXT) {
        uint8_t high_byte = (address >> 8) & 0xFF;
        uint8_t low_byte = address & 0xFF;
        SAMSUNG_CHECK(low_byte == (~high_byte & 0xFF), "address [0:15] not match standard NEC protocol", err, ESP_ERR_INVALID_ARG);
        high_byte = (command >> 24) & 0xFF;
        low_byte =  (command >> 16) & 0xFF;
        SAMSUNG_CHECK(low_byte == (~high_byte & 0xFF), "command [16:31] not match standard NEC protocol", err, ESP_ERR_INVALID_ARG);
        high_byte = (command >> 8) & 0xFF;
        low_byte =  command & 0xFF;
        SAMSUNG_CHECK(low_byte == (~high_byte & 0xFF), "command [32:47] not match standard NEC protocol", err, ESP_ERR_INVALID_ARG);
    }
    builder->make_head(builder);
    // LSB -> MSB
    for (int i = 0; i < 16; i++) {
        if (address & (1 << i)) {
            builder->make_logic1(builder);
        } else {
            builder->make_logic0(builder);
        }
    }
    for (int i = 0; i < 32; i++) {
        if (command & (1 << i)) {
            builder->make_logic1(builder);
        } else {
            builder->make_logic0(builder);
        }
    }
    builder->make_end(builder);
    return ESP_OK;
err:
    return ret;
}


static esp_err_t samsung_builder_get_result(ir_builder_t *builder, void *result, size_t *length)
{
    esp_err_t ret = ESP_OK;
    samsung_builder_t *samsung_builder = __containerof(builder, samsung_builder_t, parent);
    SAMSUNG_CHECK(result && length, "result and length can't be null", err, ESP_ERR_INVALID_ARG);
    *(rmt_item32_t **)result = samsung_builder->buffer;
    *length = samsung_builder->cursor;
    return ESP_OK;
err:
    return ret;
}

static esp_err_t samsung_builder_del(ir_builder_t *builder)
{
    samsung_builder_t *samsung_builder = __containerof(builder, samsung_builder_t, parent);
    free(samsung_builder);
    return ESP_OK;
}

ir_builder_t* ir_builder_rmt_new_samsung(const ir_builder_config_t *config)
{
    ir_builder_t *ret = NULL;
    SAMSUNG_CHECK(config, "nec configuration can't be null", err, NULL);
    SAMSUNG_CHECK(config->buffer_size, "buffer size can't be zero", err, NULL);

    uint32_t builder_size = sizeof(samsung_builder_t) + config->buffer_size * sizeof(rmt_item32_t);
    samsung_builder_t *samsung_builder = calloc(1, builder_size);
    SAMSUNG_CHECK(samsung_builder, "request memory for samsung_builder failed", err, NULL);

    samsung_builder->buffer_size = config->buffer_size;
    samsung_builder->flags = config->flags;
    if (config->flags & IR_TOOLS_FLAGS_INVERSE) {
        samsung_builder->inverse = true;
    }

    uint32_t counter_clk_hz = 0;
    SAMSUNG_CHECK(rmt_get_counter_clock((rmt_channel_t)config->dev_hdl, &counter_clk_hz) == ESP_OK,
              "get rmt counter clock failed", err, NULL);
    float ratio = (float)counter_clk_hz / 1e6;
    ESP_LOGW("rmt_setup", "ratio = %.6f", ratio);
    samsung_builder->leading_code_high_ticks = (uint32_t)(ratio * SAMSUNG_LEADING_CODE_HIGH_US);
    samsung_builder->leading_code_low_ticks = (uint32_t)(ratio * SAMSUNG_LEADING_CODE_LOW_US);;
    samsung_builder->payload_logic0_high_ticks = (uint32_t)(ratio * SAMSUNG_PAYLOAD_ZERO_HIGH_US);
    samsung_builder->payload_logic0_low_ticks = (uint32_t)(ratio * SAMSUNG_PAYLOAD_ZERO_LOW_US);
    samsung_builder->payload_logic1_high_ticks = (uint32_t)(ratio * SAMSUNG_PAYLOAD_ONE_HIGH_US);
    samsung_builder->payload_logic1_low_ticks = (uint32_t)(ratio * SAMSUNG_PAYLOAD_ONE_LOW_US);
    samsung_builder->ending_code_high_ticks = (uint32_t)(ratio * SAMSUNG_ENDING_CODE_HIGH_US);
    samsung_builder->ending_code_low_ticks = (uint32_t)(ratio * SAMSUNG_ENDING_CODE_LOW_US);; // duration fields of rmt_item32_t only take 15 bits (0x7FFF is max)
    samsung_builder->parent.make_head = samsung_builder_make_head;
    samsung_builder->parent.make_logic0 = samsung_builder_make_logic0;
    samsung_builder->parent.make_logic1 = samsung_builder_make_logic1;
    samsung_builder->parent.make_end = samsung_builder_make_end;
    samsung_builder->parent.build_frame = samsung_build_frame;
    samsung_builder->parent.get_result = samsung_builder_get_result;
    samsung_builder->parent.del = samsung_builder_del;
    samsung_builder->parent.repeat_period_ms = 5;
    return &samsung_builder->parent;
err:
    return ret;
}
