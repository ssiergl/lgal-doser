#include "lgal/dns_reply.h"

#include <cstring>

namespace lgal::dns {

namespace {
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
void wr16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v >> 8);
    p[1] = uint8_t(v);
}
}  // namespace

size_t answer(const uint8_t* q, size_t len, uint32_t ip, uint8_t* out, size_t cap) {
    if (!q || !out || len < 12) return 0;
    const uint16_t flags = rd16(q + 2);
    if (flags & 0x8000) return 0;              // a response
    if ((flags >> 11) & 0x0F) return 0;        // opcode != QUERY
    if (rd16(q + 4) != 1 || rd16(q + 6) != 0) return 0;  // exactly one question, no answers

    // question name: plain labels only (no compression in a query)
    size_t p = 12, name_len = 0;
    while (true) {
        if (p >= len) return 0;
        const uint8_t l = q[p];
        if (l == 0) break;
        if (l & 0xC0) return 0;
        name_len += l + 1;
        if (name_len > 254) return 0;
        p += 1 + l;
    }
    p += 1;  // terminating zero
    if (p + 4 > len) return 0;
    const uint16_t qtype = rd16(q + p), qclass = rd16(q + p + 2);
    const size_t qend = p + 4;
    const bool a = qtype == 1 && (qclass == 1 || qclass == 255);
    const size_t total = qend + (a ? 16 : 0);
    if (total > cap) return 0;

    std::memcpy(out, q, qend);
    wr16(out + 2, uint16_t(0x8400 | (flags & 0x0100)));  // QR, AA, copy RD; RCODE 0
    wr16(out + 4, 1);
    wr16(out + 6, a ? 1 : 0);
    wr16(out + 8, 0);
    wr16(out + 10, 0);
    if (a) {
        uint8_t* r = out + qend;
        wr16(r, 0xC00C);  // name: pointer to the question
        wr16(r + 2, 1);   // A
        wr16(r + 4, 1);   // IN
        r[6] = uint8_t(kTtl >> 24);
        r[7] = uint8_t(kTtl >> 16);
        r[8] = uint8_t(kTtl >> 8);
        r[9] = uint8_t(kTtl);
        wr16(r + 10, 4);
        r[12] = uint8_t(ip >> 24);
        r[13] = uint8_t(ip >> 16);
        r[14] = uint8_t(ip >> 8);
        r[15] = uint8_t(ip);
    }
    return total;
}

}  // namespace lgal::dns
