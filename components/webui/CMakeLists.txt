idf_component_register(
    SRCS "src/webui.cpp"
    INCLUDE_DIRS "include"
    REQUIRES lgal_core esp_http_server
)

# Pack www/ into one gzip blob at build time (pack.py) and embed it, together
# with the license texts from the repository root (shown on the maintenance page)
file(GLOB_RECURSE webui_files CONFIGURE_DEPENDS "${COMPONENT_DIR}/www/*")
set(webui_bin "${CMAKE_CURRENT_BINARY_DIR}/webui.bin")
idf_build_get_property(python PYTHON)
idf_build_get_property(project_dir PROJECT_DIR)
set(gpl "${project_dir}/LICENSE")
set(notices "${project_dir}/THIRD-PARTY-NOTICES.txt")
add_custom_command(
    OUTPUT "${webui_bin}"
    COMMAND "${python}" "${COMPONENT_DIR}/pack.py" "${COMPONENT_DIR}/www" "${webui_bin}"
            "licenses/gpl-3.0.txt=${gpl}" "licenses/third-party.txt=${notices}"
    DEPENDS "${COMPONENT_DIR}/pack.py" ${webui_files} "${gpl}" "${notices}"
    VERBATIM)
target_add_binary_data(${COMPONENT_LIB} "${webui_bin}" BINARY)
