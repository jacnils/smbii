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
#include <wiiuse/wpad.h>
#include <ogc/mutex.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "settings.hpp"
#include "theme.hpp"
#include "blur.hpp"
#include "audio.hpp"
#include "credentials.hpp"
#include "credits.hpp"
#include "install.hpp"
#include "ntlmv2.hpp"
#include "wad_data.hpp"

namespace gui {

namespace col {
    inline uint32_t bg      = 0x0E1620FF;
    inline uint32_t panel   = 0x1A2634FF;
    inline uint32_t border  = 0x2C3E52FF;
    inline uint32_t text    = 0xE8EEF4FF;
    inline uint32_t dim     = 0x8FA3B8FF;
    inline uint32_t accent  = 0x4FC3F7FF;
    inline uint32_t ok      = 0x66BB6AFF;
    inline uint32_t warn    = 0xFFB74DFF;
    inline uint32_t primary = 0x2A5F80FF;
}

struct state_t {
    mutex_t lock{};
    settings_t cfg;
    bool restart{false};
    std::string ip{"no network"};
    int port{445};
    bool listening{false};
    bool client_connected{false};
    unsigned sessions{0};
    std::deque<std::string> lines;
};

inline state_t st;

// wrapper around lwp crap
struct guard {
    guard()  { LWP_MutexLock(st.lock); }
    ~guard() { LWP_MutexUnlock(st.lock); }
};

inline void init_state() {
    LWP_MutexInit(&st.lock, false);
}

inline void log(const char* fmt, ...) {
    char buf[96];

    va_list a;
    va_start(a, fmt);
    vsnprintf(buf, sizeof buf, fmt, a);
    va_end(a);

    guard g;

    st.lines.emplace_back(buf);

    SYS_Report("[%s] ", buf);

    while (st.lines.size() > 6)
        st.lines.pop_front();
}

template <class F>
void update(F&& f) { guard g; f(st); }

class ui {
public:
    std::function<void(const settings_t&)> on_apply;

    void set_iospatch(bool active) {
        iospatch_ok_ = active;
        wad_.set_iospatch(active);
    }

    bool init() {
        theme_dir_ = st.cfg.theme;

        if (theme_dir_.empty()) {
            auto themes = scan_themes();
            for (auto& it : themes) {
                if (it.prioritize) {
                    theme_dir_ = it.dir;
                    break;
                }
            }

            if (theme_dir_.empty()) {
                for (auto& it : themes) {
                    theme_dir_ = it.dir;
                    break;
                }
            }

            if (theme_dir_.empty())
                return false;
        }

        if (!read_asset("font.ttf", ttf_)) {
            theme_dir_.clear();

            if (!read_asset("font.ttf", ttf_))
                return false;
        }

        if (GRRLIB_Init() != 0)
            return false;

        font_ = GRRLIB_LoadTTF(ttf_.data(), static_cast<s32>(ttf_.size()));
        if (!font_) {
            GRRLIB_Exit();
            return false;
        }

        snd_.init();
        sfx_on_ = st.cfg.sfx;
        music_on_ = st.cfg.music;

        apply_theme_files();
        st.cfg.theme = theme_dir_;
        wad_installed_ = wad_.installed();

        WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
        WPAD_SetVRes(WPAD_CHAN_ALL, 640, 480);

        return true;
    }

    void shutdown() {
        snd_.shutdown();
        wad_.shutdown();

        for (auto& s : sfx_)
            audio::engine::free_sample(s);

        free_tex(bg_);
        free_tex(bg_blur_);
        free_tex(logo_);
        free_tex(cursor_);
        free_tex(picker_.preview);

        if (font_)
            GRRLIB_FreeTTF(font_);
        
        GRRLIB_Exit();
    }

