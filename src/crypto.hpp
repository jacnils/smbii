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

#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/hmac.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "md4.hpp"

namespace crypto {
    inline std::array<std::uint8_t, 16> md4(
        const std::uint8_t* data,
        std::size_t len) {

        using namespace detail;

        const std::uint64_t bit_len =
            static_cast<std::uint64_t>(len) * 8u;

        std::array<u32, 4> state{
            0x67452301u,
            0xEFCDAB89u,
            0x98BADCFEu,
            0x10325476u
        };

        std::size_t remaining = len;

        while (remaining >= 64) {
            transform(state, data);
            data += 64;
            remaining -= 64;
        }

        std::array<std::uint8_t, 128> tail{};

        if (remaining != 0)
            std::memcpy(tail.data(), data, remaining);

        tail[remaining] = 0x80;

        const std::size_t padded_size =
            remaining < 56 ? 64 : 128;

        for (unsigned i = 0; i < 8; ++i)
            tail[padded_size - 8 + i] =
                static_cast<std::uint8_t>(bit_len >> (i * 8));

        transform(state, tail.data());

        if (padded_size == 128)
            transform(state, tail.data() + 64);

        std::array<std::uint8_t, 16> out{};

        for (std::size_t i = 0; i < state.size(); ++i)
            store_le32(out.data() + i * 4, state[i]);

        return out;
    }

namespace detail {

template <std::size_t N>
inline std::array<std::uint8_t, N> hmac(int type, const std::uint8_t* key, std::size_t key_len,
                                        const std::uint8_t* data, std::size_t len) {
    Hmac ctx;
    if (wc_HmacInit(&ctx, nullptr, INVALID_DEVID) != 0) throw std::runtime_error("crypto: HmacInit failed");
    if (wc_HmacSetKey(&ctx, type, key, static_cast<word32>(key_len)) != 0 ||
        wc_HmacUpdate(&ctx, data, static_cast<word32>(len)) != 0) {
        wc_HmacFree(&ctx);
        throw std::runtime_error("crypto: HMAC failed");
    }
    std::array<std::uint8_t, N> out{};
    const int r = wc_HmacFinal(&ctx, out.data());
    wc_HmacFree(&ctx);
    if (r != 0) throw std::runtime_error("crypto: HmacFinal failed");
    return out;
}

}

inline std::array<std::uint8_t, 16> hmac_md5(const std::uint8_t* key, std::size_t key_len,
                                             const std::uint8_t* data, std::size_t len) {
    return detail::hmac<16>(WC_MD5, key, key_len, data, len);
}

inline std::array<std::uint8_t, 32> hmac_sha256(const std::uint8_t* key, std::size_t key_len,
                                                const std::uint8_t* data, std::size_t len) {
    return detail::hmac<32>(WC_SHA256, key, key_len, data, len);
}

template <class Key, class Data>
inline auto hmac_md5(const Key& key, const Data& data) {
    return hmac_md5(reinterpret_cast<const std::uint8_t*>(key.data()), key.size() * sizeof(*key.data()),
                    reinterpret_cast<const std::uint8_t*>(data.data()), data.size() * sizeof(*data.data()));
}
template <class Key, class Data>
inline auto hmac_sha256(const Key& key, const Data& data) {
    return hmac_sha256(reinterpret_cast<const std::uint8_t*>(key.data()), key.size() * sizeof(*key.data()),
                       reinterpret_cast<const std::uint8_t*>(data.data()), data.size() * sizeof(*data.data()));
}

}