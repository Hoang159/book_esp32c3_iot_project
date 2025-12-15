// Copyright 2017 Espressif
// Licensed under the Apache License, Version 2.0

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/ledc.h"

#include "iot_led.h"

#define LEDC_TIMER_PRECISION (LEDC_TIMER_13_BIT)
#define LEDC_MAX_DUTY        ((1U << LEDC_TIMER_PRECISION) - 1)

/* Map a 16-bit value to current LEDC duty resolution */
#define LEDC_VALUE_TO_DUTY(value16) ( (uint32_t)((uint64_t)(value16) * (LEDC_MAX_DUTY) / 0xFFFFU) )

#define LEDC_FIXED_Q (8)
#define FLOATINT_2_FIXED(X, Q)  ((int)((X) * (1U << (Q))))
#define FIXED_2_FLOATING(X, Q)  ((int)((X) / (1U << (Q))))
#define GET_FIXED_INTEGER_PART(X, Q) ((X) >> (Q))
#define GET_FIXED_DECIMAL_PART(X, Q) ((X) & ((1U << (Q)) - 1))

typedef struct {
    int     cur;    // Q8 fixed  value 0..(255<<Q)
    int     final;  // Q8 target
    int     step;   // Q8 step (+/-)
    int     cycle;  // for blink/fade loop: number of ticks for half-period
    size_t  num;    // ticks remaining for one transition
} ledc_fade_data_t;

typedef struct {
    ledc_fade_data_t  fade_data[LEDC_CHANNEL_MAX];
    ledc_mode_t       speed_mode;
    ledc_timer_t      timer_num;
} iot_light_t;

static const char *TAG = "iot_light";

static iot_light_t           *g_light_config   = NULL;
static uint16_t              *g_gamma_table    = NULL;
static esp_timer_handle_t     g_timer_handle   = NULL;
static bool                   g_timer_started  = false;

/* -------------------- gamma & duty helpers -------------------- */

static void gamma_table_create(uint16_t *gamma_table, float correction)
{
    float value_tmp = 0.f;

    /* gamma curve: y = a * x^(1/gm), a=GAMMA_TABLE_SIZE */
    for (int i = 0; i < GAMMA_TABLE_SIZE; i++) {
        value_tmp = (float)i / (GAMMA_TABLE_SIZE - 1);
        value_tmp = powf(value_tmp, 1.0f / correction);
        gamma_table[i] = (uint16_t)FLOATINT_2_FIXED((value_tmp * GAMMA_TABLE_SIZE), LEDC_FIXED_Q);
    }

    /* bảo đảm entry cuối cùng sáng tối đa khi map sang 16-bit */
    if (gamma_table[GAMMA_TABLE_SIZE - 1] == 0) {
        gamma_table[GAMMA_TABLE_SIZE - 1] = 0xFFFFU;
    }
}

static inline uint32_t gamma_value_to_duty(int q8_value)
{
    /* q8_value là Q8 cố định: 0 .. (255<<8) */
    uint32_t idx  = GET_FIXED_INTEGER_PART(q8_value, LEDC_FIXED_Q);             // 0..255
    uint32_t frac = GET_FIXED_DECIMAL_PART(q8_value, LEDC_FIXED_Q);             // 0..255

    uint16_t cur  = LEDC_VALUE_TO_DUTY(g_gamma_table[idx]);
    uint16_t next = (idx < (GAMMA_TABLE_SIZE - 1)) ? LEDC_VALUE_TO_DUTY(g_gamma_table[idx + 1]) : cur;

    /* nội suy tuyến tính trong Q8 */
    uint32_t duty = cur + (uint32_t)(( (int)next - (int)cur ) * (int)frac / (1 << LEDC_FIXED_Q));
    if (duty > LEDC_MAX_DUTY) duty = LEDC_MAX_DUTY;
    return duty;
}

static inline esp_err_t ledc_apply_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty)
{
    esp_err_t err = ledc_set_duty(speed_mode, channel, duty);
    if (err != ESP_OK) return err;
    return ledc_update_duty(speed_mode, channel);
}

/* -------------------- periodic tick (esp_timer) -------------------- */

