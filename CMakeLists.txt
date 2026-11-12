cmake_minimum_required(VERSION 3.22)

# Module variant (flash size): idf.py -B build-N8R8 -DVARIANT=N8R8 build adds
# variants/N8R8.defaults and keeps that build's sdkconfig in its build
# directory. Without VARIANT: sdkconfig.defaults (N16R8) and ./sdkconfig.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(DEFINED VARIANT)
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/variants/${VARIANT}.defaults")
        message(FATAL_ERROR "Unknown VARIANT '${VARIANT}', see variants/")
    endif()
    list(APPEND SDKCONFIG_DEFAULTS "variants/${VARIANT}.defaults")
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()

# Firmware version lives in version.txt and ends up in esp_app_desc_t.version
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lgal_doser)
