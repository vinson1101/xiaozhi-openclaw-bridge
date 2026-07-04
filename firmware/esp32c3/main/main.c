#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ping/ping_sock.h"
#include "lwip/ip_addr.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "eyes.h"
#include "lcd.h"
#include "provisioning.h"
#include "screen.h"

static const char *TAG = "xob";
static EventGroupHandle_t wifi_events;
static esp_netif_t *wifi_netif;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;
static int wifi_retry_count;
static volatile xob_eye_state_t avatar_eye_state = XOB_EYES_IDLE;
static volatile xob_screen_status_t avatar_wifi_status = XOB_SCREEN_STATUS_OFF;
static volatile xob_screen_status_t avatar_bridge_status = XOB_SCREEN_STATUS_OFF;
static bool buttons_ready;
static bool button_task_started;
static int local_volume = 50;

#define XOB_BUTTON_VOLUME_DOWN_GPIO GPIO_NUM_7
#define XOB_BUTTON_LISTEN_GPIO GPIO_NUM_8
#define XOB_BUTTON_VOLUME_UP_GPIO GPIO_NUM_9
#define XOB_BUTTON_VOLUME_DOWN BIT0
#define XOB_BUTTON_LISTEN BIT1
#define XOB_BUTTON_VOLUME_UP BIT2
#define XOB_BUTTON_ALL (XOB_BUTTON_VOLUME_DOWN | XOB_BUTTON_LISTEN | XOB_BUTTON_VOLUME_UP)
#define XOB_BUTTON_CONFIG_CHORD (XOB_BUTTON_VOLUME_DOWN | XOB_BUTTON_VOLUME_UP)
#define XOB_VB_UART_NUM UART_NUM_1
#define XOB_VB_UART_BAUD 2000000
#define XOB_VB_TX_GPIO GPIO_NUM_20
#define XOB_VB_RX_GPIO GPIO_NUM_10
#define XOB_VB_TALK_FRAMES 12

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
    char default_target[16];
    char wifi_ssid[33];
    char wifi_password[65];
} app_config_t;

typedef struct {
    char host[96];
    uint16_t port;
} bridge_endpoint_t;

static app_config_t active_config;

typedef struct {
    const char *label;
    SemaphoreHandle_t done;
    uint32_t transmitted;
    uint32_t received;
    uint32_t duration_ms;
} ping_result_t;

typedef struct {
    gpio_num_t tx;
    gpio_num_t rx;
} vb_uart_candidate_t;

typedef struct {
    uint32_t bytes;
    uint32_t frames;
    uint32_t pcm_frames;
    uint32_t ctl_frames;
    uint32_t wake_frames;
} vb_uart_stats_t;

static void set_avatar_state(
    xob_eye_state_t eye_state,
    xob_screen_status_t wifi_status,
    xob_screen_status_t bridge_status
);
static void enter_button_provisioning(void);

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
    esp_err_t target_err = read_string(nvs, "default_target", config->default_target, sizeof(config->default_target));
    esp_err_t ssid_err = read_string(nvs, "wifi_ssid", config->wifi_ssid, sizeof(config->wifi_ssid));
    esp_err_t pass_err = read_string(nvs, "wifi_password", config->wifi_password, sizeof(config->wifi_password));
    nvs_close(nvs);
    if (target_err == ESP_ERR_NVS_NOT_FOUND || strlen(config->default_target) == 0) {
        strlcpy(config->default_target, "fake", sizeof(config->default_target));
    }

    if (url_err == ESP_ERR_NVS_NOT_FOUND || token_err == ESP_ERR_NVS_NOT_FOUND ||
        ssid_err == ESP_ERR_NVS_NOT_FOUND || pass_err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "bridge_url, device_token, wifi_ssid, or wifi_password missing; waiting for provisioning");
        return ESP_ERR_NVS_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(url_err, TAG, "read bridge_url");
    ESP_RETURN_ON_ERROR(token_err, TAG, "read device_token");
    if (target_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_RETURN_ON_ERROR(target_err, TAG, "read default_target");
    }
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
    size_t target_len = strlen(target_ssid);
    size_t ap_len = strnlen((const char *)ap->ssid, sizeof(ap->ssid));
    return target_len == ap_len && memcmp(ap->ssid, target_ssid, target_len) == 0;
}