static void led_tick_cb(void *arg)
{
    (void)arg;

    if (!g_light_config) return;

    int idle_channels = 0;

    for (int ch = 0; ch < LEDC_CHANNEL_MAX; ch++) {
        ledc_fade_data_t *fd = &g_light_config->fade_data[ch];

        if (fd->num > 0) {
            /* đang trong một pha fade */
            fd->num--;

            if (fd->step) {
                fd->cur += fd->step;

                /* nếu còn tick, cập nhật trung gian; nếu hết, chốt target */
                if (fd->num != 0) {
                    (void)ledc_apply_duty(g_light_config->speed_mode, ch, gamma_value_to_duty(fd->cur));
                } else {
                    (void)ledc_apply_duty(g_light_config->speed_mode, ch, gamma_value_to_duty(fd->final));
                    fd->cur = fd->final;
                }
            } else {
                /* không cần chia nhỏ: set trực tiếp target */
                (void)ledc_apply_duty(g_light_config->speed_mode, ch, gamma_value_to_duty(fd->final));
                fd->cur = fd->final;
            }
        } else if (fd->cycle) {
            /* blink / loop-fade: đảo chiều hoặc chuyển trạng thái mỗi half-period */
            fd->num = (fd->cycle > 0) ? (size_t)fd->cycle : 0;

            if (fd->step) {
                fd->step = -fd->step;
                fd->cur += fd->step;
            } else {
                fd->cur = (fd->cur == fd->final) ? 0 : fd->final;
            }

            (void)ledc_apply_duty(g_light_config->speed_mode, ch, gamma_value_to_duty(fd->cur));
        } else {
            idle_channels++;
        }
    }

    if (idle_channels >= LEDC_CHANNEL_MAX) {
        if (g_timer_started) {
            esp_timer_stop(g_timer_handle);
            g_timer_started = false;
        }
    }
}

static inline void timer_ensure_running(void)
{
    if (!g_timer_started && g_timer_handle) {
        /* chu kỳ tính theo us */
        esp_timer_start_periodic(g_timer_handle, (uint64_t)DUTY_SET_CYCLE * 1000ULL);
        g_timer_started = true;
    }
}

/* -------------------- Public API impl -------------------- */

esp_err_t iot_led_init(ledc_timer_t timer_num, ledc_mode_t speed_mode, uint32_t freq_hz,
                       ledc_clk_cfg_t clk_cfg, ledc_timer_bit_t duty_resolution)
{
    esp_err_t ret;

    const ledc_timer_config_t tcfg = {
        .speed_mode      = speed_mode,
        .duty_resolution = duty_resolution,   // nên là 13-bit như macro trên
        .timer_num       = timer_num,
        .freq_hz         = freq_hz,
        .clk_cfg         = clk_cfg,
    };

    ret = ledc_timer_config(&tcfg);
    LIGHT_ERROR_CHECK(ret != ESP_OK, ret, "LEDC timer config failed");

    if (!g_gamma_table) {
        g_gamma_table = calloc(GAMMA_TABLE_SIZE, sizeof(uint16_t));
        if (!g_gamma_table) return ESP_ERR_NO_MEM;
        gamma_table_create(g_gamma_table, GAMMA_CORRECTION);
    }

    if (!g_light_config) {
        g_light_config = calloc(1, sizeof(iot_light_t));
        if (!g_light_config) return ESP_ERR_NO_MEM;
        g_light_config->timer_num  = timer_num;
        g_light_config->speed_mode = speed_mode;
    }

    if (!g_timer_handle) {
        const esp_timer_create_args_t args = {
            .callback = &led_tick_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "led_fade_tick",
            .skip_unhandled_events = true,
        };
        ret = esp_timer_create(&args, &g_timer_handle);
        LIGHT_ERROR_CHECK(ret != ESP_OK, ret, "esp_timer_create failed");
    }

    return ESP_OK;
}

esp_err_t iot_led_deinit(void)
{
    if (g_timer_handle) {
        if (g_timer_started) {
            esp_timer_stop(g_timer_handle);
            g_timer_started = false;
        }
        esp_timer_delete(g_timer_handle);
        g_timer_handle = NULL;
    }

    if (g_light_config) {
        free(g_light_config);
        g_light_config = NULL;
    }

    if (g_gamma_table) {
        free(g_gamma_table);
        g_gamma_table = NULL;
    }

    return ESP_OK;
}

