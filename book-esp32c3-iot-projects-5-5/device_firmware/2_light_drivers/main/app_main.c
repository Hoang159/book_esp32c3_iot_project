/* ESP32-C3 Light Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "app_storage.h"
#include "app_priv.h"
#include "light_driver.h"

static const char *TAG = "light_drivers";

void app_main()
{
    int i = 0;
    ESP_LOGE(TAG, "app_main");

    /**
     * @brief NVS Flash initialization
     */
    ESP_LOGI(TAG, "NVS Flash initialization");
    app_storage_init();

    /**
     * @brief Application driver initialization
     */
    ESP_LOGI(TAG, "Application driver initialization");
    app_driver_init();

    while (1) {
        // Đỏ
        light_driver_set_rgb(255, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Xanh lá
        light_driver_set_rgb(0, 255, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Xanh dương
        light_driver_set_rgb(0, 0, 255);
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Trắng (R+G+B)
        light_driver_set_rgb(255, 255, 255);
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Tím (R+B)
        light_driver_set_rgb(255, 0, 255);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "[%02d] Hello world!", i++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
