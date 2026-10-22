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

#include <cstdint>

namespace smb::policy {

inline constexpr uint32_t file_write_data       = 0x00000002;
inline constexpr uint32_t file_append_data      = 0x00000004;
inline constexpr uint32_t file_write_ea         = 0x00000010;
inline constexpr uint32_t file_delete_child     = 0x00000040;
inline constexpr uint32_t file_write_attributes = 0x00000100;
inline constexpr uint32_t delete_access         = 0x00010000;
inline constexpr uint32_t write_dac             = 0x00040000;
inline constexpr uint32_t write_owner           = 0x00080000;
inline constexpr uint32_t generic_all           = 0x10000000;
inline constexpr uint32_t generic_write         = 0x40000000;

inline constexpr uint32_t write_access_mask =
    file_write_data | file_append_data | file_write_ea | file_delete_child | file_write_attributes |
    delete_access | write_dac | write_owner | generic_all | generic_write;

inline constexpr uint32_t disp_supersede    = 0;
inline constexpr uint32_t disp_open         = 1;
inline constexpr uint32_t disp_create       = 2;
inline constexpr uint32_t disp_open_if      = 3;
inline constexpr uint32_t disp_overwrite    = 4;
inline constexpr uint32_t disp_overwrite_if = 5;

inline constexpr uint32_t file_delete_on_close = 0x00001000;
inline constexpr uint32_t read_only_maximal_access = 0x001200A9;
inline constexpr uint32_t file_read_only_volume = 0x00080000;
inline constexpr uint32_t status_access_denied = 0xC0000022;

inline constexpr bool create_needs_write(uint32_t desired_access, uint32_t disposition, uint32_t create_options) {
    if (desired_access & write_access_mask) return true;
    if (create_options & file_delete_on_close) return true;
    return disposition == disp_supersede || disposition == disp_create ||
           disposition == disp_overwrite || disposition == disp_overwrite_if;
}

}