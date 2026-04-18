#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void wifi_init(void);
void wifi_connect_sta(const char *ssid, const char *pass);

bool wifi_wait_connected(uint32_t timeout_ms);
bool wifi_is_connected(void);

void nvs_save_credentials(const char *ssid, const char *pass);
bool nvs_load_credentials(char *ssid, size_t ssid_size, char *pass, size_t pass_size);

#ifdef __cplusplus
}
#endif

#endif