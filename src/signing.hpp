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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "crypto.hpp"

namespace signing {

inline constexpr std::size_t flags_offset = 16;
inline constexpr std::size_t signature_offset = 48;
inline constexpr std::size_t signature_len = 16;
inline constexpr std::uint32_t flag_signed = 0x00000008;

inline bool sign(std::vector<std::byte>& msg, const std::array<std::uint8_t, 16>& key) {
    if (msg.size() < signature_offset + signature_len)
        return false;

    msg[flags_offset] |= std::byte{static_cast<unsigned char>(flag_signed & 0xFF)};
    std::fill_n(msg.begin() + static_cast<long>(signature_offset), signature_len, std::byte{0});

    const auto mac = crypto::hmac_sha256(key.data(), key.size(),
                                         reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size());

    for (std::size_t i = 0; i < signature_len; ++i)
        msg[signature_offset + i] = static_cast<std::byte>(mac[i]);

    return true;
}

inline bool verify(const std::byte* data, std::size_t len, const std::array<std::uint8_t, 16>& key) {
    if (len < signature_offset + signature_len)
        return false;

    const auto flags = static_cast<unsigned char>(data[flags_offset]);

    if (!(flags & flag_signed))
        return false;

    std::vector<std::byte> copy(data, data + len);
    std::fill_n(copy.begin() + static_cast<long>(signature_offset), signature_len, std::byte{0});

    const auto mac = crypto::hmac_sha256(key.data(), key.size(),
                                         reinterpret_cast<const std::uint8_t*>(copy.data()), copy.size());

    for (std::size_t i = 0; i < signature_len; ++i)
        if (static_cast<unsigned char>(data[signature_offset + i]) != mac[i]) return false;

    return true;
}

}