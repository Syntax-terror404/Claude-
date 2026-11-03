#pragma once
// Fixed-size little-endian binary wire format for feed messages.
//
//  off size field
//   0   2  magic 0x4C42 ("LB")
//   2   1  version (1)
//   3   1  kind: 0=Add 1=Cancel 2=Heartbeat
//   4   8  seq        (for Heartbeat: sender's last seq)
//  12   8  order id   (Add: new order id; Cancel: target id)
//  20   8  price (i64 ticks)
//  28   4  qty
//  32   1  side
//  33   1  order type
//  34   1  time in force
//  35   1  reserved (0)
//  36   4  CRC32 of bytes [0,36)
// total 40 bytes
#include <array>
#include <cstdint>
#include <cstring>

#include "lob/feed.hpp"

namespace lob::wire {

constexpr std::size_t kSize = 40;
constexpr std::uint16_t kMagic = 0x4C42;
constexpr std::uint8_t kVersion = 1;
using Packet = std::array<std::uint8_t, kSize>;

inline std::uint32_t crc32(const std::uint8_t* p, std::size_t n) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

template <class T> inline void put(std::uint8_t* d, T v) {
    for (std::size_t i = 0; i < sizeof(T); ++i) d[i] = std::uint8_t(std::uint64_t(v) >> (8 * i));
}
template <class T> inline T get(const std::uint8_t* d) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) v |= std::uint64_t(d[i]) << (8 * i);
    return T(v);
}

enum class Kind : std::uint8_t { Add = 0, Cancel = 1, Heartbeat = 2 };

struct Decoded {
    Kind kind{Kind::Add};
    Msg msg{};                 // valid for Add / Cancel
    std::uint64_t last_seq{0}; // valid for Heartbeat
};

inline Packet encode_msg(const Msg& m) {
    Packet p{};
    put<std::uint16_t>(&p[0], kMagic);
    p[2] = kVersion;
    p[3] = std::uint8_t(m.kind == Msg::Kind::Add ? Kind::Add : Kind::Cancel);
    put<std::uint64_t>(&p[4], m.seq);
    if (m.kind == Msg::Kind::Add) {
        put<std::uint64_t>(&p[12], m.order.id);
        put<std::int64_t>(&p[20], m.order.price);
        put<std::uint32_t>(&p[28], m.order.qty);
        p[32] = std::uint8_t(m.order.side);
        p[33] = std::uint8_t(m.order.type);
        p[34] = std::uint8_t(m.order.tif);
    } else {
        put<std::uint64_t>(&p[12], m.id);
    }
    put<std::uint32_t>(&p[36], crc32(p.data(), 36));
    return p;
}

inline Packet encode_heartbeat(std::uint64_t last_seq) {
    Packet p{};
    put<std::uint16_t>(&p[0], kMagic);
    p[2] = kVersion;
    p[3] = std::uint8_t(Kind::Heartbeat);
    put<std::uint64_t>(&p[4], last_seq);
    put<std::uint32_t>(&p[36], crc32(p.data(), 36));
    return p;
}

// Returns false on short/corrupt/unknown packets; never reads past `len`.
inline bool decode(const std::uint8_t* d, std::size_t len, Decoded& out) {
    if (len != kSize) return false;
    if (get<std::uint16_t>(d) != kMagic || d[2] != kVersion) return false;
    if (get<std::uint32_t>(d + 36) != crc32(d, 36)) return false;
    if (d[3] > 2 || d[32] > 1 || d[33] > 1 || d[34] > std::uint8_t(TimeInForce::FOK)) return false;
    out = Decoded{};
    out.kind = Kind(d[3]);
    if (out.kind == Kind::Heartbeat) { out.last_seq = get<std::uint64_t>(d + 4); return true; }
    out.msg.seq = get<std::uint64_t>(d + 4);
    if (out.kind == Kind::Add) {
        out.msg.kind = Msg::Kind::Add;
        out.msg.order.id = get<std::uint64_t>(d + 12);
        out.msg.order.price = get<std::int64_t>(d + 20);
        out.msg.order.qty = get<std::uint32_t>(d + 28);
        out.msg.order.side = Side(d[32]);
        out.msg.order.type = OrderType(d[33]);
        out.msg.order.tif = TimeInForce(d[34]);
    } else {
        out.msg.kind = Msg::Kind::Cancel;
        out.msg.id = get<std::uint64_t>(d + 12);
    }
    return true;
}

}  // namespace lob::wire
