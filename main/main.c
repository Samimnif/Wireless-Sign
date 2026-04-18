#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/spi_master.h"

#include "max7219.h"
#include "wifi_manager.h"
#include "portal.h"
#include "time_manager.h"

#include "cJSON.h"

#include <time.h>
#include <sys/time.h>
#include "esp_netif_sntp.h"
#include "esp_mac.h"

#define SERVER_URL "http://samislab.tplinkdns.com:10300"
static const char *TAG = "MAIN";

static char g_server_url[128] = SERVER_URL;
static char g_device_id[32];

typedef struct
{
    char message[128];
    bool has_message;

    int brightness;
    bool show_clock;

    int message_id;
    int message_seconds;

    char message_mode[16];

    int utc_offset_hours;
    bool time_format_24h;

    int scroll_speed_ms;
} server_config_t;

typedef struct
{
    char current_time[6];
    server_config_t server_cfg;
} app_state_t;

static app_state_t g_state;
static SemaphoreHandle_t g_state_mutex;

static max7219_t g_display;

// static const uint8_t SMILEY[8] = {
//     0b00111100,
//     0b01000010,
//     0b10100101,
//     0b10000001,
//     0b10100101,
//     0b10011001,
//     0b01000010,
//     0b00111100,
// };

// static const uint8_t HEART[8] = {
//     0b00000000,
//     0b01100110,
//     0b11111111,
//     0b11111111,
//     0b01111110,
//     0b00111100,
//     0b00011000,
//     0b00000000,
// };

// static const uint8_t CROSS[8] = {
//     0b10000001,
//     0b01000010,
//     0b00100100,
//     0b00011000,
//     0b00011000,
//     0b00100100,
//     0b01000010,
//     0b10000001,
// };

