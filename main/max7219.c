#include "max7219.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAX7219";

static const uint8_t glyph_dot[5]        = {0x00, 0x60, 0x60, 0x00, 0x00}; // .
static const uint8_t glyph_comma[5]      = {0x00, 0x80, 0x60, 0x00, 0x00}; // ,
static const uint8_t glyph_colon[5]      = {0x00, 0x36, 0x36, 0x00, 0x00}; // :
static const uint8_t glyph_semicolon[5]  = {0x00, 0x56, 0x36, 0x00, 0x00}; // ;
static const uint8_t glyph_exclam[5]     = {0x00, 0x00, 0x7D, 0x00, 0x00}; // !
static const uint8_t glyph_question[5]   = {0x02, 0x01, 0x51, 0x09, 0x06}; // ?
static const uint8_t glyph_dash[5]       = {0x08, 0x08, 0x08, 0x08, 0x08}; // -
static const uint8_t glyph_underscore[5] = {0x40, 0x40, 0x40, 0x40, 0x40}; // _
static const uint8_t glyph_plus[5]       = {0x08, 0x08, 0x3E, 0x08, 0x08}; // +
static const uint8_t glyph_equal[5]      = {0x14, 0x14, 0x14, 0x14, 0x14}; // =
static const uint8_t glyph_slash[5]      = {0x20, 0x10, 0x08, 0x04, 0x02}; // /
static const uint8_t glyph_backslash[5]  = {0x02, 0x04, 0x08, 0x10, 0x20}; // \\ //
static const uint8_t glyph_pipe[5]       = {0x00, 0x00, 0x7F, 0x00, 0x00}; // |
static const uint8_t glyph_lparen[5]     = {0x00, 0x1C, 0x22, 0x41, 0x00}; // (
static const uint8_t glyph_rparen[5]     = {0x00, 0x41, 0x22, 0x1C, 0x00}; // )
static const uint8_t glyph_lbracket[5]   = {0x00, 0x7F, 0x41, 0x41, 0x00}; // [
static const uint8_t glyph_rbracket[5]   = {0x00, 0x41, 0x41, 0x7F, 0x00}; // ]
static const uint8_t glyph_lbrace[5]     = {0x08, 0x36, 0x41, 0x41, 0x00}; // {
static const uint8_t glyph_rbrace[5]     = {0x00, 0x41, 0x41, 0x36, 0x08}; // }
static const uint8_t glyph_quote[5]      = {0x00, 0x03, 0x00, 0x03, 0x00}; // '
static const uint8_t glyph_dquote[5]     = {0x03, 0x00, 0x03, 0x00, 0x00}; // "
static const uint8_t glyph_hash[5]       = {0x14, 0x7F, 0x14, 0x7F, 0x14}; // #
static const uint8_t glyph_dollar[5]     = {0x24, 0x2A, 0x7F, 0x2A, 0x12}; // $
static const uint8_t glyph_percent[5]    = {0x23, 0x13, 0x08, 0x64, 0x62}; // %
static const uint8_t glyph_amp[5]        = {0x36, 0x49, 0x55, 0x22, 0x50}; // &
static const uint8_t glyph_star[5]       = {0x14, 0x08, 0x3E, 0x08, 0x14}; // *
static const uint8_t glyph_at[5]         = {0x3E, 0x41, 0x5D, 0x55, 0x1E}; // @
static const uint8_t glyph_lt[5]         = {0x08, 0x14, 0x22, 0x41, 0x00}; // <
static const uint8_t glyph_gt[5]         = {0x00, 0x41, 0x22, 0x14, 0x08}; // >
static const uint8_t glyph_caret[5]      = {0x04, 0x02, 0x01, 0x02, 0x04}; // ^
static const uint8_t glyph_tilde[5]      = {0x08, 0x04, 0x08, 0x10, 0x08}; // ~
//static const uint8_t glyph_degree[5]     = {0x06, 0x09, 0x09, 0x06, 0x00}; // °

static const uint8_t font_5x7[][5] = {
    // SPACE (ASCII 32)
    {0x00, 0x00, 0x00, 0x00, 0x00},

    // '0' - '9' (ASCII 48-57)
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9

    // 'A' - 'Z' (ASCII 65-90)
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    // 'a' - 'z'
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a [37]
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x08, 0x14, 0x54, 0x54, 0x3C}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
};

/*
 * Assumptions:
 * - num_devices matrices are chained together
 * - each matrix is 8x8
 * - total display width = num_devices * 8
 * - buffer[y][x] stores one pixel state for row y, column x
 *
 * Physical order note:
 * Many MAX7219 chains appear reversed relative to logical screen order.
 * This implementation reverses device order when transmitting:
 *   tx device 0 <= logical last matrix
 *   tx device N-1 <= logical first matrix
 *
 * If your display appears mirrored by 8x8 blocks, change:
 *   row_bytes[n - 1 - i]
 * to:
 *   row_bytes[i]
 */

