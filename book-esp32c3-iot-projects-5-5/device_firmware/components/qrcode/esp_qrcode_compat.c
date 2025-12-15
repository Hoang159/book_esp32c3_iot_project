#include "qrcode.h"
#include "qrcodegen.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

// bool esp_qrcode_generate(const esp_qrcode_config_t *cfg, const char *payload)
// {
//     if (!payload) return false;

//     // Kích thước cần dùng do thư viện qrcodegen định nghĩa
//     size_t buf_len = qrcodegen_BUFFER_LEN_MAX;

//     // Dùng malloc để tránh tràn stack
//     uint8_t *qrcode = (uint8_t *) malloc(buf_len);
//     uint8_t *temp   = (uint8_t *) malloc(buf_len);

//     if (!qrcode || !temp) {
//         printf("esp_qrcode_generate: malloc failed (needed %zu bytes)\n", buf_len);
//         if (qrcode) free(qrcode);
//         if (temp) free(temp);
//         return false;
//     }

//     // Clear buffers (khuyến nghị)
//     memset(qrcode, 0, buf_len);
//     memset(temp, 0, buf_len);

//     bool ok = qrcodegen_encodeText(payload, temp, qrcode, qrcodegen_Ecc_LOW,
//                                    qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
//                                    qrcodegen_Mask_AUTO, true);
//     if (!ok) {
//         printf("esp_qrcode_generate: failed to encode payload\n");
//         free(qrcode);
//         free(temp);
//         return false;
//     }

//     int size = qrcodegen_getSize(qrcode);
//     int scale = cfg ? cfg->scale : 1;
//     int margin = cfg ? cfg->margin : 1;

//     // Print top margin
//     for (int mi = 0; mi < margin; mi++) {
//         for (int i = 0; i < (size + 2*margin) * scale; i++) printf("  ");
//         printf("\n");
//     }

//     for (int y = 0; y < size; y++) {
//         for (int mm = 0; mm < margin * scale; mm++) printf("  ");
//         for (int sx = 0; sx < scale; sx++) {
//             for (int x = 0; x < size; x++) {
//                 bool module = qrcodegen_getModule(qrcode, x, y);
//                 for (int s = 0; s < scale; s++) {
//                     if (module) printf("█");
//                     else printf("  ");
//                 }
//             }
//             for (int mm = 0; mm < margin * scale; mm++) printf("  ");
//             printf("\n");
//         }
//     }

//     // bottom margin
//     for (int mi = 0; mi < margin; mi++) {
//         for (int i = 0; i < (size + 2*margin) * scale; i++) printf("  ");
//         printf("\n");
//     }

//     free(qrcode);
//     free(temp);
//     return true;
// }

bool esp_qrcode_generate(const esp_qrcode_config_t *cfg, const char *payload)
{
    size_t buf_len = qrcodegen_BUFFER_LEN_MAX;
    uint8_t *qrcode = (uint8_t *) malloc(buf_len);
    uint8_t *temp   = (uint8_t *) malloc(buf_len);

    if (!qrcode || !temp) {
        printf("Malloc failed\n");
        if (qrcode) free(qrcode);
        if (temp) free(temp);
        return false;
    }

    bool ok = qrcodegen_encodeText(payload, temp, qrcode,
                                   qrcodegen_Ecc_LOW,
                                   qrcodegen_VERSION_MIN,
                                   qrcodegen_VERSION_MAX,
                                   qrcodegen_Mask_AUTO, true);

    if (!ok) {
        printf("Failed to generate QR code\n");
        free(qrcode);
        free(temp);
        return false;
    }

    int size = qrcodegen_getSize(qrcode);
    printf("\n");

    for (int y = 0; y < size; y += 2) {
        for (int x = 0; x < size; x++) {
            bool top = qrcodegen_getModule(qrcode, x, y);
            bool bottom = (y + 1 < size) ? qrcodegen_getModule(qrcode, x, y + 1) : false;

            if (top && bottom) printf("█");
            else if (top && !bottom) printf("▀");
            else if (!top && bottom) printf("▄");
            else printf(" ");
        }
        printf("\n");
    }

    printf("\nScan this QR code in RainMaker App\n\n");

    free(qrcode);
    free(temp);
    return true;
}


// void qrcode_display(const char *name, const char *username, const char *pop, const char *service_name)
// {
//     char payload[512];
//     snprintf(payload, sizeof(payload),
//              "{\"ver\":\"v1\",\"name\":\"%s\",\"username\":\"%s\",\"pop\":\"%s\",\"service_name\":\"%s\"}",
//              name ? name : "", username ? username : "", pop ? pop : "", service_name ? service_name : "");

//     esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
//     esp_qrcode_generate(&cfg, payload);
// }
void qrcode_display(const char *payload)
{
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    esp_qrcode_generate(&cfg, payload);
}
