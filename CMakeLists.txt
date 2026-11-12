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

# Board (circuit board with its pins and pump count): idf.py -DBOARD=d5v1 build
# reads boards/d5v1.cmake (model name) and builds against
# components/doser_hal/include/doser/boards/d5v1.h (pins). Board and module
# variant are independent: idf.py -B build-d5v1-N8R8 -DBOARD=d5v1 -DVARIANT=N8R8 build
set(BOARD "d5v1" CACHE STRING "Board the firmware is built for, see boards/")
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}.cmake"
   OR NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/components/doser_hal/include/doser/boards/${BOARD}.h")
    message(FATAL_ERROR "Unknown BOARD '${BOARD}', see boards/")
endif()
unset(LGAL_BOARD_MODEL)
include("${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}.cmake")
if(NOT LGAL_BOARD_MODEL)
    message(FATAL_ERROR "boards/${BOARD}.cmake does not set LGAL_BOARD_MODEL")
endif()

# Firmware version lives in version.txt and ends up in esp_app_desc_t.version
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(COMPILE_DEFINITIONS "LGAL_BOARD_ID=\"${BOARD}\"" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "LGAL_BOARD_MODEL=\"${LGAL_BOARD_MODEL}\"" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "LGAL_BOARD_HEADER=\"doser/boards/${BOARD}.h\"" APPEND)
project(lgal_doser)
message(STATUS "LGAL-Doser board ${BOARD} (${LGAL_BOARD_MODEL})")
