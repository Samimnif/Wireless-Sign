#include "time_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "TIME";
static bool sntp_initialized = false;

bool time_is_valid(void)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    return timeinfo.tm_year >= (2024 - 1900);
}

void time_sync_init(void)
{
    if (!sntp_initialized)
    {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_netif_sntp_init(&config);
        sntp_initialized = true;
    }

    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK)
    {
        ESP_LOGW(TAG, "SNTP sync timeout");
    }

    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();
}

bool get_current_time_string(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0)
    {
        return false;
    }

    if (!time_is_valid())
    {
        snprintf(out, out_size, "Time not set");
        return false;
    }

    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    strftime(out, out_size, "%Y-%m-%d %H:%M:%S", &timeinfo);
    return true;
}

time_t get_current_unix_time(void)
{
    time_t now;
    time(&now);
    return now;
}

// bool get_current_hhmm(char *out, size_t out_size)
// {
//     if (out == NULL || out_size == 0) {
//         return false;
//     }

//     if (!time_is_valid()) {
//         snprintf(out, out_size, "--:--");
//         return false;
//     }

//     time_t now;
//     struct tm timeinfo;

//     time(&now);
//     localtime_r(&now, &timeinfo);

//     strftime(out, out_size, "%H:%M", &timeinfo);
//     return true;
// }

bool get_current_hhmm(char *buf,
                      size_t len,
                      int utc_offset_hours,
                      bool use_24h,
                      bool *is_pm_out)
{
    if (buf == NULL || len < 6)
    {
        return false;
    }

    time_t now;
    time(&now);
    now += (utc_offset_hours * 3600);

    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);

    if (is_pm_out)
    {
        *is_pm_out = timeinfo.tm_hour >= 12;
    }

    if (use_24h)
    {
        snprintf(buf, len, "%02d:%02d",
                 timeinfo.tm_hour,
                 timeinfo.tm_min);
    }
    else
    {
        int hour12 = timeinfo.tm_hour % 12;
        if (hour12 == 0) hour12 = 12;

        snprintf(buf, len, "%02d:%02d",
                 hour12,
                 timeinfo.tm_min);
    }

    return true;
}