#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_SAVED_NETWORKS 5

void wifi_init(void);
void wifi_connect_sta(const char *ssid, const char *pass);

bool wifi_wait_connected(uint32_t timeout_ms);
bool wifi_is_connected(void);

void nvs_save_credentials(const char *ssid, const char *pass);
bool nvs_load_credentials(char *ssid, size_t ssid_size, char *pass, size_t pass_size);

#define WIFI_MAX_SAVED_NETWORKS 5

bool nvs_add_credentials(const char *ssid, const char *pass);
bool nvs_load_credentials_at(int index, char *ssid, size_t ssid_size, char *pass, size_t pass_size);
int nvs_get_credentials_count(void);
bool wifi_connect_saved_networks(uint32_t timeout_per_network_ms);

#ifdef __cplusplus
}
#endif

#endif