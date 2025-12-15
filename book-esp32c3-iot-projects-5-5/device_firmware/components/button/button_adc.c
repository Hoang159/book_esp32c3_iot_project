// Copyright 2020 Espressif
// Licensed under the Apache License, Version 2.0

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "driver/gpio.h"

// ==== NEW ADC APIs (IDF 5.x) ====
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "button_adc.h"
#include "esp_timer.h"

static const char *TAG = "adc button";

#define ADC_BTN_CHECK(a, str, ret_val) do {                  \
    if (!(a)) {                                              \
        ESP_LOGE(TAG, "%s(%d): %s", __FUNCTION__, __LINE__,  \
                 (str));                                     \
        return (ret_val);                                    \
    }                                                        \
} while (0)

#define NO_OF_SAMPLES   CONFIG_ADC_BUTTON_SAMPLE_TIMES   // multisampling

// Dùng DB_12 (DB_11 là alias deprecated)
#define ADC_BUTTON_ATTEN        ADC_ATTEN_DB_12
#define ADC_BUTTON_UNIT         ADC_UNIT_1
#define ADC_BUTTON_MAX_CHANNEL  CONFIG_ADC_BUTTON_MAX_CHANNEL
#define ADC_BUTTON_MAX_BUTTON   CONFIG_ADC_BUTTON_MAX_BUTTON_PER_CHANNEL

typedef struct {
    uint16_t min;   // mV
    uint16_t max;   // mV
} button_data_t;

typedef struct {
    adc1_channel_t channel_legacy;   // alias của adc_channel_t (giữ tương thích)
    adc_channel_t  channel_oneshot;  // kênh theo API mới
    uint8_t        is_init;
    button_data_t  btns[ADC_BUTTON_MAX_BUTTON];  /* các nút trên kênh */
    uint64_t       last_time;                    /* thời điểm sample cuối (us) */
} btn_adc_channel_t;

typedef struct {
    bool                           is_configured;
    adc_oneshot_unit_handle_t      unit;

    bool                           cali_inited;
    adc_cali_handle_t              cali_handle;
    int                            cali_scheme;  // 0: none, 1: curve

    btn_adc_channel_t              ch[ADC_BUTTON_MAX_CHANNEL];
    uint8_t                        ch_num;
} adc_button_t;

static adc_button_t g_button = {0};

/* ========= Helpers ========= */

static int find_unused_channel(void)
{
    for (size_t i = 0; i < ADC_BUTTON_MAX_CHANNEL; i++) {
        if (g_button.ch[i].is_init == 0) return (int)i;
    }
    return -1;
}

static int find_channel_legacy(adc1_channel_t ch_legacy)
{
    for (size_t i = 0; i < ADC_BUTTON_MAX_CHANNEL; i++) {
        if (g_button.ch[i].is_init && g_button.ch[i].channel_legacy == ch_legacy) {
            return (int)i;
        }
    }
    return -1;
}

// Trên ESP32-C3 (IDF 5.5), dùng curve fitting; nếu không hỗ trợ thì fallback raw
static bool adc_calibration_init(adc_cali_handle_t *out_handle, int *out_scheme)
{
    *out_handle = NULL;
    *out_scheme = 0;

    adc_cali_curve_fitting_config_t cfg = {
        .unit_id  = ADC_BUTTON_UNIT,
        .atten    = ADC_BUTTON_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cfg, out_handle) == ESP_OK) {
        *out_scheme = 1; // curve
        ESP_LOGI(TAG, "ADC calibration: curve fitting");
        return true;
    }

    ESP_LOGW(TAG, "ADC calibration not supported (fallback to raw)");
    return false;
}

static void adc_calibration_deinit(adc_cali_handle_t handle, int scheme)
{
    if (!handle) return;
    if (scheme == 1) { // curve
        adc_cali_delete_scheme_curve_fitting(handle);
    }
}

/* Đọc trung bình và (nếu có) chuyển sang mV bằng calibration */
static uint32_t get_adc_voltage(btn_adc_channel_t *ch)
{
    int sum = 0;
    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        int raw = 0;
        adc_oneshot_read(g_button.unit, ch->channel_oneshot, &raw);
        sum += raw;
    }
    int raw_avg = sum / NO_OF_SAMPLES;

    int mv = 0;
    if (g_button.cali_inited) {
        if (adc_cali_raw_to_voltage(g_button.cali_handle, raw_avg, &mv) != ESP_OK) {
            mv = 0; // lỗi calibration -> an toàn trả 0
        }
    } else {
        // Không có calibration: dùng raw (chú ý min/max nên đặt theo raw khi không calib)
        mv = raw_avg;
    }

    ESP_LOGV(TAG, "RAW=%d => %d mV", raw_avg, mv);
    return (uint32_t)mv;
}

/* ========= Public API ========= */

