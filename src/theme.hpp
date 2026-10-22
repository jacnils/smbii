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
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <array>

#include <dirent.h>
#include <sys/stat.h>

inline constexpr const char* themes_dir = "sd:/apps/smbii/themes";

struct palette_t {
    uint32_t bg      = 0x0E1620FF;
    uint32_t panel   = 0x1A2634FF;
    uint32_t border  = 0x2C3E52FF;
    uint32_t text    = 0xE8EEF4FF;
    uint32_t dim     = 0x8FA3B8FF;
    uint32_t accent  = 0x4FC3F7FF;
    uint32_t ok      = 0x66BB6AFF;
    uint32_t warn    = 0xFFB74DFF;
    uint32_t primary = 0x2A5F80FF;
};

enum class bg_mode_t {
    stretch,
    tile,
    center,
    scroll
};

struct style_t {
    int radius = 8;
    int button_radius = 6;
    int border = 1;

    bool shadow = true;
    uint32_t shadow_color = 0x00000066;
    int shadow_x = 3, shadow_y = 4;
    int shadow_soft = 2;
    bool button_shadow = false;

    bg_mode_t bg_mode = bg_mode_t::stretch;
    float tile_scale = 1.0f;
    int background_blur = 0;
    int panel_blur = 0;

    int sfx_volume = 100;
    int music_volume = 100;
    bool music_loop = true;

    bool promote_ff = true;
    bool attribution = true;

    bool show_logo = true;

    bool pattern = false;
    int pattern_tile = 120;
    int pattern_padding = 16;
    float pattern_speed = 30.0f;
    std::array<float, 8> pattern_scale = {1, 1, 1, 1, 1, 1, 1, 1};
};

struct theme_info {
    std::string dir;
    std::string name;
    std::string author;
    std::string description;

    bool prioritize = false;
};

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;

    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;

    return s.substr(a, b - a);
}

inline bool parse_color(std::string v, uint32_t& out) {
    v = trim(v);

    if (!v.empty() && v[0] == '#')
        v.erase(0, 1);
    else if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X'))
        v.erase(0, 2);

    if (v.size() != 6 && v.size() != 8)
        return false;

    for (char c : v)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;

    const uint32_t n = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 16));

    out = (v.size() == 6) ? ((n << 8) | 0xFF) : n;

    return true;
}

inline bool parse_bool(std::string v, bool& out) {
    for (char& c : v)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (v == "on" || v == "true" || v == "yes" || v == "1") {
        out = true; return true;
    }

    if (v == "off" || v == "false" || v == "no" || v == "0") {
        out = false; return true;
    }

    return false;
}

inline bool parse_int(const std::string& v, int lo, int hi, int& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str()) return false;
    out = static_cast<int>(std::max<long>(lo, std::min<long>(n, hi)));
    return true;
}

