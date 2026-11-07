idf_component_register(
    SRCS "src/webui.cpp"
    INCLUDE_DIRS "include"
    REQUIRES lgal_core esp_http_server
)

# Pack www/ into one gzip blob at build time (pack.py) and embed it
file(GLOB_RECURSE webui_files CONFIGURE_DEPENDS "${COMPONENT_DIR}/www/*")
set(webui_bin "${CMAKE_CURRENT_BINARY_DIR}/webui.bin")
idf_build_get_property(python PYTHON)
add_custom_command(
    OUTPUT "${webui_bin}"
    COMMAND "${python}" "${COMPONENT_DIR}/pack.py" "${COMPONENT_DIR}/www" "${webui_bin}"
    DEPENDS "${COMPONENT_DIR}/pack.py" ${webui_files}
    VERBATIM)
target_add_binary_data(${COMPONENT_LIB} "${webui_bin}" BINARY)
