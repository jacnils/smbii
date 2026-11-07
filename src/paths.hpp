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

#include <string>
#include <vector>

#include <sys/stat.h>

#include "usb.hpp"

namespace paths {

inline constexpr const char* app_subdir = "apps/smbii";

namespace detail {

inline std::string launch_device;
inline std::string data_dir_;

inline bool is_dir(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

inline bool is_file(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

inline bool device_ready(const std::string& dev) {
    if (dev == "usb:")
        return usb::available();

    return is_dir(dev + "/");
}

inline void add_unique(std::vector<std::string>& v, const std::string& s) {
    for (const auto& e : v)
        if (e == s) return;

    v.push_back(s);
}

}

inline std::vector<std::string> app_dirs() {
    using namespace detail;

    std::vector<std::string> devs;

    if (!launch_device.empty())
        add_unique(devs, launch_device);

    add_unique(devs, "sd:");
    add_unique(devs, "usb:");

    std::vector<std::string> out;

    for (const auto& d : devs) {
        if (!device_ready(d))
            continue;

        const std::string dir = d + "/" + app_subdir;

        if (is_dir(dir))
            out.push_back(dir);
    }

    return out;
}

inline std::vector<std::string> themes_dirs() {
    std::vector<std::string> out;

    for (const auto& a : app_dirs()) {
        const std::string t = a + "/themes";

        if (detail::is_dir(t))
            out.push_back(t);
    }

    return out;
}

inline std::string data_dir() {
    return detail::data_dir_.empty() ? std::string("sd:/") + app_subdir : detail::data_dir_;
}

inline std::string config_path() {
    return data_dir() + "/settings.ini";
}

inline void init(int argc, char** argv) {
    using namespace detail;

    if (argc > 0 && argv && argv[0]) {
        const std::string a0 = argv[0];

        if (a0.rfind("usb:", 0) == 0)
            launch_device = "usb:";
        else if (a0.rfind("sd:", 0) == 0)
            launch_device = "sd:";
    }

    const bool sd_has_themes = is_dir(std::string("sd:/") + app_subdir + "/themes");

    if (launch_device == "usb:" || !sd_has_themes)
        usb::wait_mounted(6000);

    const auto dirs = app_dirs();

    for (const auto& d : dirs) {
        if (is_file(d + "/settings.ini")) {
            data_dir_ = d;
            return;
        }
    }

    for (const auto& d : dirs) {
        if (is_dir(d + "/themes")) {
            data_dir_ = d;
            return;
        }
    }

    if (!dirs.empty())
        data_dir_ = dirs.front();
}

}