static esp_err_t max7219_write_all(max7219_t *dev, uint8_t reg, uint8_t data)
{
    if (dev == NULL || !dev->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (dev->num_devices <= 0 || dev->num_devices > MAX7219_MAX_DEVICES) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t buf[MAX7219_MAX_DEVICES * 2];

    for (int i = 0; i < dev->num_devices; i++) {
        buf[2 * i]     = reg;
        buf[2 * i + 1] = data;
    }

    spi_transaction_t t = {
        .length = dev->num_devices * 16,
        .tx_buffer = buf,
    };

    return spi_device_transmit(dev->spi, &t);
}

static esp_err_t max7219_write_row_all(max7219_t *dev, uint8_t reg, const uint8_t *row_bytes)
{
    if (dev == NULL || !dev->initialized || row_bytes == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (dev->num_devices <= 0 || dev->num_devices > MAX7219_MAX_DEVICES) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t buf[MAX7219_MAX_DEVICES * 2];

    for (int i = 0; i < dev->num_devices; i++) {
        buf[2 * i]     = reg;
        buf[2 * i + 1] = row_bytes[i];
    }

    spi_transaction_t t = {
        .length = dev->num_devices * 16,
        .tx_buffer = buf,
    };

    return spi_device_transmit(dev->spi, &t);
}

esp_err_t max7219_init(max7219_t *dev, const max7219_config_t *config)
{
    if (dev == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->num_devices <= 0 || config->num_devices > MAX7219_MAX_DEVICES) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(dev, 0, sizeof(*dev));
    dev->host = config->host;
    dev->num_devices = config->num_devices;

    spi_bus_config_t buscfg = {
        .mosi_io_num = config->mosi_pin,
        .miso_io_num = -1,
        .sclk_io_num = config->sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = config->clock_speed_hz,
        .mode = 0,
        .spics_io_num = config->cs_pin,
        .queue_size = 1,
    };

    esp_err_t err = spi_bus_initialize(config->host, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    err = spi_bus_add_device(config->host, &devcfg, &dev->spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        spi_bus_free(config->host);
        return err;
    }

    dev->initialized = true;

    err = max7219_write_all(dev, MAX7219_REG_DISPLAY_TEST, 0x00);
    if (err != ESP_OK) return err;

    err = max7219_write_all(dev, MAX7219_REG_DECODE_MODE, 0x00);
    if (err != ESP_OK) return err;

    err = max7219_write_all(dev, MAX7219_REG_SCAN_LIMIT, 0x07);
    if (err != ESP_OK) return err;

    err = max7219_write_all(dev, MAX7219_REG_INTENSITY, 0x05);
    if (err != ESP_OK) return err;

    err = max7219_write_all(dev, MAX7219_REG_SHUTDOWN, 0x01);
    if (err != ESP_OK) return err;

    err = max7219_clear(dev);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "MAX7219 initialized with %d devices", dev->num_devices);
    return ESP_OK;
}

esp_err_t max7219_deinit(max7219_t *dev)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (dev->spi != NULL) {
        spi_bus_remove_device(dev->spi);
        dev->spi = NULL;
    }

    spi_bus_free(dev->host);
    dev->initialized = false;
    dev->num_devices = 0;
    memset(dev->buffer, 0, sizeof(dev->buffer));

    return ESP_OK;
}

esp_err_t max7219_set_intensity(max7219_t *dev, uint8_t intensity)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (intensity > 0x0F) {
        intensity = 0x0F;
    }

    return max7219_write_all(dev, MAX7219_REG_INTENSITY, intensity);
}

esp_err_t max7219_display_test(max7219_t *dev, bool enable)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return max7219_write_all(dev, MAX7219_REG_DISPLAY_TEST, enable ? 0x01 : 0x00);
}

esp_err_t max7219_set_pixel(max7219_t *dev, int x, int y, bool on)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int width = dev->num_devices * 8;

    if (x < 0 || x >= width || y < 0 || y >= 8) {
        return ESP_ERR_INVALID_ARG;
    }

    dev->buffer[y][x] = on ? 1 : 0;
    return ESP_OK;
}

esp_err_t max7219_clear(max7219_t *dev)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int width = dev->num_devices * 8;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < width; x++) {
            dev->buffer[y][x] = 0;
        }
    }

    return max7219_refresh(dev);
}

