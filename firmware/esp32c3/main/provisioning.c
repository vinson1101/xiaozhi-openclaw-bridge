#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "provisioning.h"

static const char *TAG = "xob_prov";

static bool trim_newline(char *value) {
    size_t end = strcspn(value, "\r\n");
    bool complete = value[end] != '\0';
    value[end] = '\0';
    return complete;
}

static void discard_line(void) {
    int ch;
    while ((ch = getchar()) != '\n' && ch != '\r' && ch != EOF) {
    }
}

static bool read_line(const char *label, char *out, size_t out_len) {
    printf("%s: ", label);
    fflush(stdout);
    if (fgets(out, out_len, stdin) == NULL) {
        return false;
    }
    if (!trim_newline(out)) {
        discard_line();
        out[0] = '\0';
        ESP_LOGW(TAG, "%s is too long; retrying", label);
        return false;
    }
    return true;
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
