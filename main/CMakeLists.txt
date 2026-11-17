# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES lgal_core doser_hal doser_app webui app_update esp_app_format
)
