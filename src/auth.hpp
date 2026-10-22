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
#include <string>
#include <vector>

namespace ntlm_auth {

struct info {
    bool valid = false;
    bool anonymous = true;
    std::string user;
    std::string domain;
    std::string workstation;
    std::uint32_t flags = 0;

    std::vector<std::uint8_t> nt_response;
};

namespace detail {

inline std::uint16_t u16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
inline std::uint32_t u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

inline void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

inline std::string utf16le_to_utf8(const std::uint8_t* p, std::size_t bytes) {
    std::string out;

    for (std::size_t i = 0; i + 1 < bytes; i += 2) {
        std::uint32_t cp = u16(p + i);

        if (cp >= 0xD800 && cp < 0xDC00 && i + 3 < bytes) {
            const std::uint32_t lo = u16(p + i + 2);

            if (lo >= 0xDC00 && lo < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }

        append_utf8(out, cp);
    }

    return out;
}

struct field {
    std::uint16_t len = 0;
    const std::uint8_t* data = nullptr;
};

inline bool read_field(const std::uint8_t* msg, std::size_t msg_len, std::size_t at, field& f) {
    f.len = u16(msg + at);

    const std::uint32_t off = u32(msg + at + 4);

    if (f.len == 0) {
        f.data = msg;
        return true;
    }

    if (off > msg_len || f.len > msg_len - off)
        return false;

    f.data = msg + off;

    return true;
}

}

inline info parse_authenticate(const std::uint8_t* blob, std::size_t len) {
    using namespace detail;

    info out;

    static const std::uint8_t sig[8] = {'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};

    const std::uint8_t* msg = nullptr;
    for (std::size_t i = 0; blob && i + 8 <= len; ++i) {
        if (std::memcmp(blob + i, sig, 8) == 0) { msg = blob + i; len -= i; break; }
    }
    if (!msg || len < 64 || u32(msg + 8) != 3) return out;

    field lm, nt, domain, user, ws;
    if (!read_field(msg, len, 12, lm) || !read_field(msg, len, 20, nt) ||
        !read_field(msg, len, 28, domain) || !read_field(msg, len, 36, user) ||
        !read_field(msg, len, 44, ws))
        return out;

    if (nt.len >= 16)
        out.nt_response.assign(nt.data, nt.data + nt.len);

    out.flags = u32(msg + 60);
    const bool unicode = (out.flags & 0x00000001) != 0;

    auto text = [&](const field& f) {
        return unicode ? utf16le_to_utf8(f.data, f.len) : std::string(reinterpret_cast<const char*>(f.data), f.len);
    };

    out.user = text(user);
    out.domain = text(domain);
    out.workstation = text(ws);

    const bool anonymous_flag = (out.flags & 0x00000800) != 0;

    out.anonymous = anonymous_flag || (out.user.empty() && nt.len == 0);
    out.valid = true;

    return out;
}

}