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
#include "esp_intr_alloc.h"
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
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ping/ping_sock.h"
#include "lwip/ip_addr.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

#include "eyes.h"
#include "lcd.h"
#include "provisioning.h"
#include "screen.h"
#include "vb_ota.h"
#include "xob_opus.h"

static const char *TAG = "xob";
typedef enum {
    XOB_VOICE_IDLE = 0,
    XOB_VOICE_LISTENING,
    XOB_VOICE_THINKING,
    XOB_VOICE_SPEAKING,
} xob_voice_state_t;

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
static bool vb_wake_task_started;
static SemaphoreHandle_t vb_uart_mutex;
static QueueHandle_t vb_uart_event_queue;
static RingbufHandle_t vb6824_input_ring;
static QueueHandle_t vb6824_event_queue;
static TaskHandle_t vb6824_uart_task_handle;
static bool vb6824_uart_ready;
static volatile bool vb6824_ota_requested;
static volatile bool vb6824_ota_active;
static volatile bool vb6824_wake_refresh_requested;
static volatile bool vb6824_input_enabled;
static volatile bool vb6824_output_enabled;
static volatile bool vb6824_audio_capture_active;
static volatile int64_t vb6824_audio_capture_started_us;
static volatile bool vb6824_voice_session_active;
static volatile bool vb6824_voice_stop_requested;
static volatile bool vb6824_voice_abort_requested;
static volatile xob_voice_state_t vb6824_voice_state = XOB_VOICE_IDLE;
static volatile int vb6824_active_sock = -1;
static volatile bool avatar_refresh_paused;
static RingbufHandle_t vb6824_playback_ring;
static TaskHandle_t vb6824_playback_task_handle;
static volatile size_t vb6824_playback_enqueued_bytes;
static volatile size_t vb6824_playback_played_bytes;
static volatile esp_err_t vb6824_playback_error = ESP_OK;
static volatile int64_t vb6824_playback_last_enqueue_us;
static volatile int64_t vb6824_playback_started_us;
static volatile int64_t vb6824_playback_send_us;
static volatile uint32_t vb6824_playback_frame_count;
static volatile uint32_t vb6824_playback_slow_frames;
static volatile bool vb6824_playback_started;
static volatile bool vb6824_playback_session_active;
static xob_opus_decoder_t *tts_opus_decoder;
static xob_opus_decoder_t *vb6824_vad_decoder;
static int64_t vb6824_ota_last_request_us;
static char vb6824_ota_code[32];
static int local_volume = 70;
static char avatar_status_text[24] = "BOOT";
static char avatar_input_text[160] = "";
static char avatar_output_text[320] = "";

#define XOB_BUTTON_VOLUME_DOWN_GPIO GPIO_NUM_7
#define XOB_BUTTON_VOLUME_UP_GPIO GPIO_NUM_8
#define XOB_BUTTON_LISTEN_GPIO GPIO_NUM_9
#define XOB_BUTTON_VOLUME_DOWN BIT0
#define XOB_BUTTON_LISTEN BIT1
#define XOB_BUTTON_VOLUME_UP BIT2
#define XOB_BUTTON_ALL (XOB_BUTTON_VOLUME_DOWN | XOB_BUTTON_LISTEN | XOB_BUTTON_VOLUME_UP)
#define XOB_BUTTON_CONFIG_CHORD (XOB_BUTTON_VOLUME_DOWN | XOB_BUTTON_VOLUME_UP)
#define XOB_VB_UART_NUM UART_NUM_1
#define XOB_VB_UART_BAUD 2000000
#define XOB_VB_UART_TX_BUFFER_BYTES 4096
#define XOB_VB_UART_RX_BUFFER_BYTES 4096
#define XOB_VB_UART_QUEUE_SIZE 16
#define XOB_VB_TX_GPIO GPIO_NUM_20
#define XOB_VB_RX_GPIO GPIO_NUM_10
#define XOB_VB_INPUT_FRAME_BYTES 40
#define XOB_VB_INPUT_QUEUE_BYTES (XOB_VB_INPUT_FRAME_BYTES * 500)
#define XOB_VB_EVENT_QUEUE_LENGTH 8
#define XOB_VB_EVENT_TEXT_BYTES 128
#define XOB_VB_TALK_PROBE_FRAMES 150
#define XOB_VB_TALK_AUTO_MAX_FRAMES 6000
#define XOB_VB_AUTO_MIN_FRAMES 15
#define XOB_VB_NO_AUDIO_TIMEOUT_MS 5000
#define XOB_VB_VAD_SPEECH_LEVEL 260
#define XOB_VB_VAD_MIN_SPEECH_FRAMES 20
#define XOB_VB_VAD_END_SILENCE_FRAMES 75
#define XOB_VB_VAD_END_MAX_SPEECH_FRAMES 3
#define XOB_VB_VAD_NO_SPEECH_FRAMES 6000
#define XOB_VB_PLAY_PCM_FRAME_BYTES 320
#define XOB_VB_PLAY_FRAME_DELAY_MS 10
#define XOB_VB_PLAY_QUEUE_BYTES (12 * 1024)
#define XOB_VB_PLAY_ENQUEUE_TIMEOUT_MS 5000
#define XOB_VB_PLAY_PREROLL_BYTES 1920
#define XOB_VB_PLAY_PREROLL_MAX_WAIT_MS 80
#define XOB_VB_PLAY_START_WHEN_FREE_BELOW_BYTES (8 * 1024)
#define XOB_VB_PLAY_START_SILENCE_FRAMES 4
#define XOB_VB_PLAY_TASK_STACK_BYTES 3072
#define XOB_VB_PLAY_TASK_PRIORITY 9
#define XOB_WS_RECV_TIMEOUT_MS 90000
#define XOB_WS_MESSAGE_BUFFER_BYTES 16384
#define XOB_WS_MESSAGE_FALLBACK_BUFFER_BYTES 4096
#define XOB_WS_TTS_MAX_FRAMES 2048
#define XOB_BUTTON_LISTEN_COOLDOWN_MS 250
#define XOB_OPUS_SAMPLE_RATE 16000
#define XOB_OPUS_CHANNELS 1
#define XOB_OPUS_MAX_FRAME_MS 60
#define XOB_OPUS_MAX_SAMPLES ((XOB_OPUS_SAMPLE_RATE * XOB_OPUS_MAX_FRAME_MS) / 1000)

static int16_t tts_opus_pcm[XOB_OPUS_MAX_SAMPLES];
static int16_t vb6824_vad_pcm[XOB_OPUS_MAX_SAMPLES];

typedef struct {
    xob_eyes_frame_t eyes;
    xob_screen_status_t wifi_status;
    xob_screen_status_t bridge_status;
    char status_text[24];
    char input_text[160];
    char output_text[320];
    uint16_t text_scroll_step;
} avatar_frame_t;

static avatar_frame_t last_avatar_frame;
static bool has_last_avatar_frame;

typedef struct {
    char bridge_url[128];
    char device_token[64];
    char default_target[16];
    char wifi_ssid[33];
    char wifi_password[65];
    char wifi_ssid_prev[33];
    char wifi_password_prev[65];
} app_config_t;

typedef struct {
    char host[96];
    uint16_t port;
} bridge_endpoint_t;

typedef struct {
    uint16_t cmd;
    uint16_t len;
    uint8_t data[XOB_VB_EVENT_TEXT_BYTES];
} vb6824_event_msg_t;

static void vb6824_uart_task(void *arg);
static void vb6824_ota_event(jl_ota_evt_id evt, uint32_t data);

static app_config_t active_config;

typedef struct {
    const app_config_t *config;
    const char *source;
} vb6824_voice_session_arg_t;

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
static void set_avatar_dialog(const char *status, const char *input, const char *output);
static void show_tts_speaking_once(bool *paused);
static void enter_button_provisioning(void);
static esp_err_t set_default_target(const char *target);
static void start_vb6824_wake_task(const app_config_t *config);
static void run_vb6824_voice_session(const app_config_t *config, const char *source);
static esp_err_t dispatch_vb6824_voice_session(const app_config_t *config, const char *source);

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
    read_string(nvs, "wifi_ssid_prev", config->wifi_ssid_prev, sizeof(config->wifi_ssid_prev));
    read_string(nvs, "wifi_pass_prev", config->wifi_password_prev, sizeof(config->wifi_password_prev));
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
    ESP_LOGI(TAG, "status: volume=%d", local_volume);
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

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    const char *ssids[] = {config->wifi_ssid, config->wifi_ssid_prev};
    const char *passwords[] = {config->wifi_password, config->wifi_password_prev};
    for (size_t i = 0; i < 2; i++) {
        if (strlen(ssids[i]) == 0 || (i == 1 && strcmp(ssids[0], ssids[1]) == 0)) {
            continue;
        }
        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, ssids[i], sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, passwords[i], sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        wifi_config.sta.threshold.rssi = -127;
        wifi_config.sta.threshold.authmode = strlen(passwords[i]) > 0 ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
        wifi_config.sta.failure_retry_cnt = 3;

        wifi_retry_count = 0;
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "wifi config");
        log_target_scan_result(ssids[i]);
        ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "wifi connect");

        EventBits_t bits = xEventGroupWaitBits(
            wifi_events,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(30000)
        );
        if ((bits & WIFI_CONNECTED_BIT) != 0) {
            ESP_LOGI(TAG, "WiFi connected%s", i == 1 ? " via fallback" : "");
            log_network_diagnostics(config);
            return ESP_OK;
        }
        wifi_retry_count = 8;
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(1000));
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
    if (len > 512) {
        ESP_LOGW(TAG, "vb6824 frame too large cmd=0x%04x len=%u", cmd, len);
        return;
    }
    uint8_t frame[519] = {0x55, 0xaa, (uint8_t)(len >> 8), (uint8_t)len, (uint8_t)(cmd >> 8), (uint8_t)cmd};
    if (len > 0 && data != NULL) {
        memcpy(frame + 6, data, len);
    }
    frame[6 + len] = vb6824_sum8(frame, 6 + len);
    uart_write_bytes(XOB_VB_UART_NUM, frame, 7 + len);
}

