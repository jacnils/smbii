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

#include <ogc/isfs.h>
#include <ogc/mutex.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <errno.h>
#include <malloc.h>
#include <fcntl.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace nand {

namespace detail {

inline constexpr int32_t err_noent = -106;

inline mutex_t lock{};
inline bool mounted = false;

struct guard {
    guard()  { LWP_MutexLock(lock); }
    ~guard() { LWP_MutexUnlock(lock); }
};

alignas(32) inline u8 bounce[16 * 1024];

struct file_state {
    int32_t fd;
};

struct dir_state {
    char path[ISFS_MAXPATH];
    char* names;
    char* cur;
    uint32_t count;
    uint32_t index;
};

inline bool to_path(const char* in, char* out) {
    if (const char* colon = std::strchr(in, ':'))
        in = colon + 1;

    std::vector<std::string> parts;
    std::string cur;

    for (const char* p = in;; ++p) {
        const char ch = (*p == '\\') ? '/' : *p;
        if (ch == '/' || ch == 0) {
            if (cur == "..") { if (!parts.empty()) parts.pop_back(); }
            else if (!cur.empty() && cur != ".") parts.push_back(cur);
            cur.clear();
            if (ch == 0) break;
        } else {
            cur += ch;
        }
    }

    std::string s;

    for (const auto& part : parts)
        s += "/" + part;

    if (s.empty())
        s = "/";

    if (s.size() >= ISFS_MAXPATH)
        return false;

    std::memcpy(out, s.c_str(), s.size() + 1);

    return true;
}

inline bool is_dir(const char* path) {
    uint32_t n = 0;

    return ISFS_ReadDir(path, nullptr, &n) == ISFS_OK;
}

inline bool file_size(const char* path, uint32_t& size) {
    const int32_t fd = ISFS_Open(path, ISFS_OPEN_READ);

    if (fd >= 0) {
        alignas(32) static fstats fs;
        size = ISFS_GetFileStats(fd, &fs) == ISFS_OK ? fs.file_length : 0;
        ISFS_Close(fd);
        return true;
    }

    size = 0;

    return fd != err_noent;
}

inline bool fill_stat(const char* path, struct stat* st) {
    std::memset(st, 0, sizeof(*st));

    st->st_blksize = 4096;

    if (is_dir(path)) {
        st->st_mode = S_IFDIR | 0555;
        st->st_nlink = 2;
        return true;
    }

    uint32_t size;

    if (!file_size(path, size))
        return false;

    st->st_mode = S_IFREG | 0444;
    st->st_nlink = 1;
    st->st_size = size;
    st->st_blocks = (size + 511) / 512;

    return true;
}

inline int open_r(struct _reent* r, void* fileStruct, const char* path, int flags, int) {
    if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND | O_EXCL)) { r->_errno = EROFS; return -1; }

    char p[ISFS_MAXPATH];

    if (!to_path(path, p)) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }

    guard g;

    if (is_dir(p)) {
        r->_errno = EISDIR;
        return -1;
    }

    const int32_t fd = ISFS_Open(p, ISFS_OPEN_READ);
    if (fd < 0) {
        r->_errno = (fd == err_noent) ? ENOENT : EACCES;
        return -1;
    }

    static_cast<file_state*>(fileStruct)->fd = fd;
    return static_cast<int>(reinterpret_cast<intptr_t>(fileStruct));
}

inline int close_r(struct _reent*, void* fd) {
    guard g;

    ISFS_Close(static_cast<file_state*>(fd)->fd);

    return 0;
}

inline ssize_t read_r(struct _reent* r, void* fd, char* ptr, size_t len) {
    const int32_t h = static_cast<file_state*>(fd)->fd;
    guard g;

    size_t total = 0;
    while (total < len) {
        const uint32_t want = static_cast<uint32_t>(std::min(len - total, sizeof(bounce)));
        const int32_t got = ISFS_Read(h, bounce, want);

        if (got < 0) {
            if (total) return static_cast<ssize_t>(total);
            r->_errno = EIO;
            return -1;
        }

        if (got == 0)
            break;

        std::memcpy(ptr + total, bounce, got);
        total += got;

        if (static_cast<uint32_t>(got) < want)
            break;
    }

    return static_cast<ssize_t>(total);
}

inline off_t seek_r(struct _reent* r, void* fd, off_t pos, int dir) {
    guard g;

    const int32_t res = ISFS_Seek(static_cast<file_state*>(fd)->fd, static_cast<int32_t>(pos), dir);   // SEEK_* == IPC_SEEK_*
    if (res < 0) {
        r->_errno = EINVAL;
        return -1;
    }

    return res;
}

inline int fstat_r(struct _reent* r, void* fd, struct stat* st) {
    guard g;
    alignas(32) static fstats fs;

    if (ISFS_GetFileStats(static_cast<file_state*>(fd)->fd, &fs) != ISFS_OK) {
        r->_errno = EIO; return -1;
    }

    std::memset(st, 0, sizeof(*st));

    st->st_mode = S_IFREG | 0444;
    st->st_nlink = 1;
    st->st_size = fs.file_length;
    st->st_blksize = 4096;
    st->st_blocks = (fs.file_length + 511) / 512;

    return 0;
}

