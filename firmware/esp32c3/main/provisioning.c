#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "provisioning.h"

static const char *TAG = "xob_prov";
static esp_netif_t *ap_netif;
static httpd_handle_t ap_server;
static bool usb_serial_ready;

static esp_err_t ok_if_already_done(esp_err_t err) {
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

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

static int hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static esp_err_t form_value(const char *body, const char *key, char *out, size_t out_len) {
    size_t key_len = strlen(key);
    const char *part = body;

    while (*part != '\0') {
        const char *amp = strchr(part, '&');
        if (amp == NULL) {
            amp = part + strlen(part);
        }
        const char *eq = memchr(part, '=', amp - part);
        if (eq != NULL && (size_t)(eq - part) == key_len && strncmp(part, key, key_len) == 0) {
            size_t len = 0;
            for (const char *p = eq + 1; p < amp;) {
                char ch = *p++;
                if (ch == '+') {
                    ch = ' ';
                } else if (ch == '%') {
                    if (p + 1 >= amp) {
                        return ESP_ERR_INVALID_ARG;
                    }
                    int hi = hex_digit(p[0]);
                    int lo = hex_digit(p[1]);
                    if (hi < 0 || lo < 0) {
                        return ESP_ERR_INVALID_ARG;
                    }
                    ch = (char)((hi << 4) | lo);
                    p += 2;
                }
                if (len + 1 >= out_len) {
                    return ESP_ERR_INVALID_SIZE;
                }
                out[len++] = ch;
            }
            out[len] = '\0';
            return ESP_OK;
        }
        part = *amp == '&' ? amp + 1 : amp;
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t optional_form_value(const char *body, const char *key, char *out, size_t out_len) {
    esp_err_t err = form_value(body, key, out, out_len);
    if (err == ESP_ERR_NOT_FOUND && out_len > 0) {
        out[0] = '\0';
        return ESP_OK;
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

static esp_err_t write_config(const char *bridge_url, const char *device_token, const char *default_target, const char *wifi_ssid, const char *wifi_password) {
    nvs_handle_t nvs;
    const char *target = strlen(default_target) > 0 ? default_target : "fake";
    ESP_RETURN_ON_ERROR(nvs_open("xob", NVS_READWRITE, &nvs), TAG, "open xob NVS");
    esp_err_t err = nvs_set_str(nvs, "bridge_url", bridge_url);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "device_token", device_token);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "default_target", target);
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

static esp_err_t send_setup_page(httpd_req_t *req) {
    static const char page[] =
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>XOB Setup</title></head><body>"
        "<h1>XOB Setup</h1>"
        "<form method=\"post\" action=\"/save\">"
        "<p><label>Bridge URL<br><input name=\"bridge_url\" required maxlength=\"128\" placeholder=\"http://192.168.4.2:8788\"></label></p>"
        "<p><label>Device token<br><input name=\"device_token\" maxlength=\"64\"></label></p>"
        "<p><label>Default target<br><select name=\"default_target\"><option value=\"fake\">fake</option><option value=\"openclaw\">openclaw</option></select></label></p>"
        "<p><label>WiFi SSID<br><input name=\"wifi_ssid\" required maxlength=\"32\"></label></p>"
        "<p><label>WiFi password<br><input name=\"wifi_password\" type=\"password\" maxlength=\"64\"></label></p>"
        "<p><button type=\"submit\">Save and reboot</button></p>"
        "</form></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static void reboot_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static esp_err_t save_setup(httpd_req_t *req) {
    char body[768];
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid form");
    }

    int received = 0;
    while (received < req->content_len) {
        int read = httpd_req_recv(req, body + received, req->content_len - received);
        if (read <= 0) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid form");
        }
        received += read;
    }
    body[received] = '\0';

    char bridge_url[129];
    char device_token[65];
    char default_target[16];
    char wifi_ssid[34];
    char wifi_password[66];
    esp_err_t err = form_value(body, "bridge_url", bridge_url, sizeof(bridge_url));
    if (err == ESP_OK) {
        err = optional_form_value(body, "device_token", device_token, sizeof(device_token));
    }
    if (err == ESP_OK) {
        err = optional_form_value(body, "default_target", default_target, sizeof(default_target));
    }
    if (err == ESP_OK) {
        err = form_value(body, "wifi_ssid", wifi_ssid, sizeof(wifi_ssid));
    }
    if (err == ESP_OK) {
        err = optional_form_value(body, "wifi_password", wifi_password, sizeof(wifi_password));
    }
    if (err != ESP_OK || strlen(bridge_url) == 0 || strlen(wifi_ssid) == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid form");
    }

    err = write_config(bridge_url, device_token, default_target, wifi_ssid, wifi_password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to write xob config: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }

    httpd_resp_set_type(req, "text/html");
    err = httpd_resp_sendstr(req, "Saved. Rebooting.");
    xTaskCreate(reboot_task, "xob_reboot", 2048, NULL, 5, NULL);
    return err;
}

static void make_ap_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ssid, ssid_len, "XOB-%02X%02X%02X", mac[3], mac[4], mac[5]);
    snprintf(password, password_len, "xob-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

void xob_start_ap_provisioning(void) {
    if (ap_server != NULL) {
        return;
    }

    esp_err_t err = ok_if_already_done(esp_netif_init());
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AP provisioning netif init failed: %s", esp_err_to_name(err));
        return;
    }
    err = ok_if_already_done(esp_event_loop_create_default());
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AP provisioning event loop failed: %s", esp_err_to_name(err));
        return;
    }
    if (ap_netif == NULL) {
        ap_netif = esp_netif_create_default_wifi_ap();
        if (ap_netif == NULL) {
            ESP_LOGW(TAG, "AP provisioning netif create failed");
            return;
        }
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = false;
    err = esp_wifi_init(&init);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        ESP_LOGW(TAG, "AP provisioning WiFi init failed: %s", esp_err_to_name(err));
        return;
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "AP provisioning WiFi storage failed: %s", esp_err_to_name(err));
            return;
        }
    }

    char ssid[16];
    char password[16];
    make_ap_credentials(ssid, sizeof(ssid), password, sizeof(password));

    wifi_config_t config = {0};
    strlcpy((char *)config.ap.ssid, ssid, sizeof(config.ap.ssid));
    strlcpy((char *)config.ap.password, password, sizeof(config.ap.password));
    config.ap.ssid_len = strlen(ssid);
    config.ap.channel = 1;
    config.ap.max_connection = 1;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    (void)esp_wifi_disconnect();
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AP provisioning WiFi mode failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AP provisioning WiFi config failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
        ESP_LOGW(TAG, "AP provisioning WiFi start failed: %s", esp_err_to_name(err));
        return;
    }

    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.server_port = 80;
    err = httpd_start(&ap_server, &http_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AP provisioning HTTP start failed: %s", esp_err_to_name(err));
        ap_server = NULL;
        return;
    }

    httpd_uri_t index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = send_setup_page,
    };
    httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_setup,
    };
    err = httpd_register_uri_handler(ap_server, &index);
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(ap_server, &save);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AP provisioning route setup failed: %s", esp_err_to_name(err));
        httpd_stop(ap_server);
        ap_server = NULL;
        return;
    }
    ESP_LOGI(TAG, "AP provisioning started: ssid=%s password=xob-<lowercase suffix> url=http://192.168.4.1/", ssid);
}

void xob_run_serial_provisioning(void) {
    char bridge_url[129];
    char device_token[65];
    char default_target[16];
    char wifi_ssid[34];
    char wifi_password[66];

    while (true) {
        puts("");
        puts("XOB provisioning over USB serial");
        puts("Values are stored in NVS namespace 'xob'. device_token and wifi_password may be empty.");

        if (!read_line("bridge_url", bridge_url, sizeof(bridge_url)) ||
            !read_line("device_token", device_token, sizeof(device_token)) ||
            !read_line("default_target (empty=fake)", default_target, sizeof(default_target)) ||
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

        esp_err_t err = write_config(bridge_url, device_token, default_target, wifi_ssid, wifi_password);
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
