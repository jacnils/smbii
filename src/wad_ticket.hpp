/* smbii - SMB server for the Nintendo Wii
 * Copyright (c) 2026 Jacob Nilsson / Forwarder Factory
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "wad_parse.hpp"

namespace wad {

inline void sha1(const std::uint8_t* data, std::size_t len, std::uint8_t out[20]) {
    auto rol = [](std::uint32_t v, int s) { return (v << s) | (v >> (32 - s)); };

    std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

    auto block = [&](const std::uint8_t* p) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) w[i] = be32(p + i * 4);
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
            const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    };

    std::size_t i = 0;

    for (; i + 64 <= len; i += 64)
        block(data + i);

    std::uint8_t tail[128] = {};
    const std::size_t rest = len - i;
    std::memcpy(tail, data + i, rest);

    tail[rest] = 0x80;

    const std::size_t tail_len = (rest + 1 + 8 <= 64) ? 64 : 128;
    const std::uint64_t bits = static_cast<std::uint64_t>(len) * 8;

    for (int b = 0; b < 8; ++b)
        tail[tail_len - 1 - b] = static_cast<std::uint8_t>(bits >> (8 * b));

    block(tail);

    if (tail_len == 128)
        block(tail + 64);

    for (int j = 0; j < 5; ++j) {
        out[j * 4 + 0] = static_cast<std::uint8_t>(h[j] >> 24);
        out[j * 4 + 1] = static_cast<std::uint8_t>(h[j] >> 16);
        out[j * 4 + 2] = static_cast<std::uint8_t>(h[j] >> 8);
        out[j * 4 + 3] = static_cast<std::uint8_t>(h[j]);
    }
}

inline constexpr std::size_t ticket_size_rsa2048 = 0x2A4;
inline constexpr std::size_t ticket_body_offset  = 0x140;
inline constexpr std::size_t ticket_body_size    = 0x164;
inline constexpr std::size_t ticket_pad_offset   = 0x122;
inline constexpr std::size_t ticket_ckey_offset  = 0x1F1;

inline bool fakesign_ticket(std::uint8_t* tik) {
    std::memset(tik + 4, 0, 0x100);

    std::uint8_t* body = tik + ticket_body_offset;

    for (std::uint32_t fill = 0; fill < 0xFFFF; ++fill) {
        body[ticket_pad_offset]     = static_cast<std::uint8_t>(fill >> 8);
        body[ticket_pad_offset + 1] = static_cast<std::uint8_t>(fill);

        std::uint8_t hash[20];
        sha1(body, ticket_body_size, hash);
        if (hash[0] == 0) return true;
    }

    return false;
}

inline bool fix_ticket(std::uint8_t* tik, std::size_t size) {
    if (size < ticket_size_rsa2048 || be32(tik) != 0x00010001)
        return false;

    std::uint8_t* ckey = tik + ticket_ckey_offset;
    if (*ckey <= 1)
        return false;

    *ckey = 0;
    return fakesign_ticket(tik);
}

}