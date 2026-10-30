idf_component_register(SRCS "log_store.c"
                       INCLUDE_DIRS "include"
                       REQUIRES cocis_types cocis_log_chain storage nvs_flash)
