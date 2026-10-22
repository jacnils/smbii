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
#include <string>
#include <vector>

#include <gccore.h>

#include <asndlib.h>
#include <ogc/lwp.h>
#include <malloc.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include "oggplayer.h"

namespace audio {

struct sample {
    void* data{};
    u32 size{};
    s32 fmt{};
    s32 rate{};
};

class engine {
public:
    bool init() {
        if (ready_)
            return true;

        ASND_Init();
        ASND_Pause(0);

        ready_ = true;
        return true;
    }

    void shutdown() {
        stop_music();

        if (!ready_)
            return;

        ASND_Pause(1);
        ASND_End();

        ready_ = false;
    }

    static sample load_wav(const std::vector<u8>& f) {
        if (f.size() < 12 || std::memcmp(f.data(), "RIFF", 4) || std::memcmp(f.data() + 8, "WAVE", 4))
            return {};

        auto rd16 = [&](size_t o) {
            return static_cast<u32>(f[o] | (f[o + 1] << 8));
        };

        auto rd32 = [&](size_t o) {
            return static_cast<u32>(f[o] | (f[o + 1] << 8) | (f[o + 2] << 16) | (static_cast<u32>(f[o + 3]) << 24));
        };

        u32 fmt = 0, ch = 0, rate = 0, bits = 0;
        const u8* pcm = nullptr;
        size_t pcm_len = 0;

        size_t o = 12;

        while (o + 8 <= f.size()) {
            const u32 len = rd32(o + 4);
            const size_t body = o + 8;

            if (!std::memcmp(&f[o], "fmt ", 4) && body + 16 <= f.size()) {
                fmt = rd16(body); ch = rd16(body + 2); rate = rd32(body + 4); bits = rd16(body + 14);
            } else if (!std::memcmp(&f[o], "data", 4)) {
                if (body > f.size()) break;
                pcm = &f[body];
                pcm_len = std::min<size_t>(len, f.size() - body);
                break;
            }
            o = body + len + (len & 1);
        }

        if (!pcm || fmt != 1 || (ch != 1 && ch != 2) || (bits != 8 && bits != 16) || rate == 0)
            return {};

        const size_t n = pcm_len / (bits / 8);
        if (n == 0) return {};
        const size_t padded = (n * 2 + 31) & ~static_cast<size_t>(31);

        s16* out = static_cast<s16*>(memalign(32, padded));
        if (!out) return {};
        std::memset(out, 0, padded);

        for (size_t i = 0; i < n; ++i) {
            out[i] = bits == 16 ? static_cast<s16>(pcm[2 * i] | (pcm[2 * i + 1] << 8))
                                : static_cast<s16>((static_cast<int>(pcm[i]) - 128) << 8);
        }

        sample s;
        s.data = out;
        s.size = static_cast<u32>(padded);
        s.fmt = ch == 2 ? VOICE_STEREO_16BIT : VOICE_MONO_16BIT;
        s.rate = static_cast<s32>(rate);

        return s;
    }

    static void free_sample(sample& s) {
        if (s.data)
            std::free(s.data);

        s = {};
    }

    void play(const sample& s, int volume_pct) {
        if (!ready_ || !s.data || volume_pct <= 0)
            return;

        const int vol = std::min(255, volume_pct * 255 / 100);

        for (int v = first_sfx_voice; v < first_sfx_voice + sfx_voices; ++v) {
            if (ASND_StatusVoice(v) == SND_UNUSED) {
                ASND_SetVoice(v, s.fmt, s.rate, 0, s.data, static_cast<s32>(s.size), vol, vol, nullptr);
                return;
            }
        }
    }

    void start_music(const std::string& path, int volume_pct, bool loop) {
        stop_music();

        std::ifstream file(path, std::ios::binary | std::ios::ate);

        if (!file)
            throw std::runtime_error("Failed to open music file: " + path);

        const auto size = file.tellg();

        if (size <= 0)
            throw std::runtime_error("Invalid music file size: " + path);

        buffer_.resize(static_cast<std::size_t>(size));
        file.seekg(0, std::ios::beg);

        if (!file.read(reinterpret_cast<char*>(buffer_.data()), static_cast<std::streamsize>(buffer_.size()))) {
            buffer_.clear();
            throw std::runtime_error("Failed to read music file: " + path);
        }

        const int volume = std::clamp(volume_pct, 0, 100);
        
        SetVolumeOgg(volume);

        PlayOgg(
            buffer_.data(),
            static_cast<int>(buffer_.size()),
            0,
            loop ? OGG_INFINITE_TIME : OGG_ONE_TIME
        );
    }

    void stop_music() {
        StopOgg();
        buffer_.clear();
    }
private:
    static constexpr int first_sfx_voice = 1;
    static constexpr int sfx_voices = 6;

    std::vector<std::uint8_t> buffer_;

    bool ready_{};

    void* buf_[2]{};
    size_t buf_bytes_{};
    int channels_{2};
    s32 rate_{44100};
    int vol_{255};
    bool loop_{true};
    lwp_t thread_{};
    bool thread_running_{};
    volatile bool stop_flag_{};
};

}