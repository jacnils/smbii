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
#include <string>
#include <vector>

namespace wad {

inline std::uint16_t be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}
inline std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}
inline std::uint64_t be64(const std::uint8_t* p) {
    return (static_cast<std::uint64_t>(be32(p)) << 32) | be32(p + 4);
}
inline std::uint64_t align_up(std::uint64_t v, std::uint64_t a) { return (v + a - 1) & ~(a - 1); }

struct content_t {
    std::uint32_t cid = 0;
    std::uint16_t index = 0;
    std::uint16_t type = 0;
    std::uint64_t size = 0;
    std::size_t offset = 0;
    std::uint32_t enc_len = 0;
};

struct info_t {
    std::uint64_t title_id = 0;
    std::uint16_t title_version = 0;

    const std::uint8_t* certs = nullptr; std::uint32_t certs_size = 0;
    const std::uint8_t* crl = nullptr;   std::uint32_t crl_size = 0;
    const std::uint8_t* tik = nullptr;   std::uint32_t tik_size = 0;
    const std::uint8_t* tmd = nullptr;   std::uint32_t tmd_size = 0;

    std::vector<content_t> contents;
    std::uint64_t total_bytes = 0;
};

inline bool is_safe_title(std::uint64_t title_id) {
    const std::uint32_t hi = static_cast<std::uint32_t>(title_id >> 32);
    return hi == 0x00010001 || hi == 0x00010004 || hi == 0x00010005;
}

inline bool parse(const std::uint8_t* w, std::size_t n, info_t& out, std::string& err) {
    auto fail = [&](const char* m) { err = m; return false; };

    out = info_t{};

    if (!w || n < 0x20)
        return fail("No WAD embedded");

    const std::uint32_t hdr_size = be32(w);
    const std::uint16_t type = be16(w + 4);
    if (hdr_size != 0x20 || (type != 0x4973 /* Is */ && type != 0x6962 /* ib */))
        return fail("Embedded file is not a WAD");

    const std::uint32_t certs_size = be32(w + 0x08);
    const std::uint32_t crl_size   = be32(w + 0x0C);
    const std::uint32_t tik_size   = be32(w + 0x10);
    const std::uint32_t tmd_size   = be32(w + 0x14);
    const std::uint32_t data_size  = be32(w + 0x18);

    const std::uint64_t off_certs = align_up(hdr_size, 64);
    const std::uint64_t off_tik   = off_certs + align_up(certs_size, 64) + align_up(crl_size, 64);
    const std::uint64_t off_tmd   = off_tik + align_up(tik_size, 64);
    const std::uint64_t off_data  = off_tmd + align_up(tmd_size, 64);

    if (certs_size == 0 || tik_size == 0 || tmd_size == 0)
        return fail("WAD is missing a section");

    (void)data_size;

    if (off_certs + certs_size > n || off_tik + tik_size > n || off_tmd + tmd_size > n || off_data > n)
        return fail("WAD is truncated");

    out.certs = w + off_certs; out.certs_size = certs_size;

    if (crl_size) {
        const std::uint64_t off_crl = off_certs + align_up(certs_size, 64);
        if (off_crl + crl_size > n) return fail("WAD is truncated");
        out.crl = w + off_crl; out.crl_size = crl_size;
    }

    out.tik   = w + off_tik;   out.tik_size   = tik_size;
    out.tmd   = w + off_tmd;   out.tmd_size   = tmd_size;

    constexpr std::uint32_t tmd_contents_offset = 0xA4;
    std::uint32_t body = 0;
    switch (be32(out.tmd)) {
        case 0x00010000: body = 0x240;
            break;
        case 0x00010001: body = 0x140;
            break;
        case 0x00010002: body = 0x80;
            break;
        default:
            return fail("Unknown TMD signature type");
    }
    if (tmd_size < body + tmd_contents_offset)
        return fail("TMD is too small");

    const std::uint8_t* b = out.tmd + body;

    out.title_id = be64(b + 0x4C);
    out.title_version = be16(b + 0x9C);

    const std::uint16_t num = be16(b + 0x9E);

    if (num == 0 || tmd_size < body + tmd_contents_offset + static_cast<std::uint64_t>(num) * 0x24)
        return fail("TMD content list is invalid");

    std::uint64_t pos = 0;

    for (std::uint16_t i = 0; i < num; ++i) {
        const std::uint8_t* c = b + tmd_contents_offset + static_cast<std::size_t>(i) * 0x24;

        content_t ct;
        ct.cid = be32(c);
        ct.index = be16(c + 4);
        ct.type = be16(c + 6);
        ct.size = be64(c + 8);

        const std::uint64_t enc = align_up(ct.size, 64);
        if (enc > 0xFFFFFFFFull || off_data + pos + align_up(ct.size, 16) > n)
            return fail("WAD content is truncated");

        ct.offset = static_cast<std::size_t>(off_data + pos);
        ct.enc_len = static_cast<std::uint32_t>(enc);
        out.total_bytes += enc;
        pos += enc;
        out.contents.push_back(ct);
    }

    if (!is_safe_title(out.title_id))
        return fail("Refusing: this WAD is a system title");

    return true;
}

}