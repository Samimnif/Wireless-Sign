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
    if (freq <= 0)
    {
        buzzer_stop(bz);
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return ESP_OK;
    }

    esp_err_t err = buzzer_play_tone(bz, freq, duration_ms);
    if (err != ESP_OK)
    {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(30));
    return ESP_OK;
}

static esp_err_t play_melody(buzzer_t *bz, const int *notes, const int *durations, int count, int gap_ms)
{
    if (bz == NULL || notes == NULL || durations == NULL || count <= 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < count; i++)
    {
        esp_err_t err = play_note(bz, notes[i], durations[i]);
        if (err != ESP_OK)
        {
            return err;
        }

        if (gap_ms > 0)
        {
            vTaskDelay(pdMS_TO_TICKS(gap_ms));
        }
    }

    return ESP_OK;
}

static esp_err_t play_nokia_melody(buzzer_t *bz)
{
    const int notes[] = {
        659, 587, 370, 415,
        554, 494, 294, 330,
        494, 440, 277, 330,
        440
    };

    const int durations[] = {
        120, 120, 240, 240,
        120, 120, 240, 240,
        120, 120, 240, 240,
        300
    };

    return play_melody(bz, notes, durations, 13, 20);
}

static esp_err_t play_samsung_wash_melody(buzzer_t *bz)
{
    const int notes[] = {
        523, 659, 784, 1047,
        784, 1047, 1319, 1047,
        784, 659, 784, 523
    };

    const int durations[] = {
        150, 150, 150, 250,
        150, 150, 250, 150,
        150, 150, 150, 300
    };

    return play_melody(bz, notes, durations, 12, 40);
}

static esp_err_t play_popcorn_melody(buzzer_t *bz)
{
    const int notes[] = {
        784, 698, 784, 587,
        523, 587, 523, 494,
        523, 587, 659, 587,
        659, 784
    };

    const int durations[] = {
        100, 100, 100, 100,
        100, 100, 100, 180,
        100, 100, 100, 100,
        100, 240
    };

    return play_melody(bz, notes, durations, 14, 25);
}

static esp_err_t play_mario_coin_melody(buzzer_t *bz)
{
    const int notes[] = {988, 1319};
    const int durations[] = {80, 180};
    return play_melody(bz, notes, durations, 2, 20);
}

static esp_err_t play_mario_powerup_melody(buzzer_t *bz)
{
    const int notes[] = {523, 659, 784, 1047, 1319, 1568};
    const int durations[] = {70, 70, 70, 70, 70, 140};
    return play_melody(bz, notes, durations, 6, 15);
}

static esp_err_t play_zelda_secret_melody(buzzer_t *bz)
{
    const int notes[] = {784, 740, 622, 440, 415, 659, 831, 1047};
    const int durations[] = {120, 120, 120, 120, 120, 120, 120, 250};
    return play_melody(bz, notes, durations, 8, 25);
}

static esp_err_t play_tetris_melody(buzzer_t *bz)
{
    const int notes[] = {
        659, 494, 523, 587,
        523, 494, 440, 440,
        523, 659, 587, 523,
        494
    };

    const int durations[] = {
        180, 90, 90, 180,
        90, 90, 180, 90,
        90, 180, 90, 90,
        250
    };

    return play_melody(bz, notes, durations, 13, 20);
}

static esp_err_t play_game_over_melody(buzzer_t *bz)
{
    const int notes[] = {784, 698, 659, 587, 523, 392};
    const int durations[] = {120, 120, 120, 120, 160, 300};
    return play_melody(bz, notes, durations, 6, 30);
}

static esp_err_t play_doorbell_melody(buzzer_t *bz)
{
    const int notes[] = {659, 523};
    const int durations[] = {300, 450};
    return play_melody(bz, notes, durations, 2, 80);
}

static esp_err_t play_alarm_melody(buzzer_t *bz)
{
    const int notes[] = {1200, 0, 1200, 0, 1200, 0, 800, 0, 800};
    const int durations[] = {120, 80, 120, 80, 120, 120, 160, 80, 160};
    return play_melody(bz, notes, durations, 9, 0);
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

        case BUZZER_TONE_NOKIA:
            play_nokia_melody(bz);
            break;

        case BUZZER_TONE_SAMSUNG_WASH:
            play_samsung_wash_melody(bz);
            break;

        case BUZZER_TONE_POPCORN:
            play_popcorn_melody(bz);
            break;

        case BUZZER_TONE_MARIO_COIN:
            play_mario_coin_melody(bz);
            break;

        case BUZZER_TONE_MARIO_POWERUP:
            play_mario_powerup_melody(bz);
            break;

        case BUZZER_TONE_ZELDA_SECRET:
            play_zelda_secret_melody(bz);
            break;

        case BUZZER_TONE_TETRIS:
            play_tetris_melody(bz);
            break;

        case BUZZER_TONE_GAME_OVER:
            play_game_over_melody(bz);
            break;

        case BUZZER_TONE_DOORBELL:
            play_doorbell_melody(bz);
            break;

        case BUZZER_TONE_ALARM:
            play_alarm_melody(bz);
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