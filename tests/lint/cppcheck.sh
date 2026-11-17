#!/bin/sh
# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

# Static analysis of the firmware sources with cppcheck (warnings, performance, portability).
# ESP-IDF itself is not given to it; the board macros are those of d5v1, the sdkconfig
# options those stepgen.cpp requires. Findings checked and
# found false are listed with their reason in cppcheck-suppressions.txt.
#   tests/lint/cppcheck.sh
set -eu
cd "$(dirname "$0")/../.."
cppcheck --version
cppcheck -q -j4 --std=c++20 --enable=warning,performance,portability --error-exitcode=1 \
    --inline-suppr --suppressions-list=tests/lint/cppcheck-suppressions.txt \
    --template='{file}:{line}: {severity} {id}: {message}' \
    '-Dasm(x)=' -DLGAL_BOARD_ID='"d5v1"' -DLGAL_BOARD_MODEL='"LGAL Doser 5"' \
    -DLGAL_BOARD_HEADER='"doser/boards/d5v1.h"' \
    -DCONFIG_RMT_TX_ISR_CACHE_SAFE=1 -DCONFIG_PCNT_ISR_IRAM_SAFE=1 -DCONFIG_PCNT_CTRL_FUNC_IN_IRAM=1 \
    -DCONFIG_GPTIMER_ISR_CACHE_SAFE=1 -DCONFIG_MCPWM_CTRL_FUNC_IN_IRAM=1 \
    -Icomponents/lgal_core/include -Icomponents/doser_hal/include \
    -Icomponents/doser_app/include -Icomponents/webui/include \
    components main
