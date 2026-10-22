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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "credentials.hpp"
#include "nand_fs.hpp"
#include "usb.hpp"

inline constexpr const char* config_path = "sd:/apps/smbii/settings.ini";

struct settings_t {
    int port{445};
    int fallback_port{1337};
    std::string share_name{"wii"};
    std::filesystem::path share_root{"sd:/"};
    std::string theme;
    bool sfx{true};
    bool music{true};
    bool read_only{false};
    bool auth_enabled{false};
    std::string auth_user;
    std::string auth_nthash_hex;
};

inline credentials::info to_credentials(const settings_t& s) {
    credentials::info c;

    c.enabled = s.auth_enabled;
    c.user = s.auth_user;
    c.has_password = credentials::from_hex(s.auth_nthash_hex, c.nt_hash);

    return c;
}

inline bool is_nand_path(const std::string& p) {
    return p.rfind("nand:", 0) == 0;
}

inline bool is_usb_path(const std::string& p) {
    return p.rfind("usb:", 0) == 0;
}

// for now we cant do write to nand
// might add option later to do this anyway if youre insane
inline bool is_ro(const settings_t& s) {
    return s.read_only || is_nand_path(s.share_root.string());
}

inline settings_t load_settings() {
    settings_t s;

    std::ifstream f(config_path);

    for (std::string line; std::getline(f, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string k = line.substr(0, eq);
        const std::string v = line.substr(eq + 1);

        if (k == "port") {
            const int p = std::atoi(v.c_str());
            if (p >= 1 && p <= 65535) s.port = p;
        } else if (k == "share" && !v.empty()) {
            s.share_name = v;
        } else if (k == "root" && !v.empty()) {
            s.share_root = v;
        } else if (k == "theme") {
            s.theme = v;
        } else if (k == "sfx") {
            s.sfx = v != "0";
        } else if (k == "music") {
            s.music = v != "0";
        } else if (k == "readonly") {
            s.read_only = v == "1";
        } else if (k == "auth_enabled") {
            s.auth_enabled = v == "1";
        } else if (k == "auth_user") {
            s.auth_user = v;
        } else if (k == "auth_nthash") {
            s.auth_nthash_hex = v;
        }
    }

    return s;
}

inline bool save_settings(const settings_t& s) {
    std::ofstream f(config_path, std::ios::trunc);

    if (!f)
        return false;

    f << "port=" << s.port << "\n"
      << "share=" << s.share_name << "\n"
      << "root=" << s.share_root.string() << "\n"
      << "theme=" << s.theme << "\n"
      << "readonly=" << s.read_only << "\n"
      << "sfx=" << (s.sfx ? 1 : 0) << "\n"
      << "music=" << (s.music ? 1 : 0) << "\n"
      << "auth_enabled=" << (s.auth_enabled ? 1 : 0) << "\n"
      << "auth_user=" << s.auth_user << "\n"
      << "auth_nthash=" << s.auth_nthash_hex << "\n";

    return f.good();
}

inline std::string validate(const settings_t& s) {
    if (s.port < 1 || s.port > 65535)
        return "Port must be between 1 and 65535";
    if (s.share_name.empty() || s.share_name.size() > 80)
        return "Share name must be 1-80 characters";
    if (s.share_name.find_first_of("\\/:*?\"<>|") != std::string::npos)
        return "Share name can't contain \\ / : * ? \" < > |";
    if (is_nand_path(s.share_root.string()) && !nand::available())
        return "NAND isn't available";
    if (is_usb_path(s.share_root.string()) && !usb::available())
        return "No USB drive detected";
    if (s.auth_enabled && (s.auth_user.empty() || s.auth_nthash_hex.size() != 32))
        return "Set a username and password before requiring login";

    std::error_code ec;

    if (!std::filesystem::is_directory(s.share_root, ec))
        return "That root folder doesn't exist";

    return {};
}