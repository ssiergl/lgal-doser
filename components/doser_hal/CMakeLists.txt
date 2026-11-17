# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

idf_component_register(
    SRCS
        "src/safe_init.cpp"
        "src/tmc2209_proto.cpp"
        "src/tmc2209.cpp"
        "src/ramp.cpp"
        "src/posix_tz.cpp"
        "src/stepgen.cpp"
        "src/motion.cpp"
        "src/i2c_devices.cpp"
        "src/supply_core.cpp"
        "src/supply.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        esp_driver_spi
        esp_driver_uart
        esp_driver_rmt
        esp_driver_mcpwm
        esp_driver_pcnt
        esp_driver_gptimer
        esp_driver_i2c
        esp_adc
        esp_timer
        log
    PRIV_REQUIRES
        esp_hal_mcpwm
        esp_hal_uart
)
