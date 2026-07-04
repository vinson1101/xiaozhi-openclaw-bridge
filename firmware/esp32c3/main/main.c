#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "eyes.h"
#include "lcd.h"
#include "provisioning.h"
#include "screen.h"

static const char *TAG = "xob";
static EventGroupHandle_t wifi_events;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;
static int wifi_retry_count;
static volatile xob_eye_state_t avatar_eye_state = XOB_EYES_IDLE;
static volatile xob_screen_status_t avatar_wifi_status = XOB_SCREEN_STATUS_OFF;
static volatile xob_screen_status_t avatar_bridge_status = XOB_SCREEN_STATUS_OFF;

typedef struct {
    xob_eyes_frame_t eyes;
    xob_screen_status_t wifi_status;
    xob_screen_status_t bridge_status;
} avatar_frame_t;

static avatar_frame_t last_avatar_frame;
static bool has_last_avatar_frame;

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
        wifi_retry_count = 0;
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = (const wifi_event_sta_disconnected_t *)event_data;
        if (wifi_retry_count < 8) {
            wifi_retry_count++;
            ESP_LOGW(TAG, "WiFi disconnected, retrying (%d/8), reason=%d", wifi_retry_count, disconnected->reason);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "WiFi connection failed, reason=%d", disconnected->reason);
            xEventGroupSetBits(wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

static bool ssid_matches(const wifi_ap_record_t *ap, const char *target_ssid) {
    return strncmp((const char *)ap->ssid, target_ssid, sizeof(ap->ssid)) == 0;
}

static void log_target_scan_result(const char *target_ssid) {
    wifi_scan_config_t scan_config = {
        .show_hidden = true,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan count failed: %s", esp_err_to_name(err));
        esp_wifi_clear_ap_list();
        return;
    }
    if (ap_count == 0) {
        ESP_LOGI(TAG, "WiFi scan: aps=0 target_matches=0");
        return;
    }

    wifi_ap_record_t *records = calloc(ap_count, sizeof(*records));
    if (records == NULL) {
        ESP_LOGW(TAG, "WiFi scan results skipped: no memory");
        esp_wifi_clear_ap_list();
        return;
    }

    uint16_t records_to_read = ap_count;
    err = esp_wifi_scan_get_ap_records(&records_to_read, records);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan records failed: %s", esp_err_to_name(err));
        free(records);
        esp_wifi_clear_ap_list();
        return;
    }

    uint16_t matches = 0;
    int8_t best_rssi = -127;
    uint8_t best_channel = 0;
    wifi_auth_mode_t best_authmode = WIFI_AUTH_OPEN;
    for (uint16_t i = 0; i < records_to_read; i++) {
        if (!ssid_matches(&records[i], target_ssid)) {
            continue;
        }
        matches++;
        if (records[i].rssi > best_rssi) {
            best_rssi = records[i].rssi;
            best_channel = records[i].primary;
            best_authmode = records[i].authmode;
        }
    }
    if (matches > 0) {
        ESP_LOGI(TAG, "WiFi scan: aps=%u target_matches=%u best_channel=%u best_rssi=%d auth=%d",
                 ap_count, matches, best_channel, best_rssi, best_authmode);
    } else {
        ESP_LOGI(TAG, "WiFi scan: aps=%u target_matches=0", ap_count);
    }

    free(records);
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
    init.nvs_enable = false;
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");
    wifi_country_t country = {
        .cc = {'C', 'N', ' '},
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    ESP_RETURN_ON_ERROR(esp_wifi_set_country(&country), TAG, "wifi country");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL), TAG, "wifi event");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL), TAG, "ip event");

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, config->wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, config->wifi_password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = strlen(config->wifi_password) > 0 ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    log_target_scan_result(config->wifi_ssid);
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "wifi connect");

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

static bool avatar_eyes_equal(const xob_eyes_frame_t *left, const xob_eyes_frame_t *right) {
    return left->left_x == right->left_x &&
           left->right_x == right->right_x &&
           left->y == right->y &&
           left->width == right->width &&
           left->height == right->height &&
           left->pupil_dx == right->pupil_dx &&
           left->pupil_dy == right->pupil_dy &&
           left->openness == right->openness &&
           left->mouth_open == right->mouth_open;
}

static bool avatar_frames_equal(const avatar_frame_t *left, const avatar_frame_t *right) {
    return avatar_eyes_equal(&left->eyes, &right->eyes) &&
           left->wifi_status == right->wifi_status &&
           left->bridge_status == right->bridge_status;
}

