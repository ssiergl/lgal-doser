idf_component_register(
    SRCS
        "src/safe_init.cpp"
        "src/tmc2209_proto.cpp"
        "src/tmc2209.cpp"
        "src/ramp.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        esp_driver_spi
        esp_driver_uart
        log
    PRIV_REQUIRES
        esp_hal_uart
)
