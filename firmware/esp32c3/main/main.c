#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "eyes.h"
#include "provisioning.h"
#include "screen.h"

static const char *TAG = "xob";
static EventGroupHandle_t wifi_events;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;

typedef struct {
    char bridge_url[128];
    char device_token[64];
    char wifi_ssid[33];
    char wifi_password[65];
} app_config_t;

static esp_err_t read_string(nvs_handle_t nvs, const char *key, char *out, size_t out_len) {
    size_t required = out_len;
    esp_err_t err = nvs_get_str(nvs, key, out, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND && out_len > 0) {
        out[0] = '\0';
    }
    return err;
}

static void trim_trailing_slash(char *value) {
    size_t len = strlen(value);
    while (len > 0 && value[len - 1] == '/') {
        value[--len] = '\0';
    }
}

static esp_err_t load_config(app_config_t *config) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("xob", NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS namespace 'xob' missing; waiting for provisioning");
        return err;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "open NVS");

    esp_err_t url_err = read_string(nvs, "bridge_url", config->bridge_url, sizeof(config->bridge_url));
    esp_err_t token_err = read_string(nvs, "device_token", config->device_token, sizeof(config->device_token));
    esp_err_t ssid_err = read_string(nvs, "wifi_ssid", config->wifi_ssid, sizeof(config->wifi_ssid));
    esp_err_t pass_err = read_string(nvs, "wifi_password", config->wifi_password, sizeof(config->wifi_password));
    nvs_close(nvs);

    if (url_err == ESP_ERR_NVS_NOT_FOUND || token_err == ESP_ERR_NVS_NOT_FOUND ||
        ssid_err == ESP_ERR_NVS_NOT_FOUND || pass_err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "bridge_url, device_token, wifi_ssid, or wifi_password missing; waiting for provisioning");
        return ESP_ERR_NVS_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(url_err, TAG, "read bridge_url");
    ESP_RETURN_ON_ERROR(token_err, TAG, "read device_token");
    ESP_RETURN_ON_ERROR(ssid_err, TAG, "read wifi_ssid");
    ESP_RETURN_ON_ERROR(pass_err, TAG, "read wifi_password");
    trim_trailing_slash(config->bridge_url);
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(wifi_events, WIFI_FAIL_BIT);
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t connect_wifi(const app_config_t *config) {
    wifi_events = xEventGroupCreate();
    if (wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL), TAG, "wifi event");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL), TAG, "ip event");

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, config->wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, config->wifi_password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = strlen(config->wifi_password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    EventBits_t bits = xEventGroupWaitBits(
        wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(15000)
    );
    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        ESP_LOGI(TAG, "WiFi connected");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "WiFi connection failed or timed out");
    return ESP_FAIL;
}

static void make_device_id(char *out, size_t out_len) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, out_len, "esp32c3-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

static esp_err_t post_device_hello(const app_config_t *config) {
    char url[180];
    snprintf(url, sizeof(url), "%s/device/hello", config->bridge_url);

    char device_id[24];
    make_device_id(device_id, sizeof(device_id));

    char body[160];
    snprintf(
        body,
        sizeof(body),
        "{\"device_id\":\"%s\",\"firmware\":\"xob-esp32c3\",\"capabilities\":[\"display\",\"text\"]}",
        device_id
    );

    esp_http_client_config_t http_config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (strlen(config->device_token) > 0) {
        char auth[96];
        snprintf(auth, sizeof(auth), "Bearer %s", config->device_token);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "device hello failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "device hello status=%d", status);
    return (status >= 200 && status < 300) ? ESP_OK : ESP_FAIL;
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS needs recovery; refusing automatic erase to preserve stock data");
        return;
    }
    ESP_ERROR_CHECK(err);

    app_config_t config = {0};
    if (load_config(&config) != ESP_OK) {
        xob_run_serial_provisioning();
        return;
    }
    if (connect_wifi(&config) != ESP_OK) {
        return;
    }

    ESP_LOGI(TAG, "XOB firmware skeleton ready");
    ESP_LOGI(TAG, "bridge_url=%s", strlen(config.bridge_url) > 0 ? "configured" : "empty");
    ESP_LOGI(TAG, "device_token=%s", strlen(config.device_token) > 0 ? "configured" : "empty");
    ESP_LOGI(TAG, "wifi_ssid=%s", strlen(config.wifi_ssid) > 0 ? "configured" : "empty");
    xob_eyes_frame_t eyes = xob_eyes_frame(XOB_EYES_IDLE, 0);
    xob_screen_frame_t screen = xob_screen_render_eyes(&eyes);
    ESP_LOGI(TAG, "eyes ready: %dx%d openness=%u", eyes.width, eyes.height, eyes.openness);
    ESP_LOGI(TAG, "screen frame ready: rects=%u", screen.count);
    ESP_ERROR_CHECK(post_device_hello(&config));
    ESP_LOGI(TAG, "next: ST7789 panel driver and bridge state");
}
