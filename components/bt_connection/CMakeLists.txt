file(
    GLOB_RECURSE
        SOURCES
            ./*.c
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS
        ./
    REQUIRES
        bt
        bt_common
        bt_scan
        console
        driver
        hal_esp32
)