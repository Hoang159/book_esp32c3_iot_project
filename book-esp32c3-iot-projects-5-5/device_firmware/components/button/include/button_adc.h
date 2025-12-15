// Copyright 2020 Espressif
// Licensed under the Apache License, Version 2.0
#ifndef __IOT_BUTTON_ADC_H__
#define __IOT_BUTTON_ADC_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

// ---- New ADC (IDF 5.x) ----
#include "esp_adc/adc_oneshot.h"   // adc_channel_t, ADC_CHANNEL_x
#include "hal/adc_types.h"         // đảm bảo có ADC_CHANNEL_MAX trên các target

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
 * Backward-compat layer:
 *   - alias adc1_channel_t -> adc_channel_t
 *   - map ADC1_CHANNEL_X   -> ADC_CHANNEL_X
 *   - map ADC1_CHANNEL_MAX -> ADC_CHANNEL_MAX (có fallback)
 * ---------------------------------------------------------------- */
typedef adc_channel_t adc1_channel_t;

#ifndef ADC1_CHANNEL_0
#define ADC1_CHANNEL_0  ADC_CHANNEL_0
#define ADC1_CHANNEL_1  ADC_CHANNEL_1
#define ADC1_CHANNEL_2  ADC_CHANNEL_2
#define ADC1_CHANNEL_3  ADC_CHANNEL_3
#define ADC1_CHANNEL_4  ADC_CHANNEL_4
#define ADC1_CHANNEL_5  ADC_CHANNEL_5
#define ADC1_CHANNEL_6  ADC_CHANNEL_6
#define ADC1_CHANNEL_7  ADC_CHANNEL_7
#define ADC1_CHANNEL_8  ADC_CHANNEL_8
#define ADC1_CHANNEL_9  ADC_CHANNEL_9
#endif

/* Fallback nếu ADC_CHANNEL_MAX không được expose bởi chuỗi include hiện tại */
#ifndef ADC_CHANNEL_MAX
#define ADC_CHANNEL_MAX 10
#endif

#ifndef ADC1_CHANNEL_MAX
#define ADC1_CHANNEL_MAX  ADC_CHANNEL_MAX
#endif

/* Gộp / tách tham số channel + index thành 1 giá trị 32-bit */
#define ADC_BUTTON_COMBINE(channel, index)   ((((uint32_t)(channel)) << 8) | ((uint32_t)(index) & 0xFF))
#define ADC_BUTTON_SPLIT_INDEX(data)         ((uint32_t)(data) & 0xFF)
#define ADC_BUTTON_SPLIT_CHANNEL(data)       (((uint32_t)(data) >> 8) & 0xFF)

/**
 * @brief Cấu hình nút ADC
 *
 * adc_channel: kênh ADC. Hỗ trợ cả hằng cũ (ADC1_CHANNEL_X) lẫn mới (ADC_CHANNEL_X).
 * min/max: ngưỡng theo mV (nếu không có calibration sẽ so theo RAW).
 */
typedef struct {
    adc1_channel_t adc_channel;   /**< Kênh ADC (alias của adc_channel_t) */
    uint8_t        button_index;  /**< Chỉ số nút trên kênh */
    uint16_t       min;           /**< Điện áp min (mV) tương ứng nút */
    uint16_t       max;           /**< Điện áp max (mV) tương ứng nút */
} button_adc_config_t;

/**
 * @brief Khởi tạo nút ADC
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NOT_SUPPORTED, ESP_ERR_INVALID_STATE
 */
esp_err_t button_adc_init(const button_adc_config_t *config);

/**
 * @brief Giải phóng nút ADC trên kênh/index chỉ định
 */
esp_err_t button_adc_deinit(adc1_channel_t channel, int button_index);

/**
 * @brief Đọc mức nút ADC
 * @param button_index giá trị gộp từ ADC_BUTTON_COMBINE(channel, index)
 * @return 0: không nhấn, 1: nhấn
 */
uint8_t button_adc_get_key_level(void *button_index);

#ifdef __cplusplus
}
#endif

#endif /* __IOT_BUTTON_ADC_H__ */
