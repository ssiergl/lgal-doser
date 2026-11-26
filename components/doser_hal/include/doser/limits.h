// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// Upper bounds of one device.

#include <cstdint>

namespace doser {

// Number of pumps. Stored records, backups, the API and the web UI are sized
// for it. A board has 1..kMaxPumps pumps (doser/pins.h), and of those only the
// fitted ones are used (doser/hwconfig.h).
inline constexpr int kMaxPumps = 5;

// Highest pump speed: the Kamoer KPHM100 conveys linearly up to it.
inline constexpr uint16_t kMaxRpm = 600;

}  // namespace doser
