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
#include <netkit/tcp/tcp_server.hpp>
#include <netkit/network/network_interface.hpp>

#include <ogc/isfs.h>
#include <ogc/disc_io.h>
#include <ogc/usbstorage.h>
#include <sdcard/wiisd_io.h>

#include <ogc/system.h>
#include <wiiuse/wpad.h>
#include <gccore.h>
#include <network.h>
#include <fat.h>
#include <ogc/lwp.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <unistd.h>

#include "connection.hpp"
#include "settings.hpp"
#include "gui.hpp"

extern "C" {
#include "libpatcher.h"
}

#include <cstring>
#include <string>
#include <filesystem>

static lwp_t server_thread_id;
alignas(32) static u8 server_stack[64 * 1024];

static bool restart_requested() {
    gui::guard g;
    return gui::st.restart;
}

static void kick_server() {
    std::string ip;
    int port;
    
    {
        gui::guard g;
        if (!gui::st.listening) 
            return;
        
        ip = gui::st.ip;
        port = gui::st.port;
    }

    if (ip == "no network")
        ip = "127.0.0.1";

    const int32_t sock = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    
    if (sock < 0) 
        return;

    sockaddr_in sa{};

    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<u16>(port));

    inet_aton(ip.c_str(), &sa.sin_addr);

    net_connect(sock, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    net_close(sock);
}

static bool run_server(const settings_t& cfg) {
    int port = cfg.port;

    for (;;) {
        try {
            netkit::socket::addr addr("0.0.0.0",
                port,
                netkit::socket::addr_type::ipv4,
                netkit::socket::resolve_method::os);

            netkit::tcp::tcp_server server(addr);

            server.bind();
            server.listen();

            gui::update([&](gui::state_t& s) {
                s.port = port;
                s.listening = true;
            });

            if (port != cfg.port)
                gui::log("Cannot bind to %d, using %d instead", cfg.port, port);

            gui::log("Listening on port %d", port);
            gui::log(is_ro(cfg) ? "Sharing read-only" : "Sharing read/write");

            for (;;) {
                auto client = server.accept();

                if (restart_requested())
                    return true;

                gui::update([](gui::state_t& s) { s.client_connected = true; });
                gui::log("Client connected");

                try {
                    smb::connection conn(std::move(client), cfg.share_name, cfg.share_root, is_ro(cfg), to_credentials(cfg));
                    conn.run();
                } catch (const std::exception& e) {
                    gui::log("Client error: %.60s", e.what());
                }

                gui::update([](gui::state_t& s) {
                    s.client_connected = false;
                    ++s.sessions;
                });

                gui::log("Client disconnected");

                if (restart_requested())
                    return true;
            }
        } catch (const std::exception& e) {
            const std::string str = e.what();

            gui::update([](gui::state_t& s) {
                s.listening = false;
            });

            if (str.find("Permission denied") != std::string::npos && port != cfg.fallback_port) {
                port = cfg.fallback_port;
                continue;
            }

            gui::log("Server error: %.70s", e.what());

            return false;
        }
    }
}

static void* server_thread(void*) {
    std::string local_ip{};
    auto interfaces = netkit::network::get_interfaces();

    for (auto& it : interfaces) {
        if (it.is_up() && it.is_running()) {
            for (auto& _it : it.get_ipv4_addrs()) {
                local_ip = _it.get_ip();
                break;
            }
        }
    }

    if (!local_ip.empty()) {
        gui::update([&](gui::state_t& s) { s.ip = local_ip; });
    }

    for (;;) {
        settings_t cfg;
        {
            gui::guard g;
            cfg = gui::st.cfg;
            gui::st.restart = false;
        }

        const bool restarted = run_server(cfg);

        gui::update([](gui::state_t& s) {
            s.listening = false;
            s.client_connected = false;
        });

        if (restarted) {
            gui::log("Applying new settings...");
        } else {
            while (!restart_requested()) usleep(100 * 1000);
        }
    }

    return nullptr;
}

static int fatal(const char* msg) {
    VIDEO_Init();
    const auto rmode = VIDEO_GetPreferredMode(nullptr);
    const auto xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();

    std::printf("error: %s\n\nPress HOME to exit.\n", msg);

    for (;;) {
        WPAD_ScanPads();
        if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME) return 1;
        VIDEO_WaitVSync();
    }
}

// is this even correct?
// idk but it seems to work on my wii and wii u
bool patch_ios() {
    const bool iospatch_ok = false;
    bool vwii_patch = true;

    if (is_dolphin()) {
        return false;
    }

    bool ahbprot_reset = patch_ahbprot_reset();
    if (!ahbprot_reset) {
        return false;
    }

    if (is_wiiu())
        vwii_patch = patch_wiiu_systitleinstall();

    bool ios_verify = patch_ios_verify();

    return iospatch_ok && vwii_patch && ios_verify;
}

int main(int argc, char** argv) {
    const bool iospatch_ok = patch_ios();

    WII_Initialize();
    WPAD_Init();
    net_init();
    fatInitDefault();

    usb::begin();

    const bool nand_ok = nand::mount();

    gui::init_state();

    settings_t cfg = load_settings();

    std::error_code ec;
    bool root_reset = false;
    if (!std::filesystem::is_directory(cfg.share_root, ec)) {
        cfg.share_root = ".";
        root_reset = true;
    }
    if (!std::filesystem::is_directory(cfg.share_root, ec)) {
        return fatal("share root is not a directory, do you have an SD card inserted?");
    }
    gui::st.cfg = cfg;

    gui::ui ui;
    if (!ui.init()) {
        std::string msg = "could not start the GUI, do you have a theme on the SD card?";
        return fatal(msg.c_str());
    }

    if (root_reset)
        gui::log("saved root folder not found, using the default");

    if (!nand_ok)
        gui::log("NAND not available (ISFS failed to start)");

    ui.set_iospatch(iospatch_ok);

    ui.on_apply = [](const settings_t& c) {

        {
            gui::guard g;
            gui::st.cfg = c;
            gui::st.restart = true;
        }

        kick_server();
    };

    if (LWP_CreateThread(
        &server_thread_id,
        server_thread,
        nullptr,
        server_stack,
        sizeof(server_stack),
        40
    ) != 0) {
        ui.shutdown();
        return fatal("failed to create server thread");
    }

    while (ui.frame()) {}

    ui.shutdown();

    if (argc > 1 && std::strcmp(argv[1], "--from-hbc") == 0) {
        constexpr uint64_t hbc_ids[] = {
            0x0001000148415858ULL, // HAXX
            0x000100014A4F4449ULL, // JODI
            0x000100014C554C5AULL, // LULZ
            0x000100014F484243ULL  // OHBC
        };

        for (const uint64_t id : hbc_ids) {
            WII_LaunchTitle(id);
        }
    }

    SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);

    return 0;
}