    bool frame() {
        if (theme_pending_) {
            theme_pending_ = false;
            apply_pending_theme();
        }

        WPAD_ScanPads();
        WPAD_IR(0, &ir_);
        
        pressed_ = WPAD_ButtonsDown(0);
        held_ = WPAD_ButtonsHeld(0);
        
        ++tick_;

        state_t s;
        
        {
            guard g; 
            s = st;
        }

        sfx_on_ = s.cfg.sfx;
        
        if (s.cfg.music != music_on_) {
            music_on_ = s.cfg.music;
            refresh_music();
        }
        
        const bool kb_active = kb_.open;
        const bool picker_active = picker_.open;
        const bool confirm_active = confirm_.open;
        const bool browser_active = browser_.open;

        const wad::installer::status_t wad_st = wad_.status();
        const bool wad_busy = wad_st.phase != wad::installer::phase_t::idle;

        if ((pressed_ & WPAD_BUTTON_HOME) && !wad_busy) {
            if (kb_.open)
                kb_.open = false;
            else if (picker_.open) 
                close_picker();
            else if (prompt_)
                quit_ = true;
            else
                prompt_ = true;
        }
        
        if (prompt_ && (pressed_ & WPAD_BUTTON_B)) 
            prompt_ = false;

        locked_ = prompt_ || kb_.open || picker_.open || confirm_.open || browser_.open || wad_busy;
        
        if (scr_ == screen::main) 
            draw_main(s);
        else if (scr_ == screen::settings)
            draw_settings(s);
        else if (scr_ == screen::credits)
            draw_credits();
        else
            draw_login(s);
        
        locked_ = false;

        if (prompt_) 
            draw_prompt();
        
        if (kb_active && kb_.open) 
            draw_keyboard();
        
        if (picker_active && picker_.open) 
            draw_picker();

        if (confirm_active && confirm_.open)
            draw_confirm();

        if (browser_active && browser_.open) {
            draw_browser();
        }

        if (wad_busy)
            draw_wad_status(wad_st);

        draw_cursor();

        if (hot_key_ && hot_key_ != prev_hot_key_) {
            play(sfx_id::hover);

            WPAD_Rumble(WPAD_CHAN_0, true);
            usleep(15'000);
            WPAD_Rumble(WPAD_CHAN_0, false);
        }
        
        prev_hot_key_ = hot_key_;
        hot_key_ = 0;

        GRRLIB_Render();
        
        return !quit_;
    }

private:
    enum class screen {
        main,
        settings,
        credits,
        login
    };
    enum class sfx_id {
        hover,
        click,
        back,
        error,
        count
    };

    enum target {
        root = 0,
        share_name = 1,
        port = 2,
        login_user = 3,
        login_password = 4,
        password_confirm = 5,
    };

    struct keyboard_t {
        bool open{};
        bool numeric{};
        bool mask{};
        bool shift{};
        size_t max_len{64};
        int target{root};
        std::string title;
        std::string buf;
    };

    struct picker_t {
        bool open{};
        std::vector<theme_info> items;
        int sel{0};
        int top{0};
        GRRLIB_texImg* preview{};
    };

    GRRLIB_ttfFont* font_{};
    GRRLIB_texImg *bg_{}, *bg_blur_{}, *logo_{}, *cursor_{};
    GRRLIB_texImg* pattern_tex_[8]{};
    int pattern_count_{};
    float pattern_offset_{};
    uint32_t pattern_last_ms_{};
    
    std::vector<uint8_t> ttf_;
    ir_t ir_{};
    uint32_t pressed_{}, held_{}, tick_{};
    bool prompt_{}, quit_{}, locked_{};

    screen scr_{screen::main};
    keyboard_t kb_;
    picker_t picker_;
    int bs_hold_{};

    struct browse_entry { std::string label; std::string path; };
    struct browser_t {
        bool open{};
        std::string path;
        std::vector<browse_entry> items;
        int top{0};
        std::string error;
    };
    browser_t browser_;

    struct credit_line {
        std::string text;
        int size;
        int kind;
        int x;
        int y;
    };

    static constexpr int credits_vx = 44, credits_vy = 96, credits_vw = 552, credits_vh = 298;
    std::vector<credit_line> credit_lines_;
    int credits_h_{};
    float credits_scroll_{};
    float credits_delay_{};
    float credits_end_{};
    uint32_t credits_last_ms_{};
    
    std::string draft_root_, draft_share_, draft_port_;
    bool draft_ro_{};
    std::string msg_;
    
    bool draft_login_enabled_{};
    std::string draft_login_user_;
    std::string draft_login_nthash_;
    std::string draft_login_pending_pw_;
    std::string draft_login_msg_;
    bool draft_login_msg_ok_{true};
    
    bool msg_ok_{true};

    wad::installer wad_{wad_data::bytes.data(), wad_data::bytes.size()};
    bool wad_installed_{};
    struct confirm_t { bool open{}; bool install{}; };
    confirm_t confirm_;
    bool iospatch_ok_{false};

    std::string theme_dir_;
    std::string theme_name_{"Default"};
    style_t style_;
    std::string pending_theme_;
    bool theme_pending_{};

    audio::engine snd_;
    audio::sample sfx_[static_cast<int>(sfx_id::count)];
    std::string music_path_;
    
    bool sfx_on_{true};
    bool music_on_{true};
    int hot_key_{};
    int prev_hot_key_{};

    static bool read_path(const std::string& path, std::vector<uint8_t>& out) {
        FILE* f = std::fopen(path.c_str(), "rb");
        
        if (!f) 
            return false;
        
        std::fseek(f, 0, SEEK_END);
        
        long n = std::ftell(f);
        
        std::rewind(f);
        
        out.resize(n > 0 ? n : 0);
        
        bool ok = n > 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
        
        std::fclose(f);
        
        return ok;
    }

    static bool exists(const std::string& path) {
        FILE* f = std::fopen(path.c_str(), "rb");
        
        if (!f)
            return false;
        
        std::fclose(f);
        
        return true;
    }

    bool read_asset(const char* name, std::vector<uint8_t>& out) const {
        return (!theme_dir_.empty() && read_path(theme_path(theme_dir_, name), out));
    }

    std::string find_asset_path(const char* name) const {
        if (!theme_dir_.empty()) {
            const std::string p = theme_path(theme_dir_, name);
            
            if (exists(p))
                return p;
        }

        return {};
    }

    static GRRLIB_texImg* tex_from(const std::vector<uint8_t>& buf) {
        return buf.empty() ? nullptr : GRRLIB_LoadTexture(buf.data());
    }

    GRRLIB_texImg* load_tex(const char* name) const {
        std::vector<uint8_t> buf;
        return read_asset(name, buf) ? tex_from(buf) : nullptr;
    }

    static void free_tex(GRRLIB_texImg*& t) {
        if (t) GRRLIB_FreeTexture(t);
        t = nullptr;
    }
    
    void apply_theme_files() {
        palette_t p;
        style_t sty;
        theme_info info;
        
        if (!theme_dir_.empty() && !parse_theme_ini(theme_path(theme_dir_, "theme.ini"), info, p, sty)) {
            theme_dir_.clear();
            p = palette_t{};
            sty = style_t{};
            info = theme_info{};
        }

        style_ = sty;
        col::bg = p.bg;
        col::panel = p.panel;
        col::border = p.border;
        col::text = p.text;
        col::dim = p.dim;
        col::accent = p.accent;
        col::ok = p.ok;
        col::warn = p.warn;
        col::primary = p.primary;

        theme_name_ = theme_dir_.empty() ? "Default" : (info.name.empty() ? theme_dir_ : info.name);

        free_tex(bg_);
        free_tex(bg_blur_);
        free_tex(logo_);
        free_tex(cursor_);
        
        bg_ = load_tex("background.png");
        logo_ = load_tex("logo.png");
        cursor_ = load_tex("cursor.png");

        for (auto& t : pattern_tex_)
            free_tex(t);

        pattern_count_ = 0;

        if (style_.pattern) {
            char name[16];

            for (int i = 0; i < 8; ++i) {
                std::snprintf(name, sizeof name, "pattern%d.png", i + 1);
                GRRLIB_texImg* t = load_tex(name);

                if (!t)
                    break;

                pattern_tex_[i] = t;
                pattern_count_ = i + 1;
            }

            if (pattern_count_ == 0) {
                style_.pattern = false;

                log("Pattern background enabled but no pattern1.png found, skipping it");
            }
        }

        pattern_offset_ = 0;
        pattern_last_ms_ = static_cast<uint32_t>(ticks_to_millisecs(gettime()));

        if (bg_ && (style_.panel_blur > 0 || style_.background_blur > 0)) {
            const bool wrap = style_.bg_mode == bg_mode_t::tile;
            
            if (!gfx::blur_supported(bg_)) {
                log("Blur skipped: background size must be a multiple of 4");
            } else {
                if (style_.panel_blur > 0) 
                    bg_blur_ = gfx::blurred_copy(bg_, style_.panel_blur, wrap);
                
                if (style_.background_blur > 0) 
                    gfx::blur_in_place(bg_, style_.background_blur, wrap);
            }
        }

        load_audio();
    }

    bool switch_theme(const std::string& dir) {
        const std::string old = theme_dir_;
        theme_dir_ = dir;

        std::vector<uint8_t> nt;
        GRRLIB_ttfFont* nf = nullptr;
        
        if (read_asset("font.ttf", nt)) 
            nf = GRRLIB_LoadTTF(nt.data(), static_cast<s32>(nt.size()));
        
        if (!nf) {
            theme_dir_ = old; return false;
        }

        if (font_) 
            GRRLIB_FreeTTF(font_);
        
        font_ = nf;
        ttf_ = std::move(nt);

        apply_theme_files();
        
        return true;
    }

    void request_theme(const std::string& dir) {
        pending_theme_ = dir;
        theme_pending_ = true;
    }

    void apply_pending_theme() {
        if (!switch_theme(pending_theme_)) {
            msg_ = "Couldn't load that theme";
            msg_ok_ = false;
            play(sfx_id::error);
            return;
        }
        
        msg_ = "Theme: " + theme_name_;
        msg_ok_ = true;
        
        persist([&](settings_t& c) {
            c.theme = theme_dir_;
        });
    }

    template <class F>
    void persist(F&& change) {
        settings_t c;
        
        {
            guard g;
            change(st.cfg);
            c = st.cfg;
        }
        
        if (!save_settings(c)) {
            msg_ = "Could not write settings.ini";
            msg_ok_ = false;
        }
    }

    void load_audio() {
        snd_.stop_music();
        
        for (auto& s : sfx_) 
            audio::engine::free_sample(s);

        static const char* names[] = {"hover.wav", "click.wav", "back.wav", "error.wav"};
        
        for (int i = 0; i < static_cast<int>(sfx_id::count); ++i) {
            std::vector<uint8_t> buf;
            if (read_asset(names[i], buf)) sfx_[i] = audio::engine::load_wav(buf);
        }

        music_path_ = find_asset_path("music.ogg");
        
        refresh_music();
    }

    void refresh_music() {
        if (music_on_ && !music_path_.empty()) 
            snd_.start_music(music_path_, style_.music_volume, style_.music_loop);
        else
            snd_.stop_music();
    }

    void play(sfx_id id) {
        if (sfx_on_) 
            snd_.play(sfx_[static_cast<int>(id)], style_.sfx_volume);
    }

    void note_hover(int x, int y, int w) {
        hot_key_ = (((x + 1) << 20) ^ ((y + 1) << 8) ^ (w & 0xFF)) | 1;
    }

    void text(int x, int y, const std::string& s, int size, uint32_t c) {
        GRRLIB_PrintfTTF(x, y, font_, s.c_str(), size, c);
    }
    
    int width(const std::string& s, int size) { return GRRLIB_WidthTTF(font_, s.c_str(), size); }

    std::string fit(std::string s, int size, int max_w) {
        if (width(s, size) <= max_w) 
            return s;
        
        while (!s.empty() && width(s + "...", size) > max_w) 
            s.pop_back();
        
        return s + "...";
    }
    
    static int corner_inset(int row, int h, int r) {
        if (r <= 0) 
            return 0;
        
        const int d = std::min(row, h - 1 - row);
        
        if (d >= r) 
            return 0;
        
        const float dy = r - d - 0.5f;
        return static_cast<int>(r - std::sqrt(static_cast<float>(r) * r - dy * dy) + 0.5f);
    }

    void draw_scaled(GRRLIB_texImg* t, float left, float top, float sx, float sy) {
        if (!t) 
            return;

        GRRLIB_DrawImg(
            left, top,
            t,
            0,
            sx, sy,
            0xFFFFFFFF
        );
    }
    
    void draw_bg_tex(GRRLIB_texImg* t, int x0, int y0, int x1, int y1) {
        if (!t || !t->w || !t->h)
            return;

        const float w = static_cast<float>(t->w);
        const float h = static_cast<float>(t->h);

        switch (style_.bg_mode) {
            case bg_mode_t::stretch:
                draw_scaled(t, 0, 0, 640.0f / w, 480.0f / h);
                break;

            case bg_mode_t::center:
                draw_scaled(t, (640.0f - w) / 2, (480.0f - h) / 2, 1, 1);
                break;

            case bg_mode_t::tile: {
                const float s = style_.tile_scale;
                
                if (s <= 0.0f)
                    break;

                const float tw = w * s;
                const float th = h * s;

                const int c0 = static_cast<int>(std::floor(x0 / tw));
                const int c1 = static_cast<int>(std::floor((x1 - 1) / tw));
                const int r0 = static_cast<int>(std::floor(y0 / th));
                const int r1 = static_cast<int>(std::floor((y1 - 1) / th));

                for (int r = r0; r <= r1; ++r) {
                    for (int c = c0; c <= c1; ++c) {
                        draw_scaled(t, std::floor(c * tw),
                                    std::floor(r * th), s, s);
                    }
                }

                break;
            }
            case bg_mode_t::scroll: {
                const float s = style_.tile_scale;
                if (s <= 0.0f)
                    break;

                const float tw = w * s;
                const float th = h * s;

                const uint32_t now = static_cast<uint32_t>(
                    ticks_to_millisecs(gettime())
                );

                const float dt = static_cast<float>(
                    std::min<uint32_t>(now - pattern_last_ms_, 100)
                );

                pattern_last_ms_ = now;

                if (!locked_)
                    pattern_offset_ += style_.pattern_speed * dt / 1000.0f;

                const float loop_dist = std::max(tw, th);

                if (loop_dist > 0.0f)
                    pattern_offset_ = std::fmod(pattern_offset_, loop_dist);

                const float ox = std::fmod(pattern_offset_, tw);
                const float oy = std::fmod(pattern_offset_, th);

                const int c0 = static_cast<int>(std::floor(x0 / tw)) - 1;
                const int c1 = static_cast<int>(std::floor((x1 - 1) / tw)) + 1;
                const int r0 = static_cast<int>(std::floor(y0 / th)) - 1;
                const int r1 = static_cast<int>(std::floor((y1 - 1) / th)) + 1;

                for (int r = r0; r <= r1; ++r) {
                    for (int c = c0; c <= c1; ++c) {
                        const float x = std::floor(c * tw - ox);
                        const float y = std::floor(r * th - oy);

                        draw_scaled(t, x, y, s, s);
                    }
                }

                break;
            }
        }
    }

    void draw_pattern_overlay() {
        const uint32_t now = static_cast<uint32_t>(ticks_to_millisecs(gettime()));
        const float dt = static_cast<float>(std::min<uint32_t>(now - pattern_last_ms_, 100));
        pattern_last_ms_ = now;
        if (!locked_) pattern_offset_ += style_.pattern_speed * dt / 1000.0f;

        const int tile = style_.pattern_tile;
        const int pad = style_.pattern_padding;
        const int inner = std::max(1, tile - 2 * pad);
        const int n = pattern_count_;

        const float loop_dist = static_cast<float>(tile) * n;
        if (loop_dist > 0) pattern_offset_ = std::fmod(pattern_offset_, loop_dist);
        const int tile_offset = static_cast<int>(std::floor(pattern_offset_ / tile));
        const int pixel_offset = static_cast<int>(pattern_offset_) % tile;

        const int cols = (640 / tile) + n + 2;
        const int rows = (480 / tile) + n + 2;

        for (int row = -1; row < rows; ++row) {
            for (int col = -1; col < cols; ++col) {
                const int world = col + row + 2 * tile_offset;
                const int idx = ((world % n) + n) % n;
                GRRLIB_texImg* img = pattern_tex_[idx];
                if (!img || !img->w || !img->h) continue;

                const int x = col * tile - pixel_offset;
                const int y = row * tile - pixel_offset;

                const float by_w = static_cast<float>(inner) / img->w, by_h = static_cast<float>(inner) / img->h;
                const float fit = std::min(by_w, by_h) * style_.pattern_scale[static_cast<std::size_t>(idx)];
                const float dw = img->w * fit, dh = img->h * fit;

                draw_scaled(img, x + pad + (inner - dw) / 2.0f, y + pad + (inner - dh) / 2.0f, fit, fit);
            }
        }
    }

    void draw_background() {
        GRRLIB_FillScreen(col::bg);

        draw_bg_tex(bg_, 0, 0, 640, 480);

        if (style_.pattern && pattern_count_ > 0)
            draw_pattern_overlay();
    }

    void fill_span(int x, int y, int w, int h, uint32_t color, bool frost) {
        if (w <= 0 || h <= 0) 
            return;
        
        if (frost && bg_blur_) {
            GRRLIB_ClipDrawing(x, y, w, h);
            draw_bg_tex(bg_blur_, x, y, x + w, y + h);
            GRRLIB_ClipReset();
        }
        
        GRRLIB_Rectangle(x, y, w, h, color, true);
    }
    
    void rounded(int x, int y, int w, int h, int r, uint32_t fill, uint32_t border, int bw, bool frost = false) {
        if (w <= 0 || h <= 0) 
            return;
        
        const int half = std::min(w, h) / 2;
        r = std::max(0, std::min(r, half));
        
        if (!(border & 0xFF)) 
            bw = 0;
        
        bw = std::max(0, std::min(bw, half));
        
        const int ri = std::max(0, r - bw);
        const int ih = h - 2 * bw;

        auto flush = [&](int from, int to, int o, int in) {
            const int ry = y + from, rh = to - from;
            if (in < 0) {
                fill_span(x + o, ry, w - 2 * o, rh, border, false); return;
            }
            if (bw > 0) {
                fill_span(x + o, ry, in - o, rh, border, false);
                fill_span(x + w - in, ry, in - o, rh, border, false);
            }
            
            fill_span(x + in, ry, w - 2 * in, rh, fill, frost);
        };

        int start = 0, ko = 0, ki = 0;
        bool have = false;
        
        for (int row = 0; row <= h; ++row) {
            const bool end = row == h;
            int o = 0, in = 0;
            if (!end) {
                o = corner_inset(row, h, r);
                const int irow = row - bw;
                
                if (bw == 0)
                    in = o;
                else if (irow < 0 || irow >= ih) 
                    in = -1;
                else
                    in = std::max(o, bw + corner_inset(irow, ih, ri));
            }
            
            if (have && (end || o != ko || in != ki)) {
                flush(start, row, ko, ki); have = false;
            }
            
            if (!end && !have) {
                start = row; ko = o; ki = in; have = true;
            }
        }
    }

    void shadow(int x, int y, int w, int h, int r) {
        const uint32_t base = style_.shadow_color;
        const int n = style_.shadow_soft;
        const uint32_t a = (base & 0xFF) / (n + 1);
        
        if (a == 0) 
            return;
        
        const uint32_t c = (base & 0xFFFFFF00) | a;
        
        for (int i = n; i >= 0; --i)
            rounded(x + style_.shadow_x - i, y + style_.shadow_y - i, w + 2 * i, h + 2 * i, r + i, c, 0, 0);
    }

    int button_radius() const {
        return style_.button_radius < 0 ? style_.radius : style_.button_radius;
    }

    void panel(int x, int y, int w, int h) {
        if (style_.shadow) 
            shadow(x, y, w, h, style_.radius);
        
        rounded(x, y, w, h, style_.radius, col::panel, col::border, style_.border, style_.panel_blur > 0 && bg_blur_);
    }

    void box(int x, int y, int w, int h, uint32_t fill, uint32_t border) {
        if (style_.button_shadow) 
            shadow(x, y, w, h, button_radius());
        
        rounded(x, y, w, h, button_radius(), fill, border, style_.border);
    }

    bool hovering(int x, int y, int w, int h) const {
        return !locked_ && ir_.valid && ir_.x >= x && ir_.x < x + w && ir_.y >= y && ir_.y < y + h;
    }

    bool button(int x, int y, int w, int h, const char* label, bool primary = false, bool enabled = true, sfx_id snd = sfx_id::click) {
        const bool hot = enabled && hovering(x, y, w, h);
        
        if (hot) 
            note_hover(x, y, w);
        
        box(x, y, w, h, hot ? col::accent : (primary ? col::primary : col::panel), col::border);
        text(x + (w - width(label, 18)) / 2, y + (h - 18) / 2 - 2, label, 18,
             hot ? col::bg : (enabled ? col::text : col::border));
        
        if (hot && (pressed_ & WPAD_BUTTON_A)) {
            play(snd); return true;
        }
        
        return false;
    }

    void dot(int x, int y, int r, uint32_t base) {
        uint8_t a = static_cast<uint8_t>(170 + 85 * std::sin(tick_ * 0.08f));
        GRRLIB_Circle(x, y, r, (base & 0xFFFFFF00) | a, true);
    }

    void draw_main(const state_t& s) {
        draw_background();

        if (logo_ && style_.show_logo)
            GRRLIB_DrawImg(30, 18, logo_, 0, 1, 1, 0xFFFFFFFF);
        else if (style_.show_logo) {
            std::string title = "smbii " + std::string(SMBII_VERSION);
            text(30, 18, title, 40, col::accent);
        }

        if (style_.attribution)
            text(30, 62, "by Jacob Nilsson / Forwarder Factory", 16, col::dim);

        if (style_.promote_ff)
            text(30, 450, "https://forwarderfactory.com", 16, col::dim);

        panel(30, 90, 580, 100);
        dot(58, 120, 8, s.listening ? col::ok : col::warn);

        text(78, 108, s.listening ? "Server running" : "Starting...", 22, col::text);
        text(40, 158, "\\\\" + s.ip + "\\" + s.cfg.share_name, 26, col::accent);

        if (s.listening && s.port != 445)
            text(380, 112, "Non-standard port " + std::to_string(s.port), 15, col::warn);

        panel(30, 205, 280, 70);

        text(42, 212, "Client", 15, col::dim);
        text(42, 232, s.client_connected ? "Connected" : "Waiting", 22,
             s.client_connected ? col::ok : col::text);

        text(210, 240, std::to_string(s.sessions) + " sessions", 14, col::dim);

        panel(330, 205, 280, 70);
        text(342, 212, "Sharing", 15, col::dim);

        const bool ro = is_ro(s.cfg);
        const std::string mode = ro ? "Read-only" : "Read/write";
        text(598 - width(mode, 14), 213, mode, 14, ro ? col::warn : col::ok);

        const std::string root = s.cfg.share_root.string();

        text(342, 232, fit(root, 20, 256), 20, col::text);

        panel(30, 290, 580, 118);
        int y = 300;

        for (size_t i = 0; i < s.lines.size(); ++i, y += 15)
            text(42, y, s.lines[i], 14, i + 1 == s.lines.size() ? col::text : col::dim);

        if (button(250, 418, 110, 30, "Credits"))
            enter_credits();

        if (button(370, 418, 110, 30, "Settings")) 
            enter_settings(s);
        
        if (button(500, 418, 110, 30, "Exit")) 
            prompt_ = true;
    }

    void draw_prompt() {
        GRRLIB_Rectangle(0, 0, 640, 480, 0x000000B0, true);

        panel(160, 170, 320, 140);

        const char* t = "Exit smbii?";

        text(320 - width(t, 24) / 2, 188, t, 24, col::text);
        text(188, 222, "Press Home again to exit, B to cancel", 13, col::dim);

        if (button(190, 255, 120, 34, "Yes", true))
            quit_ = true;

        if (button(330, 255, 120, 34, "No", true, true, sfx_id::back))
            prompt_ = false;
    }

    void enter_settings(const state_t& s) {
        draft_root_  = s.cfg.share_root.string();
        draft_share_ = s.cfg.share_name;
        draft_port_  = std::to_string(s.cfg.port);
        draft_ro_    = s.cfg.read_only;
        draft_login_enabled_ = s.cfg.auth_enabled;
        draft_login_user_ = s.cfg.auth_user;
        draft_login_nthash_ = s.cfg.auth_nthash_hex;
        draft_login_pending_pw_.clear();
        draft_login_msg_.clear();

        msg_.clear();
        scr_ = screen::settings;
    }

    bool field(int x, int y, int w, const char* label, const std::string& value) {
        text(x, y, label, 15, col::dim);

        const int by = y + 22, bh = 38;
        const bool hot = hovering(x, by, w, bh);

        if (hot)
            note_hover(x, by, w);

        box(x, by, w, bh, col::panel, hot ? col::accent : col::border);

        std::string shown = value;

        while (width(shown, 20) > w - 24 && shown.size() > 1)
            shown.erase(0, 1);

        text(x + 12, by + 8, shown, 20, col::text);

        if (hot && (pressed_ & WPAD_BUTTON_A)) {
            play(sfx_id::click); return true;
        }

        return false;
    }

    void draw_settings(const state_t& s) {
        draw_background();

        text(30, 18, "Settings", 40, col::accent);
        text(30, 62, "Change how SMBii operates", 16, col::dim);

        if (field(30, 88,  262, "Root folder on the SD card", draft_root_))
            open_keyboard(0, "Root folder", draft_root_, false, 64);

        if (button(298, 110, 82, 38, "Browse"))
            open_browser();

        if (button(384, 110, 68, 38, "SD"))
            draft_root_ = "sd:/";

        if (button(458, 110, 68, 38, "USB"))
            draft_root_ = "usb:/";

        if (button(532, 110, 68, 38, "NAND"))
            draft_root_ = "nand:/";

        if (field(30, 150, 580, "Share name", draft_share_))
            open_keyboard(1, "Share name", draft_share_, false, 48);

        if (field(30, 212, 580, "Port", draft_port_))
            open_keyboard(2, "Port", draft_port_, true, 5);

        if (field(30, 274, 290, "Theme", theme_name_))
            open_picker();

        text(340, 274, "Sound", 15, col::dim);

        if (button(340, 296, 130, 38, s.cfg.sfx ? "SFX: On" : "SFX: Off", s.cfg.sfx))
            persist([](settings_t& c) { c.sfx = !c.sfx; });

        if (button(480, 296, 130, 38, s.cfg.music ? "Music: On" : "Music: Off", s.cfg.music))
            persist([](settings_t& c) { c.music = !c.music; });

        const bool nand = is_nand_path(draft_root_);
        const bool ro = draft_ro_ || nand;
        if (button(30, 344, 200, 32, ro ? "Read-only: On" : "Read-only: Off", ro, !nand))
            draft_ro_ = !draft_ro_;

        const std::string login_label = draft_login_enabled_ ? "Login: required" : "Login: guest access";
        if (button(236, 344, 190, 32, login_label.c_str(), draft_login_enabled_)) {
            draft_login_msg_.clear();
            scr_ = screen::login;
        }

        if (nand)
            text(242, 380, "The NAND is always shared read-only", 13, col::warn);
        
        if (is_usb_path(draft_root_)) {
            const bool ready = usb::available();
            text(242, 380, ready ? "USB drive ready" : "No USB drive detected", 13, ready ? col::ok : col::warn);
        }

        if (!msg_.empty())
            text(30, 380, fit(msg_, 16, 580), 16, msg_ok_ ? col::ok : col::warn);
        else if (s.listening)
            text(30, 380, "Running on port " + std::to_string(s.port), 16, col::dim);

        {
            if (wad_data::bytes.empty() == false) {
                std::string label = std::string(wad_installed_ ? "Uninstall Channel" : "Install Channel");

                if (button(30, 418, 290, 30, fit(label, 18, 270).c_str(), false, true)) {
                    confirm_.open = true;
                    confirm_.install = !wad_installed_;
                }
            }
        }

        if (button(350, 418, 120, 30, "Apply", true))
            apply(s);
        if (button(490, 418, 120, 30, "Back", false, true, sfx_id::back))
            scr_ = screen::main;
    }

    void enter_credits() {
        credit_lines_.clear();
        const int max_w = credits_vw - 24;
        int y = 6;
        bool first = true;

        auto push = [&](const std::string& t, int size, int kind) {
            credit_lines_.push_back({t, size, kind, (credits_vw - width(t, size)) / 2, y});
            y += size + 8;
        };

        for (const char* raw : credits::lines) {
            std::string t = raw;
            int kind = 0, size = 16;
            if (t.rfind("# ", 0) == 0)      { kind = 1; size = 22; t.erase(0, 2); }
            else if (t.rfind("~ ", 0) == 0) { kind = 2; size = 13; t.erase(0, 2); }

            if (t.empty()) { y += 10; continue; }
            if (kind == 1 && !first) y += 12;
            first = false;

            std::string cur;
            size_t pos = 0;
            for (;;) {
                const size_t sp = t.find(' ', pos);
                const std::string word = t.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos);
                const std::string trial = cur.empty() ? word : cur + " " + word;
                if (!cur.empty() && width(trial, size) > max_w) { push(cur, size, kind); cur = word; }
                else                                            cur = trial;
                if (sp == std::string::npos) break;
                pos = sp + 1;
            }
            if (!cur.empty()) push(cur, size, kind);
        }

        credits_h_ = y + 8;
        credits_scroll_ = 0;
        credits_delay_ = 3000;
        credits_end_ = 0;
        credits_last_ms_ = static_cast<uint32_t>(ticks_to_millisecs(gettime()));
        scr_ = screen::credits;
    }

