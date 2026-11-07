idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES lgal_core doser_hal doser_app webui app_update esp_app_format
)
