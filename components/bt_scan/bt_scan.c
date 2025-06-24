/*
 * Copyright 2025 Emiliano Gonzalez (egonzalez . hiperion @ gmail . com))
 * * Project Site: https://github.com/hiperiondev/esp32-bt-audio-gateway *
 *
 * This is based on other projects, please contact their authors for more information.
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"

#include "bt_common.h"

#define MAX_DEVICES 20

static const char *TAG = "bt_scan";

bt_device_t devices[MAX_DEVICES];
static uint32_t num_devices = 0;
static bool discovery_complete = false;

char *bda2str(esp_bd_addr_t bda, char *str, size_t size) {
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return str;
}

bool get_name_from_eir(uint8_t *eir, int eir_len, char *bdname, uint8_t *bdname_len) {
    if (!eir || eir_len <= 0 || !bdname) {
        return false;
    }
    uint8_t *p = eir;
    while (p < eir + eir_len) {
        uint8_t len = *p++;
        if (len == 0)
            break;
        if (p + len > eir + eir_len)
            break; // Safety check
        uint8_t type = *p++;
        if (type == ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME || type == ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME) {
            int name_len = len - 1;
            if (name_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
                name_len = ESP_BT_GAP_MAX_BDNAME_LEN;
            }
            memcpy(bdname, p, name_len);
            bdname[name_len] = '\0';
            if (bdname_len) {
                *bdname_len = name_len;
            }
            return true;
        }
        p += len - 1;
    }
    return false;
}

bool device_exists(esp_bd_addr_t bda) {
    for (int i = 0; i < num_devices; i++) {
        if (memcmp(devices[i].bda, bda, ESP_BD_ADDR_LEN) == 0) {
            return true;
        }
    }
    return false;
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT:
            if (!device_exists(param->disc_res.bda) && num_devices < MAX_DEVICES) {
                memcpy(devices[num_devices].bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
                char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = { 0 };
                uint8_t name_len = 0;
                uint8_t *eir = NULL;
                int eir_len = 0;
                for (int i = 0; i < param->disc_res.num_prop; i++) {
                    if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR) {
                        eir = (uint8_t *)param->disc_res.prop[i].val;
                        eir_len = param->disc_res.prop[i].len;
                        break;
                    }
                }
                if (eir && get_name_from_eir(eir, eir_len, name, &name_len)) {
                    strncpy(devices[num_devices].name, name, ESP_BT_GAP_MAX_BDNAME_LEN);
                    devices[num_devices].name[ESP_BT_GAP_MAX_BDNAME_LEN] = '\0';
                } else {
                    strcpy(devices[num_devices].name, "Unknown");
                }
                num_devices++;
            }
            break;
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                ESP_LOGI(TAG, "Discovery stopped");
                discovery_complete = true;
            } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
                ESP_LOGI(TAG, "Discovery started");
            }
            break;
        default:
            break;
    }
}

bt_device_t *bt_scan_start(uint32_t *devices_qty) {
    esp_err_t ret;

    // Register GAP callback
    ret = esp_bt_gap_register_callback(bt_app_gap_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register callback failed: %s", esp_err_to_name(ret));
        return NULL;
    }

    const char *dev_name = "esp32-bt-audio-gateway";
    ret = esp_bt_gap_set_device_name(dev_name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(ret));
        return NULL;
    }

    ret = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set scan mode failed: %s", esp_err_to_name(ret));
        return NULL;
    }

    ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 8, 0); // 8*1.28s ≈ 10s
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start discovery failed: %s", esp_err_to_name(ret));
        return NULL;
    }

    while (!discovery_complete) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    *devices_qty = num_devices;
    return devices;
}
