cmake_minimum_required(VERSION 3.22)

# Firmware version lives in version.txt and ends up in esp_app_desc_t.version
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lgal_doser)
