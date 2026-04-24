idf_component_register(SRCS "buzzer.c" "time_manager.c" "portal.c" "wifi_manager.c" "max7219.c" "main.c"
                    PRIV_REQUIRES esp_wifi esp_netif esp_event 
                                 nvs_flash esp_http_server esp_http_client
                                 esp_driver_spi json esp_driver_ledc
                    INCLUDE_DIRS ".")
