#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "provisioning.h"

static const char *TAG = "xob_prov";
static bool usb_serial_ready;

static esp_err_t ensure_usb_serial(void) {
    if (usb_serial_ready) {
        return ESP_OK;
    }
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 256,
        .rx_buffer_size = 256,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        usb_serial_ready = true;
    }
    return err;
}

static bool read_line(const char *label, char *out, size_t out_len) {
    if (ensure_usb_serial() != ESP_OK) {
        return false;
    }
    printf("%s: ", label);
    fflush(stdout);

    size_t len = 0;
    while (true) {
        char ch;
        int read = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(1000));
        if (read <= 0) {
            continue;
        }
        if (ch == '\n' || ch == '\r') {
            out[len] = '\0';
            return true;
        }
        if (len + 1 >= out_len) {
            out[0] = '\0';
            ESP_LOGW(TAG, "%s is too long; retrying", label);
            while (usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(10)) > 0 && ch != '\n' && ch != '\r') {
            }
            return false;
        }
        out[len++] = ch;
    }
}

static esp_err_t write_config(const char *bridge_url, const char *device_token, const char *wifi_ssid, const char *wifi_password) {
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open("xob", NVS_READWRITE, &nvs), TAG, "open xob NVS");
    esp_err_t err = nvs_set_str(nvs, "bridge_url", bridge_url);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "device_token", device_token);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "wifi_ssid", wifi_ssid);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "wifi_password", wifi_password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

void xob_run_serial_provisioning(void) {
    char bridge_url[129];
    char device_token[65];
    char wifi_ssid[34];
    char wifi_password[66];

    while (true) {
        puts("");
        puts("XOB provisioning over USB serial");
        puts("Values are stored in NVS namespace 'xob'. device_token and wifi_password may be empty.");

        if (!read_line("bridge_url", bridge_url, sizeof(bridge_url)) ||
            !read_line("device_token", device_token, sizeof(device_token)) ||
            !read_line("wifi_ssid", wifi_ssid, sizeof(wifi_ssid)) ||
            !read_line("wifi_password", wifi_password, sizeof(wifi_password))) {
            ESP_LOGW(TAG, "serial input ended; retrying");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (strlen(bridge_url) == 0 || strlen(wifi_ssid) == 0) {
            ESP_LOGW(TAG, "bridge_url and wifi_ssid are required; retrying");
            continue;
        }

        esp_err_t err = write_config(bridge_url, device_token, wifi_ssid, wifi_password);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to write xob config: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "xob config saved; rebooting");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
}