static uint32_t ssid_hash(const uint8_t *ssid, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= ssid[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t string_hash(const char *value) {
    return ssid_hash((const uint8_t *)value, strlen(value));
}

static void log_target_scan_result(const char *target_ssid) {
    wifi_scan_config_t scan_config = {
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.max = 500,
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
    size_t target_len = strlen(target_ssid);
    uint32_t target_hash = ssid_hash((const uint8_t *)target_ssid, target_len);
    if (ap_count == 0) {
        ESP_LOGI(TAG, "WiFi scan: aps=0 target_len=%u target_hash=%08lx target_matches=0",
                 (unsigned)target_len, (unsigned long)target_hash);
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
        ESP_LOGI(TAG, "WiFi scan: aps=%u target_len=%u target_hash=%08lx target_matches=%u best_channel=%u best_rssi=%d auth=%d",
                 ap_count, (unsigned)target_len, (unsigned long)target_hash, matches,
                 best_channel, best_rssi, best_authmode);
    } else {
        ESP_LOGI(TAG, "WiFi scan: aps=%u target_len=%u target_hash=%08lx target_matches=0",
                 ap_count, (unsigned)target_len, (unsigned long)target_hash);
    }

    free(records);
}

static void on_ping_end(esp_ping_handle_t hdl, void *args) {
    ping_result_t *result = (ping_result_t *)args;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &result->transmitted, sizeof(result->transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &result->received, sizeof(result->received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &result->duration_ms, sizeof(result->duration_ms));
    xSemaphoreGive(result->done);
}

static esp_err_t ping_target(const char *label, const ip_addr_t *target_addr) {
    ping_result_t result = {
        .label = label,
        .done = xSemaphoreCreateBinary(),
    };
    if (result.done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.target_addr = *target_addr;
    config.count = 3;
    config.interval_ms = 300;
    config.timeout_ms = 1000;
    config.task_stack_size = 3072;
    if (wifi_netif != NULL) {
        config.interface = esp_netif_get_netif_impl_index(wifi_netif);
    }

    esp_ping_callbacks_t callbacks = {
        .on_ping_end = on_ping_end,
        .cb_args = &result,
    };

    esp_ping_handle_t ping = NULL;
    esp_err_t err = esp_ping_new_session(&config, &callbacks, &ping);
    if (err != ESP_OK) {
        vSemaphoreDelete(result.done);
        ESP_LOGW(TAG, "ping %s start failed: %s", label, esp_err_to_name(err));
        return err;
    }

    err = esp_ping_start(ping);
    if (err == ESP_OK && xSemaphoreTake(result.done, pdMS_TO_TICKS(5000)) != pdTRUE) {
        err = ESP_ERR_TIMEOUT;
    }
    esp_ping_delete_session(ping);
    vSemaphoreDelete(result.done);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ping %s failed: %s", label, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "ping %s: sent=%lu received=%lu duration_ms=%lu",
             label,
             (unsigned long)result.transmitted,
             (unsigned long)result.received,
             (unsigned long)result.duration_ms);
    return result.received > 0 ? ESP_OK : ESP_FAIL;
}

static bool bridge_url_ipv4_target(const char *bridge_url, ip_addr_t *out) {
    const char *host = strstr(bridge_url, "://");
    host = host == NULL ? bridge_url : host + 3;
    if (*host == '[' || *host == '\0') {
        return false;
    }

    char host_buf[64] = {0};
    size_t len = 0;
    while (host[len] != '\0' && host[len] != ':' && host[len] != '/' && len + 1 < sizeof(host_buf)) {
        host_buf[len] = host[len];
        len++;
    }
    host_buf[len] = '\0';
    return len > 0 && ipaddr_aton(host_buf, out) == 1 && out->type == IPADDR_TYPE_V4;
}

static bool bridge_http_endpoint(const char *bridge_url, bridge_endpoint_t *out) {
    const char *host = strstr(bridge_url, "http://");
    if (host == NULL) {
        return false;
    }
    host += strlen("http://");
    size_t len = 0;
    while (host[len] != '\0' && host[len] != ':' && host[len] != '/' && len + 1 < sizeof(out->host)) {
        out->host[len] = host[len];
        len++;
    }
    out->host[len] = '\0';
    out->port = 80;
    if (host[len] == ':') {
        int port = atoi(host + len + 1);
        if (port <= 0 || port > 65535) {
            return false;
        }
        out->port = (uint16_t)port;
    }
    return len > 0;
}

static void log_safe_status(const app_config_t *config) {
    bridge_endpoint_t endpoint = {0};
    bool http = bridge_http_endpoint(config->bridge_url, &endpoint);
    ESP_LOGI(TAG, "config: bridge_url=%s device_token=%s wifi_ssid=%s target=%s",
             strlen(config->bridge_url) > 0 ? "configured" : "empty",
             strlen(config->device_token) > 0 ? "configured" : "empty",
             strlen(config->wifi_ssid) > 0 ? "configured" : "empty",
             strlen(config->default_target) > 0 ? config->default_target : "fake");
    if (http) {
        ESP_LOGI(TAG, "bridge_endpoint: scheme=http port=%u host_hash=%08lx",
                 endpoint.port, (unsigned long)string_hash(endpoint.host));
    } else {
        ESP_LOGI(TAG, "bridge_endpoint: scheme=unsupported port=0 host_hash=00000000");
    }
}

static void log_network_diagnostics(const app_config_t *config) {
    if (wifi_netif == NULL) {
        ESP_LOGW(TAG, "netif diagnostics skipped: no STA netif");
        return;
    }

    esp_netif_ip_info_t ip_info = {0};
    esp_err_t err = esp_netif_get_ip_info(wifi_netif, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "netif ip info failed: %s", esp_err_to_name(err));
        return;
    }

    esp_netif_dns_info_t dns_info = {0};
    err = esp_netif_get_dns_info(wifi_netif, ESP_NETIF_DNS_MAIN, &dns_info);
    if (err == ESP_OK && dns_info.ip.type == ESP_IPADDR_TYPE_V4) {
        ESP_LOGI(TAG, "netif: ip=" IPSTR " mask=" IPSTR " gw=" IPSTR " dns=" IPSTR,
                 IP2STR(&ip_info.ip),
                 IP2STR(&ip_info.netmask),
                 IP2STR(&ip_info.gw),
                 IP2STR(&dns_info.ip.u_addr.ip4));
    } else {
        ESP_LOGI(TAG, "netif: ip=" IPSTR " mask=" IPSTR " gw=" IPSTR " dns=unavailable",
                 IP2STR(&ip_info.ip),
                 IP2STR(&ip_info.netmask),
                 IP2STR(&ip_info.gw));
    }

    ip_addr_t gateway = {0};
    gateway.type = IPADDR_TYPE_V4;
    gateway.u_addr.ip4.addr = ip_info.gw.addr;
    (void)ping_target("gateway", &gateway);

    ip_addr_t internet = {0};
    IP_ADDR4(&internet, 223, 5, 5, 5);
    (void)ping_target("internet", &internet);

    ip_addr_t bridge_host = {0};
    if (bridge_url_ipv4_target(config->bridge_url, &bridge_host)) {
        (void)ping_target("bridge_host", &bridge_host);
    } else {
        ESP_LOGI(TAG, "ping bridge_host skipped: bridge URL host is not an IPv4 literal");
    }
}

static esp_err_t connect_wifi(const app_config_t *config) {
    wifi_events = xEventGroupCreate();
    if (wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    wifi_netif = esp_netif_create_default_wifi_sta();
    if (wifi_netif == NULL) {
        return ESP_FAIL;
    }

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
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.rssi = -127;
    wifi_config.sta.threshold.authmode = strlen(config->wifi_password) > 0 ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.failure_retry_cnt = 3;

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
        log_network_diagnostics(config);
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
        "{\"device_id\":\"%s\",\"firmware\":\"xob-esp32c3\",\"capabilities\":[\"display\",\"text\",\"audio_upload\",\"websocket\"]}",
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

static bool json_escape(char *out, size_t out_len, const char *in) {
    size_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
            if (pos + 2 >= out_len) {
                return false;
            }
            out[pos++] = '\\';
            out[pos++] = (char)*p;
        } else if (*p < 0x20) {
            return false;
        } else {
            if (pos + 1 >= out_len) {
                return false;
            }
            out[pos++] = (char)*p;
        }
    }
    out[pos] = '\0';
    return true;
}

static esp_err_t post_device_command(const app_config_t *config, const char *text) {
    char escaped_text[260];
    if (!json_escape(escaped_text, sizeof(escaped_text), text)) {
        return ESP_ERR_INVALID_SIZE;
    }
    char escaped_target[32];
    const char *target = strlen(config->default_target) > 0 ? config->default_target : "fake";
    if (!json_escape(escaped_target, sizeof(escaped_target), target)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char url[180];
    snprintf(url, sizeof(url), "%s/device/command", config->bridge_url);

    char device_id[24];
    make_device_id(device_id, sizeof(device_id));

    char body[384];
    snprintf(
        body,
        sizeof(body),
        "{\"device_id\":\"%s\",\"target\":\"%s\",\"text\":\"%s\"}",
        device_id,
        escaped_target,
        escaped_text
    );

    esp_http_client_config_t http_config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
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
        ESP_LOGW(TAG, "device command failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "device command status=%d", status);
    return (status >= 200 && status < 300) ? ESP_OK : ESP_FAIL;
}

static esp_err_t post_device_audio_probe(const app_config_t *config) {
    static const uint8_t audio[320] = {0};
    const char *target = strlen(config->default_target) > 0 ? config->default_target : "fake";

    char device_id[24];
    make_device_id(device_id, sizeof(device_id));

    char url[240];
    snprintf(
        url,
        sizeof(url),
        "%s/device/audio?device_id=%s&target=%s&sample_rate=16000&channels=1",
        config->bridge_url,
        device_id,
        target
    );

    esp_http_client_config_t http_config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Content-Type", "audio/pcm");
    if (strlen(config->device_token) > 0) {
        char auth[96];
        snprintf(auth, sizeof(auth), "Bearer %s", config->device_token);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    esp_http_client_set_post_field(client, (const char *)audio, sizeof(audio));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "device audio probe failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "device audio probe status=%d", status);
    return (status >= 200 && status < 300) ? ESP_OK : ESP_FAIL;
}

static uint8_t vb6824_sum8(const uint8_t *data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + data[i]);
    }
    return sum;
}

static void vb6824_send_frame(uint16_t cmd, const uint8_t *data, uint16_t len) {
    uint8_t frame[64] = {0x55, 0xaa, (uint8_t)(len >> 8), (uint8_t)len, (uint8_t)(cmd >> 8), (uint8_t)cmd};
    if (len > 0 && data != NULL) {
        memcpy(frame + 6, data, len);
    }
    frame[6 + len] = vb6824_sum8(frame, 6 + len);
    uart_write_bytes(XOB_VB_UART_NUM, frame, 7 + len);
}

static void log_vb6824_raw_bytes(const vb_uart_candidate_t *candidate, const uint8_t *data, size_t len) {
    char hex[97];
    size_t shown = len > 32 ? 32 : len;
    for (size_t i = 0; i < shown; i++) {
        snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02x ", data[i]);
    }
    hex[shown * 3] = '\0';
    ESP_LOGI(TAG, "vb6824 uart raw tx=%d rx=%d bytes=%u data=%s%s",
             candidate->tx, candidate->rx, (unsigned)len, hex, len > shown ? "..." : "");
}

static void parse_vb6824_frames(const vb_uart_candidate_t *candidate, const uint8_t *data, size_t len, vb_uart_stats_t *stats) {
    stats->bytes += len;
    for (size_t i = 0; i + 7 <= len; i++) {
        if (data[i] != 0x55 || data[i + 1] != 0xaa) {
            continue;
        }
        uint16_t body_len = ((uint16_t)data[i + 2] << 8) | data[i + 3];
        size_t frame_len = 7 + body_len;
        if (i + frame_len > len) {
            continue;
        }
        if (vb6824_sum8(data + i, frame_len - 1) != data[i + frame_len - 1]) {
            continue;
        }
        uint16_t cmd = ((uint16_t)data[i + 4] << 8) | data[i + 5];
        const uint8_t *body = data + i + 6;
        stats->frames++;
        if (cmd == 0x2080) {
            stats->pcm_frames++;
            if (stats->pcm_frames <= 3) {
                ESP_LOGI(TAG, "vb6824 pcm frame tx=%d rx=%d len=%u", candidate->tx, candidate->rx, body_len);
            }
        } else if (cmd == 0x0180) {
            stats->ctl_frames++;
            ESP_LOGI(TAG, "vb6824 ctl frame tx=%d rx=%d len=%u text=%.*s",
                     candidate->tx, candidate->rx, body_len, body_len, body);
        } else if (cmd == 0x0280) {
            stats->wake_frames++;
            ESP_LOGI(TAG, "vb6824 wake frame tx=%d rx=%d len=%u text=%.*s",
                     candidate->tx, candidate->rx, body_len, body_len, body);
        }
        i += frame_len - 1;
    }
}

static esp_err_t read_vb6824_uart_candidate(const vb_uart_candidate_t *candidate, vb_uart_stats_t *stats) {
    memset(stats, 0, sizeof(*stats));

    const uart_config_t uart_config = {
        .baud_rate = XOB_VB_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(XOB_VB_UART_NUM, 4096, 512, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_param_config(XOB_VB_UART_NUM, &uart_config);
    if (err == ESP_OK) {
        err = uart_set_pin(XOB_VB_UART_NUM, candidate->tx, candidate->rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        uart_driver_delete(XOB_VB_UART_NUM);
        return err;
    }

    ESP_LOGI(TAG, "vb6824 uart try tx=%d rx=%d baud=%d", candidate->tx, candidate->rx, XOB_VB_UART_BAUD);
    uart_flush_input(XOB_VB_UART_NUM);
    uint8_t one = 1;
    for (int attempt = 0; attempt < 4; attempt++) {
        vb6824_send_frame(0x0207, &one, 1);
        int64_t end_us = esp_timer_get_time() + 250000;
        while (esp_timer_get_time() < end_us) {
            uint8_t buf[256];
            int got = uart_read_bytes(XOB_VB_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(30));
            if (got > 0) {
                if (stats->bytes == 0 && stats->frames == 0) {
                    log_vb6824_raw_bytes(candidate, buf, got);
                }
                parse_vb6824_frames(candidate, buf, got, stats);
            }
        }
    }

    uart_driver_delete(XOB_VB_UART_NUM);
    gpio_reset_pin(candidate->tx);
    gpio_reset_pin(candidate->rx);
    return ESP_OK;
}

static esp_err_t run_vb6824_uart_probe(void) {
    static const vb_uart_candidate_t candidates[] = {
        {.tx = GPIO_NUM_10, .rx = GPIO_NUM_21},
        {.tx = GPIO_NUM_20, .rx = GPIO_NUM_21},
        {.tx = GPIO_NUM_6, .rx = GPIO_NUM_21},
        {.tx = GPIO_NUM_13, .rx = GPIO_NUM_21},
        {.tx = GPIO_NUM_10, .rx = GPIO_NUM_20},
        {.tx = GPIO_NUM_20, .rx = GPIO_NUM_10},
        {.tx = GPIO_NUM_6, .rx = GPIO_NUM_10},
        {.tx = GPIO_NUM_13, .rx = GPIO_NUM_10},
        {.tx = GPIO_NUM_10, .rx = GPIO_NUM_13},
        {.tx = GPIO_NUM_20, .rx = GPIO_NUM_13},
    };

    ESP_LOGI(TAG, "vb6824 uart probe start candidates=%u", (unsigned)(sizeof(candidates) / sizeof(candidates[0])));
    vb_uart_stats_t best = {0};
    vb_uart_candidate_t best_candidate = {0};
    esp_err_t last_err = ESP_FAIL;
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        vb_uart_stats_t stats;
        last_err = read_vb6824_uart_candidate(&candidates[i], &stats);
        if (last_err != ESP_OK) {
            ESP_LOGW(TAG, "vb6824 uart tx=%d rx=%d err=%s",
                     candidates[i].tx, candidates[i].rx, esp_err_to_name(last_err));
            continue;
        }
        ESP_LOGI(TAG, "vb6824 uart result tx=%d rx=%d bytes=%" PRIu32 " frames=%" PRIu32 " pcm=%" PRIu32 " ctl=%" PRIu32 " wake=%" PRIu32,
                 candidates[i].tx, candidates[i].rx, stats.bytes, stats.frames, stats.pcm_frames, stats.ctl_frames, stats.wake_frames);
        if (stats.frames > best.frames || (stats.frames == best.frames && stats.bytes > best.bytes)) {
            best = stats;
            best_candidate = candidates[i];
        }
        if (stats.frames > 0) {
            break;
        }
    }
    if (best.frames > 0 || best.bytes > 0) {
        ESP_LOGI(TAG, "vb6824 uart best tx=%d rx=%d bytes=%" PRIu32 " frames=%" PRIu32,
                 best_candidate.tx, best_candidate.rx, best.bytes, best.frames);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "vb6824 uart probe complete no bytes");
    return last_err == ESP_OK ? ESP_FAIL : last_err;
}

static int recv_some(int sock, char *buffer, size_t buffer_len, int timeout_ms) {
    struct timeval timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return recv(sock, buffer, buffer_len, 0);
}

static esp_err_t recv_exact(int sock, void *buffer, size_t len) {
    uint8_t *out = (uint8_t *)buffer;
    size_t pos = 0;
    while (pos < len) {
        int got = recv(sock, out + pos, len - pos, 0);
        if (got <= 0) {
            return ESP_FAIL;
        }
        pos += got;
    }
    return ESP_OK;
}

static esp_err_t websocket_send_masked_frame(int sock, uint8_t opcode, const void *payload, size_t len) {
    if (len >= 256) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t mask[4];
    uint32_t random = esp_random();
    memcpy(mask, &random, sizeof(mask));

    uint8_t header[8] = {(uint8_t)(0x80 | opcode), 0, 0, 0, mask[0], mask[1], mask[2], mask[3]};
    size_t header_len = 6;
    if (len < 126) {
        header[1] = 0x80 | (uint8_t)len;
        header[2] = mask[0];
        header[3] = mask[1];
        header[4] = mask[2];
        header[5] = mask[3];
    } else {
        header[1] = 0x80 | 126;
        header[2] = (uint8_t)(len >> 8);
        header[3] = (uint8_t)len;
        header_len = 8;
    }
    if (send(sock, header, header_len, 0) != (int)header_len) {
        return ESP_FAIL;
    }
    const uint8_t *bytes = (const uint8_t *)payload;
    uint8_t masked[256];
    for (size_t i = 0; i < len; i++) {
        masked[i] = bytes[i] ^ mask[i % 4];
    }
    return send(sock, masked, len, 0) == (int)len ? ESP_OK : ESP_FAIL;
}

static esp_err_t websocket_send_vb6824_audio(int sock, int wanted_frames, size_t *sent_bytes) {
    const vb_uart_candidate_t candidate = {.tx = XOB_VB_TX_GPIO, .rx = XOB_VB_RX_GPIO};
    *sent_bytes = 0;

    const uart_config_t uart_config = {
        .baud_rate = XOB_VB_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(XOB_VB_UART_NUM, 4096, 512, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_param_config(XOB_VB_UART_NUM, &uart_config);
    if (err == ESP_OK) {
        err = uart_set_pin(XOB_VB_UART_NUM, candidate.tx, candidate.rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        uart_driver_delete(XOB_VB_UART_NUM);
        return err;
    }

    ESP_LOGI(TAG, "vb6824 websocket audio start tx=%d rx=%d frames=%d", candidate.tx, candidate.rx, wanted_frames);
    uart_flush_input(XOB_VB_UART_NUM);
    uint8_t one = 1;
    vb6824_send_frame(0x0207, &one, 1);

    uint8_t pending[256];
    size_t pending_len = 0;
    int sent_frames = 0;
    int64_t end_us = esp_timer_get_time() + 2500000;
    while (sent_frames < wanted_frames && esp_timer_get_time() < end_us) {
        uint8_t buf[256];
        int got = uart_read_bytes(XOB_VB_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (got <= 0) {
            continue;
        }
        if (pending_len + (size_t)got > sizeof(pending)) {
            pending_len = 0;
        }
        memcpy(pending + pending_len, buf, got);
        pending_len += got;

        size_t pos = 0;
        while (pos + 7 <= pending_len) {
            if (pending[pos] != 0x55 || pending[pos + 1] != 0xaa) {
                pos++;
                continue;
            }
            uint16_t body_len = ((uint16_t)pending[pos + 2] << 8) | pending[pos + 3];
            size_t frame_len = 7 + body_len;
            if (pos + frame_len > pending_len) {
                break;
            }
            if (vb6824_sum8(pending + pos, frame_len - 1) != pending[pos + frame_len - 1]) {
                pos++;
                continue;
            }
            uint16_t cmd = ((uint16_t)pending[pos + 4] << 8) | pending[pos + 5];
            const uint8_t *body = pending + pos + 6;
            if (cmd == 0x2080) {
                if (sent_frames < wanted_frames) {
                    err = websocket_send_masked_frame(sock, 2, body, body_len);
                    if (err != ESP_OK) {
                        break;
                    }
                    *sent_bytes += body_len;
                    sent_frames++;
                }
            } else if (cmd == 0x0280) {
                ESP_LOGI(TAG, "vb6824 wake word: %.*s", body_len, body);
            }
            pos += frame_len;
            if (sent_frames >= wanted_frames) {
                break;
            }
        }
        if (err != ESP_OK) {
            break;
        }
        if (pos > 0) {
            memmove(pending, pending + pos, pending_len - pos);
            pending_len -= pos;
        }
    }

    uart_driver_delete(XOB_VB_UART_NUM);
    gpio_reset_pin(candidate.tx);
    gpio_reset_pin(candidate.rx);
    if (err == ESP_OK && sent_frames > 0) {
        ESP_LOGI(TAG, "vb6824 websocket audio sent frames=%d bytes=%u", sent_frames, (unsigned)*sent_bytes);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "vb6824 websocket audio failed frames=%d bytes=%u err=%s",
             sent_frames, (unsigned)*sent_bytes, esp_err_to_name(err));
    return err == ESP_OK ? ESP_FAIL : err;
}

static esp_err_t websocket_send_masked_text(int sock, const char *text) {
    return websocket_send_masked_frame(sock, 1, text, strlen(text));
}

static esp_err_t recv_websocket_text(int sock, char *out, size_t out_len) {
    uint8_t header[2];
    if (recv_exact(sock, header, sizeof(header)) != ESP_OK) {
        return ESP_FAIL;
    }
    size_t len = header[1] & 0x7f;
    if (len == 126) {
        uint8_t extended[2];
        if (recv_exact(sock, extended, sizeof(extended)) != ESP_OK) {
            return ESP_FAIL;
        }
        len = ((size_t)extended[0] << 8) | extended[1];
    } else if (len == 127) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if ((header[0] & 0x0f) != 1 || len >= out_len) {
        return ESP_FAIL;
    }
    if (recv_exact(sock, out, len) != ESP_OK) {
        return ESP_FAIL;
    }
    out[len] = '\0';
    return ESP_OK;
}

static bool json_contains_string(const char *json, const char *key, const char *value) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\": \"%s\"", key, value);
    if (strstr(json, needle) != NULL) {
        return true;
    }
    snprintf(needle, sizeof(needle), "\"%s\":\"%s\"", key, value);
    return strstr(json, needle) != NULL;
}

static esp_err_t probe_xiaozhi_websocket(const app_config_t *config, bool send_talk_probe, bool use_vb_audio) {
    bridge_endpoint_t endpoint = {0};
    if (!bridge_http_endpoint(config->bridge_url, &endpoint)) {
        ESP_LOGW(TAG, "websocket probe supports http:// Bridge URLs only");
        return ESP_ERR_NOT_SUPPORTED;
    }

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    char port[8];
    snprintf(port, sizeof(port), "%u", endpoint.port);
    struct addrinfo *res = NULL;
    int gai = getaddrinfo(endpoint.host, port, &hints, &res);
    if (gai != 0 || res == NULL) {
        ESP_LOGW(TAG, "websocket probe DNS failed: %d", gai);
        return ESP_FAIL;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        return ESP_FAIL;
    }
    esp_err_t err = connect(sock, res->ai_addr, res->ai_addrlen) == 0 ? ESP_OK : ESP_FAIL;
    freeaddrinfo(res);
    if (err != ESP_OK) {
        close(sock);
        ESP_LOGW(TAG, "websocket probe connect failed");
        return err;
    }

    char device_id[24];
    make_device_id(device_id, sizeof(device_id));
    char auth[96] = "";
    if (strlen(config->device_token) > 0) {
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s\r\n", config->device_token);
    }
    const char *target = strlen(config->default_target) > 0 ? config->default_target : "fake";
    char request[512];
    snprintf(
        request,
        sizeof(request),
        "GET /device/ws?target=%s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "%s"
        "Protocol-Version: 1\r\n"
        "Device-Id: %s\r\n"
        "Client-Id: %s\r\n"
        "\r\n",
        target,
        endpoint.host,
        endpoint.port,
        auth,
        device_id,
        device_id
    );
    if (send(sock, request, strlen(request), 0) != (int)strlen(request)) {
        close(sock);
        return ESP_FAIL;
    }

    char response[512];
    int total = 0;
    while (total + 1 < (int)sizeof(response)) {
        int got = recv_some(sock, response + total, sizeof(response) - total - 1, 3000);
        if (got <= 0) {
            close(sock);
            return ESP_FAIL;
        }
        total += got;
        response[total] = '\0';
        if (strstr(response, "\r\n\r\n") != NULL) {
            break;
        }
    }
    if (strstr(response, " 101 ") == NULL) {
        char status_line[96];
        size_t status_len = strcspn(response, "\r\n");
        if (status_len >= sizeof(status_line)) {
            status_len = sizeof(status_line) - 1;
        }
        memcpy(status_line, response, status_len);
        status_line[status_len] = '\0';
        close(sock);
        ESP_LOGW(TAG, "websocket probe upgrade failed: %s", status_line);
        return ESP_FAIL;
    }

    char hello[192];
    snprintf(
        hello,
        sizeof(hello),
        "{\"type\":\"hello\",\"version\":1,\"features\":{\"mcp\":true},"
        "\"transport\":\"websocket\",\"audio_params\":{\"format\":\"opus\","
        "\"sample_rate\":16000,\"channels\":1,\"frame_duration\":%d}}",
        use_vb_audio ? 20 : 60
    );
    err = websocket_send_masked_text(sock, hello);
    if (err == ESP_OK) {
        char message[384];
        err = recv_websocket_text(sock, message, sizeof(message));
        if (err == ESP_OK &&
            json_contains_string(message, "type", "hello") &&
            json_contains_string(message, "transport", "websocket")) {
            ESP_LOGI(TAG, "websocket hello complete");
        } else {
            err = ESP_FAIL;
        }
    }
    if (err == ESP_OK && send_talk_probe) {
        const char listen_start[] = "{\"session_id\":\"\",\"type\":\"listen\",\"state\":\"start\",\"mode\":\"manual\"}";
        const char listen_stop[] = "{\"session_id\":\"\",\"type\":\"listen\",\"state\":\"stop\"}";
        err = websocket_send_masked_text(sock, listen_start);
        if (err == ESP_OK && use_vb_audio) {
            size_t sent_bytes = 0;
            err = websocket_send_vb6824_audio(sock, XOB_VB_TALK_FRAMES, &sent_bytes);
        } else if (err == ESP_OK) {
            static const uint8_t audio[160] = {0};
            err = websocket_send_masked_frame(sock, 2, audio, sizeof(audio));
        }
        if (err == ESP_OK) {
            err = websocket_send_masked_text(sock, listen_stop);
        }
        if (err == ESP_OK) {
            char message[384];
            err = recv_websocket_text(sock, message, sizeof(message));
            if (err == ESP_OK && json_contains_string(message, "type", "stt")) {
                ESP_LOGI(TAG, "websocket stt received");
            } else {
                err = ESP_FAIL;
            }
            for (int i = 0; err == ESP_OK && i < 3; i++) {
                err = recv_websocket_text(sock, message, sizeof(message));
                if (err != ESP_OK || !json_contains_string(message, "type", "tts")) {
                    err = ESP_FAIL;
                }
            }
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "websocket talk probe complete");
            }
        }
    }
    close(sock);
    return err;
}

static esp_err_t ensure_usb_command_serial(void) {
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 256,
        .rx_buffer_size = 256,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

static void serial_command_task(void *arg) {
    const app_config_t *config = (const app_config_t *)arg;
    esp_err_t err = ensure_usb_command_serial();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "serial command input unavailable: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    puts("XOB serial text command ready. Type text and press Enter.");
    char line[181];
    size_t len = 0;
    while (true) {
        char ch;
        int read = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(500));
        if (read <= 0) {
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (len == 0) {
                continue;
            }
            line[len] = '\0';
            if (strcmp(line, ":config") == 0 || strcmp(line, ":setup") == 0) {
                enter_button_provisioning();
                continue;
            }
            if (strcmp(line, ":status") == 0) {
                log_safe_status(config);
                len = 0;
                continue;
            }
            if (strcmp(line, ":vb") == 0 || strcmp(line, ":vb6824") == 0) {
                set_avatar_state(XOB_EYES_LISTENING, avatar_wifi_status, avatar_bridge_status);
                err = run_vb6824_uart_probe();
                set_avatar_state(
                    err == ESP_OK ? XOB_EYES_IDLE : XOB_EYES_ERROR,
                    avatar_wifi_status,
                    avatar_bridge_status
                );
                len = 0;
                continue;
            }
            if (strcmp(line, ":voice") == 0 || strcmp(line, ":audio") == 0) {
                set_avatar_state(XOB_EYES_LISTENING, avatar_wifi_status, avatar_bridge_status);
                err = post_device_audio_probe(config);
                set_avatar_state(
                    err == ESP_OK ? XOB_EYES_SPEAKING : XOB_EYES_ERROR,
                    avatar_wifi_status,
                    err == ESP_OK ? XOB_SCREEN_STATUS_OK : XOB_SCREEN_STATUS_ERROR
                );
                if (err == ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(1200));
                    set_avatar_state(XOB_EYES_IDLE, avatar_wifi_status, avatar_bridge_status);
                }
                len = 0;
                continue;
            }
            if (strcmp(line, ":ws") == 0) {
                set_avatar_state(XOB_EYES_THINKING, avatar_wifi_status, avatar_bridge_status);
                err = probe_xiaozhi_websocket(config, false, false);
                set_avatar_state(
                    err == ESP_OK ? XOB_EYES_SPEAKING : XOB_EYES_ERROR,
                    avatar_wifi_status,
                    err == ESP_OK ? XOB_SCREEN_STATUS_OK : XOB_SCREEN_STATUS_ERROR
                );
                if (err == ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(1200));
                    set_avatar_state(XOB_EYES_IDLE, avatar_wifi_status, avatar_bridge_status);
                }
                len = 0;
                continue;
            }
            if (strcmp(line, ":talk") == 0) {
                set_avatar_state(XOB_EYES_LISTENING, avatar_wifi_status, avatar_bridge_status);
                err = probe_xiaozhi_websocket(config, true, false);
                set_avatar_state(
                    err == ESP_OK ? XOB_EYES_SPEAKING : XOB_EYES_ERROR,
                    avatar_wifi_status,
                    err == ESP_OK ? XOB_SCREEN_STATUS_OK : XOB_SCREEN_STATUS_ERROR
                );
                if (err == ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(1200));
                    set_avatar_state(XOB_EYES_IDLE, avatar_wifi_status, avatar_bridge_status);
                }
                len = 0;
                continue;
            }
            if (strcmp(line, ":vb-talk") == 0) {
                set_avatar_state(XOB_EYES_LISTENING, avatar_wifi_status, avatar_bridge_status);
                err = probe_xiaozhi_websocket(config, true, true);
                set_avatar_state(
                    err == ESP_OK ? XOB_EYES_SPEAKING : XOB_EYES_ERROR,
                    avatar_wifi_status,
                    err == ESP_OK ? XOB_SCREEN_STATUS_OK : XOB_SCREEN_STATUS_ERROR
                );
                if (err == ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(1200));
                    set_avatar_state(XOB_EYES_IDLE, avatar_wifi_status, avatar_bridge_status);
                }
                len = 0;
                continue;
            }
            set_avatar_state(XOB_EYES_THINKING, avatar_wifi_status, avatar_bridge_status);
            err = post_device_command(config, line);
            set_avatar_state(
                err == ESP_OK ? XOB_EYES_SPEAKING : XOB_EYES_ERROR,
                avatar_wifi_status,
                err == ESP_OK ? XOB_SCREEN_STATUS_OK : XOB_SCREEN_STATUS_ERROR
            );
            if (err == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(1200));
                set_avatar_state(XOB_EYES_IDLE, avatar_wifi_status, avatar_bridge_status);
            }
            len = 0;
            continue;
        }
        if (len + 1 < sizeof(line)) {
            line[len++] = ch;
        }
    }
}

static void start_serial_command_task(const app_config_t *config) {
    BaseType_t created = xTaskCreate(serial_command_task, "xob_serial_cmd", 8192, (void *)config, 2, NULL);
    if (created != pdPASS) {
        ESP_LOGW(TAG, "serial command task not started");
    }
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

static esp_err_t init_buttons(void) {
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << XOB_BUTTON_VOLUME_DOWN_GPIO) |
                        (1ULL << XOB_BUTTON_LISTEN_GPIO) |
                        (1ULL << XOB_BUTTON_VOLUME_UP_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    buttons_ready = err == ESP_OK;
    return err;
}

static uint8_t button_mask(void) {
    if (!buttons_ready) {
        return 0;
    }

    uint8_t mask = 0;
    if (gpio_get_level(XOB_BUTTON_VOLUME_DOWN_GPIO) == 0) {
        mask |= XOB_BUTTON_VOLUME_DOWN;
    }
    if (gpio_get_level(XOB_BUTTON_LISTEN_GPIO) == 0) {
        mask |= XOB_BUTTON_LISTEN;
    }
    if (gpio_get_level(XOB_BUTTON_VOLUME_UP_GPIO) == 0) {
        mask |= XOB_BUTTON_VOLUME_UP;
    }
    return mask;
}

static void enter_button_provisioning(void) {
    ESP_LOGW(TAG, "provisioning requested");
    set_avatar_state(XOB_EYES_LISTENING, XOB_SCREEN_STATUS_PENDING, XOB_SCREEN_STATUS_PENDING);
    xob_start_ap_provisioning();
    xob_run_serial_provisioning();
}

static void button_task(void *arg) {
    (void)arg;
    uint8_t last = button_mask();
    int64_t config_chord_since = 0;

    while (true) {
        uint8_t mask = button_mask();
        uint8_t pressed = mask & (uint8_t)~last;
        int64_t now = esp_timer_get_time();

        if (mask != last) {
            ESP_LOGI(TAG, "button mask=0x%02x", mask);
        }
        if ((mask & XOB_BUTTON_CONFIG_CHORD) == XOB_BUTTON_CONFIG_CHORD && (mask & XOB_BUTTON_LISTEN) == 0) {
            if (config_chord_since == 0) {
                config_chord_since = now;
            } else if (now - config_chord_since >= 2000000) {
                enter_button_provisioning();
            }
        } else {
            config_chord_since = 0;
            if ((pressed & XOB_BUTTON_VOLUME_DOWN) != 0 && local_volume > 0) {
                local_volume -= 5;
                ESP_LOGI(TAG, "volume=%d", local_volume);
            }
            if ((pressed & XOB_BUTTON_VOLUME_UP) != 0 && local_volume < 100) {
                local_volume += 5;
                ESP_LOGI(TAG, "volume=%d", local_volume);
            }
            if ((pressed & XOB_BUTTON_LISTEN) != 0) {
                ESP_LOGI(TAG, "interrupt/listen button pressed");
                set_avatar_state(XOB_EYES_LISTENING, avatar_wifi_status, avatar_bridge_status);
            }
        }

        last = mask;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void start_button_task(void) {
    if (!buttons_ready || button_task_started) {
        return;
    }
    BaseType_t created = xTaskCreate(button_task, "xob_buttons", 3072, NULL, 3, NULL);
    if (created == pdPASS) {
        button_task_started = true;
    } else {
        ESP_LOGW(TAG, "button task not started");
    }
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
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);

    start_avatar_screen();
    err = init_buttons();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "button init skipped: %s", esp_err_to_name(err));
    } else if ((button_mask() & XOB_BUTTON_ALL) == XOB_BUTTON_ALL) {
        enter_button_provisioning();
        return;
    } else {
        start_button_task();
    }

    memset(&active_config, 0, sizeof(active_config));
    if (load_config(&active_config) != ESP_OK) {
        set_avatar_state(XOB_EYES_LISTENING, XOB_SCREEN_STATUS_PENDING, XOB_SCREEN_STATUS_OFF);
        xob_start_ap_provisioning();
        xob_run_serial_provisioning();
        return;
    }
    set_avatar_state(XOB_EYES_THINKING, XOB_SCREEN_STATUS_PENDING, XOB_SCREEN_STATUS_OFF);
    if (connect_wifi(&active_config) != ESP_OK) {
        set_avatar_state(XOB_EYES_LISTENING, XOB_SCREEN_STATUS_ERROR, XOB_SCREEN_STATUS_OFF);
        xob_start_ap_provisioning();
        xob_run_serial_provisioning();
        return;
    }
    set_avatar_state(XOB_EYES_THINKING, XOB_SCREEN_STATUS_OK, XOB_SCREEN_STATUS_PENDING);
    start_serial_command_task(&active_config);

    ESP_LOGI(TAG, "XOB firmware skeleton ready");
    ESP_LOGI(TAG, "bridge_url=%s", strlen(active_config.bridge_url) > 0 ? "configured" : "empty");
    ESP_LOGI(TAG, "device_token=%s", strlen(active_config.device_token) > 0 ? "configured" : "empty");
    ESP_LOGI(TAG, "default_target=%s", strlen(active_config.default_target) > 0 ? active_config.default_target : "fake");
    ESP_LOGI(TAG, "wifi_ssid=%s", strlen(active_config.wifi_ssid) > 0 ? "configured" : "empty");
    err = post_device_hello(&active_config);
    if (err != ESP_OK) {
        set_avatar_state(XOB_EYES_IDLE, XOB_SCREEN_STATUS_OK, XOB_SCREEN_STATUS_ERROR);
        return;
    }
    set_avatar_state(XOB_EYES_IDLE, XOB_SCREEN_STATUS_OK, XOB_SCREEN_STATUS_OK);
    ESP_LOGI(TAG, "Bridge hello complete");
}
