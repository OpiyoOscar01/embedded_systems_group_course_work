idf_component_register(SRCS "auth.c" "auth_crypto.c"
                       INCLUDE_DIRS "include"
                       REQUIRES cocis_types mbedtls)
