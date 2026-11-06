idf_component_register(
    SRCS
        "src/hwconfig.cpp"
        "src/hwtest_api.cpp"
        "src/dosing_core.cpp"
        "src/dosing_store.cpp"
        "src/dosing.cpp"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES "www/hwtest.html"
    REQUIRES doser_hal lgal_core esp_http_server nvs_flash esp_timer
)
