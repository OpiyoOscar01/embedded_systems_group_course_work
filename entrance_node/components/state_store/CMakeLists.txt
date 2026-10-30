idf_component_register(SRCS "state_store.c"
                       INCLUDE_DIRS "include"
                       REQUIRES cocis_types cocis_time occupancy auth storage nvs_flash)
