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

// Offset in the image: image header (24) + first segment header (8) +
// esp_app_desc_t (256); ota.cpp checks it against the ESP-IDF types.
constexpr size_t kBoardDescOffset = 24 + 8 + 256;

// true for 1..kMaxBoardId characters a-z, 0-9
bool valid_board_id(const char* id);
// Board ID of the descriptor at data (len bytes available), "" if there is
// none or it is invalid (an image built before board IDs existed).
std::string image_board(const uint8_t* data, size_t len);

}  // namespace lgal::ota