static esp_err_t vb6824_send_frame_nonblocking(uint16_t cmd, const uint8_t *data, uint16_t len, int timeout_ms) {
    if (len > 512) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t frame[519] = {0x55, 0xaa, (uint8_t)(len >> 8), (uint8_t)len, (uint8_t)(cmd >> 8), (uint8_t)cmd};
    if (len > 0 && data != NULL) {
        memcpy(frame + 6, data, len);
    }
    frame[6 + len] = vb6824_sum8(frame, 6 + len);

    size_t frame_len = 7 + len;
    int written = uart_write_bytes(XOB_VB_UART_NUM, (const char *)frame, frame_len);
    if (written != (int)frame_len) {
        return ESP_FAIL;
    }
    return uart_wait_tx_done(XOB_VB_UART_NUM, pdMS_TO_TICKS(timeout_ms));
}

static esp_err_t ensure_vb6824_input_queue(void) {
    if (vb6824_input_ring == NULL) {
        vb6824_input_ring = xRingbufferCreate(XOB_VB_INPUT_QUEUE_BYTES, RINGBUF_TYPE_BYTEBUF);
        if (vb6824_input_ring == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static esp_err_t ensure_vb6824_event_queue(void) {
    if (vb6824_event_queue == NULL) {
        vb6824_event_queue = xQueueCreate(XOB_VB_EVENT_QUEUE_LENGTH, sizeof(vb6824_event_msg_t));
        if (vb6824_event_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static void vb6824_drain_input_queue(void) {
    if (vb6824_input_ring == NULL) {
        return;
    }
    while (true) {
        size_t item_len = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceive(vb6824_input_ring, &item_len, 0);
        if (item == NULL) {
            break;
        }
        vRingbufferReturnItem(vb6824_input_ring, item);
    }
}

static void vb6824_queue_event(uint16_t cmd, const uint8_t *body, uint16_t body_len) {
    if (vb6824_event_queue == NULL) {
        return;
    }
    vb6824_event_msg_t event = {
        .cmd = cmd,
        .len = body_len < XOB_VB_EVENT_TEXT_BYTES - 1 ? body_len : XOB_VB_EVENT_TEXT_BYTES - 1,
    };
    if (event.len > 0 && body != NULL) {
        memcpy(event.data, body, event.len);
    }
    event.data[event.len] = '\0';
    (void)xQueueSend(vb6824_event_queue, &event, 0);
}

static void vb6824_queue_input_audio(const uint8_t *body, uint16_t body_len) {
    if (!vb6824_input_enabled || vb6824_input_ring == NULL || body == NULL || body_len == 0) {
        return;
    }
    while (xRingbufferGetCurFreeSize(vb6824_input_ring) < body_len) {
        size_t item_len = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceive(vb6824_input_ring, &item_len, 0);
        if (item == NULL) {
            break;
        }
        vRingbufferReturnItem(vb6824_input_ring, item);
    }
    (void)xRingbufferSend(vb6824_input_ring, body, body_len, 0);
}

static void vb6824_handle_frame(uint16_t cmd, const uint8_t *body, uint16_t body_len) {
    switch (cmd) {
    case 0x2080:
        vb6824_queue_input_audio(body, body_len);
        break;
    case 0x0105:
        if (vb6824_ota_requested && !vb6824_ota_active) {
            vb6824_ota_active = true;
            int ret = jl_ota_start(vb6824_ota_event);
            ESP_LOGI(TAG, "vb6824 ota start accepted ret=%d", ret);
        }
        break;
    case 0x0180:
    case 0x0280:
        vb6824_queue_event(cmd, body, body_len);
        break;
    default:
        break;
    }
}

static void vb6824_parse_uart_bytes(uint8_t *pending, size_t *pending_len, const uint8_t *data, size_t len) {
    if (*pending_len + len > 1024) {
        *pending_len = 0;
    }
    memcpy(pending + *pending_len, data, len);
    *pending_len += len;

    size_t pos = 0;
    while (pos + 7 <= *pending_len) {
        if (pending[pos] != 0x55 || pending[pos + 1] != 0xaa) {
            pos++;
            continue;
        }
        uint16_t body_len = ((uint16_t)pending[pos + 2] << 8) | pending[pos + 3];
        if (body_len > 512) {
            pos++;
            continue;
        }
        size_t frame_len = 7 + body_len;
        if (pos + frame_len > *pending_len) {
            break;
        }
        if (vb6824_sum8(pending + pos, frame_len - 1) != pending[pos + frame_len - 1]) {
            pos++;
            continue;
        }
        uint16_t cmd = ((uint16_t)pending[pos + 4] << 8) | pending[pos + 5];
        vb6824_handle_frame(cmd, pending + pos + 6, body_len);
        pos += frame_len;
    }
    if (pos > 0) {
        memmove(pending, pending + pos, *pending_len - pos);
        *pending_len -= pos;
    }
}

static void vb6824_uart_task(void *arg) {
    (void)arg;
    uart_event_t event;
    uint8_t buf[512];
    uint8_t pending[1024];
    size_t pending_len = 0;
    while (true) {
        if (xQueueReceive(vb_uart_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (event.type == UART_DATA) {
            while (true) {
                int got = uart_read_bytes(XOB_VB_UART_NUM, buf, sizeof(buf), 0);
                if (got <= 0) {
                    break;
                }
                if (vb6824_ota_active) {
                    jl_ondata(buf, (uint16_t)got);
                }
                vb6824_parse_uart_bytes(pending, &pending_len, buf, (size_t)got);
            }
        } else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
            ESP_LOGW(TAG, "vb6824 uart overflow event=%d", event.type);
            uart_flush_input(XOB_VB_UART_NUM);
            xQueueReset(vb_uart_event_queue);
            pending_len = 0;
        }
    }
}

static void vb6824_audio_enable_input(bool enable) {
    if (enable == vb6824_input_enabled) {
        return;
    }
    if (enable) {
        vb6824_drain_input_queue();
    }
    vb6824_input_enabled = enable;
    if (!enable) {
        vb6824_drain_input_queue();
    }
}

static esp_err_t vb6824_start_voice_input(void) {
    bool input_was_enabled = vb6824_input_enabled;
    vb6824_audio_enable_input(true);
    if (input_was_enabled) {
        return ESP_OK;
    }
    uint8_t one = 1;
    if (xSemaphoreTake(vb_uart_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    vb6824_send_frame(0x0207, &one, 1);
    xSemaphoreGive(vb_uart_mutex);
    return ESP_OK;
}

static uint16_t vb6824_audio_read(uint8_t *data, uint16_t size, int timeout_ms) {
    if (!vb6824_input_enabled || vb6824_input_ring == NULL || data == NULL || size == 0) {
        return 0;
    }
    size_t item_len = 0;
    uint8_t *item = (uint8_t *)xRingbufferReceive(
        vb6824_input_ring,
        &item_len,
        pdMS_TO_TICKS(timeout_ms)
    );
    if (item == NULL) {
        return 0;
    }
    if (item_len > size) {
        item_len = 0;
    } else {
        memcpy(data, item, item_len);
    }
    vRingbufferReturnItem(vb6824_input_ring, item);
    return (uint16_t)item_len;
}

static esp_err_t ensure_vb6824_playback_queue(void);

static esp_err_t ensure_vb6824_uart(void) {
    if (vb_uart_mutex == NULL) {
        vb_uart_mutex = xSemaphoreCreateMutex();
        if (vb_uart_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_RETURN_ON_ERROR(ensure_vb6824_input_queue(), TAG, "vb6824 input queue");
    ESP_RETURN_ON_ERROR(ensure_vb6824_event_queue(), TAG, "vb6824 event queue");
    if (xSemaphoreTake(vb_uart_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    if (vb6824_uart_ready) {
        xSemaphoreGive(vb_uart_mutex);
        return ESP_OK;
    }

    const uart_config_t uart_config = {
        .baud_rate = XOB_VB_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;
#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif
    esp_err_t err = uart_driver_install(
        XOB_VB_UART_NUM,
        XOB_VB_UART_RX_BUFFER_BYTES,
        XOB_VB_UART_TX_BUFFER_BYTES,
        XOB_VB_UART_QUEUE_SIZE,
        &vb_uart_event_queue,
        intr_alloc_flags
    );
    if (err == ESP_OK) {
        err = uart_param_config(XOB_VB_UART_NUM, &uart_config);
    }
    if (err == ESP_OK) {
        err = uart_set_pin(XOB_VB_UART_NUM, XOB_VB_TX_GPIO, XOB_VB_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK && vb6824_uart_task_handle == NULL) {
        BaseType_t created = xTaskCreate(vb6824_uart_task, "xob_vb_uart", 4096, NULL, 9, &vb6824_uart_task_handle);
        if (created != pdPASS) {
            vb6824_uart_task_handle = NULL;
            err = ESP_ERR_NO_MEM;
        }
    }
    if (err == ESP_OK) {
        vb6824_uart_ready = true;
        uint32_t actual_baud = 0;
        (void)uart_get_baudrate(XOB_VB_UART_NUM, &actual_baud);
        ESP_LOGI(TAG, "vb6824 uart ready baud=%lu rxbuf=%d txbuf=%d",
                 (unsigned long)actual_baud,
                 XOB_VB_UART_RX_BUFFER_BYTES,
                 XOB_VB_UART_TX_BUFFER_BYTES);
        esp_err_t play_err = ensure_vb6824_playback_queue();
        if (play_err == ESP_OK) {
            ESP_LOGI(TAG, "vb6824 playback queue ready bytes=%d", XOB_VB_PLAY_QUEUE_BYTES);
        } else {
            ESP_LOGW(TAG, "vb6824 playback queue prealloc failed: %s", esp_err_to_name(play_err));
        }
    } else {
        if (vb6824_uart_task_handle != NULL) {
            vTaskDelete(vb6824_uart_task_handle);
            vb6824_uart_task_handle = NULL;
        }
        uart_driver_delete(XOB_VB_UART_NUM);
    }
    xSemaphoreGive(vb_uart_mutex);
    return err;
}

static void vb6824_ota_event(jl_ota_evt_id evt, uint32_t data) {
    switch (evt) {
    case JL_OTA_START:
        vb6824_ota_active = true;
        ESP_LOGI(TAG, "vb6824 ota start");
        break;
    case JL_OTA_STOP:
        vb6824_ota_active = false;
        ESP_LOGI(TAG, "vb6824 ota stop");
        break;
    case JL_OTA_PROCESS:
        ESP_LOGI(TAG, "vb6824 ota progress=%u", (unsigned)data);
        break;
    case JL_OTA_SUCCESS:
        vb6824_ota_active = false;
        vb6824_ota_requested = false;
        vb6824_wake_refresh_requested = true;
        ESP_LOGI(TAG, "vb6824 ota success");
        set_avatar_state(XOB_EYES_IDLE, avatar_wifi_status, avatar_bridge_status);
        break;
    case JL_OTA_FAIL:
        vb6824_ota_active = false;
        vb6824_ota_requested = false;
        ESP_LOGW(TAG, "vb6824 ota failed");
        set_avatar_state(XOB_EYES_ERROR, avatar_wifi_status, avatar_bridge_status);
        break;
    case JL_OTA_RETRY:
        ESP_LOGW(TAG, "vb6824 ota retry");
        break;
    case JL_OTA_REGET_WAKE:
        vb6824_wake_refresh_requested = true;
        ESP_LOGI(TAG, "vb6824 ota refresh wake word");
        break;
    default:
        ESP_LOGI(TAG, "vb6824 ota event=%d data=%u", evt, (unsigned)data);
        break;
    }
}

static esp_err_t vb6824_send_ota_start_request(void) {
    if (!vb6824_ota_requested || vb6824_ota_active) {
        return ESP_OK;
    }
    if (xSemaphoreTake(vb_uart_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t one = 1;
    vb6824_send_frame(0x0205, &one, 1);
    vb6824_ota_last_request_us = esp_timer_get_time();
    xSemaphoreGive(vb_uart_mutex);
    ESP_LOGI(TAG, "vb6824 ota enter request sent");
    return ESP_OK;
}

static esp_err_t start_vb6824_ota_code(const char *code) {
    if (code == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    while (*code == ' ') {
        code++;
    }
    size_t code_len = strnlen(code, sizeof(vb6824_ota_code));
    if (code_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (code_len >= sizeof(vb6824_ota_code)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (vb6824_ota_active) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ensure_vb6824_uart();
    if (err != ESP_OK) {
        return err;
    }
    memset(vb6824_ota_code, 0, sizeof(vb6824_ota_code));
    strlcpy(vb6824_ota_code, code, sizeof(vb6824_ota_code));
    jl_set_uart_port((uint8_t)XOB_VB_UART_NUM);
    int ret = jl_ota_set_code(vb6824_ota_code);
    vb6824_ota_requested = true;
    vb6824_ota_active = false;
    vb6824_ota_last_request_us = 0;
    ESP_LOGI(TAG, "vb6824 ota code configured len=%u ret=%d", (unsigned)code_len, ret);
    set_avatar_state(XOB_EYES_THINKING, avatar_wifi_status, avatar_bridge_status);
    return vb6824_send_ota_start_request();
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
    esp_err_t err = uart_driver_install(XOB_VB_UART_NUM, 4096, XOB_VB_UART_TX_BUFFER_BYTES, 0, NULL, 0);
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

static void set_socket_recv_timeout(int sock, int timeout_ms) {
    struct timeval timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

static int recv_some(int sock, char *buffer, size_t buffer_len, int timeout_ms) {
    set_socket_recv_timeout(sock, timeout_ms);
    return recv(sock, buffer, buffer_len, 0);
}

static esp_err_t recv_exact(int sock, void *buffer, size_t len) {
    set_socket_recv_timeout(sock, XOB_WS_RECV_TIMEOUT_MS);
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

static bool vb6824_vad_is_speech(const uint8_t *payload, uint16_t payload_len, uint32_t *level) {
    if (level != NULL) {
        *level = 0;
    }
    if (payload == NULL || payload_len == 0) {
        return false;
    }
    if (vb6824_vad_decoder == NULL &&
        xob_opus_decoder_create(XOB_OPUS_SAMPLE_RATE, XOB_OPUS_CHANNELS, &vb6824_vad_decoder) != ESP_OK) {
        return false;
    }

    size_t pcm_samples = 0;
    if (xob_opus_decode(
            vb6824_vad_decoder,
            payload,
            payload_len,
            vb6824_vad_pcm,
            XOB_OPUS_MAX_SAMPLES,
            &pcm_samples
        ) != ESP_OK ||
        pcm_samples == 0) {
        return false;
    }

    uint64_t total = 0;
    for (size_t i = 0; i < pcm_samples; i++) {
        int32_t sample = vb6824_vad_pcm[i];
        total += (uint32_t)(sample < 0 ? -sample : sample);
    }
    uint32_t mean = (uint32_t)(total / pcm_samples);
    if (level != NULL) {
        *level = mean;
    }
    return mean >= XOB_VB_VAD_SPEECH_LEVEL;
}

static esp_err_t websocket_send_vb6824_audio(
    int sock,
    int max_frames,
    bool auto_stop,
    size_t *sent_bytes,
    bool *heard_speech
) {
    *sent_bytes = 0;
    if (heard_speech != NULL) {
        *heard_speech = !auto_stop;
    }
    if (vb6824_ota_requested || vb6824_ota_active) {
        ESP_LOGW(TAG, "vb6824 audio blocked during ota");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ensure_vb6824_uart();
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "vb6824 websocket audio start tx=%d rx=%d max_frames=%d auto=%d", XOB_VB_TX_GPIO, XOB_VB_RX_GPIO, max_frames, auto_stop);
    err = vb6824_start_voice_input();
    if (err != ESP_OK) {
        return err;
    }
    vb6824_audio_capture_active = true;
    vb6824_audio_capture_started_us = esp_timer_get_time();
    int sent_frames = 0;
    int speech_frames = 0;
    bool end_window[XOB_VB_VAD_END_SILENCE_FRAMES] = {0};
    int end_window_index = 0;
    int end_window_frames = 0;
    int end_window_speech = 0;
    uint32_t peak_level = 0;
    bool no_speech_timeout = false;
    int64_t start_us = esp_timer_get_time();
    int64_t end_us = start_us + ((int64_t)max_frames * 20000) + 1500000;
    if (vb6824_vad_decoder != NULL) {
        (void)xob_opus_decoder_reset(vb6824_vad_decoder);
    }
    while (sent_frames < max_frames && esp_timer_get_time() < end_us) {
        int64_t now_us = esp_timer_get_time();
        if (vb6824_voice_abort_requested) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        if (vb6824_voice_stop_requested) {
            ESP_LOGI(TAG, "vb6824 websocket audio stop requested frames=%d", sent_frames);
            break;
        }
        if (sent_frames == 0 && now_us - start_us >= (int64_t)XOB_VB_NO_AUDIO_TIMEOUT_MS * 1000) {
            ESP_LOGW(TAG, "vb6824 websocket audio no frames");
            break;
        }
        uint8_t buf[64];
        uint16_t got = vb6824_audio_read(buf, sizeof(buf), 50);
        if (got <= 0) {
            continue;
        }
        err = websocket_send_masked_frame(sock, 2, buf, got);
        if (err != ESP_OK) {
            break;
        }
        *sent_bytes += got;
        sent_frames++;
        if (auto_stop) {
            uint32_t level = 0;
            bool is_speech = vb6824_vad_is_speech(buf, got, &level);
            if (is_speech) {
                speech_frames++;
                if (level > peak_level) {
                    peak_level = level;
                }
            }
            if (speech_frames >= XOB_VB_VAD_MIN_SPEECH_FRAMES) {
                if (end_window_frames >= XOB_VB_VAD_END_SILENCE_FRAMES && end_window[end_window_index]) {
                    end_window_speech--;
                }
                end_window[end_window_index] = is_speech;
                if (is_speech) {
                    end_window_speech++;
                }
                end_window_index = (end_window_index + 1) % XOB_VB_VAD_END_SILENCE_FRAMES;
                if (end_window_frames < XOB_VB_VAD_END_SILENCE_FRAMES) {
                    end_window_frames++;
                }
            }
            if (speech_frames == 0 && sent_frames >= XOB_VB_VAD_NO_SPEECH_FRAMES) {
                ESP_LOGI(TAG, "vb6824 websocket no speech frames=%d peak=%lu", sent_frames, (unsigned long)peak_level);
                no_speech_timeout = true;
                break;
            }
            if (sent_frames >= XOB_VB_AUTO_MIN_FRAMES &&
                speech_frames >= XOB_VB_VAD_MIN_SPEECH_FRAMES &&
                end_window_frames >= XOB_VB_VAD_END_SILENCE_FRAMES &&
                end_window_speech <= XOB_VB_VAD_END_MAX_SPEECH_FRAMES) {
                ESP_LOGI(TAG, "vb6824 websocket vad stop frames=%d speech=%d tail_speech=%d peak=%lu",
                         sent_frames, speech_frames, end_window_speech, (unsigned long)peak_level);
                break;
            }
        }
    }

    vb6824_audio_capture_active = false;
    vb6824_audio_capture_started_us = 0;
    vb6824_audio_enable_input(false);
    if (heard_speech != NULL) {
        *heard_speech = !auto_stop || speech_frames >= XOB_VB_VAD_MIN_SPEECH_FRAMES;
    }
    if (err == ESP_OK && auto_stop && speech_frames < XOB_VB_VAD_MIN_SPEECH_FRAMES) {
        ESP_LOGI(TAG, "vb6824 websocket listen ended without speech frames=%d peak=%lu timeout=%d",
                 sent_frames, (unsigned long)peak_level, no_speech_timeout);
        return ESP_ERR_NOT_FOUND;
    }
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

static esp_err_t websocket_send_abort(int sock) {
    static const char abort_message[] = "{\"session_id\":\"\",\"type\":\"abort\"}";
    return websocket_send_masked_text(sock, abort_message);
}

static esp_err_t discard_websocket_payload(int sock, size_t len) {
    uint8_t trash[128];
    while (len > 0) {
        size_t chunk = len < sizeof(trash) ? len : sizeof(trash);
        esp_err_t err = recv_exact(sock, trash, chunk);
        if (err != ESP_OK) {
            return err;
        }
        len -= chunk;
    }
    return ESP_OK;
}

static esp_err_t recv_websocket_frame(int sock, uint8_t *opcode, char *out, size_t out_len, size_t *payload_len) {
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
    *opcode = header[0] & 0x0f;
    *payload_len = len;
    if (len >= out_len) {
        if (out_len > 0) {
            out[0] = '\0';
        }
        return discard_websocket_payload(sock, len);
    }
    if (recv_exact(sock, out, len) != ESP_OK) {
        return ESP_FAIL;
    }
    out[len] = '\0';
    return ESP_OK;
}

static esp_err_t recv_websocket_text(int sock, char *out, size_t out_len) {
    uint8_t opcode = 0;
    size_t payload_len = 0;
    esp_err_t err = recv_websocket_frame(sock, &opcode, out, out_len, &payload_len);
    if (err != ESP_OK) {
        return err;
    }
    return opcode == 1 ? ESP_OK : ESP_FAIL;
}

static uint16_t read_le16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static esp_err_t vb6824_play_pcm(const uint8_t *pcm, size_t pcm_len, size_t *queued_bytes);
static esp_err_t play_tts_opus_frame(const uint8_t *payload, size_t payload_len, size_t *pcm_bytes, size_t *played_bytes);

static void vb6824_drain_playback_queue(void) {
    if (vb6824_playback_ring == NULL) {
        return;
    }
    while (true) {
        size_t item_len = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(
            vb6824_playback_ring,
            &item_len,
            0,
            XOB_VB_PLAY_PCM_FRAME_BYTES
        );
        if (item == NULL) {
            break;
        }
        vRingbufferReturnItem(vb6824_playback_ring, item);
    }
}

static void vb6824_audio_enable_output(bool enable) {
    if (enable == vb6824_output_enabled) {
        return;
    }
    vb6824_output_enabled = enable;
    vb6824_playback_session_active = enable;
    if (!enable) {
        vb6824_drain_playback_queue();
    }
}

static bool vb6824_audio_write(const uint8_t *data, uint16_t len, int timeout_ms) {
    if (vb6824_playback_ring == NULL || data == NULL || len == 0) {
        return false;
    }
    if (!vb6824_output_enabled &&
        xRingbufferGetCurFreeSize(vb6824_playback_ring) < XOB_VB_PLAY_START_WHEN_FREE_BELOW_BYTES) {
        vb6824_audio_enable_output(true);
    }
    return xRingbufferSend(vb6824_playback_ring, data, len, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void vb6824_reset_playback_buffer(void) {
    vb6824_audio_enable_output(false);
    vb6824_drain_playback_queue();
    vb6824_playback_enqueued_bytes = 0;
    vb6824_playback_played_bytes = 0;
    vb6824_playback_last_enqueue_us = 0;
    vb6824_playback_started_us = 0;
    vb6824_playback_send_us = 0;
    vb6824_playback_frame_count = 0;
    vb6824_playback_slow_frames = 0;
    vb6824_playback_started = false;
    vb6824_playback_error = ESP_OK;
}

static void vb6824_prepare_playback_session(void) {
    vb6824_reset_playback_buffer();
    if (tts_opus_decoder != NULL) {
        (void)xob_opus_decoder_reset(tts_opus_decoder);
    }
    vb6824_audio_enable_output(true);
}

static void vb6824_finish_playback_session(void) {
    vb6824_audio_enable_output(false);
}

static bool wav_pcm16_16k_mono_data(const uint8_t *wav, size_t len, const uint8_t **pcm, size_t *pcm_len) {
    if (len < 44 ||
        memcmp(wav, "RIFF", 4) != 0 ||
        memcmp(wav + 8, "WAVE", 4) != 0 ||
        memcmp(wav + 12, "fmt ", 4) != 0 ||
        memcmp(wav + 36, "data", 4) != 0 ||
        read_le32(wav + 16) != 16 ||
        read_le16(wav + 20) != 1 ||
        read_le16(wav + 22) != 1 ||
        read_le32(wav + 24) != 16000 ||
        read_le16(wav + 34) != 16) {
        return false;
    }
    size_t data_len = read_le32(wav + 40);
    if (data_len > len - 44) {
        return false;
    }
    *pcm = wav + 44;
    *pcm_len = data_len;
    return data_len > 0;
}

static esp_err_t vb6824_write_pcm_frame(const uint8_t *pcm, size_t pcm_len) {
    if (pcm == NULL || pcm_len == 0 || pcm_len > XOB_VB_PLAY_PCM_FRAME_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    int64_t started_us = esp_timer_get_time();
    vb6824_send_frame(0x2081, pcm, (uint16_t)pcm_len);
    int64_t elapsed_us = esp_timer_get_time() - started_us;
    vb6824_playback_send_us += elapsed_us;
    vb6824_playback_frame_count++;
    if (elapsed_us > 5000) {
        vb6824_playback_slow_frames++;
    }
    return ESP_OK;
}

static void vb6824_playback_task(void *arg) {
    (void)arg;
    TickType_t last_time = xTaskGetTickCount();
    while (true) {
        if (!vb6824_output_enabled) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        size_t item_len = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(
            vb6824_playback_ring,
            &item_len,
            portMAX_DELAY,
            XOB_VB_PLAY_PCM_FRAME_BYTES
        );
        if (item == NULL) {
            continue;
        }
        if (!vb6824_output_enabled) {
            vRingbufferReturnItem(vb6824_playback_ring, item);
            vb6824_drain_playback_queue();
            continue;
        }
        if (!vb6824_playback_started) {
            while (true) {
                if (!vb6824_output_enabled) {
                    break;
                }
                size_t buffered_bytes = vb6824_playback_enqueued_bytes - vb6824_playback_played_bytes;
                int64_t last_enqueue_us = vb6824_playback_last_enqueue_us;
                int64_t idle_us = last_enqueue_us > 0 ? esp_timer_get_time() - last_enqueue_us : 0;
                if (buffered_bytes >= XOB_VB_PLAY_PREROLL_BYTES ||
                    (buffered_bytes > 0 && idle_us >= (int64_t)XOB_VB_PLAY_PREROLL_MAX_WAIT_MS * 1000)) {
                    vb6824_playback_started = true;
                    vb6824_playback_started_us = esp_timer_get_time();
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        esp_err_t err = vb6824_output_enabled && vb6824_uart_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_time) >= pdMS_TO_TICKS(XOB_VB_PLAY_FRAME_DELAY_MS)) {
                last_time = now;
            }
            err = vb6824_write_pcm_frame(item, item_len);
        }
        if (err == ESP_OK) {
            vb6824_playback_played_bytes += item_len;
        } else {
            vb6824_playback_error = err;
            ESP_LOGW(TAG, "vb6824 playback queue failed err=%s", esp_err_to_name(err));
        }
        vRingbufferReturnItem(vb6824_playback_ring, item);
        vTaskDelayUntil(&last_time, pdMS_TO_TICKS(XOB_VB_PLAY_FRAME_DELAY_MS));
        if (!vb6824_output_enabled) {
            vb6824_drain_playback_queue();
        }
    }
}

static esp_err_t ensure_vb6824_playback_queue(void) {
    if (vb6824_playback_ring == NULL) {
        vb6824_playback_ring = xRingbufferCreate(XOB_VB_PLAY_QUEUE_BYTES, RINGBUF_TYPE_BYTEBUF);
        if (vb6824_playback_ring == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (vb6824_playback_task_handle == NULL) {
        BaseType_t created = xTaskCreate(vb6824_playback_task, "xob_vb_play", XOB_VB_PLAY_TASK_STACK_BYTES, NULL, XOB_VB_PLAY_TASK_PRIORITY, &vb6824_playback_task_handle);
        if (created != pdPASS) {
            vb6824_playback_task_handle = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static esp_err_t vb6824_wait_playback(size_t target_bytes, int timeout_ms) {
    int64_t deadline = esp_timer_get_time() + ((int64_t)timeout_ms * 1000);
    while (vb6824_playback_played_bytes < target_bytes && esp_timer_get_time() < deadline) {
        if (vb6824_voice_abort_requested || !vb6824_output_enabled) {
            return ESP_ERR_INVALID_STATE;
        }
        if (vb6824_playback_error != ESP_OK) {
            return vb6824_playback_error;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    esp_err_t err = vb6824_playback_played_bytes >= target_bytes ? ESP_OK : ESP_ERR_TIMEOUT;
    int64_t started_us = vb6824_playback_started_us;
    if (started_us > 0) {
        ESP_LOGI(TAG, "vb6824 playback elapsed_ms=%lld played=%u target=%u frames=%lu send_ms=%lld slow_frames=%lu",
                 (long long)((esp_timer_get_time() - started_us) / 1000),
                 (unsigned)vb6824_playback_played_bytes,
                 (unsigned)target_bytes,
                 (unsigned long)vb6824_playback_frame_count,
                 (long long)(vb6824_playback_send_us / 1000),
                 (unsigned long)vb6824_playback_slow_frames);
    }
    return err;
}

static esp_err_t play_tts_pcm_frame(
    const uint8_t *payload,
    size_t payload_len,
    size_t *pcm_bytes,
    size_t *played_bytes
) {
    const uint8_t *pcm = payload;
    size_t pcm_len = payload_len;
    if (payload_len >= 4 && memcmp(payload, "RIFF", 4) == 0) {
        if (!wav_pcm16_16k_mono_data(payload, payload_len, &pcm, &pcm_len)) {
            ESP_LOGW(TAG, "unsupported tts audio format bytes=%u", (unsigned)payload_len);
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    size_t played = 0;
    esp_err_t err = vb6824_play_pcm(pcm, pcm_len, &played);
    if (err != ESP_OK) {
        return err;
    }
    *pcm_bytes += pcm_len;
    *played_bytes += played;
    return ESP_OK;
}

static esp_err_t play_tts_opus_frame(
    const uint8_t *payload,
    size_t payload_len,
    size_t *pcm_bytes,
    size_t *played_bytes
) {
    if (tts_opus_decoder == NULL) {
        ESP_RETURN_ON_ERROR(
            xob_opus_decoder_create(XOB_OPUS_SAMPLE_RATE, XOB_OPUS_CHANNELS, &tts_opus_decoder),
            TAG,
            "opus decoder create"
        );
    }

    size_t pcm_samples = 0;
    esp_err_t err = xob_opus_decode(
        tts_opus_decoder,
        payload,
        payload_len,
        tts_opus_pcm,
        XOB_OPUS_MAX_SAMPLES,
        &pcm_samples
    );
    if (err != ESP_OK) {
        return err;
    }
    return play_tts_pcm_frame((const uint8_t *)tts_opus_pcm, pcm_samples * sizeof(int16_t), pcm_bytes, played_bytes);
}

static esp_err_t play_tts_audio_frame(
    const uint8_t *payload,
    size_t payload_len,
    bool prefer_opus,
    size_t *pcm_bytes,
    size_t *played_bytes
) {
    if (payload_len >= 4 && memcmp(payload, "RIFF", 4) == 0) {
        return play_tts_pcm_frame(payload, payload_len, pcm_bytes, played_bytes);
    }
    if (prefer_opus) {
        esp_err_t err = play_tts_opus_frame(payload, payload_len, pcm_bytes, played_bytes);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        if ((payload_len % 2) != 0 || payload_len < 1000) {
            return err;
        }
        ESP_LOGW(TAG, "opus decode failed; falling back to pcm bytes=%u", (unsigned)payload_len);
    }
    return play_tts_pcm_frame(payload, payload_len, pcm_bytes, played_bytes);
}

static esp_err_t vb6824_play_pcm(const uint8_t *pcm, size_t pcm_len, size_t *played_bytes) {
    *played_bytes = 0;
    if (vb6824_ota_requested || vb6824_ota_active) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pcm == NULL || pcm_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_vb6824_uart();
    if (err != ESP_OK) {
        return err;
    }
    ESP_RETURN_ON_ERROR(ensure_vb6824_playback_queue(), TAG, "playback queue");
    static int applied_volume = -1;
    uint8_t volume = (uint8_t)((local_volume * 31) / 100);
    if (applied_volume != volume) {
        if (xSemaphoreTake(vb_uart_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        err = vb6824_send_frame_nonblocking(0x0203, &volume, 1, 100);
        xSemaphoreGive(vb_uart_mutex);
        if (err != ESP_OK) {
            return err;
        }
        applied_volume = volume;
    }
    if (vb6824_playback_enqueued_bytes == 0) {
        static const uint8_t silence[XOB_VB_PLAY_PCM_FRAME_BYTES] = {0};
        for (int i = 0; i < XOB_VB_PLAY_START_SILENCE_FRAMES; i++) {
            if (!vb6824_audio_write(silence, sizeof(silence), XOB_VB_PLAY_ENQUEUE_TIMEOUT_MS)) {
                ESP_LOGW(TAG, "vb6824 playback start silence queue full");
                return ESP_ERR_TIMEOUT;
            }
            vb6824_playback_enqueued_bytes += sizeof(silence);
            vb6824_playback_last_enqueue_us = esp_timer_get_time();
        }
    }

    for (size_t pos = 0; pos < pcm_len; pos += XOB_VB_PLAY_PCM_FRAME_BYTES) {
        size_t chunk = pcm_len - pos;
        if (chunk > XOB_VB_PLAY_PCM_FRAME_BYTES) {
            chunk = XOB_VB_PLAY_PCM_FRAME_BYTES;
        }
        if (!vb6824_audio_write(pcm + pos, (uint16_t)chunk, XOB_VB_PLAY_ENQUEUE_TIMEOUT_MS)) {
            ESP_LOGW(TAG, "vb6824 playback queue full queued=%u", (unsigned)*played_bytes);
            return ESP_ERR_TIMEOUT;
        }
        *played_bytes += chunk;
        vb6824_playback_enqueued_bytes += chunk;
        vb6824_playback_last_enqueue_us = esp_timer_get_time();
    }
    ESP_LOGD(TAG, "vb6824 playback queued pcm bytes=%u", (unsigned)*played_bytes);
    return ESP_OK;
}

static esp_err_t play_test_tone(void) {
    const size_t sample_count = 16000;
    const size_t pcm_len = sample_count * 2;
    uint8_t *pcm = malloc(pcm_len);
    if (pcm == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < sample_count; i++) {
        int16_t sample = ((i / 16) & 1) ? 12000 : -12000;
        pcm[i * 2] = (uint8_t)(sample & 0xff);
        pcm[i * 2 + 1] = (uint8_t)((sample >> 8) & 0xff);
    }

    int old_volume = local_volume;
    local_volume = 100;
    vb6824_prepare_playback_session();
    size_t queued = 0;
    esp_err_t err = vb6824_play_pcm(pcm, pcm_len, &queued);
    if (err == ESP_OK) {
        err = vb6824_wait_playback(vb6824_playback_enqueued_bytes, 30000);
    }
    vb6824_finish_playback_session();
    local_volume = old_volume;
    free(pcm);
    ESP_LOGI(TAG, "test tone queued=%u err=%s", (unsigned)queued, esp_err_to_name(err));
    return err;
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

static bool json_extract_string(const char *json, const char *key, char *out, size_t out_len) {
    if (json == NULL || key == NULL || out == NULL || out_len == 0) {
        return false;
    }
    char needle[32];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *pos = strstr(json, needle);
    if (pos == NULL) {
        return false;
    }
    pos = strchr(pos + strlen(needle), ':');
    if (pos == NULL) {
        return false;
    }
    pos++;
    while (*pos == ' ') {
        pos++;
    }
    if (*pos != '"') {
        return false;
    }
    pos++;
    size_t used = 0;
    while (*pos != '\0' && *pos != '"' && used + 1 < out_len) {
        if (*pos == '\\' && pos[1] != '\0') {
            pos++;
            if (*pos == 'n' || *pos == 'r' || *pos == 't') {
                out[used++] = ' ';
                pos++;
                continue;
            }
        }
        out[used++] = *pos++;
    }
    out[used] = '\0';
    return true;
}

static esp_err_t probe_xiaozhi_websocket(
    const app_config_t *config,
    bool send_talk_probe,
    bool use_vb_audio,
    const char *listen_mode,
    int vb_max_frames,
    bool auto_stop
) {
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
        ESP_LOGW(TAG, "websocket probe socket create failed");
        return ESP_FAIL;
    }
    struct timeval ws_timeout = {
        .tv_sec = 8,
        .tv_usec = 0,
    };
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &ws_timeout, sizeof(ws_timeout));
    (void)setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &ws_timeout, sizeof(ws_timeout));
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
        ESP_LOGW(TAG, "websocket probe upgrade send failed");
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
    vb6824_active_sock = sock;

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
    bool server_audio_is_opus = false;
    if (err == ESP_OK) {
        char message[384];
        err = recv_websocket_text(sock, message, sizeof(message));
        if (err == ESP_OK &&
            json_contains_string(message, "type", "hello") &&
            json_contains_string(message, "transport", "websocket")) {
            server_audio_is_opus = json_contains_string(message, "format", "opus");
            ESP_LOGI(TAG, "websocket hello complete");
        } else {
            err = ESP_FAIL;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "websocket hello failed: %s", esp_err_to_name(err));
    }
    if (err == ESP_OK && send_talk_probe) {
        bool continuous = use_vb_audio && auto_stop && strcmp(listen_mode, "auto") == 0;
        int turn = 0;
        while (err == ESP_OK) {
            turn++;
            if (vb6824_voice_abort_requested) {
                err = ESP_ERR_INVALID_STATE;
                break;
            }
            char listen_start[96];
            snprintf(
                listen_start,
                sizeof(listen_start),
                "{\"session_id\":\"\",\"type\":\"listen\",\"state\":\"start\",\"mode\":\"%s\"}",
                listen_mode
            );
            const char listen_stop[] = "{\"session_id\":\"\",\"type\":\"listen\",\"state\":\"stop\"}";
            vb6824_voice_state = XOB_VOICE_LISTENING;
            set_avatar_state(XOB_EYES_LISTENING, avatar_wifi_status, avatar_bridge_status);
            err = websocket_send_masked_text(sock, listen_start);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "websocket listen start sent turn=%d", turn);
            }
            bool heard_speech = !auto_stop;
            if (err == ESP_OK && use_vb_audio) {
                size_t sent_bytes = 0;
                err = websocket_send_vb6824_audio(sock, vb_max_frames, auto_stop, &sent_bytes, &heard_speech);
            } else if (err == ESP_OK) {
                static const uint8_t audio[160] = {0};
                err = websocket_send_masked_frame(sock, 2, audio, sizeof(audio));
                heard_speech = true;
            }
            if (err == ESP_ERR_NOT_FOUND) {
                ESP_LOGI(TAG, "websocket continuous listen idle turn=%d", turn);
                err = ESP_OK;
                break;
            }
            if (err == ESP_OK && !heard_speech) {
                ESP_LOGI(TAG, "websocket listen no speech turn=%d", turn);
                break;
            }
            if (err == ESP_OK) {
                err = websocket_send_masked_text(sock, listen_stop);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "websocket listen stop sent turn=%d", turn);
                    vb6824_voice_state = XOB_VOICE_THINKING;
                    set_avatar_state(XOB_EYES_THINKING, avatar_wifi_status, avatar_bridge_status);
                }
            }
            if (err != ESP_OK) {
                break;
            }
            size_t message_len = XOB_WS_MESSAGE_BUFFER_BYTES;
            char *message = malloc(message_len);
            if (message == NULL) {
                message_len = XOB_WS_MESSAGE_FALLBACK_BUFFER_BYTES;
                message = malloc(message_len);
                if (message != NULL) {
                    ESP_LOGW(TAG, "websocket message buffer fallback bytes=%u", (unsigned)message_len);
                }
            }
            bool turn_aborted = false;
            bool abort_sent = false;
            if (message == NULL) {
                err = ESP_ERR_NO_MEM;
            } else {
                if (vb6824_voice_abort_requested) {
                    turn_aborted = true;
                    vb6824_audio_enable_output(false);
                    err = websocket_send_abort(sock);
                    if (err == ESP_OK) {
                        abort_sent = true;
                        ESP_LOGI(TAG, "websocket abort sent turn=%d", turn);
                    }
                }
                if (err == ESP_OK) {
                    err = recv_websocket_text(sock, message, message_len);
                }
                if (err == ESP_OK && json_contains_string(message, "type", "stt")) {
                    char text[160];
                    if (!turn_aborted && json_extract_string(message, "text", text, sizeof(text))) {
                        set_avatar_dialog("THINKING", text, "");
                    }
                    ESP_LOGI(TAG, "websocket stt received");
                } else {
                    err = ESP_FAIL;
                }
                size_t tts_audio_bytes = 0;
                size_t tts_pcm_bytes = 0;
                size_t played_audio_bytes = 0;
                bool saw_tts_stop = false;
                bool tts_refresh_paused = false;
                if (err == ESP_OK && !turn_aborted) {
                    vb6824_prepare_playback_session();
                }
                for (int i = 0; err == ESP_OK && i < XOB_WS_TTS_MAX_FRAMES && !saw_tts_stop; i++) {
                    if (vb6824_voice_abort_requested) {
                        turn_aborted = true;
                        vb6824_audio_enable_output(false);
                        if (!abort_sent) {
                            err = websocket_send_abort(sock);
                            if (err != ESP_OK) {
                                break;
                            }
                            abort_sent = true;
                            ESP_LOGI(TAG, "websocket abort sent turn=%d", turn);
                        }
                    }
                    uint8_t opcode = 0;
                    size_t payload_len = 0;
                    err = recv_websocket_frame(sock, &opcode, message, message_len, &payload_len);
                    if (err != ESP_OK) {
                        break;
                    }
                    if (opcode == 2) {
                        tts_audio_bytes += payload_len;
                        if (turn_aborted) {
                            continue;
                        }
                        show_tts_speaking_once(&tts_refresh_paused);
                        if (payload_len >= message_len) {
                            ESP_LOGW(TAG, "tts audio too large to play bytes=%u", (unsigned)payload_len);
                            err = ESP_ERR_INVALID_SIZE;
                            break;
                        }
                        err = play_tts_audio_frame(
                            (const uint8_t *)message,
                            payload_len,
                            server_audio_is_opus,
                            &tts_pcm_bytes,
                            &played_audio_bytes
                        );
                        continue;
                    }
                    if (opcode != 1 || !json_contains_string(message, "type", "tts")) {
                        err = ESP_FAIL;
                        break;
                    }
                    if (json_contains_string(message, "state", "sentence_start")) {
                        char text[320];
                        if (!turn_aborted && json_extract_string(message, "text", text, sizeof(text))) {
                            set_avatar_dialog("SPEAKING", NULL, text);
                        }
                        if (!turn_aborted) {
                            show_tts_speaking_once(&tts_refresh_paused);
                        }
                    }
                    if (json_contains_string(message, "state", "stop")) {
                        saw_tts_stop = true;
                    }
                }
                if (err == ESP_OK && !turn_aborted && (!saw_tts_stop || tts_audio_bytes == 0)) {
                    err = ESP_FAIL;
                }
                if (err == ESP_OK) {
                    if (!turn_aborted && played_audio_bytes > 0) {
                        vb6824_audio_enable_output(true);
                        err = vb6824_wait_playback(vb6824_playback_enqueued_bytes, XOB_WS_RECV_TIMEOUT_MS);
                        if (err != ESP_OK && vb6824_voice_abort_requested) {
                            turn_aborted = true;
                            err = ESP_OK;
                        }
                    }
                }
                vb6824_finish_playback_session();
                avatar_refresh_paused = false;
                if (err == ESP_OK) {
                    if (turn_aborted) {
                        vb6824_voice_abort_requested = false;
                        vb6824_voice_stop_requested = false;
                        vb6824_voice_state = XOB_VOICE_LISTENING;
                        set_avatar_dialog("LISTENING", "", "");
                        set_avatar_state(XOB_EYES_LISTENING, avatar_wifi_status, avatar_bridge_status);
                        vTaskDelay(pdMS_TO_TICKS(120));
                        ESP_LOGI(TAG, "websocket talk turn aborted turn=%d", turn);
                    } else if (continuous) {
                        vb6824_voice_state = XOB_VOICE_LISTENING;
                        set_avatar_state(XOB_EYES_LISTENING, avatar_wifi_status, avatar_bridge_status);
                        vTaskDelay(pdMS_TO_TICKS(320));
                    } else {
                        set_avatar_dialog("DONE", NULL, NULL);
                    }
                    ESP_LOGI(TAG, "websocket tts audio received bytes=%u", (unsigned)tts_audio_bytes);
                    ESP_LOGI(TAG, "websocket tts audio buffered pcm bytes=%u", (unsigned)tts_pcm_bytes);
                    ESP_LOGI(TAG, "websocket tts audio played bytes=%u", (unsigned)played_audio_bytes);
                    ESP_LOGI(TAG, "websocket talk turn complete turn=%d", turn);
                    ESP_LOGI(TAG, "websocket talk probe complete");
                }
                free(message);
            }
            if (!continuous || err != ESP_OK || vb6824_voice_abort_requested) {
                break;
            }
            ESP_LOGI(TAG, "websocket continuous re-enter listening turn=%d", turn + 1);
        }
    }
    if (vb6824_active_sock == sock) {
        vb6824_active_sock = -1;
    }
    close(sock);
    if (err != ESP_OK) {
        if (vb6824_voice_abort_requested) {
            ESP_LOGI(TAG, "websocket probe interrupted: %s", esp_err_to_name(err));
        } else {
            ESP_LOGW(TAG, "websocket probe failed: %s", esp_err_to_name(err));
        }
    }
    return err;
}

static bool vb6824_text_contains(const uint8_t *body, uint16_t len, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || len < needle_len) {
        return false;
    }
    for (size_t i = 0; i + needle_len <= len; i++) {
        if (memcmp(body + i, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

static bool vb6824_is_wake_command(const uint8_t *body, uint16_t len) {
    return vb6824_text_contains(body, len, "小元") ||
           vb6824_text_contains(body, len, "小智");
}

static void run_vb6824_voice_session(const app_config_t *config, const char *source) {
    if (vb6824_ota_requested || vb6824_ota_active) {
        ESP_LOGW(TAG, "xiaoyuan wake ignored during vb6824 ota source=%s", source);
        return;
    }
    ESP_LOGI(TAG, "xiaoyuan wake source=%s", source);
    vb6824_voice_state = XOB_VOICE_LISTENING;
    set_avatar_dialog("LISTENING", "", "");
    set_avatar_state(XOB_EYES_LISTENING, avatar_wifi_status, XOB_SCREEN_STATUS_PENDING);
    vb6824_voice_stop_requested = false;
    vb6824_voice_abort_requested = false;
    esp_err_t prelisten_err = vb6824_start_voice_input();
    if (prelisten_err == ESP_OK) {
        vb6824_audio_capture_started_us = esp_timer_get_time();
    } else {
        ESP_LOGW(TAG, "vb6824 prelisten failed err=%s", esp_err_to_name(prelisten_err));
    }
    esp_err_t err = probe_xiaozhi_websocket(
        config,
        true,
        true,
        "auto",
        XOB_VB_TALK_AUTO_MAX_FRAMES,
        true
    );
    if (!vb6824_audio_capture_active) {
        vb6824_audio_enable_input(false);
    }
    if (vb6824_voice_abort_requested) {
        vb6824_audio_enable_output(false);
        avatar_refresh_paused = false;
        vb6824_voice_state = XOB_VOICE_IDLE;
        set_avatar_state(XOB_EYES_IDLE, avatar_wifi_status, XOB_SCREEN_STATUS_OK);
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "xiaoyuan voice session failed source=%s err=%s", source, esp_err_to_name(err));
        vb6824_voice_state = XOB_VOICE_IDLE;
        set_avatar_state(XOB_EYES_ERROR, avatar_wifi_status, XOB_SCREEN_STATUS_ERROR);
    } else {
        vb6824_voice_state = XOB_VOICE_IDLE;
        vTaskDelay(pdMS_TO_TICKS(1200));
        set_avatar_state(XOB_EYES_IDLE, avatar_wifi_status, XOB_SCREEN_STATUS_OK);
    }
}

static void vb6824_voice_session_task(void *arg) {
    vb6824_voice_session_arg_t *session = (vb6824_voice_session_arg_t *)arg;
    run_vb6824_voice_session(session->config, session->source);
    free(session);
    vb6824_voice_session_active = false;
    vTaskDelete(NULL);
}

static esp_err_t dispatch_vb6824_voice_session(const app_config_t *config, const char *source) {
    if (vb6824_voice_session_active) {
        xob_voice_state_t state = vb6824_voice_state;
        if (state == XOB_VOICE_LISTENING) {
            vb6824_voice_abort_requested = true;
            vb6824_voice_stop_requested = true;
            vb6824_audio_enable_input(false);
            vb6824_audio_enable_output(false);
            avatar_refresh_paused = false;
            ESP_LOGI(TAG, "xiaoyuan listen cancel requested source=%s", source);
        } else {
            vb6824_voice_abort_requested = true;
            vb6824_voice_stop_requested = true;
            vb6824_audio_enable_input(false);
            vb6824_audio_enable_output(false);
            avatar_refresh_paused = false;
            set_avatar_dialog("LISTENING", "", "");
            set_avatar_state(XOB_EYES_LISTENING, avatar_wifi_status, avatar_bridge_status);
            ESP_LOGI(TAG, "xiaoyuan active session abort requested source=%s state=%d", source, state);
        }
        return ESP_ERR_INVALID_STATE;
    }
    vb6824_voice_session_arg_t *session = calloc(1, sizeof(*session));
    if (session == NULL) {
        return ESP_ERR_NO_MEM;
    }
    session->config = config;
    session->source = source;
    vb6824_voice_session_active = true;
    vb6824_voice_state = XOB_VOICE_LISTENING;
    BaseType_t created = xTaskCreate(vb6824_voice_session_task, "xob_vb_session", 12288, session, 4, NULL);
    if (created != pdPASS) {
        vb6824_voice_session_active = false;
        vb6824_voice_state = XOB_VOICE_IDLE;
        free(session);
        ESP_LOGW(TAG, "xiaoyuan voice session not started source=%s", source);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void vb6824_wake_task(void *arg) {
    const app_config_t *config = (const app_config_t *)arg;
    bool wake_word_requested = false;

    while (true) {
        esp_err_t err = ensure_vb6824_uart();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "vb6824 wake listener unavailable: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (vb6824_ota_requested && !vb6824_ota_active) {
            int64_t now = esp_timer_get_time();
            if (vb6824_ota_last_request_us == 0 || now - vb6824_ota_last_request_us >= 500000) {
                err = vb6824_send_ota_start_request();
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "vb6824 ota enter request failed: %s", esp_err_to_name(err));
                }
            }
        }
        if (!vb6824_ota_requested && !vb6824_ota_active &&
            (!wake_word_requested || vb6824_wake_refresh_requested) &&
            xSemaphoreTake(vb_uart_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            uint8_t one = 1;
            vb6824_send_frame(0x0207, &one, 1);
            xSemaphoreGive(vb_uart_mutex);
            wake_word_requested = true;
            vb6824_wake_refresh_requested = false;
        }
        vb6824_event_msg_t event;
        if (xQueueReceive(vb6824_event_queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        if (event.cmd == 0x0280) {
            ESP_LOGI(TAG, "vb6824 configured wake word len=%u text=%.*s", event.len, event.len, event.data);
            continue;
        }
        if (event.cmd != 0x0180 || vb6824_ota_requested || vb6824_ota_active) {
            continue;
        }
        ESP_LOGI(TAG, "vb6824 voice command len=%u text=%.*s", event.len, event.len, event.data);
        if (vb6824_is_wake_command(event.data, event.len)) {
            dispatch_vb6824_voice_session(config, "vb6824");
            continue;
        }
        if (vb6824_voice_session_active ||
            vb6824_audio_capture_active ||
            vb6824_playback_session_active ||
            avatar_refresh_paused) {
            continue;
        }
    }
}

static void start_vb6824_wake_task(const app_config_t *config) {
    if (vb_wake_task_started) {
        return;
    }
    BaseType_t created = xTaskCreate(vb6824_wake_task, "xob_vb_wake", 8192, (void *)config, 3, NULL);
    if (created == pdPASS) {
        vb_wake_task_started = true;
    } else {
        ESP_LOGW(TAG, "vb6824 wake task not started");
    }
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
            if (strncmp(line, ":target", strlen(":target")) == 0 &&
                (line[strlen(":target")] == '\0' || line[strlen(":target")] == ' ')) {
                const char *target = line + strlen(":target");
                while (*target == ' ') {
                    target++;
                }
                if (*target == '\0') {
                    ESP_LOGW(TAG, "usage: :target <agent>");
                    len = 0;
                    continue;
                }
                err = set_default_target(target);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "default_target saved; rebooting");
                    vTaskDelay(pdMS_TO_TICKS(500));
                    esp_restart();
                }
                ESP_LOGW(TAG, "default_target save failed: %s", esp_err_to_name(err));
                len = 0;
                continue;
            }
            if (strncmp(line, ":vb-ota", strlen(":vb-ota")) == 0 &&
                (line[strlen(":vb-ota")] == '\0' || line[strlen(":vb-ota")] == ' ')) {
                const char *code = line + strlen(":vb-ota");
                while (*code == ' ') {
                    code++;
                }
                if (*code == '\0') {
                    ESP_LOGW(TAG, "usage: :vb-ota <code>");
                    len = 0;
                    continue;
                }
                err = start_vb6824_ota_code(code);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "vb6824 ota requested");
                } else {
                    ESP_LOGW(TAG, "vb6824 ota request failed: %s", esp_err_to_name(err));
                }
                len = 0;
                continue;
            }
            if (strcmp(line, ":vb") == 0 || strcmp(line, ":vb6824") == 0) {
                set_avatar_state(XOB_EYES_LISTENING, avatar_wifi_status, avatar_bridge_status);
                if (vb6824_uart_ready) {
                    ESP_LOGI(TAG, "vb6824 wake listener active tx=%d rx=%d", XOB_VB_TX_GPIO, XOB_VB_RX_GPIO);
                    err = ESP_OK;
                } else {
                    err = run_vb6824_uart_probe();
                }
                set_avatar_state(
                    err == ESP_OK ? XOB_EYES_IDLE : XOB_EYES_ERROR,
                    avatar_wifi_status,
                    avatar_bridge_status
                );
                len = 0;
                continue;
            }
            if (strcmp(line, ":tone") == 0) {
                set_avatar_state(XOB_EYES_SPEAKING, avatar_wifi_status, avatar_bridge_status);
                err = play_test_tone();
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
                err = probe_xiaozhi_websocket(config, false, false, "manual", XOB_VB_TALK_PROBE_FRAMES, false);
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
                vb6824_voice_stop_requested = false;
                err = probe_xiaozhi_websocket(config, true, false, "manual", XOB_VB_TALK_PROBE_FRAMES, false);
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
            if (strcmp(line, ":tts") == 0) {
                set_avatar_state(XOB_EYES_THINKING, avatar_wifi_status, avatar_bridge_status);
                vb6824_voice_stop_requested = false;
                err = probe_xiaozhi_websocket(config, true, false, "tts_debug", XOB_VB_TALK_PROBE_FRAMES, false);
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
            if (strcmp(line, ":ask") == 0) {
                set_avatar_state(XOB_EYES_THINKING, avatar_wifi_status, avatar_bridge_status);
                vb6824_voice_stop_requested = false;
                err = probe_xiaozhi_websocket(config, true, false, "ask_debug", XOB_VB_TALK_PROBE_FRAMES, false);
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
                vb6824_voice_stop_requested = false;
                err = probe_xiaozhi_websocket(config, true, true, "manual", XOB_VB_TALK_PROBE_FRAMES, false);
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
    BaseType_t created = xTaskCreate(serial_command_task, "xob_serial_cmd", 12288, (void *)config, 2, NULL);
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
           left->bridge_status == right->bridge_status &&
           strcmp(left->status_text, right->status_text) == 0 &&
           strcmp(left->input_text, right->input_text) == 0 &&
           strcmp(left->output_text, right->output_text) == 0 &&
           left->text_scroll_step == right->text_scroll_step;
}

static void copy_display_text(char *out, size_t out_len, const char *in) {
    if (out_len == 0) {
        return;
    }
    if (in == NULL) {
        out[0] = '\0';
        return;
    }
    for (const unsigned char *p = (const unsigned char *)in; *p != '\0'; p++) {
        if (*p >= 0x80) {
            out[0] = '\0';
            return;
        }
    }
    size_t pos = 0;
    while (*in != '\0' && pos + 1 < out_len) {
        unsigned char lead = (unsigned char)*in;
        size_t char_len = 1;
        if ((lead & 0xe0) == 0xc0) {
            char_len = 2;
        } else if ((lead & 0xf0) == 0xe0) {
            char_len = 3;
        } else if ((lead & 0xf8) == 0xf0) {
            char_len = 4;
        }
        for (size_t i = 1; i < char_len; i++) {
            if ((in[i] & 0xc0) != 0x80) {
                char_len = 1;
                break;
            }
        }
        if (pos + char_len >= out_len) {
            break;
        }
        memcpy(out + pos, in, char_len);
        pos += char_len;
        in += char_len;
    }
    out[pos] = '\0';
}

static const char *eye_state_label(xob_eye_state_t state) {
    switch (state) {
    case XOB_EYES_LISTENING:
        return "LISTENING";
    case XOB_EYES_THINKING:
        return "THINKING";
    case XOB_EYES_SPEAKING:
        return "SPEAKING";
    case XOB_EYES_ERROR:
        return "ERROR";
    case XOB_EYES_IDLE:
    default:
        return "IDLE";
    }
}

static void set_avatar_dialog(const char *status, const char *input, const char *output) {
    if (status != NULL) {
        copy_display_text(avatar_status_text, sizeof(avatar_status_text), status);
    }
    if (input != NULL) {
        copy_display_text(avatar_input_text, sizeof(avatar_input_text), input);
    }
    if (output != NULL) {
        copy_display_text(avatar_output_text, sizeof(avatar_output_text), output);
    }
}

static avatar_frame_t avatar_frame(uint32_t tick_ms) {
    xob_eye_state_t state = avatar_eye_state;
    bool scroll = strlen(avatar_output_text) > 36 || strlen(avatar_input_text) > 36;
    uint32_t eye_tick = state == XOB_EYES_IDLE ? tick_ms : 0;
    avatar_frame_t frame = {
        .eyes = xob_eyes_frame(state, eye_tick),
        .wifi_status = avatar_wifi_status,
        .bridge_status = avatar_bridge_status,
        .text_scroll_step = scroll ? (uint16_t)(tick_ms / 120) : 0,
    };
    copy_display_text(frame.status_text, sizeof(frame.status_text), avatar_status_text);
    copy_display_text(frame.input_text, sizeof(frame.input_text), avatar_input_text);
    copy_display_text(frame.output_text, sizeof(frame.output_text), avatar_output_text);
    return frame;
}

static void set_avatar_state(
    xob_eye_state_t eye_state,
    xob_screen_status_t wifi_status,
    xob_screen_status_t bridge_status
) {
    avatar_eye_state = eye_state;
    avatar_wifi_status = wifi_status;
    avatar_bridge_status = bridge_status;
    set_avatar_dialog(eye_state_label(eye_state), NULL, NULL);
}

static void show_tts_speaking_once(bool *paused) {
    if (*paused) {
        return;
    }
    vb6824_voice_state = XOB_VOICE_SPEAKING;
    set_avatar_state(XOB_EYES_SPEAKING, avatar_wifi_status, XOB_SCREEN_STATUS_OK);
    vTaskDelay(pdMS_TO_TICKS(120));
    avatar_refresh_paused = true;
    *paused = true;
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

static esp_err_t set_default_target(const char *target) {
    if (strlen(target) == 0 || strlen(target) >= sizeof(active_config.default_target)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open("xob", NVS_READWRITE, &nvs), TAG, "open xob NVS");
    esp_err_t err = nvs_set_str(nvs, "default_target", target);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static void button_task(void *arg) {
    (void)arg;
    uint8_t last = button_mask();
    int64_t config_chord_since = 0;
    int64_t listen_button_allowed_us = 0;

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
                char status[16];
                snprintf(status, sizeof(status), "VOL %d", local_volume);
                set_avatar_dialog(status, NULL, NULL);
                ESP_LOGI(TAG, "volume=%d", local_volume);
            }
            if ((pressed & XOB_BUTTON_VOLUME_UP) != 0 && local_volume < 100) {
                local_volume += 5;
                char status[16];
                snprintf(status, sizeof(status), "VOL %d", local_volume);
                set_avatar_dialog(status, NULL, NULL);
                ESP_LOGI(TAG, "volume=%d", local_volume);
            }
            if ((pressed & XOB_BUTTON_LISTEN) != 0) {
                if (now < listen_button_allowed_us) {
                    ESP_LOGI(TAG, "interrupt/listen button ignored during cooldown");
                } else {
                    listen_button_allowed_us = now + ((int64_t)XOB_BUTTON_LISTEN_COOLDOWN_MS * 1000);
                    ESP_LOGI(TAG, "interrupt/listen button pressed");
                    if (strlen(active_config.bridge_url) > 0) {
                        dispatch_vb6824_voice_session(&active_config, "button");
                    } else {
                        set_avatar_state(XOB_EYES_LISTENING, avatar_wifi_status, avatar_bridge_status);
                    }
                }
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
    ESP_RETURN_ON_ERROR(xob_lcd_draw_frame(&screen), TAG, "draw avatar");
    return xob_lcd_draw_dialog_text(frame->status_text, frame->input_text, frame->output_text, frame->text_scroll_step);
}

static void avatar_task(void *arg) {
    (void)arg;
    while (true) {
        if (vb6824_audio_capture_active || avatar_refresh_paused) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
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
        BaseType_t created = xTaskCreate(avatar_task, "xob_avatar", 12288, NULL, 2, NULL);
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
    start_vb6824_wake_task(&active_config);
}
