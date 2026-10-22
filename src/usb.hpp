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

#include <fat.h>
#include <ogc/lwp.h>
#include <ogc/usbstorage.h>

#include <sys/stat.h>
#include <unistd.h>

namespace usb {

namespace detail {

inline volatile bool mounted = false;
inline volatile bool stop = false;
inline bool running = false;
inline lwp_t thread{};

inline bool probe() {
    struct stat st;
    return ::stat("usb:/", &st) == 0;
}


inline bool try_mount() {
    if (probe())
        return true;

    if (!fatMountSimple("usb", &__io_usbstorage))
        return false;

    return probe();
}

inline void* monitor(void*) {
    int tick = 0;

    while (!stop) {
        if (tick % 20 == 0) {
            if (!mounted) {
                if (try_mount())
                    mounted = true;
            } else if (!__io_usbstorage.isInserted()) {
                fatUnmount("usb");
                mounted = false;
            }
        }

        usleep(100 * 1000);

        ++tick;
    }

    return nullptr;
}

}

inline void begin() {
    using namespace detail;

    if (running)
        return;

    stop = false;

    if (LWP_CreateThread(&thread, monitor, nullptr, nullptr, 16 * 1024, 30) == 0)
        running = true;
}

inline bool available() {
    return detail::mounted;
}

inline void shutdown() {
    using namespace detail;

    if (running) {
        stop = true;
        LWP_JoinThread(thread, nullptr);
        running = false;
    }

    if (mounted) {
        fatUnmount("usb");
        mounted = false;
    }
}

}