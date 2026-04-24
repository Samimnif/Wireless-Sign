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