static avatar_frame_t avatar_frame(uint32_t tick_ms) {
    xob_eye_state_t state = avatar_eye_state;
    return (avatar_frame_t){
        .eyes = xob_eyes_frame(state, tick_ms),
        .wifi_status = avatar_wifi_status,
        .bridge_status = avatar_bridge_status,
    };
}

static void set_avatar_state(
    xob_eye_state_t eye_state,
    xob_screen_status_t wifi_status,
    xob_screen_status_t bridge_status
) {
    avatar_eye_state = eye_state;
    avatar_wifi_status = wifi_status;
    avatar_bridge_status = bridge_status;
}

static esp_err_t draw_avatar_frame(const avatar_frame_t *frame) {
    xob_screen_frame_t screen = xob_screen_render_avatar(&frame->eyes, frame->wifi_status, frame->bridge_status);
    return xob_lcd_draw_frame(&screen);
}

static void avatar_task(void *arg) {
    (void)arg;
    while (true) {
        uint32_t tick_ms = (uint32_t)(esp_timer_get_time() / 1000);
        avatar_frame_t frame = avatar_frame(tick_ms);
        if (!has_last_avatar_frame || !avatar_frames_equal(&last_avatar_frame, &frame)) {
            esp_err_t err = draw_avatar_frame(&frame);
            if (err == ESP_OK) {
                last_avatar_frame = frame;
                has_last_avatar_frame = true;
            } else {
                ESP_LOGW(TAG, "avatar draw failed: %s", esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static void start_avatar_screen(void) {
    avatar_frame_t frame = avatar_frame(0);
    xob_screen_frame_t screen = xob_screen_render_avatar(&frame.eyes, frame.wifi_status, frame.bridge_status);
    ESP_LOGI(TAG, "eyes ready: %dx%d openness=%u", frame.eyes.width, frame.eyes.height, frame.eyes.openness);
    ESP_LOGI(TAG, "screen frame ready: rects=%u", screen.count);

    esp_err_t err = xob_lcd_init();
    if (err == ESP_OK) {
        ESP_ERROR_CHECK(xob_lcd_draw_frame(&screen));
        last_avatar_frame = frame;
        has_last_avatar_frame = true;
        BaseType_t created = xTaskCreate(avatar_task, "xob_avatar", 4096, NULL, 2, NULL);
        if (created != pdPASS) {
            ESP_LOGW(TAG, "avatar task not started");
        }
    } else {
        ESP_LOGW(TAG, "LCD init skipped: %s", esp_err_to_name(err));
    }
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS needs recovery; refusing automatic erase to preserve stock data");
        return;
    }
    ESP_ERROR_CHECK(err);

    start_avatar_screen();

    app_config_t config = {0};
    if (load_config(&config) != ESP_OK) {
        set_avatar_state(XOB_EYES_LISTENING, XOB_SCREEN_STATUS_PENDING, XOB_SCREEN_STATUS_OFF);
        xob_run_serial_provisioning();
        return;
    }
    set_avatar_state(XOB_EYES_THINKING, XOB_SCREEN_STATUS_PENDING, XOB_SCREEN_STATUS_OFF);
    if (connect_wifi(&config) != ESP_OK) {
        set_avatar_state(XOB_EYES_LISTENING, XOB_SCREEN_STATUS_ERROR, XOB_SCREEN_STATUS_OFF);
        xob_run_serial_provisioning();
        return;
    }
    set_avatar_state(XOB_EYES_THINKING, XOB_SCREEN_STATUS_OK, XOB_SCREEN_STATUS_PENDING);

    ESP_LOGI(TAG, "XOB firmware skeleton ready");
    ESP_LOGI(TAG, "bridge_url=%s", strlen(config.bridge_url) > 0 ? "configured" : "empty");
    ESP_LOGI(TAG, "device_token=%s", strlen(config.device_token) > 0 ? "configured" : "empty");
    ESP_LOGI(TAG, "wifi_ssid=%s", strlen(config.wifi_ssid) > 0 ? "configured" : "empty");
    err = post_device_hello(&config);
    if (err != ESP_OK) {
        set_avatar_state(XOB_EYES_IDLE, XOB_SCREEN_STATUS_OK, XOB_SCREEN_STATUS_ERROR);
        return;
    }
    set_avatar_state(XOB_EYES_IDLE, XOB_SCREEN_STATUS_OK, XOB_SCREEN_STATUS_OK);
    ESP_LOGI(TAG, "Bridge hello complete");
}