    void draw_credits() {
        draw_background();

        text(30, 18, "Credits", 40, col::accent);

        panel(30, 84, 580, 322);

        const uint32_t now = static_cast<uint32_t>(ticks_to_millisecs(gettime()));
        const float dt = static_cast<float>(std::min<uint32_t>(now - credits_last_ms_, 100));

        credits_last_ms_ = now;

        const float max_scroll = std::max(0.0f, static_cast<float>(credits_h_ - credits_vh));

        if (!locked_) {
            bool manual = false;

            if (held_ & WPAD_BUTTON_UP)   { credits_scroll_ -= 140.0f * dt / 1000.0f; manual = true; }
            if (held_ & WPAD_BUTTON_DOWN) { credits_scroll_ += 140.0f * dt / 1000.0f; manual = true; }

            if (manual) {
                credits_delay_ = 3000;
                credits_end_ = 0;
            } else if (credits_delay_ > 0) {
                credits_delay_ -= dt;
            } else if (credits_scroll_ < max_scroll) {
                credits_scroll_ += 24.0f * dt / 1000.0f;
            } else if ((credits_end_ += dt) >= 3000) {
                credits_scroll_ = 0;
                credits_delay_ = 3000;
                credits_end_ = 0;
            }
        }

        credits_scroll_ = std::max(0.0f, std::min(credits_scroll_, max_scroll));

        GRRLIB_ClipDrawing(credits_vx, credits_vy, credits_vw, credits_vh);

        const int off = static_cast<int>(credits_scroll_);

        for (const credit_line& l : credit_lines_) {
            const int y = credits_vy + l.y - off;
            if (y + l.size + 8 < credits_vy || y > credits_vy + credits_vh) continue;   // off screen
            const uint32_t c = l.kind == 1 ? col::accent : (l.kind == 2 ? col::dim : col::text);
            text(credits_vx + l.x, y, l.text, l.size, c);
        }

        GRRLIB_ClipReset();

        if (credits_h_ > credits_vh) {
            const int thumb = std::max(20, credits_vh * credits_vh / credits_h_);
            const int ty = credits_vy + static_cast<int>((credits_vh - thumb) * (credits_scroll_ / max_scroll));

            rounded(600, credits_vy, 4, credits_vh, 2, col::border, 0, 0);
            rounded(600, ty, 4, thumb, 2, col::accent, 0, 0);
        }

        if (button(490, 418, 120, 30, "Back", false, true, sfx_id::back) ||
            (!locked_ && (pressed_ & WPAD_BUTTON_B))) {

            scr_ = screen::main;
        }
    }

