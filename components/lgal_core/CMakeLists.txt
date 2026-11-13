# The flags of the language picker are part of the shared web design in the brand
# repository (Git submodule brand/, web/ is GPL-3.0-or-later)
idf_build_get_property(project_dir PROJECT_DIR)
set(flags_js "${project_dir}/brand/web/flags.js")
if(NOT EXISTS "${flags_js}")
    message(FATAL_ERROR "brand/ is empty: run 'git submodule update --init' (or clone with --recursive)")
endif()

idf_component_register(
    SRCS
        "src/led.cpp"
        "src/button.cpp"
        "src/device.cpp"
        "src/device_name.cpp"
        "src/settings.cpp"
        "src/logbuf.cpp"
        "src/net.cpp"
        "src/http.cpp"
        "src/sysapi.cpp"
        "src/ota.cpp"
        "src/ota_manifest.cpp"
        "src/ota_image.cpp"
        "src/json.cpp"
        "src/json_reader.cpp"
        "src/sha256.cpp"
        "src/entropy.cpp"
        "src/events.cpp"
        "src/auth_core.cpp"
        "src/auth.cpp"
        "src/net_config.cpp"
        "src/mqtt_config.cpp"
        "src/mqtt.cpp"
        "src/netapi.cpp"
        "src/dns_reply.cpp"
        "src/captive.cpp"
        "src/setup.cpp"
        "src/ws_core.cpp"
        "src/ws.cpp"
        "src/timesync.cpp"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES "www/recovery.html" "www/i18n.js" "${flags_js}" "www/login.html" "www/setup.html"
    REQUIRES
        bootloader_support
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_spi
        esp_eth
        esp_netif
        esp_event
        esp_wifi
        esp_http_server
        esp_http_client
        esp_app_format
        app_update
        esp_partition
        espcoredump
        nvs_flash
        mbedtls
        esp-tls
        mqtt
        esp_timer
        spi_flash
        log
        lwip
)

# Board descriptor in the image (lgal/ota_image.h): keep it although nothing
# references it
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lgal_board_desc")
