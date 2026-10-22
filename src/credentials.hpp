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
#include <cstdio>
#include <string>

#include "ntlmv2.hpp"

namespace credentials {

    struct info {
        bool enabled = false;
        std::string user;
        std::array<std::uint8_t, 16> nt_hash{};
        bool has_password = false;
    };

    inline std::string to_hex(const std::uint8_t* d, std::size_t n) {
        static const char* h = "0123456789abcdef";
        std::string s;
        s.reserve(n * 2);
        for (std::size_t i = 0; i < n; ++i) { s += h[d[i] >> 4]; s += h[d[i] & 0xF]; }
        return s;
    }

    inline bool from_hex(const std::string& s, std::array<std::uint8_t, 16>& out) {
        if (s.size() != 32) return false;
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (std::size_t i = 0; i < 16; ++i) {
            const int hi = nib(s[i * 2]), lo = nib(s[i * 2 + 1]);
            if (hi < 0 || lo < 0) return false;
            out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
        }
        return true;
    }

    inline void set_password(info& c, std::string& pw) {
        c.nt_hash = ntlmv2::nt_hash(pw);
        c.has_password = true;
        for (char& ch : pw) ch = '\0';
        pw.clear();
    }

}