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
#include "buzzer.h"

#include "cJSON.h"

#include <time.h>
#include <sys/time.h>
#include "esp_netif_sntp.h"
#include "esp_mac.h"

#define SERVER_URL "http://samislab.tplinkdns.com:10300"
static const char *TAG = "MAIN";

static char g_server_url[128] = SERVER_URL;
static char g_device_id[32];

static buzzer_t g_buzzer;

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

    bool flip_display;
    int scroll_speed_ms;

    bool sound_enabled;
    char sound_mode[16];

    char tone_command[16];
    int tone_id;
} server_config_t;

typedef struct
{
    char current_time[6];
    server_config_t server_cfg;
} app_state_t;

static app_state_t g_state;
static SemaphoreHandle_t g_state_mutex;

static max7219_t g_display;

void make_device_id(char *out, size_t out_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(out, out_size,
             "matrix-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void play_named_tone(const char *tone)
{
    if (tone == NULL)
        return;

    if (strcmp(tone, "success") == 0)
    {
        buzzer_play_pattern(&g_buzzer, BUZZER_TONE_SUCCESS);
    }
    else if (strcmp(tone, "error") == 0)
    {
        buzzer_play_pattern(&g_buzzer, BUZZER_TONE_ERROR);
    }
    else if (strcmp(tone, "warning") == 0)
    {
        buzzer_play_pattern(&g_buzzer, BUZZER_TONE_WARNING);
    }
    else if (strcmp(tone, "boot") == 0)
    {
        buzzer_play_pattern(&g_buzzer, BUZZER_TONE_BOOT);
    }
    else if (strcmp(tone, "wifi") == 0)
    {
        buzzer_play_pattern(&g_buzzer, BUZZER_TONE_WIFI_CONNECTED);
    }
    else if (strcmp(tone, "message") == 0)
    {
        buzzer_play_pattern(&g_buzzer, BUZZER_TONE_MESSAGE);
    }
    else
    {
        buzzer_play_pattern(&g_buzzer, BUZZER_TONE_NOTIFICATION);
    }
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

esp_err_t ack_tone(int tone_id)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/api/device/%s/tone_ack",
             g_server_url, g_device_id);

    char post_data[64];
    snprintf(post_data, sizeof(post_data),
             "{\"tone_id\":%d}", tone_id);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return ESP_FAIL;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
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
    cJSON *flip_display = cJSON_GetObjectItem(root, "flip_display");
    cJSON *sound_enabled = cJSON_GetObjectItem(root, "sound_enabled");
    cJSON *sound_mode = cJSON_GetObjectItem(root, "sound_mode");
    cJSON *tone_command = cJSON_GetObjectItem(root, "tone_command");
    cJSON *tone_id = cJSON_GetObjectItem(root, "tone_id");

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
    cfg->flip_display = cJSON_IsBool(flip_display) ? cJSON_IsTrue(flip_display) : false;

    cfg->sound_enabled = cJSON_IsBool(sound_enabled) ? cJSON_IsTrue(sound_enabled) : true;

    if (cJSON_IsString(sound_mode) && sound_mode->valuestring)
    {
        strncpy(cfg->sound_mode, sound_mode->valuestring, sizeof(cfg->sound_mode) - 1);
        cfg->sound_mode[sizeof(cfg->sound_mode) - 1] = '\0';
    }
    else
    {
        strcpy(cfg->sound_mode, "message");
    }

    cfg->tone_id = cJSON_IsNumber(tone_id) ? tone_id->valueint : 0;

    if (cJSON_IsString(tone_command) && tone_command->valuestring)
    {
        strncpy(cfg->tone_command, tone_command->valuestring, sizeof(cfg->tone_command) - 1);
        cfg->tone_command[sizeof(cfg->tone_command) - 1] = '\0';
    }
    else
    {
        cfg->tone_command[0] = '\0';
    }

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
        g_state.server_cfg.flip_display = cfg->flip_display;
        g_state.server_cfg.sound_enabled = cfg->sound_enabled;
        g_state.server_cfg.tone_id = cfg->tone_id;

        strncpy(g_state.server_cfg.sound_mode, cfg->sound_mode,
                sizeof(g_state.server_cfg.sound_mode) - 1);
        g_state.server_cfg.sound_mode[sizeof(g_state.server_cfg.sound_mode) - 1] = '\0';

        strncpy(g_state.server_cfg.tone_command, cfg->tone_command,
                sizeof(g_state.server_cfg.tone_command) - 1);
        g_state.server_cfg.tone_command[sizeof(g_state.server_cfg.tone_command) - 1] = '\0';

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
    bool flip_display = false;

    char time_copy[6] = "--:--";
    char message_copy[128] = {0};
    bool has_message = false;
    int brightness = 5;
    bool show_clock = true;

    bool sound_enabled = true;
    char sound_mode[16] = "message";
    bool tone_pending = false;
    char tone_command[16] = "none";
    int tone_id = 0;

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
            flip_display = g_state.server_cfg.flip_display;
            strncpy(message_mode, g_state.server_cfg.message_mode, sizeof(message_mode) - 1);
            message_mode[sizeof(message_mode) - 1] = '\0';
            message_seconds = g_state.server_cfg.message_seconds;
            sound_enabled = g_state.server_cfg.sound_enabled;
            tone_id = g_state.server_cfg.tone_id;

            strncpy(sound_mode, g_state.server_cfg.sound_mode, sizeof(sound_mode) - 1);
            sound_mode[sizeof(sound_mode) - 1] = '\0';

            strncpy(tone_command, g_state.server_cfg.tone_command, sizeof(tone_command) - 1);
            tone_command[sizeof(tone_command) - 1] = '\0';

            xSemaphoreGive(g_state_mutex);
        }

        max7219_set_intensity(display, brightness);
        max7219_set_flip(display, flip_display);

        ESP_LOGI(TAG, "display_task: time='%s' show_clock=%d brightness=%d has_message=%d tone_pending=%d sound_enabled=%d",
                 time_copy, show_clock, brightness, has_message, tone_pending, sound_enabled);

        static int last_played_tone_id = 0;

        if (sound_enabled &&
            tone_id > 0 &&
            tone_id != last_played_tone_id &&
            strlen(tone_command) > 0)
        {
            play_named_tone(tone_command);
            ack_tone(tone_id);
            last_played_tone_id = tone_id;
        }

        if (has_message && strlen(message_copy) > 0)
        {
            buzzer_beep(&g_buzzer, 100);
            max7219_message_arrival_animation(display, 40);
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
            if (!wifi_is_connected() && ((xTaskGetTickCount() / pdMS_TO_TICKS(500)) % 2))
            {
                max7219_set_pixel(display, 30, 0, true); // WIFI disconnect indicator
                max7219_set_pixel(display, 31, 0, true);
                max7219_set_pixel(display, 30, 1, true);
                max7219_set_pixel(display, 31, 1, true);
            }
            max7219_refresh(display);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}

// ─── app_main ─────────────────────────────────────────────────────
void app_main(void)
{
    make_device_id(g_device_id, sizeof(g_device_id));
    ESP_LOGI(TAG, "Device ID: %s", g_device_id);

    buzzer_init(&g_buzzer, 10);

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
    max7219_set_flip(&g_display, false); // upside down
    ESP_LOGI(TAG, "Driver test starting");
    // LOGO - TBD
    max7219_clear(&g_display);
    max7219_draw_text(&g_display, 4, "_SM_");
    max7219_refresh(&g_display);

    // 2. Init WiFi
    wifi_init();

    // 3. Check if credentials exist in NVS
    // char ssid[64], pass[64];
    // if (nvs_load_credentials(ssid, sizeof(ssid), pass, sizeof(pass)))
    // {
    //     ESP_LOGI(TAG, "Found saved credentials for: %s", ssid);
    //     wifi_connect_sta(ssid, pass);

    //     // Wait for result (10 seconds)
    //     if (wifi_wait_connected(10000))
    //     {
    //         ESP_LOGI(TAG, "Connected! No need for AP setup.");

    //         time_sync_init();

    //         // Init shared state
    //         memset(&g_state, 0, sizeof(g_state));
    //         g_state.server_cfg.show_clock = true;
    //         g_state.server_cfg.brightness = 5;
    //         g_state.server_cfg.scroll_speed_ms = 60;
    //         strcpy(g_state.current_time, "--:--");

    //         // Create tasks
    //         xTaskCreate(clock_task, "clock_task", 2048, NULL, 5, NULL);
    //         xTaskCreate(fetch_task, "fetch_task", 6144, NULL, 5, NULL);
    //         xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 5, NULL);
    //         xTaskCreate(display_task, "display_task", 4096, &g_display, 5, NULL);

    //         vTaskDelete(NULL); // optional but clean
    //     }

    //     ESP_LOGW(TAG, "Saved credentials failed. Starting config AP...");
    // }
    // else
    // {
    //     // max7219_scroll_text(&display, "Connect to AP for WIFI setup", 30);
    //     ESP_LOGI(TAG, "No credentials found. Starting config AP...");
    // }

    if (wifi_connect_saved_networks(10000))
    {
        ESP_LOGI(TAG, "Connected to one saved WiFi.");
        buzzer_play_pattern(&g_buzzer, BUZZER_TONE_WIFI_CONNECTED);
        time_sync_init();

        memset(&g_state, 0, sizeof(g_state));

        g_state.server_cfg.show_clock = true;
        g_state.server_cfg.brightness = 5;
        g_state.server_cfg.scroll_speed_ms = 60;

        strcpy(g_state.current_time, "--:--");
        xTaskCreate(clock_task, "clock_task", 2048, NULL, 5, NULL);
        xTaskCreate(fetch_task, "fetch_task", 6144, NULL, 5, NULL);
        xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 5, NULL);
        xTaskCreate(display_task, "display_task", 4096, &g_display, 5, NULL);
        vTaskDelete(NULL);
    }
    else
    {
        buzzer_play_pattern(&g_buzzer, BUZZER_TONE_WIFI_FAILED);
    }

    ESP_LOGW(TAG, "No saved WiFi worked. Starting config portal...");
    // 4. Start webserver for configuration
    portal_start();

    ESP_LOGI(TAG, "Connect to 'Matrix_Config_AP'");
    ESP_LOGI(TAG, "Then open http://192.168.4.1 in your browser");

    while (!wifi_is_connected())
    {
        max7219_scroll_text(&g_display,
                            ":wifi: Connect to 'Matrix_Config_AP' and setup WiFi",
                            35);
    }
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