esp_err_t max7219_refresh(max7219_t *dev)
{
    if (dev == NULL || !dev->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (dev->num_devices <= 0 || dev->num_devices > MAX7219_MAX_DEVICES) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (int row = 0; row < 8; row++) {
        uint8_t row_bytes[MAX7219_MAX_DEVICES] = {0};

        for (int d = 0; d < dev->num_devices; d++) {
            uint8_t b = 0;

            for (int bit = 0; bit < 8; bit++) {
                int x = d * 8 + bit;
                if (dev->buffer[row][x]) {
                    b |= (1U << (7 - bit));
                }
            }

            row_bytes[d] = b;
        }

        esp_err_t err = max7219_write_row_all(dev, MAX7219_REG_DIGIT0 + row, row_bytes);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t max7219_draw_bitmap8(max7219_t *dev, int x, const uint8_t bitmap[8])
{
    if (dev == NULL || bitmap == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int row = 0; row < 8; row++) {
        for (int bit = 0; bit < 8; bit++) {
            bool on = ((bitmap[row] >> (7 - bit)) & 0x01) != 0;
            int px = x + bit;

            if (px >= 0 && px < dev->num_devices * 8) {
                dev->buffer[row][px] = on ? 1 : 0;
            }
        }
    }

    return ESP_OK;
}

static const uint8_t *max7219_get_char_bitmap(char c)
{
    if (c == ' ') return font_5x7[0];

    if (c >= '0' && c <= '9') {
        return font_5x7[1 + (c - '0')];
    }

    if (c >= 'A' && c <= 'Z') {
        return font_5x7[11 + (c - 'A')];
    }

    if (c >= 'a' && c <= 'z') {
        return font_5x7[37 + (c - 'a')];
    }

    switch (c) {
        case '.': return glyph_dot;
        case ',': return glyph_comma;
        case ':': return glyph_colon;
        case ';': return glyph_semicolon;
        case '!': return glyph_exclam;
        case '?': return glyph_question;
        case '-': return glyph_dash;
        case '_': return glyph_underscore;
        case '+': return glyph_plus;
        case '=': return glyph_equal;
        case '/': return glyph_slash;
        case '\\': return glyph_backslash;
        case '|': return glyph_pipe;
        case '(': return glyph_lparen;
        case ')': return glyph_rparen;
        case '[': return glyph_lbracket;
        case ']': return glyph_rbracket;
        case '{': return glyph_lbrace;
        case '}': return glyph_rbrace;
        case '\'': return glyph_quote;
        case '"': return glyph_dquote;
        case '#': return glyph_hash;
        case '$': return glyph_dollar;
        case '%': return glyph_percent;
        case '&': return glyph_amp;
        case '*': return glyph_star;
        case '@': return glyph_at;
        case '<': return glyph_lt;
        case '>': return glyph_gt;
        case '^': return glyph_caret;
        case '~': return glyph_tilde;
        default:  return font_5x7[0];
    }
}

esp_err_t max7219_draw_char(max7219_t *dev, int x, char c)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *glyph = max7219_get_char_bitmap(c);
    if (glyph == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Draw 5 columns, 7 rows
    for (int col = 0; col < 5; col++) {
        uint8_t col_bits = glyph[col];

        for (int row = 0; row < 7; row++) {
            bool on = ((col_bits >> row) & 0x01) != 0;

            int px = x + col;
            int py = row;

            if (px >= 0 && px < dev->num_devices * 8) {
                dev->buffer[py][px] = on ? 1 : 0;
            }
        }
    }

    // spacing column
    int space_col = x + 5;
    if (space_col >= 0 && space_col < dev->num_devices * 8) {
        for (int row = 0; row < 8; row++) {
            dev->buffer[row][space_col] = 0;
        }
    }

    return ESP_OK;
}

esp_err_t max7219_draw_text(max7219_t *dev, int x, const char *text)
{
    if (dev == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int cursor_x = x;

    while (*text) {
        esp_err_t err = max7219_draw_char(dev, cursor_x, *text);
        if (err != ESP_OK) {
            return err;
        }

        cursor_x += 6;
        text++;
    }

    return ESP_OK;
}

static int max7219_text_width(const char *text)
{
    if (text == NULL) {
        return 0;
    }

    int width = 0;
    while (*text) {
        width += 6;   // 5 columns + 1 spacing
        text++;
    }

    return width;
}

esp_err_t max7219_scroll_text(max7219_t *dev, const char *text, int delay_ms)
{
    if (dev == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (delay_ms < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int display_width = dev->num_devices * 8;
    int text_width = max7219_text_width(text);

    // Start fully off-screen to the right
    // End when fully off-screen to the left
    for (int x = display_width; x >= -text_width; x--) {
        esp_err_t err = max7219_clear(dev);
        if (err != ESP_OK) {
            return err;
        }

        err = max7219_draw_text(dev, x, text);
        if (err != ESP_OK) {
            return err;
        }

        err = max7219_refresh(dev);
        if (err != ESP_OK) {
            return err;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    return ESP_OK;
}