    int draw_wrapped(int x, int y, int max_w, const std::string& s, int size, uint32_t color, int max_lines = 3) {
        std::string line;
        int lines = 0;
        size_t pos = 0;

        while (pos <= s.size() && lines < max_lines) {
            const size_t sp = s.find(' ', pos);
            const std::string word = s.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos);
            const std::string trial = line.empty() ? word : line + " " + word;

            if (!line.empty() && width(trial, size) > max_w) {
                text(x, y, line, size, color);
                y += size + 4;
                ++lines;
                line = word;
            } else {
                line = trial;
            }

            if (sp == std::string::npos)
                break;

            pos = sp + 1;
        }

        if (!line.empty() && lines < max_lines) {
            text(x, y, line, size, color);
            y += size + 4;
        }

        return y;
    }

    void draw_confirm() {
        GRRLIB_Rectangle(0, 0, 640, 480, 0x000000B0, true);
        panel(110, 130, 420, 230);

        const std::string t = fit(std::string(confirm_.install ? "Install Channel?" : "Uninstall Channel?"), 22, 380);

        text(320 - width(t, 22) / 2, 158, t, 22, col::text);
        text(130, 200, "This changes the contents of the Wii NAND.", 15, col::dim);
        text(130, 222, "Do not turn the console off while it runs.", 15, col::dim);

        if (button(170, 298, 120, 34, "Yes", true)) {
            confirm_.open = false;
            if (confirm_.install)
                wad_.start_install();
            else
                wad_.start_uninstall();
        }

        if (button(350, 298, 120, 34, "No", false, true, sfx_id::back))
            confirm_.open = false;
    }

