// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// Upper bound for the number of pumps of one device. Stored records, backups,
// the API and the web UI are sized for it. A board has 1..kMaxPumps pumps
// (doser/pins.h), and of those only the fitted ones are used
// (doser/hwconfig.h).

namespace doser {

inline constexpr int kMaxPumps = 5;

}  // namespace doser