void make_device_id(char *out, size_t out_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(out, out_size,
             "matrix-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t fetch_server_json(const char *url, char *out_buf, size_t out_buf_size)
{
    if (url == NULL || out_buf == NULL || out_buf_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    out_buf[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int header_ret = esp_http_client_fetch_headers(client);
    if (header_ret < 0)
    {
        ESP_LOGE(TAG, "HTTP fetch headers failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int status = esp_http_client_get_status_code(client);
    int content_length = esp_http_client_get_content_length(client);

    ESP_LOGI(TAG, "HTTP status = %d, content_length = %d", status, content_length);

    if (status != 200)
    {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int total_read = 0;
    while (total_read < (int)out_buf_size - 1)
    {
        int read_len = esp_http_client_read(
            client,
            out_buf + total_read,
            (out_buf_size - 1) - total_read);

        if (read_len < 0)
        {
            ESP_LOGE(TAG, "HTTP read failed");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        if (read_len == 0)
        {
            break;
        }

        total_read += read_len;
    }

    out_buf[total_read] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}

esp_err_t send_heartbeat(void)
{
    char url[192];
    snprintf(url, sizeof(url), "%s/alive", g_server_url);

    char post_data[256];
    snprintf(post_data, sizeof(post_data),
             "{\"device_id\":\"%s\",\"ip\":\"%s\",\"free_mem\":%u}",
             g_device_id,
             "", // optional: fill in IP later
             (unsigned)esp_get_free_heap_size());

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Heartbeat failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

void heartbeat_task(void *pv)
{
    while (1)
    {
        if (wifi_is_connected())
        {
            send_heartbeat();
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

esp_err_t ack_message(int message_id)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/api/device/%s/ack",
             g_server_url, g_device_id);

    char post_data[64];
    snprintf(post_data, sizeof(post_data),
             "{\"message_id\":%d}", message_id);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ACK failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

bool parse_server_config_json(const char *json_str, server_config_t *cfg)
{
    if (json_str == NULL || cfg == NULL)
    {
        return false;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL)
    {
        const char *err_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "Failed to parse JSON");
        ESP_LOGE(TAG, "Payload was: %s", json_str);
        if (err_ptr != NULL)
        {
            ESP_LOGE(TAG, "JSON error before: %.32s", err_ptr);
        }
        return false;
    }

    memset(cfg, 0, sizeof(*cfg));

    cJSON *message = cJSON_GetObjectItem(root, "message");
    cJSON *has_message = cJSON_GetObjectItem(root, "has_message");
    cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
    cJSON *show_clock = cJSON_GetObjectItem(root, "show_clock");
    cJSON *message_id = cJSON_GetObjectItem(root, "message_id");
    cJSON *message_seconds = cJSON_GetObjectItem(root, "message_seconds");
    cJSON *utc_offset_hours = cJSON_GetObjectItem(root, "utc_offset_hours");
    cJSON *time_format_24h = cJSON_GetObjectItem(root, "time_format_24h");
    cJSON *scroll_speed_ms = cJSON_GetObjectItem(root, "scroll_speed_ms");

    if (cJSON_IsString(message) && message->valuestring != NULL)
    {
        strncpy(cfg->message, message->valuestring, sizeof(cfg->message) - 1);
        cfg->message[sizeof(cfg->message) - 1] = '\0';
    }

    if (cJSON_IsBool(has_message))
    {
        cfg->has_message = cJSON_IsTrue(has_message);
    }
    else
    {
        cfg->has_message = strlen(cfg->message) > 0;
    }

    cfg->brightness = cJSON_IsNumber(brightness) ? brightness->valueint : 5;
    cfg->show_clock = cJSON_IsBool(show_clock) ? cJSON_IsTrue(show_clock) : true;
    cfg->message_id = cJSON_IsNumber(message_id) ? message_id->valueint : 0;
    cfg->message_seconds = cJSON_IsNumber(message_seconds) ? message_seconds->valueint : 15;
    cfg->utc_offset_hours = cJSON_IsNumber(utc_offset_hours) ? utc_offset_hours->valueint : 0;
    cfg->time_format_24h = cJSON_IsBool(time_format_24h) ? cJSON_IsTrue(time_format_24h) : true;
    cfg->scroll_speed_ms = cJSON_IsNumber(scroll_speed_ms) ? scroll_speed_ms->valueint : 60;

    strcpy(cfg->message_mode, "scroll");

    cJSON_Delete(root);
    return true;
}

void update_shared_server_config(const server_config_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        strncpy(g_state.server_cfg.message, cfg->message, sizeof(g_state.server_cfg.message) - 1);
        g_state.server_cfg.message[sizeof(g_state.server_cfg.message) - 1] = '\0';

        g_state.server_cfg.has_message = cfg->has_message;
        g_state.server_cfg.brightness = cfg->brightness;
        g_state.server_cfg.show_clock = cfg->show_clock;
        g_state.server_cfg.message_id = cfg->message_id;
        g_state.server_cfg.message_seconds = cfg->message_seconds;
        g_state.server_cfg.utc_offset_hours = cfg->utc_offset_hours;
        g_state.server_cfg.time_format_24h = cfg->time_format_24h;
        g_state.server_cfg.scroll_speed_ms = cfg->scroll_speed_ms;

        strncpy(g_state.server_cfg.message_mode,
                cfg->message_mode,
                sizeof(g_state.server_cfg.message_mode) - 1);
        g_state.server_cfg.message_mode[sizeof(g_state.server_cfg.message_mode) - 1] = '\0';

        xSemaphoreGive(g_state_mutex);
    }
}

void clock_task(void *pv)
{
    while (1)
    {
        char buf[6];

        if (get_current_hhmm(buf, sizeof(buf)))
        {
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                strcpy(g_state.current_time, buf);
                xSemaphoreGive(g_state_mutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void fetch_task(void *pv)
{
    char json_buf[512];
    char url[256];

    while (1)
    {
        if (wifi_is_connected())
        {

            snprintf(url, sizeof(url), "%s/api/device/%s/config",
                     g_server_url, g_device_id);
            ESP_LOGI(TAG, "Fetching URL: %s", url);
            esp_err_t err = fetch_server_json(url, json_buf, sizeof(json_buf));
            if (err == ESP_OK)
            {
                ESP_LOGI(TAG, "Received JSON: %s", json_buf);

                server_config_t cfg;
                if (parse_server_config_json(json_buf, &cfg))
                {
                    update_shared_server_config(&cfg);
                    ESP_LOGI(TAG, "Server config updated");
                }
                else
                {
                    ESP_LOGW(TAG, "JSON parse failed");
                }
            }
            else
            {
                ESP_LOGW(TAG, "Fetch failed");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void display_task(void *pv)
{
    max7219_t *display = (max7219_t *)pv;

    int message_id = 0;
    char message_mode[16] = "scroll";
    int message_seconds = 15;

    int scroll_speed_ms = 60;
    int utc_offset_hours = 0;
    bool time_format_24h = true;

    char time_copy[6] = "--:--";
    char message_copy[128] = {0};
    bool has_message = false;
    int brightness = 5;
    bool show_clock = true;

    while (1)
    {
        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            strncpy(time_copy, g_state.current_time, sizeof(time_copy) - 1);
            time_copy[sizeof(time_copy) - 1] = '\0';

            strncpy(message_copy, g_state.server_cfg.message, sizeof(message_copy) - 1);
            message_copy[sizeof(message_copy) - 1] = '\0';

            has_message = g_state.server_cfg.has_message;
            brightness = g_state.server_cfg.brightness;
            show_clock = g_state.server_cfg.show_clock;
            message_id = g_state.server_cfg.message_id;

            scroll_speed_ms = g_state.server_cfg.scroll_speed_ms;
            utc_offset_hours = g_state.server_cfg.utc_offset_hours;
            time_format_24h = g_state.server_cfg.time_format_24h;
            strncpy(message_mode, g_state.server_cfg.message_mode, sizeof(message_mode) - 1);
            message_mode[sizeof(message_mode) - 1] = '\0';
            message_seconds = g_state.server_cfg.message_seconds;
            xSemaphoreGive(g_state_mutex);
        }

        max7219_set_intensity(display, brightness);

        ESP_LOGI(TAG, "display_task: time='%s' show_clock=%d brightness=%d has_message=%d",
                 time_copy, show_clock, brightness, has_message);

        if (has_message && strlen(message_copy) > 0)
        {
            if (strcmp(message_mode, "static") == 0)
            {
                max7219_clear(display);
                max7219_draw_text(display, 0, message_copy);
                max7219_refresh(display);
                vTaskDelay(pdMS_TO_TICKS(message_seconds * 1000));
            }
            else if (strcmp(message_mode, "scroll") == 0)
            {
                TickType_t end_time = xTaskGetTickCount() + pdMS_TO_TICKS(message_seconds * 1000);
                while (xTaskGetTickCount() < end_time)
                {
                    max7219_scroll_text(display, message_copy, scroll_speed_ms);
                }
            }
            // max7219_scroll_text(display, message_copy, scroll_speed_ms);
            ack_message(message_id);

            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                g_state.server_cfg.has_message = false;
                g_state.server_cfg.message[0] = '\0';
                xSemaphoreGive(g_state_mutex);
            }
        }
        else if (show_clock)
        {
            max7219_clear(display);
            max7219_draw_text(display, 0, time_copy);
            max7219_refresh(display);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}

static void draw_wifi_bars(max7219_t *display, int level)

{

    max7219_clear(display);

    // base positions near the middle of 32x8 display

    // x grows left to right, y grows top to bottom

    // bottom row is y = 7

    // small center dot

    max7219_set_pixel(display, 15, 7, true);

    max7219_set_pixel(display, 16, 7, true);

    // level 1

    if (level >= 1) {

        max7219_set_pixel(display, 13, 6, true);
        max7219_set_pixel(display, 18, 6, true);

    }

    // level 2

    if (level >= 2) {

        max7219_set_pixel(display, 11, 5, true);
        max7219_set_pixel(display, 12, 5, true);
        max7219_set_pixel(display, 19, 5, true);
        max7219_set_pixel(display, 20, 5, true);

    }

    // level 3

    if (level >= 3) {

        max7219_set_pixel(display, 9, 4, true);
        max7219_set_pixel(display, 10, 4, true);
        max7219_set_pixel(display, 21, 4, true);
        max7219_set_pixel(display, 22, 4, true);

    }

    // level 4

    if (level >= 4) {

        max7219_set_pixel(display, 7, 3, true);
        max7219_set_pixel(display, 8, 3, true);
        max7219_set_pixel(display, 23, 3, true);
        max7219_set_pixel(display, 24, 3, true);

    }

    max7219_refresh(display);

}

// ─── app_main ─────────────────────────────────────────────────────
void app_main(void)
{
    make_device_id(g_device_id, sizeof(g_device_id));
    ESP_LOGI(TAG, "Device ID: %s", g_device_id);

    // 1. Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    max7219_config_t cfg = {
        .host = SPI2_HOST,
        .mosi_pin = 6,
        .sclk_pin = 4,
        .cs_pin = 7,
        .clock_speed_hz = 1000000,
        .num_devices = 4,
    };

    g_state_mutex = xSemaphoreCreateMutex();

    ESP_ERROR_CHECK(max7219_init(&g_display, &cfg));
    ESP_LOGI(TAG, "Driver test starting");

    // 2. Init WiFi
    wifi_init();

    // 3. Check if credentials exist in NVS
    char ssid[64], pass[64];
    if (nvs_load_credentials(ssid, sizeof(ssid), pass, sizeof(pass)))
    {
        ESP_LOGI(TAG, "Found saved credentials for: %s", ssid);
        wifi_connect_sta(ssid, pass);

        // Wait for result (10 seconds)
        if (wifi_wait_connected(10000))
        {
            ESP_LOGI(TAG, "Connected! No need for AP setup.");

            time_sync_init();

            // Init shared state
            memset(&g_state, 0, sizeof(g_state));
            g_state.server_cfg.show_clock = true;
            g_state.server_cfg.brightness = 5;
            g_state.server_cfg.scroll_speed_ms = 60;
            strcpy(g_state.current_time, "--:--");

            // Create tasks
            xTaskCreate(clock_task, "clock_task", 2048, NULL, 5, NULL);
            xTaskCreate(fetch_task, "fetch_task", 6144, NULL, 5, NULL);
            xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 5, NULL);
            xTaskCreate(display_task, "display_task", 4096, &g_display, 5, NULL);

            vTaskDelete(NULL); // optional but clean
        }

        ESP_LOGW(TAG, "Saved credentials failed. Starting config AP...");
    }
    else
    {
        // max7219_scroll_text(&display, "Connect to AP for WIFI setup", 30);
        ESP_LOGI(TAG, "No credentials found. Starting config AP...");
    }

    // 4. Start webserver for configuration
    portal_start();

    ESP_LOGI(TAG, "Connect to 'Matrix_Config_AP' (pw: 12345678)");
    ESP_LOGI(TAG, "Then open http://192.168.4.1 in your browser");

    while (!wifi_is_connected())
    {
        max7219_scroll_text(&g_display,
                            "Connect to 'Matrix_Config_AP' and setup WiFi",
                            35);
    }

    // ESP_LOGI(TAG, "Drawing emojis");

    // max7219_clear(&display);

    // max7219_draw_bitmap8(&display, 0,  SMILEY);
    // max7219_draw_bitmap8(&display, 8,  HEART);
    // max7219_draw_bitmap8(&display, 16, CROSS);
    // max7219_draw_bitmap8(&display, 24, SMILEY);

    // max7219_refresh(&display);
    // vTaskDelay(pdMS_TO_TICKS(2000));

    // // ─── Test 4: Moving vertical line ─────────
    // ESP_LOGI(TAG, "Moving line test");

    // for (int x = 0; x < 32; x++) {
    //     max7219_clear(&display);

    //     for (int y = 0; y < 8; y++) {
    //         max7219_set_pixel(&display, x, y, true);
    //         max7219_set_pixel(&display, x+1, y, true);
    //     }

    //     max7219_refresh(&display);
    //     vTaskDelay(pdMS_TO_TICKS(50));
    // }

    // // ─── Test 5: Moving pixel ─────────
    // ESP_LOGI(TAG, "Moving pixel test");

    // for (int x = 0; x < 32; x++) {
    //     max7219_clear(&display);

    //     max7219_set_pixel(&display, x, x % 8, true);

    //     max7219_refresh(&display);
    //     vTaskDelay(pdMS_TO_TICKS(80));
    // }

    // max7219_clear(&display);
    // max7219_set_pixel(&display, 7, 3, true);   // last col of matrix 1
    // max7219_set_pixel(&display, 8, 3, true);   // first col of matrix 2
    // max7219_set_pixel(&display, 9, 3, true);   // second col of matrix 2
    // max7219_refresh(&display);
    // vTaskDelay(pdMS_TO_TICKS(2000));

    // max7219_clear(&display);
    // max7219_draw_text(&display, 0, "HEJ");
    // max7219_refresh(&display);
    // vTaskDelay(pdMS_TO_TICKS(2000));

    // max7219_clear(&display);
    // max7219_draw_text(&display, 0, "1234");
    // max7219_refresh(&display);
    // vTaskDelay(pdMS_TO_TICKS(2000));

    // max7219_clear(&display);
    // max7219_draw_text(&display, 0, "ESP32");
    // max7219_refresh(&display);
    // vTaskDelay(pdMS_TO_TICKS(2000));

    // max7219_clear(&display);
    // max7219_draw_char(&display, 0, 'H');
    // max7219_draw_char(&display, 6, 'I');
    // max7219_refresh(&display);

    // ESP_ERROR_CHECK(max7219_scroll_text(&display, "abcdABCD", 80));
    // vTaskDelay(pdMS_TO_TICKS(500));

    // ESP_ERROR_CHECK(max7219_scroll_text(&display, "ESP32 WIFI SETUP", 60));
    // vTaskDelay(pdMS_TO_TICKS(500));

    // ESP_ERROR_CHECK(max7219_scroll_text(&display, "HI :)", 80));
    // vTaskDelay(pdMS_TO_TICKS(500));
}
/**
    ## Flow Diagram
    ```
    Boot
    │
    ├─ NVS has credentials?
    │    ├─ YES → Try connecting to STA
    │    │         ├─ Success → Done ✅
    │    │         └─ Fail    → Fall through to AP mode
    │    └─ NO  → Fall through to AP mode
    │
    └─ Start AP + Webserver
        User visits 192.168.4.1
        Fills form → POST /config
        Credentials saved to NVS
        ESP32 restarts → back to top

        ┌─────────────┬────────┬─────────────────────────────┐
        │ Register    │ Addr   │ Purpose                     │
        ├─────────────┼────────┼─────────────────────────────┤
        │ SHUTDOWN    │ 0x0C   │ 0=off, 1=normal operation   │
        │ DECODE_MODE │ 0x09   │ 0x00 = raw LED control      │
        │ INTENSITY   │ 0x0A   │ 0x00–0x0F brightness        │
        │ SCAN_LIMIT  │ 0x0B   │ 0x07 = show all 8 rows      │
        │ DIGIT 0–7   │ 0x01–8 │ one byte = one row of LEDs  │
        │ DISPLAY TEST│ 0x0F   │ 0x01 = all LEDs on (test)   │
        └─────────────┴────────┴─────────────────────────────┘
 */
