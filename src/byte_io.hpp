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
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace smb {

class byte_writer {
public:
	void put_u8(std::uint8_t v) { buf_.push_back(static_cast<std::byte>(v)); }

	void put_u16(std::uint16_t v) {
		put_u8(static_cast<std::uint8_t>(v & 0xFF));
		put_u8(static_cast<std::uint8_t>((v >> 8) & 0xFF));
	}

	void put_u32(std::uint32_t v) {
		put_u8(static_cast<std::uint8_t>(v & 0xFF));
		put_u8(static_cast<std::uint8_t>((v >> 8) & 0xFF));
		put_u8(static_cast<std::uint8_t>((v >> 16) & 0xFF));
		put_u8(static_cast<std::uint8_t>((v >> 24) & 0xFF));
	}

	void put_u64(std::uint64_t v) {
		put_u32(static_cast<std::uint32_t>(v & 0xFFFFFFFFu));
		put_u32(static_cast<std::uint32_t>((v >> 32) & 0xFFFFFFFFu));
	}

	void put_bytes(const void* data, std::size_t len) {
		const auto* p = static_cast<const std::uint8_t*>(data);
		for (std::size_t i = 0; i < len; ++i) put_u8(p[i]);
	}

	void put_bytes(const std::vector<std::uint8_t>& data) { put_bytes(data.data(), data.size()); }

	void put_zeros(std::size_t count) {
		for (std::size_t i = 0; i < count; ++i) put_u8(0);
	}

	void put_utf16le(const std::string& s) {
		for (char c : s) put_u16(static_cast<std::uint16_t>(static_cast<unsigned char>(c)));
	}

	[[nodiscard]] std::size_t size() const { return buf_.size(); }

	void patch_u8(std::size_t offset, std::uint8_t v) { buf_[offset] = static_cast<std::byte>(v); }

	void patch_u16(std::size_t offset, std::uint16_t v) {
		buf_[offset] = static_cast<std::byte>(v & 0xFF);
		buf_[offset + 1] = static_cast<std::byte>((v >> 8) & 0xFF);
	}

	void patch_u32(std::size_t offset, std::uint32_t v) {
		buf_[offset] = static_cast<std::byte>(v & 0xFF);
		buf_[offset + 1] = static_cast<std::byte>((v >> 8) & 0xFF);
		buf_[offset + 2] = static_cast<std::byte>((v >> 16) & 0xFF);
		buf_[offset + 3] = static_cast<std::byte>((v >> 24) & 0xFF);
	}

	[[nodiscard]] const std::vector<std::byte>& data() const { return buf_; }

private:
	std::vector<std::byte> buf_;
};

class byte_reader {
public:
	byte_reader(const std::byte* data, std::size_t size) : data_(data), size_(size) {}

	[[nodiscard]] std::size_t pos() const { return pos_; }
	[[nodiscard]] std::size_t remaining() const { return size_ - pos_; }
	void seek(std::size_t p) { pos_ = p; }

	std::uint8_t get_u8() {
		check(1);
		return static_cast<std::uint8_t>(data_[pos_++]);
	}

	std::uint16_t get_u16() {
		std::uint16_t lo = get_u8();
		std::uint16_t hi = get_u8();
		return static_cast<std::uint16_t>(lo | (hi << 8));
	}

	std::uint32_t get_u32() {
		std::uint32_t lo = get_u16();
		std::uint32_t hi = get_u16();
		return lo | (hi << 16);
	}

	std::uint64_t get_u64() {
		std::uint64_t lo = get_u32();
		std::uint64_t hi = get_u32();
		return lo | (hi << 32);
	}

	void get_bytes(void* out, std::size_t len) {
		check(len);
		std::memcpy(out, data_ + pos_, len);
		pos_ += len;
	}

	[[nodiscard]] std::string get_utf16le_at(std::size_t offset, std::size_t len) const {
		check_at(offset, len);
		std::string out;
		out.reserve(len / 2);
		for (std::size_t i = 0; i + 1 < len; i += 2) {
			auto lo = static_cast<std::uint8_t>(data_[offset + i]);
			auto hi = static_cast<std::uint8_t>(data_[offset + i + 1]);
			std::uint16_t ch = static_cast<std::uint16_t>(lo | (hi << 8));
			if (ch < 128) out.push_back(static_cast<char>(ch));
		}
		return out;
	}

	[[nodiscard]] const std::byte* raw() const { return data_; }
	[[nodiscard]] std::size_t size() const { return size_; }

private:
	void check(std::size_t len) const {
		if (pos_ + len > size_) throw std::out_of_range("message truncated");
	}

	void check_at(std::size_t offset, std::size_t len) const {
		if (offset + len > size_ || offset + len < offset) throw std::out_of_range("field out of bounds");
	}

	const std::byte* data_;
	std::size_t size_;
	std::size_t pos_ = 0;
};

}