    void draw_wad_status(const wad::installer::status_t& st) {
        const bool done = st.phase == wad::installer::phase_t::done;

        GRRLIB_Rectangle(0, 0, 640, 480, 0x000000B0, true);
        panel(100, 130, 440, 220);

        const uint32_t title_col = !done ? col::text : (st.ok ? col::ok : col::warn);
        text(320 - width(st.title, 24) / 2, 148, st.title, 24, title_col);

        constexpr int bx = 130, by = 192, bw = 380, bh = 18;

        rounded(bx, by, bw, bh, bh / 2, col::bg, col::border, style_.border);

        const int fill = (bw - 4) * std::max(0, std::min(st.percent, 100)) / 100;

        if (fill >= 4)
            rounded(bx + 2, by + 2, fill, bh - 4, (bh - 4) / 2, st.ok || !done ? col::accent : col::warn, 0, 0);

        if (!done) {
            text(bx, by + 28, fit(st.stage, 16, bw), 16, col::text);
            text(bx, by + 54, "Do not turn off the console.", 14, col::warn);
        } else {
            draw_wrapped(bx, by + 28, bw, st.message, 16, st.ok ? col::text : col::warn);

            if (button(260, 300, 120, 34, "OK", true)) {
                wad_.acknowledge();
                wad_installed_ = wad_.installed();
            }
        }
    }