esp_err_t iot_led_regist_channel(ledc_channel_t channel, gpio_num_t gpio_num)
{
    LIGHT_ERROR_CHECK(g_light_config == NULL, ESP_ERR_INVALID_ARG, "iot_led_init() must be called first");

#ifdef CONFIG_SPIRAM_SUPPORT
    LIGHT_ERROR_CHECK(gpio_num == GPIO_NUM_16 || gpio_num == GPIO_NUM_17, ESP_ERR_INVALID_ARG,
                      "gpio_num must not conflict with PSRAM (IO16/IO17)");
#endif

    const ledc_channel_config_t chcfg = {
        .gpio_num   = gpio_num,
        .channel    = channel,
        .intr_type  = LEDC_INTR_DISABLE,
        .speed_mode = g_light_config->speed_mode,
        .timer_sel  = g_light_config->timer_num,
        .duty       = 0,
        .hpoint     = 0,
        .flags.output_invert = 0,
    };

    esp_err_t ret = ledc_channel_config(&chcfg);
    LIGHT_ERROR_CHECK(ret != ESP_OK, ret, "LEDC channel config failed");
    return ESP_OK;
}

esp_err_t iot_led_get_channel(ledc_channel_t channel, uint8_t *dst)
{
    LIGHT_ERROR_CHECK(g_light_config == NULL, ESP_ERR_INVALID_ARG, "iot_led_init() must be called first");
    LIGHT_ERROR_CHECK(dst == NULL, ESP_ERR_INVALID_ARG, "dst is NULL");
    int cur_q8 = g_light_config->fade_data[channel].cur;
    if (cur_q8 < 0) cur_q8 = 0;
    if (cur_q8 > (255 << LEDC_FIXED_Q)) cur_q8 = (255 << LEDC_FIXED_Q);
    *dst = (uint8_t)FIXED_2_FLOATING(cur_q8, LEDC_FIXED_Q);
    return ESP_OK;
}

esp_err_t iot_led_set_channel(ledc_channel_t channel, uint8_t value, uint32_t fade_ms)
{
    LIGHT_ERROR_CHECK(g_light_config == NULL, ESP_ERR_INVALID_ARG, "iot_led_init() must be called first");

    ledc_fade_data_t *fd = &g_light_config->fade_data[channel];
    fd->final = FLOATINT_2_FIXED(value, LEDC_FIXED_Q);

    if (fade_ms == 0) {
        /* set ngay */
        fd->cur = fd->final;
        fd->num = 0;
        fd->step = 0;
        fd->cycle = 0;
        return ledc_apply_duty(g_light_config->speed_mode, channel, gamma_value_to_duty(fd->final));
    }

    /* số tick cần chạy */
    fd->num = (fade_ms < DUTY_SET_CYCLE) ? 1 : (fade_ms / DUTY_SET_CYCLE);

    int diff = abs(fd->final - fd->cur);
    fd->step = (fd->num > 0) ? (diff / (int)fd->num) : diff;

    if (fd->final < fd->cur) fd->step = -fd->step;

    /* blink loop off khi set trực tiếp */
    fd->cycle = 0;

    timer_ensure_running();
    return ESP_OK;
}

esp_err_t iot_led_start_blink(ledc_channel_t channel, uint8_t value, uint32_t period_ms, bool fade_flag)
{
    LIGHT_ERROR_CHECK(g_light_config == NULL, ESP_ERR_INVALID_ARG, "iot_led_init() must be called first");

    ledc_fade_data_t *fd = &g_light_config->fade_data[channel];
    fd->final = fd->cur = FLOATINT_2_FIXED(value, LEDC_FIXED_Q);

    int half_ticks = (period_ms / 2) / DUTY_SET_CYCLE;
    if (half_ticks <= 0) half_ticks = 1;
    fd->cycle = half_ticks;

    if (fade_flag) {
        fd->num  = half_ticks;
        fd->step = (fd->num > 0) ? -(fd->cur / fd->num) : 0;
    } else {
        fd->num  = 0;
        fd->step = 0; // blink: bật/tắt tức thời mỗi nửa chu kỳ
    }

    timer_ensure_running();
    return ESP_OK;
}

esp_err_t iot_led_stop_blink(ledc_channel_t channel)
{
    LIGHT_ERROR_CHECK(g_light_config == NULL, ESP_ERR_INVALID_ARG, "iot_led_init() must be called first");
    ledc_fade_data_t *fd = &g_light_config->fade_data[channel];
    fd->cycle = 0;
    fd->num   = 0;
    fd->step  = 0;
    return ESP_OK;
}

esp_err_t iot_led_set_gamma_table(const uint16_t gamma_table[GAMMA_TABLE_SIZE])
{
    LIGHT_ERROR_CHECK(g_gamma_table == NULL, ESP_ERR_INVALID_ARG, "iot_led_init() must be called first");
    memcpy(g_gamma_table, gamma_table, GAMMA_TABLE_SIZE * sizeof(uint16_t));
    return ESP_OK;
}