esp_err_t button_adc_init(const button_adc_config_t *config)
{
    ADC_BTN_CHECK(config != NULL, "Pointer of config is invalid", ESP_ERR_INVALID_ARG);
    ADC_BTN_CHECK(config->button_index < ADC_BUTTON_MAX_BUTTON, "button_index out of range", ESP_ERR_NOT_SUPPORTED);
    ADC_BTN_CHECK(config->max > 0, "key max voltage invalid", ESP_ERR_INVALID_ARG);

    // alias cũ -> kiểu mới
    adc1_channel_t ch_legacy = config->adc_channel;
    adc_channel_t  ch_oneshot = (adc_channel_t)config->adc_channel;

    int ch_index = find_channel_legacy(ch_legacy);
    if (ch_index >= 0) {
        // channel đã init: button_index chưa được dùng
        ADC_BTN_CHECK(g_button.ch[ch_index].btns[config->button_index].max == 0,
                      "The button_index has been used", ESP_ERR_INVALID_STATE);
    } else {
        int unused = find_unused_channel();
        ADC_BTN_CHECK(unused >= 0, "exceed max channel number, can't create a new channel", ESP_ERR_INVALID_STATE);
        ch_index = unused;
    }

    // Khởi tạo unit & calibration một lần
    if (!g_button.is_configured) {
        adc_oneshot_unit_init_cfg_t unit_cfg = {
            .unit_id = ADC_BUTTON_UNIT,
        };
        esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &g_button.unit);
        ADC_BTN_CHECK(err == ESP_OK, "adc_oneshot_new_unit failed", err);

        g_button.cali_inited = adc_calibration_init(&g_button.cali_handle, &g_button.cali_scheme);
        g_button.is_configured = true;
    }

    // Cấu hình channel nếu chưa có
    if (g_button.ch[ch_index].is_init == 0) {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten    = ADC_BUTTON_ATTEN,
        };
        esp_err_t err = adc_oneshot_config_channel(g_button.unit, ch_oneshot, &chan_cfg);
        ADC_BTN_CHECK(err == ESP_OK, "adc_oneshot_config_channel failed", err);

        g_button.ch[ch_index].channel_legacy  = ch_legacy;
        g_button.ch[ch_index].channel_oneshot = ch_oneshot;
        g_button.ch[ch_index].is_init         = 1;
        g_button.ch[ch_index].last_time       = 0;
    }

    // Lưu ngưỡng mV cho button_index
    g_button.ch[ch_index].btns[config->button_index].max = config->max;
    g_button.ch[ch_index].btns[config->button_index].min = config->min;
    g_button.ch_num++;

    return ESP_OK;
}

esp_err_t button_adc_deinit(adc1_channel_t channel, int button_index)
{
    ADC_BTN_CHECK(button_index < ADC_BUTTON_MAX_BUTTON, "button_index out of range", ESP_ERR_INVALID_ARG);

    int ch_index = find_channel_legacy(channel);
    ADC_BTN_CHECK(ch_index >= 0, "can't find the channel", ESP_ERR_INVALID_ARG);

    // Clear entry nút
    g_button.ch[ch_index].btns[button_index].max = 0;
    g_button.ch[ch_index].btns[button_index].min = 0;

    // Nếu channel không còn nút nào, đánh dấu free
    uint8_t unused_button = 0;
    for (size_t i = 0; i < ADC_BUTTON_MAX_BUTTON; i++) {
        if (g_button.ch[ch_index].btns[i].max == 0) unused_button++;
    }
    if (unused_button == ADC_BUTTON_MAX_BUTTON && g_button.ch[ch_index].is_init) {
        g_button.ch[ch_index].is_init = 0;
        g_button.ch[ch_index].channel_legacy = (adc1_channel_t)0xFF;
        ESP_LOGD(TAG, "all buttons unused on channel, mark channel free");
    }

    // Nếu mọi channel đều free -> giải phóng unit + calibration
    uint8_t unused_ch = 0;
    for (size_t i = 0; i < ADC_BUTTON_MAX_CHANNEL; i++) {
        if (g_button.ch[i].is_init == 0) unused_ch++;
    }
    if (unused_ch == ADC_BUTTON_MAX_CHANNEL && g_button.is_configured) {
        if (g_button.cali_inited) {
            adc_calibration_deinit(g_button.cali_handle, g_button.cali_scheme);
            g_button.cali_handle = NULL;
            g_button.cali_inited = false;
            g_button.cali_scheme = 0;
        }
        if (g_button.unit) {
            adc_oneshot_del_unit(g_button.unit);
            g_button.unit = NULL;
        }
        memset(&g_button, 0, sizeof(g_button));
        ESP_LOGD(TAG, "all channels unused, ADC oneshot deinitialized");
    }

    return ESP_OK;
}

uint8_t button_adc_get_key_level(void *button_index)
{
    static uint16_t last_mv = 0;

    uint32_t ch_legacy = ADC_BUTTON_SPLIT_CHANNEL(button_index);
    uint32_t index     = ADC_BUTTON_SPLIT_INDEX(button_index);

    // dùng MAX mới (alias ở header cũng có)
    ADC_BTN_CHECK(ch_legacy < ADC1_CHANNEL_MAX, "channel out of range", 0);
    ADC_BTN_CHECK(index < ADC_BUTTON_MAX_BUTTON, "button_index out of range", 0);

    int ch_index = find_channel_legacy((adc1_channel_t)ch_legacy);
    ADC_BTN_CHECK(ch_index >= 0, "The button_index is not init", 0);

    // Lấy mẫu ít nhất mỗi 1ms
    uint64_t now = esp_timer_get_time(); // us
    if ((now - g_button.ch[ch_index].last_time) > 1000) {
        last_mv = (uint16_t)get_adc_voltage(&g_button.ch[ch_index]);
        g_button.ch[ch_index].last_time = now;
    }

    if (last_mv <= g_button.ch[ch_index].btns[index].max &&
        last_mv >  g_button.ch[ch_index].btns[index].min) {
        return 1;
    }
    return 0;
}
