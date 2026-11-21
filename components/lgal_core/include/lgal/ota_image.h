// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// Board ID inside a firmware image (pure logic, host-testable:
// tests/host/test_ota_manifest.cpp).
//
// Every image carries a BoardDesc as ESP-IDF custom app descriptor
// (section .rodata_custom_desc), which the linker places right behind
// esp_app_desc_t. An update is accepted only for the board the device was
// built for: lgal/ota.h checks it in the header, before anything is written.

#include <cstddef>
#include <cstdint>
#include <string>

namespace lgal::ota {

constexpr uint32_t kBoardDescMagic = 0x4452424Cu;  // "LBRD"
constexpr size_t kMaxBoardId = 15;                 // [a-z0-9], e.g. "d5v1"

struct BoardDesc {
    uint32_t magic;
    char board[kMaxBoardId + 1];  // NUL-terminated
    uint32_t reserved[3];
};
static_assert(sizeof(BoardDesc) == 32);

// Offset of the description in the first segment of an image: image header
// (24) + first segment header (8). An app has esp_app_desc_t there, a
// bootloader esp_bootloader_desc_t, whose first byte is kBootloaderDescMagic
// (ota.cpp checks offset and magic against the ESP-IDF types).
constexpr size_t kDescOffset = 24 + 8;
constexpr uint8_t kBootloaderDescMagic = 80;  // ESP_BOOTLOADER_DESC_MAGIC_BYTE

// true when the image starts with a bootloader: a complete flash image
// (bootloader, partition table, app; "-full.bin") instead of the app alone.
// data must hold at least kDescOffset + 1 bytes.
bool is_bootloader_image(const uint8_t* data, size_t len);

// Offset in the image: image header (24) + first segment header (8) +
// esp_app_desc_t (256); ota.cpp checks it against the ESP-IDF types.
constexpr size_t kBoardDescOffset = kDescOffset + 256;

// true for 1..kMaxBoardId characters a-z, 0-9
bool valid_board_id(const char* id);
// Board ID of the descriptor at data (len bytes available), "" if there is
// none or it is invalid (an image built before board IDs existed).
std::string image_board(const uint8_t* data, size_t len);

}  // namespace lgal::ota