inline int stat_r(struct _reent* r, const char* file, struct stat* st) {
    char p[ISFS_MAXPATH];

    if (!to_path(file, p)) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }

    guard g;

    if (!fill_stat(p, st)) {
        r->_errno = ENOENT;
        return -1;
    }

    return 0;
}

inline ssize_t write_r(struct _reent* r, void*, const char*, size_t) {
    r->_errno = EROFS;
    return -1;
}

inline int link_r(struct _reent* r, const char*, const char*) {
    r->_errno = EROFS;
    return -1;
}

inline int unlink_r(struct _reent* r, const char*) {
    r->_errno = EROFS;
    return -1;
}

inline int rename_r(struct _reent* r, const char*, const char*) {
    r->_errno = EROFS;
    return -1;
}

inline int mkdir_r(struct _reent* r, const char*, int) {
    r->_errno = EROFS;
    return -1;
}

inline DIR_ITER* diropen_r(struct _reent* r, DIR_ITER* dirState, const char* path) {
    char p[ISFS_MAXPATH];

    if (!to_path(path, p)) {
        r->_errno = ENAMETOOLONG;
        return nullptr;
    }

    guard g;
    uint32_t n = 0;

    if (ISFS_ReadDir(p, nullptr, &n) != ISFS_OK) {
        r->_errno = ENOENT;
        return nullptr;
    }

    auto* d = static_cast<dir_state*>(dirState->dirStruct);
    std::memset(d, 0, sizeof(*d));
    std::strcpy(d->path, p);

    if (n > 0) {
        const size_t bytes = (static_cast<size_t>(n) * 13 + 31) & ~static_cast<size_t>(31);
        d->names = static_cast<char*>(memalign(32, bytes));

        if (!d->names) {
            r->_errno = ENOMEM;
            return nullptr;
        }

        std::memset(d->names, 0, bytes);

        if (ISFS_ReadDir(p, d->names, &n) != ISFS_OK) {
            free(d->names);
            d->names = nullptr;
            r->_errno = EIO;
            return nullptr;
        }
    }

    d->count = n;
    d->cur = d->names;

    return dirState;
}

inline int dirreset_r(struct _reent*, DIR_ITER* dirState) {
    auto* d = static_cast<dir_state*>(dirState->dirStruct);

    d->cur = d->names;
    d->index = 0;

    return 0;
}

inline int dirnext_r(struct _reent* r, DIR_ITER* dirState, char* filename, struct stat* st) {
    auto* d = static_cast<dir_state*>(dirState->dirStruct);

    if (d->index >= d->count || !d->cur) {
        r->_errno = ENOENT;
        return -1;
    }

    const char* name = d->cur;

    d->cur += std::strlen(name) + 1;
    ++d->index;

    std::strcpy(filename, name);

    std::string child = d->path;

    if (child.back() != '/')
        child += '/';

    child += name;

    guard g;

    if (child.size() >= ISFS_MAXPATH || !fill_stat(child.c_str(), st)) {
        std::memset(st, 0, sizeof(*st));

        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;
    }

    return 0;
}

inline int dirclose_r(struct _reent*, DIR_ITER* dirState) {
    auto* d = static_cast<dir_state*>(dirState->dirStruct);

    if (d->names)
        free(d->names);

    d->names = d->cur = nullptr;

    return 0;
}

inline int statvfs_r(struct _reent*, const char*, struct statvfs* b) {
    std::memset(b, 0, sizeof(*b));

    b->f_bsize = b->f_frsize = 16384;
    b->f_blocks = 32768;
    b->f_flag = 1; // read only
    b->f_namemax = 12;

    return 0;
}

}

inline bool mount() {
    using namespace detail;

    if (mounted)
        return true;

    if (ISFS_Initialize() < 0)
        return false;

    LWP_MutexInit(&lock, true);

    static devoptab_t dt{};

    dt.name = "nand";
    dt.structSize = sizeof(file_state);
    dt.open_r = open_r;
    dt.close_r = close_r;
    dt.write_r = write_r;
    dt.read_r = read_r;
    dt.seek_r = seek_r;
    dt.fstat_r = fstat_r;
    dt.stat_r = stat_r;
    dt.link_r = link_r;
    dt.unlink_r = unlink_r;
    dt.rename_r = rename_r;
    dt.mkdir_r = mkdir_r;
    dt.dirStateSize = sizeof(dir_state);
    dt.diropen_r = diropen_r;
    dt.dirreset_r = dirreset_r;
    dt.dirnext_r = dirnext_r;
    dt.dirclose_r = dirclose_r;
    dt.statvfs_r = statvfs_r;

    if (AddDevice(&dt) < 0)
        return false;

    mounted = true;

    return true;
}

inline bool available() {
    return detail::mounted;
}

}