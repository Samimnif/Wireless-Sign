#include "buzzer.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "BUZZER";

#define BUZZER_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER       LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BUZZER_DUTY_RES         LEDC_TIMER_10_BIT
#define BUZZER_DEFAULT_FREQ_HZ  2000
#define BUZZER_DEFAULT_DUTY     512

esp_err_t buzzer_init(buzzer_t *buzzer, int gpio_num)
{
    if (buzzer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    buzzer->gpio_num = gpio_num;
    buzzer->ledc_channel = BUZZER_LEDC_CHANNEL;
    buzzer->ledc_timer = BUZZER_LEDC_TIMER;
    buzzer->duty_resolution = BUZZER_DUTY_RES;
    buzzer->default_frequency = BUZZER_DEFAULT_FREQ_HZ;
    buzzer->initialized = false;

    ledc_timer_config_t timer_conf = {
        .speed_mode = BUZZER_LEDC_MODE,
        .timer_num = BUZZER_LEDC_TIMER,
        .duty_resolution = BUZZER_DUTY_RES,
        .freq_hz = BUZZER_DEFAULT_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t channel_conf = {
        .gpio_num = gpio_num,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };

    err = ledc_channel_config(&channel_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return err;
    }

    buzzer->initialized = true;
    ESP_LOGI(TAG, "Buzzer initialized on GPIO %d", gpio_num);

    return ESP_OK;
}

esp_err_t buzzer_start(buzzer_t *buzzer, uint32_t freq_hz)
{
    if (buzzer == NULL || !buzzer->initialized || freq_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ledc_set_freq(
        BUZZER_LEDC_MODE,
        BUZZER_LEDC_TIMER,
        freq_hz
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_set_freq failed: %s", esp_err_to_name(err));
        return err;
    }

    err = ledc_set_duty(
        BUZZER_LEDC_MODE,
        BUZZER_LEDC_CHANNEL,
        BUZZER_DEFAULT_DUTY
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_set_duty failed: %s", esp_err_to_name(err));
        return err;
    }

    return ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

esp_err_t buzzer_stop(buzzer_t *buzzer)
{
    if (buzzer == NULL || !buzzer->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ledc_set_duty(
        BUZZER_LEDC_MODE,
        BUZZER_LEDC_CHANNEL,
        0
    );

    if (err != ESP_OK) {
        return err;
    }

    return ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

esp_err_t buzzer_play_tone(buzzer_t *buzzer, uint32_t freq_hz, uint32_t duration_ms)
{
    esp_err_t err = buzzer_start(buzzer, freq_hz);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    return buzzer_stop(buzzer);
}

esp_err_t buzzer_beep(buzzer_t *buzzer, uint32_t duration_ms)
{
    if (buzzer == NULL || !buzzer->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    return buzzer_play_tone(buzzer, buzzer->default_frequency, duration_ms);
}

esp_err_t buzzer_deinit(buzzer_t *buzzer)
{
    if (buzzer == NULL || !buzzer->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    buzzer_stop(buzzer);
    buzzer->initialized = false;
    return ESP_OK;
}

static esp_err_t play_note(buzzer_t *bz, int freq, int duration_ms)
{
    ESP_ERROR_CHECK(buzzer_play_tone(bz, freq, duration_ms));
    vTaskDelay(pdMS_TO_TICKS(30));
    return ESP_OK;
}

esp_err_t buzzer_play_pattern(buzzer_t *bz, buzzer_tone_t tone)
{
    if (bz == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (tone)
    {
        case BUZZER_TONE_SUCCESS:
            play_note(bz, 800, 120);
            play_note(bz, 1200, 150);
            break;

        case BUZZER_TONE_ERROR:
            play_note(bz, 1200, 150);
            play_note(bz, 500, 250);
            break;

        case BUZZER_TONE_WARNING:
            play_note(bz, 1000, 100);
            play_note(bz, 1000, 100);
            play_note(bz, 1000, 100);
            break;

        case BUZZER_TONE_BOOT:
            play_note(bz, 500, 100);
            play_note(bz, 800, 100);
            play_note(bz, 1200, 150);
            break;

        case BUZZER_TONE_WIFI_CONNECTED:
            play_note(bz, 900, 80);
            play_note(bz, 1300, 120);
            break;

        case BUZZER_TONE_WIFI_FAILED:
            play_note(bz, 700, 150);
            play_note(bz, 400, 250);
            break;

        case BUZZER_TONE_MESSAGE:
            play_note(bz, 1400, 120);
            break;

        case BUZZER_TONE_NOTIFICATION:
        default:
            play_note(bz, 1000, 100);
            break;
    }

    return ESP_OK;
}

esp_err_t buzzer_play_custom(
    buzzer_t *bz,
    const int *freqs,
    const int *durations,
    int count)
{
    if (!bz || !freqs || !durations || count <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < count; i++) {
        buzzer_play_tone(bz, freqs[i], durations[i]);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return ESP_OK;
}