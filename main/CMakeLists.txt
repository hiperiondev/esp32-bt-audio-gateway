idf_component_register(
    SRCS main.c
    INCLUDE_DIRS
        .
    REQUIRES
        nvs_flash
        bt_common
        bt_scan
        bt_connection
        hal_esp32
)
