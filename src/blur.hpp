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

#include <grrlib.h>

#include <algorithm>
#include <vector>

namespace gfx {

inline void blur_line(u32* d, int n, int stride, int r, bool wrap, std::vector<u32>& tmp) {
    auto at = [&](int i) -> u32 {
        if (wrap) { i %= n; if (i < 0) i += n; }
        else      { i = std::max(0, std::min(i, n - 1)); }
        return d[i * stride];
    };

    int sr = 0, sg = 0, sb = 0, sa = 0;
    for (int i = -r; i <= r; ++i) {
        const u32 p = at(i);
        sr += p >> 24; sg += (p >> 16) & 255; sb += (p >> 8) & 255; sa += p & 255;
    }

    const int div = 2 * r + 1;
    for (int i = 0; i < n; ++i) {
        tmp[i] = (static_cast<u32>(sr / div) << 24) | (static_cast<u32>(sg / div) << 16) |
                 (static_cast<u32>(sb / div) << 8)  |  static_cast<u32>(sa / div);

        const u32 add = at(i + r + 1), sub = at(i - r);

        sr += static_cast<int>(add >> 24)         - static_cast<int>(sub >> 24);
        sg += static_cast<int>((add >> 16) & 255) - static_cast<int>((sub >> 16) & 255);
        sb += static_cast<int>((add >> 8) & 255)  - static_cast<int>((sub >> 8) & 255);
        sa += static_cast<int>(add & 255)         - static_cast<int>(sub & 255);
    }

    for (int i = 0; i < n; ++i)
        d[i * stride] = tmp[i];
}

inline void blur_pixels(std::vector<u32>& px, int w, int h, int radius, bool wrap) {
    int r = std::min(radius, 32);

    r = std::min(r, (std::min(w, h) - 1) / 2);

    if (r < 1)
        return;

    std::vector<u32> tmp(std::max(w, h));

    for (int it = 0; it < 2; ++it) {
        for (int y = 0; y < h; ++y)
            blur_line(&px[y * w], w, 1, r, wrap, tmp);

        for (int x = 0; x < w; ++x)
            blur_line(&px[x], h, w, r, wrap, tmp);
    }
}

inline bool blur_supported(const GRRLIB_texImg* t) {
    return t && t->w >= 8 && t->h >= 8 && (t->w % 4) == 0 && (t->h % 4) == 0;
}

inline std::vector<u32> read_pixels(GRRLIB_texImg* t) {
    const int w = static_cast<int>(t->w), h = static_cast<int>(t->h);
    std::vector<u32> px(static_cast<size_t>(w) * h);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            px[static_cast<size_t>(y) * w + x] = GRRLIB_GetPixelFromtexImg(x, y, t);

    return px;
}

inline void write_pixels(GRRLIB_texImg* t, const std::vector<u32>& px) {
    const int w = static_cast<int>(t->w), h = static_cast<int>(t->h);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            GRRLIB_SetPixelTotexImg(x, y, t, px[static_cast<size_t>(y) * w + x]);

    GRRLIB_FlushTex(t);
}

inline bool blur_in_place(GRRLIB_texImg* t, int radius, bool wrap) {
    if (!blur_supported(t))
        return false;

    auto px = read_pixels(t);

    blur_pixels(px, static_cast<int>(t->w), static_cast<int>(t->h), radius, wrap);
    write_pixels(t, px);

    return true;
}

inline GRRLIB_texImg* blurred_copy(GRRLIB_texImg* src, int radius, bool wrap) {
    if (!blur_supported(src))
        return nullptr;

    GRRLIB_texImg* dst = GRRLIB_CreateEmptyTexture(src->w, src->h);

    if (!dst)
        return nullptr;

    auto px = read_pixels(src);

    blur_pixels(px, static_cast<int>(src->w), static_cast<int>(src->h), radius, wrap);
    write_pixels(dst, px);

    return dst;
}

}