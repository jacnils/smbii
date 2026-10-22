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
#include <cstdint>
#include <string>
#include <vector>

#include "crypto.hpp"
#include "auth.hpp"

namespace ntlmv2 {

inline std::string to_utf16le(const std::string& utf8) {
    std::string out;
    std::size_t i = 0;

    auto put16 = [&](std::uint32_t cp) {
        if (cp < 0x10000) {
            out += static_cast<char>(cp & 0xFF);
            out += static_cast<char>((cp >> 8) & 0xFF);
        } else {
            cp -= 0x10000;
            const std::uint32_t hi = 0xD800 + (cp >> 10), lo = 0xDC00 + (cp & 0x3FF);
            out += static_cast<char>(hi & 0xFF);
            out += static_cast<char>((hi >> 8) & 0xFF);
            out += static_cast<char>(lo & 0xFF);
            out += static_cast<char>((lo >> 8) & 0xFF);
        }
    };

    while (i < utf8.size()) {
        const unsigned char c0 = static_cast<unsigned char>(utf8[i]);
        std::uint32_t cp; int n;
        if (c0 < 0x80) { cp = c0; n = 0; }
        else if ((c0 & 0xE0) == 0xC0) { cp = c0 & 0x1F; n = 1; }
        else if ((c0 & 0xF0) == 0xE0) { cp = c0 & 0x0F; n = 2; }
        else if ((c0 & 0xF8) == 0xF0) { cp = c0 & 0x07; n = 3; }
        else { ++i; continue; }

        if (i + n >= utf8.size()) break;
        bool ok = true;
        for (int k = 1; k <= n; ++k) {
            const unsigned char c = static_cast<unsigned char>(utf8[i + k]);
            if ((c & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (c & 0x3F);
        }

        if (!ok) {
            ++i;
            continue;
        }

        put16(cp);

        i += n + 1;
    }
    return out;
}

inline std::string ascii_upper(std::string s) {
    for (char& c : s) if (c >= 'a' && c <= 'z') c -= 32;
    return s;
}

inline std::array<std::uint8_t, 16> nt_hash(const std::string& password_utf8) {
    const std::string u16 = to_utf16le(password_utf8);
    return crypto::md4(reinterpret_cast<const std::uint8_t*>(u16.data()), u16.size());
}

inline std::array<std::uint8_t, 16> ntowfv2(const std::array<std::uint8_t, 16>& nthash,
                                            const std::string& user, const std::string& domain) {
    const std::string id = to_utf16le(ascii_upper(user) + domain);
    return crypto::hmac_md5(nthash.data(), nthash.size(),
                            reinterpret_cast<const std::uint8_t*>(id.data()), id.size());
}

struct result {
    bool ok = false;
    std::array<std::uint8_t, 16> session_key{};
};

inline result verify(const ntlm_auth::info& auth, const std::array<std::uint8_t, 16>& nthash,
                     const std::array<std::uint8_t, 8>& challenge) {
    result r;

    if (!auth.valid || auth.anonymous || auth.nt_response.size() < 16)
        return r;

    const auto key = ntowfv2(nthash, auth.user, auth.domain);

    std::vector<std::uint8_t> msg(challenge.begin(), challenge.end());

    msg.insert(msg.end(), auth.nt_response.begin() + 16, auth.nt_response.end());

    const auto proof = crypto::hmac_md5(key.data(), key.size(), msg.data(), msg.size());

    if (!std::equal(proof.begin(), proof.end(), auth.nt_response.begin()))
        return r;

    r.ok = true;
    r.session_key = crypto::hmac_md5(key.data(), key.size(), proof.data(), proof.size());

    return r;
}

}