    static std::string join_path(const std::string& dir, const std::string& name) {
        return (!dir.empty() && dir.back() == '/') ? dir + name : dir + "/" + name;
    }

    static std::string parent_of(const std::string& p) {
        if (p.empty() || p == ".") return "";

        std::string s = p;
        const size_t colon = s.find(':');
        const size_t min_len = colon == std::string::npos ? 1 : colon + 2;
        while (s.size() > min_len && s.back() == '/') s.pop_back();

        if (colon != std::string::npos && s.size() <= colon + 2)
            return "";

        const size_t slash = s.find_last_of('/');

        if (slash == std::string::npos)
            return "";
        if (colon != std::string::npos && slash == colon + 1)
            return s.substr(0, colon + 2);

        return s.substr(0, slash);
    }

    void browser_load(const std::string& path) {
        browser_.path = path;
        browser_.items.clear();
        browser_.top = 0;
        browser_.error.clear();

        if (path.empty()) {
            browser_.items.push_back({"App folder  (.)", "."});
            browser_.items.push_back({"SD card", "sd:/"});

            if (usb::available())
                browser_.items.push_back({"USB drive", "usb:/"});
            if (nand::available())
                browser_.items.push_back({"NAND  (read-only)", "nand:/"});

            return;
        }

        browser_.items.push_back({"..", parent_of(path)});

        DIR* d = opendir(path.c_str());

        if (!d) {
            browser_.error = "Can't read this folder";
            return;
        }

        std::vector<std::string> dirs;
        int seen = 0;

        while (dirent* e = readdir(d)) {
            const std::string name = e->d_name;

            if (name == "." || name == "..")
                continue;

            if (++seen > 400) {
                browser_.error = "Only the first 400 entries are shown";
                break;
            }

            struct stat sb;

            if (stat(join_path(path, name).c_str(), &sb) == 0 && S_ISDIR(sb.st_mode))
                dirs.push_back(name);
        }

        closedir(d);

        std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b) {
            return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
                return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
            });
        });

        for (const auto& n : dirs)
            browser_.items.push_back({n, join_path(path, n)});
    }

    void open_browser() {
        browser_ = {};
        browser_.open = true;

        struct stat sb;

        if (!draft_root_.empty() && stat(draft_root_.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode))
            browser_load(draft_root_);
        else
            browser_load("");
    }

    void browser_scroll(int delta) {
        const int n = static_cast<int>(browser_.items.size());
        browser_.top = std::max(0, std::min(browser_.top + delta, std::max(0, n - browser_rows)));
    }

    static constexpr int browser_rows = 6;

    void draw_browser() {
        GRRLIB_Rectangle(0, 0, 640, 480, 0x000000B0, true);

        panel(30, 40, 580, 380);
        text(46, 48, "Choose the folder to share", 20, col::text);
        text(46, 76, fit(browser_.path.empty() ? std::string("Drives") : browser_.path, 15, 548), 15, col::accent);

        if (pressed_ & WPAD_BUTTON_UP)
            browser_scroll(-1);

        if (pressed_ & WPAD_BUTTON_DOWN)
            browser_scroll(1);

        bool select_here = (pressed_ & WPAD_BUTTON_PLUS) && !browser_.path.empty();

        bool close = false, navigate = false;
        std::string nav;
        if (pressed_ & WPAD_BUTTON_B) {
            if (browser_.path.empty()) close = true;
            else { nav = parent_of(browser_.path); navigate = true; }
        }

        constexpr int rh = 36;
        for (int i = 0; i < browser_rows; ++i) {
            const int idx = browser_.top + i;

            if (idx >= static_cast<int>(browser_.items.size()))
                break;

            const browse_entry& e = browser_.items[idx];

            const int y = 100 + i * rh;
            const bool hot = hovering(44, y, 552, rh - 2);

            if (hot)
                note_hover(44, y, 552);

            box(44, y, 552, rh - 2, col::bg, hot ? col::accent : col::border);

            const bool up = e.label == "..";
            const uint32_t icon = up ? col::dim : col::accent;

            rounded(54, y + 9, 10, 5, 2, icon, 0, 0);
            rounded(54, y + 12, 22, 14, 3, icon, 0, 0);

            text(88, y + 8, fit(e.label, 18, 490), 18, col::text);

            if (hot && (pressed_ & WPAD_BUTTON_A)) {
                play(sfx_id::click);
                nav = e.path;
                navigate = true;
            }
        }

        if (browser_.items.size() <= 1 && !browser_.path.empty())
            text(88, 100 + rh + 8, "(no subfolders)", 16, col::dim);

        const int n = static_cast<int>(browser_.items.size());
        if (n > browser_rows) {
            const int track = browser_rows * rh - 2;
            const int thumb = std::max(20, track * browser_rows / n);
            const int ty = 100 + (track - thumb) * browser_.top / (n - browser_rows);
            rounded(600, 100, 4, track, 2, col::border, 0, 0);
            rounded(600, ty, 4, thumb, 2, col::accent, 0, 0);
        }

        if (button(44, 322, 100, 30, "Page up"))
            browser_scroll(-browser_rows);

        if (button(152, 322, 100, 30, "Page down"))
            browser_scroll(browser_rows);

        if (!browser_.error.empty())
            text(268, 328, fit(browser_.error, 14, 328), 14, col::warn);

        if (button(322, 376, 190, 32, "Select", true, !browser_.path.empty()))
            select_here = true;

        if (button(520, 376, 76, 32, "Cancel", false, true, sfx_id::back))
            close = true;

        if (select_here) {
            draft_root_ = browser_.path;
            browser_.open = false;
        } else if (close) {
            browser_.open = false;
        } else if (navigate) {
            browser_load(nav);
        }
    }

        void draw_login(const state_t&) {
        draw_background();
        text(30, 18, "Login", 40, col::accent);
        text(30, 62, "Set up authentication.", 16, col::dim);

        if (field(30, 92, 580, "Username", draft_login_user_))
            open_keyboard(3, "Username", draft_login_user_, false, 32);

        const std::string pw_shown = draft_login_nthash_.empty() ? "(not set)" : "********";
        if (field(30, 154, 460, "Password", pw_shown))
            open_keyboard(4, "New password", "", false, 64, true);
        if (button(500, 176, 110, 38, "Clear", false, !draft_login_nthash_.empty())) {
            draft_login_nthash_.clear();
            draft_login_msg_ = "Password cleared";
            draft_login_msg_ok_ = true;
        }

        const bool can_enable = !draft_login_user_.empty() && !draft_login_nthash_.empty();
        if (button(30, 224, 290, 34, draft_login_enabled_ ? "Require this login: On" : "Require this login: Off",
                   draft_login_enabled_, can_enable || draft_login_enabled_)) {
            draft_login_enabled_ = !draft_login_enabled_;
        }

        if (!can_enable && !draft_login_enabled_)
            text(336, 226, "Set a username and password first", 14, col::dim);

        text(30, 262, "When off, every connection is guest. Note that Windows may refuse this.", 14, col::dim);
        text(30, 280, "When on, only this account is accepted; guest logons are refused.", 14, col::dim);

        if (!draft_login_msg_.empty())
            text(30, 370, fit(draft_login_msg_, 16, 580), 16, draft_login_msg_ok_ ? col::ok : col::warn);

        if (button(490, 418, 120, 30, "Back", false, true, sfx_id::back))
            scr_ = screen::settings;
    }

    void apply(const state_t& s) {
        settings_t cfg = s.cfg;

        cfg.share_name = draft_share_;
        cfg.share_root = draft_root_;
        cfg.port = draft_port_.empty() ? 0 : std::atoi(draft_port_.c_str());
        cfg.read_only = draft_ro_;
        cfg.auth_enabled = draft_login_enabled_;
        cfg.auth_user = draft_login_user_;
        cfg.auth_nthash_hex = draft_login_nthash_;

        const std::string err = validate(cfg);
        if (!err.empty()) {
            msg_ = err; msg_ok_ = false; play(sfx_id::error); return;
        }

        const bool saved = save_settings(cfg);

        if (on_apply)
            on_apply(cfg);

        msg_ = "Applied. Changes will be made when SMBii restarts";

        if (!saved)
            msg_ += " (could not write settings.ini)";

        msg_ok_ = saved;
    }

    static constexpr int picker_rows = 6;

    void open_picker() {
        picker_ = {};
        picker_.open = true;
        picker_.items = scan_themes();

        for (size_t i = 0; i < picker_.items.size(); ++i)
            if (picker_.items[i].dir == theme_dir_) picker_.sel = static_cast<int>(i);

        picker_.top = picker_.sel >= picker_rows ? picker_.sel - picker_rows + 1 : 0;

        load_preview();
    }

    void close_picker() {
        free_tex(picker_.preview);

        picker_.open = false;
    }

    void load_preview() {
        free_tex(picker_.preview);

        const theme_info& t = picker_.items[picker_.sel];
        const std::string path = theme_path(t.dir, "preview.png");

        std::vector<uint8_t> buf;
        if (read_path(path, buf))
            picker_.preview = tex_from(buf);
    }

    void select_theme_row(int idx) {
        const int n = static_cast<int>(picker_.items.size());
        idx = std::max(0, std::min(idx, n - 1));

        if (idx == picker_.sel)
            return;

        picker_.sel = idx;

        if (picker_.sel < picker_.top)
            picker_.top = picker_.sel;

        if (picker_.sel >= picker_.top + picker_rows)
            picker_.top = picker_.sel - picker_rows + 1;

        load_preview();
    }

    void draw_picker() {
        GRRLIB_Rectangle(0, 0, 640, 480, 0x000000B0, true);

        panel(30, 40, 580, 380);
        text(46, 48, "Choose a theme", 20, col::text);

        if (pressed_ & WPAD_BUTTON_UP)
            select_theme_row(picker_.sel - 1);

        if (pressed_ & WPAD_BUTTON_DOWN)
            select_theme_row(picker_.sel + 1);

        constexpr int rh = 36;

        for (int i = 0; i < picker_rows; ++i) {
            const int idx = picker_.top + i;
            if (idx >= static_cast<int>(picker_.items.size())) break;

            const int y = 84 + i * rh;
            const bool is_sel = idx == picker_.sel;
            const bool hot = hovering(44, y, 244, rh - 2);

            if (hot)
                note_hover(44, y, 244);

            box(44, y, 244, rh - 2, is_sel ? col::primary : col::bg, hot ? col::accent : col::border);
            text(54, y + 8, fit(picker_.items[idx].name, 18, 200), 18, col::text);

            if (picker_.items[idx].dir == theme_dir_)
                GRRLIB_Circle(274, y + rh / 2 - 1, 5, col::ok, true);

            if (hot && (pressed_ & WPAD_BUTTON_A)) {
                play(sfx_id::click); select_theme_row(idx);
            }
        }

        constexpr int px = 304, py = 84, pw = 292, ph = 219;
        box(px, py, pw, ph, col::bg, col::border);

        if (picker_.preview) {
            const float sc = std::min(static_cast<float>(pw - 4) / picker_.preview->w,
                                      static_cast<float>(ph - 4) / picker_.preview->h);
            draw_scaled(picker_.preview, px + (pw - picker_.preview->w * sc) / 2,
                        py + (ph - picker_.preview->h * sc) / 2, sc, sc);
        } else {
            const char* none = "No preview";
            text(px + (pw - width(none, 16)) / 2, py + ph / 2 - 8, none, 16, col::dim);
        }

        const theme_info& t = picker_.items[picker_.sel];

        text(px, 310, fit(t.name, 20, pw), 20, col::accent);

        if (!t.author.empty())
            text(px, 334, fit("by " + t.author, 14, pw), 14, col::dim);

        if (!t.description.empty())
            text(px, 352, fit(t.description, 14, pw), 14, col::text);

        if (button(364, 378, 120, 32, "Use theme", true)) {
            request_theme(t.dir);
            close_picker();
        } else if (button(490, 378, 106, 32, "Cancel", true, true, sfx_id::back)) {
            close_picker();
        }
    }

    void open_keyboard(int target, const char* title, const std::string& initial, bool numeric, size_t max_len, bool mask = false) {
        kb_ = {};
        kb_.open = true;
        kb_.target = target;
        kb_.title = title;
        kb_.buf = initial;
        kb_.numeric = numeric;
        kb_.max_len = max_len;
        kb_.mask = mask;
    }

    void close_keyboard(bool commit) {
        if (commit) {
            switch (kb_.target) {
                case root: draft_root_ = kb_.buf; break;
                case share_name: draft_share_ = kb_.buf; break;
                case port: draft_port_ = kb_.buf; break;
                case login_user: draft_login_user_ = kb_.buf; break;
                case login_password:
                    draft_login_pending_pw_ = kb_.buf;
                    kb_.buf.clear();
                    open_keyboard(5, "Confirm password", "", false, 64, true);
                    return;
                case password_confirm: {
                    if (kb_.buf == draft_login_pending_pw_) {
                        auto hash = ntlmv2::nt_hash(kb_.buf);
                        draft_login_nthash_ = credentials::to_hex(hash.data(), hash.size());
                        draft_login_msg_ = "Password set";
                        draft_login_msg_ok_ = true;
                    } else {
                        draft_login_msg_ = "Passwords didn't match, try again";
                        draft_login_msg_ok_ = false;
                    }

                    for (char& c : draft_login_pending_pw_)
                        c = '\0';

                    draft_login_pending_pw_.clear();

                    for (char& c : kb_.buf)
                        c = '\0';

                    break;
                }
            }
        } else if (kb_.target == 4 || kb_.target == 5) {
            for (char& c : draft_login_pending_pw_)
                c = '\0';

            draft_login_pending_pw_.clear();
        }

        kb_.open = false;
    }

    void type_char(char c) {
        if (kb_.buf.size() < kb_.max_len)
            kb_.buf.push_back(c);
    }

    void draw_keyboard() {
        GRRLIB_Rectangle(0, 0, 640, 480, 0x000000B0, true);

        panel(40, 60, 560, 330);
        text(56, 68, kb_.title, 18, col::dim);

        box(56, 96, 528, 38, col::bg, col::accent);
        std::string shown = kb_.mask ? std::string(kb_.buf.size(), '*') : kb_.buf;

        while (width(shown, 20) > 500 && shown.size() > 1)
            shown.erase(0, 1);

        text(66, 104, shown, 20, col::text);

        if ((tick_ / 30) % 2 == 0)
            GRRLIB_Rectangle(68 + width(shown, 20), 103, 2, 24, col::accent, true);

        bool backspace = pressed_ & WPAD_BUTTON_B;
        if (held_ & WPAD_BUTTON_B) { if (++bs_hold_ > 20 && bs_hold_ % 3 == 0) backspace = true; }
        else bs_hold_ = 0;
        if (pressed_ & WPAD_BUTTON_1) kb_.shift = !kb_.shift;

        static const char* rows[] = {"1234567890-_", "qwertyuiop", "asdfghjkl:", "zxcvbnm./"};
        constexpr int kw = 38, kh = 38, gap = 4;
        int y = 146;

        for (const char* row : rows) {
            const int n = static_cast<int>(std::strlen(row));
            int x = 320 - (n * kw + (n - 1) * gap) / 2;

            for (int i = 0; i < n; ++i, x += kw + gap) {
                char c = row[i];

                if (kb_.shift && std::isalpha(static_cast<unsigned char>(c)))
                    c = static_cast<char>(std::toupper(c));

                const bool enabled = !kb_.numeric || std::isdigit(static_cast<unsigned char>(c));
                const char label[2] = {c, 0};

                if (button(x, y, kw, kh, label, false, enabled))
                    type_char(c);
            }

            y += kh + gap;
        }

        int x = 72;
        if (button(x, y, 78, kh, kb_.shift ? "Shift" : "shift", kb_.shift, !kb_.numeric))
            kb_.shift = !kb_.shift;

        x += 78 + gap;

        if (button(x, y, 128, kh, kb_.shift ? "Space" : "space", false, !kb_.numeric))
            type_char(' ');

        x += 128 + gap;
        if (button(x, y, 118, kh, kb_.shift ? "Backspace" : "backspace"))
            backspace = true;

        x += 118 + gap;
        const bool cancel = button(x, y, 78, kh, kb_.shift ? "Cancel" : "cancel", false, true, sfx_id::back) || (pressed_ & WPAD_BUTTON_MINUS);
        x += 78 + gap;

        const bool done = button(x, y, 78, kh, kb_.shift ? "Done" : "done", false) || (pressed_ & WPAD_BUTTON_PLUS);

        if (backspace && !kb_.buf.empty())
            kb_.buf.pop_back();

        if (done)
            close_keyboard(true);
        else if (cancel)
            close_keyboard(false);
    }

    void draw_cursor() {
        if (!ir_.valid)
            return;

        if (cursor_) {
            GRRLIB_DrawImg(ir_.x - cursor_->w / 2, ir_.y - cursor_->h / 2, cursor_, ir_.angle, 1, 1, 0xFFFFFFFF);
        } else {
            GRRLIB_Circle(ir_.x, ir_.y, 7, 0xFFFFFFFF, true);
            GRRLIB_Circle(ir_.x, ir_.y, 4, 0x000000FF, true);
        }
    }
};

}
