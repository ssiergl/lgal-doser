#include "lgal/ota_image.h"

#include <cstring>

namespace lgal::ota {

bool valid_board_id(const char* id) {
    size_t n = 0;
    for (; id[n]; ++n) {
        const char c = id[n];
        if (n >= kMaxBoardId || !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
    }
    return n > 0;
}

std::string image_board(const uint8_t* data, size_t len) {
    if (!data || len < sizeof(BoardDesc)) return {};
    BoardDesc d;
    std::memcpy(&d, data, sizeof(d));
    if (d.magic != kBoardDescMagic || !std::memchr(d.board, 0, sizeof(d.board)) || !valid_board_id(d.board)) return {};
    return d.board;
}

}  // namespace lgal::ota
