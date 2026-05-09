#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int gpio_num;
    int ledc_channel;
    int ledc_timer;
    int duty_resolution;
    int default_frequency;
    bool initialized;
} buzzer_t;

typedef enum
{
    BUZZER_TONE_NOTIFICATION,
    BUZZER_TONE_SUCCESS,
    BUZZER_TONE_ERROR,
    BUZZER_TONE_WARNING,
    BUZZER_TONE_BOOT,
    BUZZER_TONE_WIFI_CONNECTED,
    BUZZER_TONE_WIFI_FAILED,
    BUZZER_TONE_MESSAGE,
    BUZZER_TONE_CUSTOM
} buzzer_tone_t;

esp_err_t buzzer_play_pattern(buzzer_t *bz, buzzer_tone_t tone);

esp_err_t buzzer_play_custom(
    buzzer_t *bz,
    const int *freqs,
    const int *durations,
    int count
);

/**
 * Initialize buzzer driver.
 */
esp_err_t buzzer_init(buzzer_t *buzzer, int gpio_num);

/**
 * Start playing a tone at the given frequency in Hz.
 */
esp_err_t buzzer_start(buzzer_t *buzzer, uint32_t freq_hz);

/**
 * Stop the buzzer.
 */
esp_err_t buzzer_stop(buzzer_t *buzzer);

/**
 * Play a tone for duration_ms, then stop.
 */
esp_err_t buzzer_play_tone(buzzer_t *buzzer, uint32_t freq_hz, uint32_t duration_ms);

/**
 * Simple beep helper.
 */
esp_err_t buzzer_beep(buzzer_t *buzzer, uint32_t duration_ms);

/**
 * Deinitialize buzzer.
 */
esp_err_t buzzer_deinit(buzzer_t *buzzer);

#ifdef __cplusplus
}
#endif

#endif