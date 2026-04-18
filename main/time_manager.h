#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

void time_sync_init(void);
bool time_is_valid(void);

bool get_current_time_string(char *out, size_t out_size);
time_t get_current_unix_time(void);
bool get_current_hhmm(char *out, size_t out_size);
//bool get_current_hhmm_offset(char *out, size_t out_size, int utc_offset_hours, bool use_24h)

#ifdef __cplusplus
}
#endif

#endif