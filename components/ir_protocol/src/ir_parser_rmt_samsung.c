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
// limitations under the License.
#include <stdlib.h>
#include <sys/cdefs.h>
#include "esp_log.h"
#include "ir_tools.h"
#include "ir_timings.h"
#include "driver/rmt.h"

static const char *TAG = "samsung_parser";
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

#define SAMSUNG_DATA_FRAME_RMT_WORDS (48)

typedef struct {
    ir_parser_t parent;
    uint32_t flags;
    uint32_t leading_code_high_ticks;
    uint32_t leading_code_low_ticks;
    uint32_t ending_code_high_ticks;
    uint32_t ending_code_low_ticks;
    uint32_t payload_logic0_high_ticks;
    uint32_t payload_logic0_low_ticks;
    uint32_t payload_logic1_high_ticks;
    uint32_t payload_logic1_low_ticks;
    uint32_t margin_ticks;
    rmt_item32_t *buffer;
    uint8_t buffer_length;
    uint32_t cursor;
    uint32_t last_address;
    uint32_t last_command;
    bool inverse;
} samsung_parser_t;

static inline bool samsung_check_in_range(uint32_t raw_ticks, uint32_t target_ticks, uint32_t margin_ticks)
{
    return (raw_ticks < (target_ticks + margin_ticks)) && (raw_ticks > (target_ticks - margin_ticks));
}

static bool samsung_parse_head(samsung_parser_t *samsung_parser)
{
    samsung_parser->cursor = 0;
    rmt_item32_t item = samsung_parser->buffer[samsung_parser->cursor];

    bool level = (item.level0 == samsung_parser->inverse) && (item.level1 != samsung_parser->inverse);
    if (!level)
    {
        ESP_LOGW("parser error", "level : {%u, %u}\n", item.level0, item.level1);
        return false;
    }
    bool margin = samsung_check_in_range(item.duration0, samsung_parser->leading_code_high_ticks, samsung_parser->margin_ticks);
    if (!margin)
    {
        ESP_LOGW("parser error", "0 : {%u, %u}\n", item.duration0, samsung_parser->leading_code_high_ticks);
        return false;
    }
    margin &= samsung_check_in_range(item.duration1, samsung_parser->leading_code_low_ticks, samsung_parser->margin_ticks);
    if (!margin)
    {
        ESP_LOGW("parser error", "1 : {%u, %u}\n", item.duration1, samsung_parser->leading_code_low_ticks);
        return false;
    }
    bool ret = level && margin;
    samsung_parser->cursor += 1;
    return ret;
}

static bool samsung_parse_logic0(samsung_parser_t *samsung_parser)
{
    rmt_item32_t item = samsung_parser->buffer[samsung_parser->cursor];
    bool ret = (item.level0 == samsung_parser->inverse) && (item.level1 != samsung_parser->inverse) &&
               samsung_check_in_range(item.duration0, samsung_parser->payload_logic0_high_ticks, samsung_parser->margin_ticks) &&
               samsung_check_in_range(item.duration1, samsung_parser->payload_logic0_low_ticks, samsung_parser->margin_ticks);
    return ret;
}

static bool samsung_parse_logic1(samsung_parser_t *samsung_parser)
{
    rmt_item32_t item = samsung_parser->buffer[samsung_parser->cursor];
    bool ret = (item.level0 == samsung_parser->inverse) && (item.level1 != samsung_parser->inverse) &&
               samsung_check_in_range(item.duration0, samsung_parser->payload_logic1_high_ticks, samsung_parser->margin_ticks) &&
               samsung_check_in_range(item.duration1, samsung_parser->payload_logic1_low_ticks, samsung_parser->margin_ticks);
    return ret;
}

static esp_err_t samsung_parse_logic(ir_parser_t *parser, bool *logic)
{
    esp_err_t ret = ESP_FAIL;
    bool logic_value = false;
    samsung_parser_t *samsung_parser = __containerof(parser, samsung_parser_t, parent);
    if (samsung_parse_logic0(samsung_parser)) {
        logic_value = false;
        ret = ESP_OK;
    } else if (samsung_parse_logic1(samsung_parser)) {
        logic_value = true;
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        *logic = logic_value;
    }
    samsung_parser->cursor += 1;
    return ret;
}

static bool samsung_parse_ending_frame(samsung_parser_t *samsung_parser)
{
    rmt_item32_t item = samsung_parser->buffer[samsung_parser->buffer_length - 1];
    bool level = (item.level0 == samsung_parser->inverse) && (item.level1 != samsung_parser->inverse);
    if (!level)
    {
        ESP_LOGW("parser error", "level : {%u, %u}\n", item.level0, item.level1);
        return false;
    }
    bool margin = samsung_check_in_range(item.duration0, samsung_parser->ending_code_high_ticks, samsung_parser->margin_ticks);
    if (!margin)
    {
        ESP_LOGW("parser error", "0 : {%u, %u}\n", item.duration0, samsung_parser->ending_code_high_ticks);
        return false;
    }
    margin &= (item.duration1 < samsung_parser->margin_ticks);
    if (!margin)
    {
        ESP_LOGW("parser error", "1 : {%u, %u}\n", item.duration1, samsung_parser->ending_code_low_ticks);
        return false;
    }
    return  level && margin;
}

