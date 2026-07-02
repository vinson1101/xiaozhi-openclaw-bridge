#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "xob";

static esp_err_t read_string(nvs_handle_t nvs, const char *key, char *out, size_t out_len) {
    size_t required = out_len;
    esp_err_t err = nvs_get_str(nvs, key, out, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND && out_len > 0) {
        out[0] = '\0';
    }
    return err;
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t nvs;
    err = nvs_open("xob", NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS namespace 'xob' missing; waiting for provisioning");
        return;
    }
    ESP_ERROR_CHECK(err);

    char bridge_url[128];
    char device_token[64];
    esp_err_t url_err = read_string(nvs, "bridge_url", bridge_url, sizeof(bridge_url));
    esp_err_t token_err = read_string(nvs, "device_token", device_token, sizeof(device_token));
    nvs_close(nvs);

    if (url_err == ESP_ERR_NVS_NOT_FOUND || token_err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "bridge_url or device_token missing; waiting for provisioning");
        return;
    }
    ESP_ERROR_CHECK(url_err);
    ESP_ERROR_CHECK(token_err);

    ESP_LOGI(TAG, "XOB firmware skeleton ready");
    ESP_LOGI(TAG, "bridge_url=%s", strlen(bridge_url) > 0 ? "configured" : "empty");
    ESP_LOGI(TAG, "device_token=%s", strlen(device_token) > 0 ? "configured" : "empty");
    ESP_LOGI(TAG, "next: WiFi, ST7789 eyes, HTTP device hello");
}
