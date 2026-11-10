idf_component_register(
    SRCS
        "src/alarms.cpp"
        "src/history_core.cpp"
        "src/history.cpp"
        "src/history_json.cpp"
        "src/history_api.cpp"
        "src/backup_json.cpp"
        "src/backup_api.cpp"
        "src/ha_core.cpp"
        "src/event_cursor.cpp"
        "src/mqtt_ha.cpp"
        "src/hwconfig.cpp"
        "src/hwtest_api.cpp"
        "src/engineering_api.cpp"
        "src/dosing_core.cpp"
        "src/dosing_store.cpp"
        "src/dosing.cpp"
        "src/dosing_api.cpp"
        "src/schedule_core.cpp"
        "src/schedule_store.cpp"
        "src/scheduler.cpp"
        "src/schedule_json.cpp"
        "src/schedule_api.cpp"
    INCLUDE_DIRS "include"
    REQUIRES doser_hal lgal_core esp_http_server nvs_flash esp_timer esp_app_format
    PRIV_REQUIRES littlefs esp_driver_tsens
)
