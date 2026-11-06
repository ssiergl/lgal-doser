cmake_minimum_required(VERSION 3.22)

# Device secret (CONFIG_LGAL_AP_SECRET) lives outside of git in
# sdkconfig.secrets and is read in addition if present.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.secrets")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.secrets")
else()
    message(WARNING "sdkconfig.secrets missing – the default secret is used for "
                    "the setup password. See README, section 'Erstinbetriebnahme'.")
endif()

# Firmware version lives in version.txt and ends up in esp_app_desc_t.version
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lgal_doser)
