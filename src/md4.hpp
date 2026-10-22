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

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crypto {

namespace detail {

using u32 = std::uint32_t;

constexpr u32 f(u32 x, u32 y, u32 z) noexcept {
    return (x & y) | (~x & z);
}

constexpr u32 g(u32 x, u32 y, u32 z) noexcept {
    return (x & y) | (x & z) | (y & z);
}

constexpr u32 h(u32 x, u32 y, u32 z) noexcept {
    return x ^ y ^ z;
}

constexpr u32 rotl(u32 x, unsigned n) noexcept {
    return std::rotl(x, static_cast<int>(n));
}

constexpr u32 load_le32(const std::uint8_t* p) noexcept {
    return static_cast<u32>(p[0])
         | (static_cast<u32>(p[1]) << 8)
         | (static_cast<u32>(p[2]) << 16)
         | (static_cast<u32>(p[3]) << 24);
}

constexpr void store_le32(std::uint8_t* p, u32 x) noexcept {
    p[0] = static_cast<std::uint8_t>(x);
    p[1] = static_cast<std::uint8_t>(x >> 8);
    p[2] = static_cast<std::uint8_t>(x >> 16);
    p[3] = static_cast<std::uint8_t>(x >> 24);
}

inline void transform(
    std::array<u32, 4>& state,
    const std::uint8_t* block) noexcept {

    std::array<u32, 16> x{};

    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = load_le32(block + i * 4);

    u32 a = state[0];
    u32 b = state[1];
    u32 c = state[2];
    u32 d = state[3];

#define MD4_FF(a, b, c, d, k, s) \
    (a) = rotl((a) + f((b), (c), (d)) + x[(k)], (s))

    MD4_FF(a,b,c,d, 0, 3);
    MD4_FF(d,a,b,c, 1, 7);
    MD4_FF(c,d,a,b, 2,11);
    MD4_FF(b,c,d,a, 3,19);
    MD4_FF(a,b,c,d, 4, 3);
    MD4_FF(d,a,b,c, 5, 7);
    MD4_FF(c,d,a,b, 6,11);
    MD4_FF(b,c,d,a, 7,19);
    MD4_FF(a,b,c,d, 8, 3);
    MD4_FF(d,a,b,c, 9, 7);
    MD4_FF(c,d,a,b,10,11);
    MD4_FF(b,c,d,a,11,19);
    MD4_FF(a,b,c,d,12, 3);
    MD4_FF(d,a,b,c,13, 7);
    MD4_FF(c,d,a,b,14,11);
    MD4_FF(b,c,d,a,15,19);

#undef MD4_FF

#define MD4_GG(a, b, c, d, k, s) \
    (a) = rotl((a) + g((b), (c), (d)) + x[(k)] + 0x5A827999u, (s))

    MD4_GG(a,b,c,d, 0, 3);
    MD4_GG(d,a,b,c, 4, 5);
    MD4_GG(c,d,a,b, 8, 9);
    MD4_GG(b,c,d,a,12,13);
    MD4_GG(a,b,c,d, 1, 3);
    MD4_GG(d,a,b,c, 5, 5);
    MD4_GG(c,d,a,b, 9, 9);
    MD4_GG(b,c,d,a,13,13);
    MD4_GG(a,b,c,d, 2, 3);
    MD4_GG(d,a,b,c, 6, 5);
    MD4_GG(c,d,a,b,10, 9);
    MD4_GG(b,c,d,a,14,13);
    MD4_GG(a,b,c,d, 3, 3);
    MD4_GG(d,a,b,c, 7, 5);
    MD4_GG(c,d,a,b,11, 9);
    MD4_GG(b,c,d,a,15,13);

#undef MD4_GG

#define MD4_HH(a, b, c, d, k, s) \
    (a) = rotl((a) + h((b), (c), (d)) + x[(k)] + 0x6ED9EBA1u, (s))

    MD4_HH(a,b,c,d, 0, 3);
    MD4_HH(d,a,b,c, 8, 9);
    MD4_HH(c,d,a,b, 4,11);
    MD4_HH(b,c,d,a,12,15);
    MD4_HH(a,b,c,d, 2, 3);
    MD4_HH(d,a,b,c,10, 9);
    MD4_HH(c,d,a,b, 6,11);
    MD4_HH(b,c,d,a,14,15);
    MD4_HH(a,b,c,d, 1, 3);
    MD4_HH(d,a,b,c, 9, 9);
    MD4_HH(c,d,a,b, 5,11);
    MD4_HH(b,c,d,a,13,15);
    MD4_HH(a,b,c,d, 3, 3);
    MD4_HH(d,a,b,c,11, 9);
    MD4_HH(c,d,a,b, 7,11);
    MD4_HH(b,c,d,a,15,15);

#undef MD4_HH

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

}

}
