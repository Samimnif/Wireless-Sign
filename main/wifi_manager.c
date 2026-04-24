#include <string.h>

#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define MAX_RETRIES 5
static int retry_count = 0;

static const char *TAG = "WIFI";

static EventGroupHandle_t wifi_events;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

// ─── WiFi Event Handler ──────────────────────────────────────────
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (retry_count < MAX_RETRIES)
        {
            esp_wifi_connect();
            retry_count++;
            ESP_LOGI(TAG, "Retrying... (%d/%d)", retry_count, MAX_RETRIES);
        }
        else
        {
            xEventGroupSetBits(wifi_events, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect after %d retries", MAX_RETRIES);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_count = 0;
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

// ─── WiFi init (AP+STA mode) ──────────────────────────────────────
void wifi_init(void)
{
    wifi_events = xEventGroupCreate();
    if (wifi_events == NULL)
    {
        ESP_LOGE(TAG, "Failed to create wifi event group");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    // esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    // esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    // Configure AP
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = "Matrix_Config_AP",
            .ssid_len = strlen("Matrix_Config_AP"),
            .channel = 1,
            .password = "", //keep it open 12345678
            .max_connection = 2,
            .ssid_hidden = 0,
            .authmode = WIFI_AUTH_OPEN, //WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = true,
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_config_t ap_check;
    esp_wifi_get_config(WIFI_IF_AP, &ap_check);
    ESP_LOGI(TAG, "AP SSID: %s", ap_check.ap.ssid);
    ESP_LOGI(TAG, "AP Channel: %d", ap_check.ap.channel);
    ESP_LOGI(TAG, "AP Auth: %d", ap_check.ap.authmode);
}

// ─── Connect to STA with given credentials ───────────────────────
void wifi_connect_sta(const char *ssid, const char *pass)
{
    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid));
    strncpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password));

    retry_count = 0;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_connect());
    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
}

// ─── NVS: Save credentials ───────────────────────────────────────
void nvs_save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open("wifi_creds", NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_str(handle, "ssid", ssid));
    ESP_ERROR_CHECK(nvs_set_str(handle, "pass", pass));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
    ESP_LOGI(TAG, "Credentials saved to NVS");
}

// ─── NVS: Load credentials ───────────────────────────────────────
// Returns true if credentials exist
bool nvs_load_credentials(char *ssid, size_t ssid_size, char *pass, size_t pass_size)
{
    nvs_handle_t handle;
    if (nvs_open("wifi_creds", NVS_READONLY, &handle) != ESP_OK)
        return false;

    bool ok = (nvs_get_str(handle, "ssid", ssid, &ssid_size) == ESP_OK) &&
              (nvs_get_str(handle, "pass", pass, &pass_size) == ESP_OK) &&
              strlen(ssid) > 0;

    nvs_close(handle);
    return ok;
}

bool wifi_wait_connected(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(
        wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    return (bits & WIFI_CONNECTED_BIT) != 0;
}

bool wifi_is_connected(void)
{
    if (wifi_events == NULL)
    {
        return false;
    }

    return (xEventGroupGetBits(wifi_events) & WIFI_CONNECTED_BIT) != 0;
}