inline bool parse_theme_ini(const std::string& path, theme_info& info, palette_t& pal, style_t& sty) {
    std::ifstream f(path);

    if (!f)
        return false;

    for (std::string line; std::getline(f, line);) {
        line = trim(line);

        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));

        for (char& c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (key == "name")        { info.name = val; continue; }
        if (key == "author")      { info.author = val; continue; }
        if (key == "description") { info.description = val; continue; }

        int n; bool b;

        if      (key == "radius")          { if (parse_int(val, 0, 32, n))   sty.radius = n; continue; }
        else if (key == "button_radius")   { if (parse_int(val, -1, 32, n))  sty.button_radius = n; continue; }
        else if (key == "border_width")    { if (parse_int(val, 0, 4, n))    sty.border = n; continue; }
        else if (key == "shadow")          { if (parse_bool(val, b))         sty.shadow = b; continue; }
        else if (key == "button_shadow")   { if (parse_bool(val, b))         sty.button_shadow = b; continue; }
        else if (key == "shadow_x")        { if (parse_int(val, -16, 16, n)) sty.shadow_x = n; continue; }
        else if (key == "shadow_y")        { if (parse_int(val, -16, 16, n)) sty.shadow_y = n; continue; }
        else if (key == "shadow_soft")     { if (parse_int(val, 0, 6, n))    sty.shadow_soft = n; continue; }
        else if (key == "background_blur") { if (parse_int(val, 0, 32, n))   sty.background_blur = n; continue; }
        else if (key == "panel_blur")      { if (parse_int(val, 0, 32, n))   sty.panel_blur = n; continue; }
        else if (key == "sfx_volume")      { if (parse_int(val, 0, 100, n))  sty.sfx_volume = n; continue; }
        else if (key == "music_volume")    { if (parse_int(val, 0, 100, n))  sty.music_volume = n; continue; }
        else if (key == "music_loop")      { if (parse_bool(val, b))         sty.music_loop = b; continue; }
        else if (key == "__default")       { if (parse_bool(val, b))         info.prioritize = b; continue; }
        else if (key == "show_attribution"){ if (parse_bool(val, b))         sty.attribution = b; continue; }
        else if (key == "show_logo")       { if (parse_bool(val, b))         sty.show_logo = b; continue; }
        else if (key == "show_url")        { if (parse_bool(val, b))         sty.promote_ff = b; continue; }
        else if (key == "tile_scale")      {
            const float s = static_cast<float>(std::atof(val.c_str()));
            if (s > 0.0f) sty.tile_scale = std::max(0.25f, std::min(s, 4.0f));
            continue;
        }
        else if (key == "pattern")         { if (parse_bool(val, b))         sty.pattern = b; continue; }
        else if (key == "pattern_tile")    { if (parse_int(val, 24, 480, n)) sty.pattern_tile = n; continue; }
        else if (key == "pattern_padding") { if (parse_int(val, 0, 120, n))  sty.pattern_padding = n; continue; }
        else if (key == "pattern_speed")   { sty.pattern_speed = static_cast<float>(std::atof(val.c_str())); continue; }
        else if (key.rfind("pattern", 0) == 0 && key.size() == 13 &&
                 key.substr(8) == "_scale" &&
                 key[7] >= '1' && key[7] <= '8') {
            const float sc = static_cast<float>(std::atof(val.c_str()));

            if (sc > 0.0f)
                sty.pattern_scale[static_cast<std::size_t>(key[7] - '1')] =
                    std::min(sc, 4.0f);

            continue;
                 }
        else if (key == "background_mode") {
            std::string m = val;
            for (char& c : m) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (m == "stretch")     sty.bg_mode = bg_mode_t::stretch;
            else if (m == "tile")   sty.bg_mode = bg_mode_t::tile;
            else if (m == "center") sty.bg_mode = bg_mode_t::center;
            else if (m == "scroll") sty.bg_mode = bg_mode_t::scroll;
            continue;
        }

        // colors
        uint32_t c;
        if (!parse_color(val, c))
            continue;

        if      (key == "bg")           pal.bg = c;
        else if (key == "panel")        pal.panel = c;
        else if (key == "border")       pal.border = c;
        else if (key == "text")         pal.text = c;
        else if (key == "dim")          pal.dim = c;
        else if (key == "accent")       pal.accent = c;
        else if (key == "ok")           pal.ok = c;
        else if (key == "warn")         pal.warn = c;
        else if (key == "primary")      pal.primary = c;
        else if (key == "shadow_color") sty.shadow_color = c;
    }

    return true;
}

inline std::string theme_path(const std::string& dir, const char* file) {
    return std::string(themes_dir) + "/" + dir + "/" + file;
}

inline std::vector<theme_info> scan_themes() {
    std::vector<theme_info> found;

    if (DIR* d = opendir(themes_dir)) {
        while (dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name == "." || name == "..") continue;

            struct stat sb;
            const std::string base = std::string(themes_dir) + "/" + name;
            if (stat(base.c_str(), &sb) != 0 || !S_ISDIR(sb.st_mode)) continue;

            theme_info t;
            t.dir = name;
            palette_t unused_pal;
            style_t unused_style;
            if (!parse_theme_ini(base + "/theme.ini", t, unused_pal, unused_style)) continue;
            if (t.name.empty()) t.name = name;
            found.push_back(std::move(t));
        }
        closedir(d);
    }

    std::sort(found.begin(), found.end(),
              [](const theme_info& a, const theme_info& b) { return a.dir < b.dir; });

    std::vector<theme_info> out;

    out.insert(out.end(), found.begin(), found.end());

    return out;
}