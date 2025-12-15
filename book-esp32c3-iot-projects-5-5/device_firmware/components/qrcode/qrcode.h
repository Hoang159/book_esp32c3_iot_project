#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int scale;   // kích thước hiển thị mỗi module (số ký tự mỗi module)
    int margin;  // lề xung quanh QR
} esp_qrcode_config_t;

#define ESP_QRCODE_CONFIG_DEFAULT() (esp_qrcode_config_t){ .scale = 2, .margin = 1 }

/* Gọi để sinh & in QR từ payload (chuỗi). Trả về true nếu thành công. */
bool esp_qrcode_generate(const esp_qrcode_config_t *cfg, const char *payload);

/* Hàm tương đương qrcode_display (nếu code khác gọi) */
void qrcode_display(const char *payload);

#ifdef __cplusplus
}
#endif
