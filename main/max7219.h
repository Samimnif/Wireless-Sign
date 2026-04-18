#ifndef MAX7219_H
#define MAX7219_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX7219_REG_NOOP         0x00
#define MAX7219_REG_DIGIT0       0x01
#define MAX7219_REG_DIGIT1       0x02
#define MAX7219_REG_DIGIT2       0x03
#define MAX7219_REG_DIGIT3       0x04
#define MAX7219_REG_DIGIT4       0x05
#define MAX7219_REG_DIGIT5       0x06
#define MAX7219_REG_DIGIT6       0x07
#define MAX7219_REG_DIGIT7       0x08
#define MAX7219_REG_DECODE_MODE  0x09
#define MAX7219_REG_INTENSITY    0x0A
#define MAX7219_REG_SCAN_LIMIT   0x0B
#define MAX7219_REG_SHUTDOWN     0x0C
#define MAX7219_REG_DISPLAY_TEST 0x0F

#define MAX7219_MAX_DEVICES 4
#define MAX7219_HEIGHT 8
#define MAX7219_WIDTH(devices) ((devices) * 8)

typedef struct {
    spi_host_device_t host;
    int mosi_pin;
    int sclk_pin;
    int cs_pin;
    int clock_speed_hz;
    int num_devices;
} max7219_config_t;

typedef struct {
    spi_device_handle_t spi;
    spi_host_device_t host;
    bool initialized;
    int num_devices;
    uint8_t buffer[MAX7219_HEIGHT][MAX7219_MAX_DEVICES * 8];
} max7219_t;

esp_err_t max7219_init(max7219_t *dev, const max7219_config_t *config);
esp_err_t max7219_deinit(max7219_t *dev);

esp_err_t max7219_clear(max7219_t *dev);
esp_err_t max7219_refresh(max7219_t *dev);
esp_err_t max7219_set_intensity(max7219_t *dev, uint8_t intensity);
esp_err_t max7219_display_test(max7219_t *dev, bool enable);
esp_err_t max7219_set_pixel(max7219_t *dev, int x, int y, bool on);
esp_err_t max7219_draw_bitmap8(max7219_t *dev, int x, const uint8_t bitmap[8]);
esp_err_t max7219_draw_char(max7219_t *dev, int x, char c);
esp_err_t max7219_draw_text(max7219_t *dev, int x, const char *text);
esp_err_t max7219_scroll_text(max7219_t *dev, const char *text, int delay_ms);

#ifdef __cplusplus
}
#endif

#endif