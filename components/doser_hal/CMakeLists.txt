idf_component_register(
    SRCS "src/safe_init.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_spi
)
