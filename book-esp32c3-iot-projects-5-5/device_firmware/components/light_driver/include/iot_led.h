// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
// Licensed under the Apache License, Version 2.0

#ifndef __IOT_LED_H__
#define __IOT_LED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdint.h>

#define GAMMA_CORRECTION 0.8   /**< Gamma curve parameter */
#define GAMMA_TABLE_SIZE 256   /**< Gamma table size, used for led fade*/
#define DUTY_SET_CYCLE   20    /**< Set duty cycle in milliseconds */

/**
 * Macro which can be used to check the error code,
 * and terminate the program in case the code is not ESP_OK.
 * Prints the error code, error location, and the failed statement to serial output.
 *
 * Disabled if assertions are disabled.
 */
#define LIGHT_ERROR_CHECK(con, err, format, ...) do { \
        if (con) { \
            if(*format != '\0') \
                ESP_LOGW(TAG, "<%s> " format, esp_err_to_name(err), ##__VA_ARGS__); \
            return err; \
        } \
    } while(0)

#define LIGHT_PARAM_CHECK(con) do { \
        if (!(con)) { \
            ESP_LOGE(TAG, "<ESP_ERR_INVALID_ARG> !(%s)", #con); \
            return ESP_ERR_INVALID_ARG; \
        } \
    } while(0)

/**
  * @brief Initialize and set the ledc timer for the iot led
  */
esp_err_t iot_led_init(ledc_timer_t timer_num, ledc_mode_t speed_mode, uint32_t freq_hz,
                       ledc_clk_cfg_t clk_cfg, ledc_timer_bit_t duty_resolution);

/**
  * @brief DeInitializes the iot led and free resource
  */
esp_err_t iot_led_deinit(void);

/**
  * @brief Register LEDC channel & GPIO
  */
esp_err_t iot_led_regist_channel(ledc_channel_t channel, gpio_num_t gpio_num);

/**
  * @brief Get current channel value (0..255)
  */
esp_err_t iot_led_get_channel(ledc_channel_t channel, uint8_t* dst);

/**
  * @brief Set channel to value with fade time (ms)
  */
esp_err_t iot_led_set_channel(ledc_channel_t channel, uint8_t value, uint32_t fade_ms);

/**
  * @brief Start blink / loop-fade on a channel
  * @param fade_flag 1: loop fade, 0: blink
  */
esp_err_t iot_led_start_blink(ledc_channel_t channel, uint8_t value, uint32_t period_ms, bool fade_flag);

/**
  * @brief Stop blink / loop-fade
  */
esp_err_t iot_led_stop_blink(ledc_channel_t channel);

/**
  * @brief Override gamma table (optional)
  */
esp_err_t iot_led_set_gamma_table(const uint16_t gamma_table[GAMMA_TABLE_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* __IOT_LED_H__ */
