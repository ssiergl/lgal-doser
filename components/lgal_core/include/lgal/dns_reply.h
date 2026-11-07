#pragma once
// DNS answers for the captive portal of the setup AP: every name resolves
// to the device. No ESP dependencies (host tests: tests/host/test_dns.cpp).

#include <cstddef>
#include <cstdint>

namespace lgal::dns {

inline constexpr size_t kMaxPacket = 512;
inline constexpr uint32_t kTtl = 60;

// Reply to the standard query q (one question). A/IN (or ANY class) gets
// one answer with ip (host byte order); other types an empty NOERROR reply
// (so clients do not wait for AAAA). Authority/additional records of the
// query (e.g. EDNS) are not echoed. Returns the reply length, 0 = no reply
// (malformed, a response, not a standard query, more than one question, or
// cap too small).
size_t answer(const uint8_t* q, size_t len, uint32_t ip, uint8_t* out, size_t cap);

}  // namespace lgal::dns
