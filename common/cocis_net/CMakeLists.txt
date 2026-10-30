idf_component_register(SRCS "cocis_net.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mqtt esp_wifi esp_event esp_netif lwip nvs_flash)