static esp_err_t samsung_parser_input(ir_parser_t *parser, void *raw_data, uint32_t length)
{
    esp_err_t ret = ESP_OK;
    samsung_parser_t *samsung_parser = __containerof(parser, samsung_parser_t, parent);
    SAMSUNG_CHECK(raw_data, "input data can't be null", err, ESP_ERR_INVALID_ARG);
    ESP_LOGI("samsung_parser", "length = %u", length);
    // Data Frame costs 34 items and Repeat Frame costs 2 items
    if (length != 50)
    {
        ret = ESP_FAIL;
        goto err;
    }
    samsung_parser->buffer = raw_data;
    samsung_parser->buffer_length = length;
    // ESP_LOGI("parser input", "length : %u \t buffersize : %u\n", length, (uint32_t)(sizeof(samsung_parser->buffer)/sizeof(samsung_parser->buffer[0])) );
    return ret;
err:
    return ret;
}

static esp_err_t samsung_parser_get_scan_code(ir_parser_t *parser, uint32_t *address, uint32_t *command, bool *repeat)
{
    esp_err_t ret = ESP_FAIL;
    uint32_t addr = 0;
    uint32_t cmd = 0;
    bool logic_value = false;
    samsung_parser_t *samsung_parser = __containerof(parser, samsung_parser_t, parent);
    SAMSUNG_CHECK(address && command && repeat, "address, command and repeat can't be null", out, ESP_ERR_INVALID_ARG);

    // Not dealing with repeat frames
    *repeat = false;

    if (samsung_parse_head(samsung_parser))
    {
        if (samsung_parse_ending_frame(samsung_parser))
        {
            for (int i = 0; i < 16; i++) {
                if (samsung_parse_logic(parser, &logic_value) == ESP_OK)
                {
                    addr |= (logic_value << i);
                }
            }
            for (int i = 0; i < 32; i++)
            {
                if (samsung_parse_logic(parser, &logic_value) == ESP_OK)
                {
                    cmd |= (logic_value << i);
                }
            }

            *address = addr;
            *command = cmd;
            // keep it as potential repeat code
            samsung_parser->last_address = addr;
            samsung_parser->last_command = cmd;
            ret = ESP_OK;
        }
    }
    return ret;
out:
    return ret;
}

static esp_err_t samsung_parser_del(ir_parser_t *parser)
{
    samsung_parser_t *samsung_parser = __containerof(parser, samsung_parser_t, parent);
    free(samsung_parser);
    return ESP_OK;
}

ir_parser_t *ir_parser_rmt_new_samsung(const ir_parser_config_t *config)
{
    ir_parser_t *ret = NULL;
    SAMSUNG_CHECK(config, "nec configuration can't be null", err, NULL);

    samsung_parser_t *samsung_parser = calloc(1, sizeof(samsung_parser_t));
    SAMSUNG_CHECK(samsung_parser, "request memory for samsung_parser failed", err, NULL);

    samsung_parser->flags = config->flags;
    if (config->flags & IR_TOOLS_FLAGS_INVERSE) {
        samsung_parser->inverse = true;
    }

    uint32_t counter_clk_hz = 0;
    SAMSUNG_CHECK(rmt_get_counter_clock((rmt_channel_t)config->dev_hdl, &counter_clk_hz) == ESP_OK,
              "get rmt counter clock failed", err, NULL);
    float ratio = (float)counter_clk_hz / 1e6;
    samsung_parser->margin_ticks = (uint32_t)(ratio * config->margin_us);
    samsung_parser->leading_code_high_ticks = (uint32_t)(ratio * SAMSUNG_LEADING_CODE_HIGH_US);
    samsung_parser->leading_code_low_ticks = (uint32_t)(ratio * SAMSUNG_LEADING_CODE_LOW_US);
    samsung_parser->ending_code_high_ticks = (uint32_t)(ratio * SAMSUNG_ENDING_CODE_HIGH_US);
    samsung_parser->ending_code_low_ticks = 0;
    samsung_parser->payload_logic0_high_ticks = (uint32_t)(ratio * SAMSUNG_PAYLOAD_ZERO_HIGH_US);
    samsung_parser->payload_logic0_low_ticks = (uint32_t)(ratio * SAMSUNG_PAYLOAD_ZERO_LOW_US);
    samsung_parser->payload_logic1_high_ticks = (uint32_t)(ratio * SAMSUNG_PAYLOAD_ONE_HIGH_US);
    samsung_parser->payload_logic1_low_ticks = (uint32_t)(ratio * SAMSUNG_PAYLOAD_ONE_LOW_US);
    samsung_parser->parent.input = samsung_parser_input;
    samsung_parser->parent.get_scan_code = samsung_parser_get_scan_code;
    samsung_parser->parent.del = samsung_parser_del;
    return &samsung_parser->parent;
err:
    return ret;
}
