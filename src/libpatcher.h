/* Copyright (c) 2021 WiiLink24 Developers

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/
#include <gccore.h>

bool patch_memory_range(u32 *start, u32 *end, const u16 original_patch[],
                        const u16 new_patch[], u32 patch_size);

bool patch_ios_range(const u16 original_patch[], const u16 new_patch[],
                     u32 patch_size);

// Applies specific patches.
bool patch_ahbprot_reset_for_ver(s32 ios_version);
bool patch_ahbprot_reset();

// These functions expect AHBPROT has already been disabled via the above.
bool patch_isfs_permissions();
bool patch_es_identify();
bool patch_ios_verify();
bool patch_wiiu_systitleinstall();

// Applies all patches.
bool apply_patches();

bool is_wiiu();
bool is_dolphin();

#define IS_WIIU read16(WIIU_REG) == 0xCAFE
