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

#include <ogc/es.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <malloc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "wad_parse.hpp"
#include "wad_ticket.hpp"

namespace wad {

class installer {
public:
    enum class phase_t {
        idle,
        running,
        done
    };

    struct status_t {
        phase_t phase = phase_t::idle;
        int percent = 0;
        std::string title;
        std::string stage;
        std::string message;
        bool ok = false;
    };

    installer(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {
        LWP_MutexInit(&lock_, false);
        valid_ = parse(data_, size_, info_, error_);
    }

    void set_iospatch(bool active) {
        patched_ = active;
    }

    bool valid() const {
        return valid_;
    }
    const std::string& error() const {
        return error_;
    }

    bool installed() const {
        if (!valid_)
            return false;

        u32 tmd_size = 0;

        return ES_GetStoredTMDSize(info_.title_id, &tmd_size) >= 0;
    }

    bool start_install()   { return start(true); }
    bool start_uninstall() { return start(false); }

    status_t status() {
        LWP_MutexLock(lock_);
        status_t s = st_;
        LWP_MutexUnlock(lock_);
        return s;
    }

    void acknowledge() {
        LWP_MutexLock(lock_);
        if (st_.phase == phase_t::done) st_ = status_t{};
        LWP_MutexUnlock(lock_);
    }

    void shutdown() {
        if (thread_started_) {
            LWP_JoinThread(thread_, nullptr);
            thread_started_ = false;
        }
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    info_t info_;
    bool valid_ = false;
    std::string error_;

    mutex_t lock_{};
    status_t st_;
    lwp_t thread_{};
    bool thread_started_ = false;
    bool installing_ = true;
    bool patched_ = true;

    bool start(bool install) {
        if (!valid_)
            return false;

        LWP_MutexLock(lock_);

        const bool busy = st_.phase == phase_t::running;

        if (!busy) {
            st_ = status_t{};
            st_.phase = phase_t::running;
            st_.title = install ? "Installing" : "Uninstalling";
            st_.stage = "Starting";
        }

        LWP_MutexUnlock(lock_);

        if (busy)
            return false;

        if (thread_started_) {
            LWP_JoinThread(thread_, nullptr);
            thread_started_ = false;
        }

        installing_ = install;

        if (LWP_CreateThread(&thread_, &installer::entry, this, nullptr, 64 * 1024, 50) != 0) {
            finish(false, "Couldn't start the worker thread");
            return false;
        }

        thread_started_ = true;
        return true;
    }

    static void* entry(void* self) {
        static_cast<installer*>(self)->work();
        return nullptr;
    }

    void work() {
        std::string err;
        const bool ok = installing_ ? do_install(err) : do_uninstall(err);

        if (ok)
            finish(true, installing_ ? "Installed" : "Removed");
        else
            finish(false, err);
    }

    void set_stage(const std::string& stage, int percent) {
        LWP_MutexLock(lock_);

        st_.stage = stage;
        st_.percent = std::max(0, std::min(percent, 100));

        LWP_MutexUnlock(lock_);
    }

    void finish(bool ok, const std::string& message) {
        LWP_MutexLock(lock_);

        st_.phase = phase_t::done;
        st_.ok = ok;
        st_.title = ok ? "Done" : "Failed";
        st_.message = message;
        st_.percent = ok ? 100 : st_.percent;

        LWP_MutexUnlock(lock_);
    }

    std::string es_error(const char* what, s32 code) const {
        char buf[200];
        switch (code) {
            case -1017:
            case -1022:
                    std::snprintf(buf, sizeof buf, "%s failed (ES %d): the WAD's signature or hash was rejected.",
                                  what, static_cast<int>(code));
                break;
            case -2011:
                    std::snprintf(buf, sizeof buf,
                                  "%s failed (IOSC %d): signature verification failed. Add <ahb_access/> to meta.xml.",
                                  what, static_cast<int>(code));
                break;
            case -1035:
                std::snprintf(buf, sizeof buf,
                              "%s failed (ES %d): a newer version of this title is already installed. Uninstall it first.",
                              what, static_cast<int>(code));
                break;
            default:
                std::snprintf(buf, sizeof buf, "%s failed (ES error %d)", what, static_cast<int>(code));
        }

        return buf;
    }

    struct aligned_buf {
        void* p = nullptr;
        explicit aligned_buf(std::size_t n) {
            const std::size_t sz = static_cast<std::size_t>(align_up(std::max<std::size_t>(n, 32), 32));
            p = memalign(32, sz);
            if (p) std::memset(p, 0, sz);
        }
        ~aligned_buf() { if (p) free(p); }
        aligned_buf(const aligned_buf&) = delete;
        aligned_buf& operator=(const aligned_buf&) = delete;
        u8* bytes() const { return static_cast<u8*>(p); }
    };

    bool do_install(std::string& err) {
        aligned_buf certs(info_.certs_size), crl(info_.crl_size), tik(info_.tik_size), tmd(info_.tmd_size);
        aligned_buf chunk(0x4000);

        if (!certs.p || !crl.p || !tik.p || !tmd.p || !chunk.p) {
            err = "Out of memory";
            return false;
        }

        std::memcpy(certs.p, info_.certs, info_.certs_size);

        if (info_.crl_size)
            std::memcpy(crl.p, info_.crl, info_.crl_size);

        std::memcpy(tik.p, info_.tik, info_.tik_size);
        std::memcpy(tmd.p, info_.tmd, info_.tmd_size);

        fix_ticket(tik.bytes(), info_.tik_size);

        signed_blob* crl_blob = info_.crl_size ? static_cast<signed_blob*>(crl.p) : nullptr;

        set_stage("Adding ticket", 0);
        s32 r = ES_AddTicket(static_cast<signed_blob*>(tik.p), info_.tik_size,
                             static_cast<signed_blob*>(certs.p), info_.certs_size, crl_blob, info_.crl_size);
        if (r < 0) {
            ES_AddTitleCancel();
            err = es_error("Adding the ticket", r);
            return false;
        }

        set_stage("Starting title install", 0);

        r = ES_AddTitleStart(static_cast<signed_blob*>(tmd.p), info_.tmd_size,
                             static_cast<signed_blob*>(certs.p), info_.certs_size, crl_blob, info_.crl_size);

        if (r < 0) {
            ES_AddTitleCancel();
            err = es_error("Starting the install", r);
            return false;
        }

        std::uint64_t done = 0;
        const std::size_t count = info_.contents.size();

        for (std::size_t i = 0; i < count; ++i) {
            const content_t& c = info_.contents[i];
            const std::string label = "Installing content " + std::to_string(i + 1) + " of " + std::to_string(count);

            set_stage(label, info_.total_bytes ? static_cast<int>(done * 100 / info_.total_bytes) : 0);

            const s32 cfd = ES_AddContentStart(info_.title_id, c.cid);

            if (cfd < 0) {
                ES_AddTitleCancel();
                err = es_error("Starting a content", cfd);
                return false;
            }

            std::uint32_t remaining = c.enc_len;
            std::size_t off = c.offset;

            while (remaining > 0) {
                const std::uint32_t n = std::min<std::uint32_t>(remaining, 0x4000);

                std::memset(chunk.p, 0, n);
                if (off < size_)
                    std::memcpy(chunk.p, data_ + off, std::min<std::size_t>(n, size_ - off));

                r = ES_AddContentData(cfd, chunk.bytes(), n);

                if (r < 0) {
                    ES_AddTitleCancel();
                    err = es_error("Writing content", r);
                    return false;
                }

                off += n;
                remaining -= n;
                done += n;
                set_stage(label, info_.total_bytes ? static_cast<int>(done * 100 / info_.total_bytes) : 0);
            }

            r = ES_AddContentFinish(cfd);

            if (r < 0) {
                ES_AddTitleCancel();
                err = es_error("Finishing a content", r);
                return false;
            }
        }

        set_stage("Finishing", 99);
        r = ES_AddTitleFinish();

        if (r < 0) {
            ES_AddTitleCancel();
            err = es_error("Finishing the install", r);
            return false;
        }

        return true;
    }

    bool do_uninstall(std::string& err) {
        const std::uint64_t id = info_.title_id;

        set_stage("Removing tickets", 10);
        u32 n = 0;

        if (ES_GetNumTicketViews(id, &n) >= 0 && n > 0) {
            tikview* views = static_cast<tikview*>(memalign(32, align_up(n * sizeof(tikview), 32)));
            if (views) {
                if (ES_GetTicketViews(id, views, n) >= 0)
                    for (u32 i = 0; i < n; ++i)
                        ES_DeleteTicket(&views[i]);

                free(views);
            }
        }

        set_stage("Removing content", 40);

        ES_DeleteTitleContent(id);

        set_stage("Removing title", 75);

        const s32 r = ES_DeleteTitle(id);

        if (installed()) {
            err = es_error("Removing the title", r < 0 ? r : -1);
            return false;
        }

        return true;
    }
};

}