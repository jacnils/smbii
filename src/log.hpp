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

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace smb {

class logger {
public:
	static logger& instance() {
		static logger inst;
		return inst;
	}

	void init(const std::string& path) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (file_) std::fclose(file_);
		file_ = std::fopen(path.c_str(), "a");
	}

	void log(const char* fmt, ...) {
		std::lock_guard<std::mutex> lock(mutex_);

		std::time_t t = std::time(nullptr);
		char timebuf[16]{};

		if (const auto* tm = std::localtime(&t))
			std::strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm);

		SYS_Report("[%s] ", timebuf);

		va_list args;
		va_start(args, fmt);

		char buffer[1024];
		std::vsnprintf(buffer, sizeof(buffer), fmt, args);

		va_end(args);

		SYS_Report("%s\n", buffer);
	}

private:
	std::FILE* file_ = nullptr;
	std::mutex mutex_;
};

} // namespace smb

#define SMB_LOG(...) ::smb::logger::instance().log(__VA_ARGS__)
