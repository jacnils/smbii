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

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <vector>

namespace smb::ntlm {

using bytes = std::vector<std::uint8_t>;

namespace der {
	inline bytes length(std::size_t len) {
		if (len < 128)
			return {static_cast<std::uint8_t>(len)};

		bytes len_bytes;
		std::size_t remaining = len;

		while (remaining > 0) {
			len_bytes.insert(len_bytes.begin(), static_cast<std::uint8_t>(remaining & 0xFF));
			remaining >>= 8;
		}

		bytes out;

		out.push_back(static_cast<std::uint8_t>(0x80 | len_bytes.size()));
		out.insert(out.end(), len_bytes.begin(), len_bytes.end());

		return out;
	}

	inline bytes wrap(std::uint8_t tag, const bytes& content) {
		bytes out;

		out.push_back(tag);

		auto len = length(content.size());

		out.insert(out.end(), len.begin(), len.end());
		out.insert(out.end(), content.begin(), content.end());

		return out;
	}

	inline bytes concat(std::initializer_list<bytes> parts) {
		bytes out;
		for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
		return out;
	}
}

inline bytes oid_spnego() {
	return {0x06, 0x06, 0x2B, 0x06, 0x01, 0x05, 0x05, 0x02};
}

inline bytes oid_ntlmssp() {
	return {0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0A};
}

inline bytes build_neg_token_init() {
	bytes mech_type_list = der::wrap(0x30, oid_ntlmssp());
	bytes mech_types = der::wrap(0xA0, mech_type_list);
	bytes neg_token_init = der::wrap(0x30, mech_types);
	bytes neg_token_init_choice = der::wrap(0xA0, neg_token_init);

	bytes inner = der::concat({oid_spnego(), neg_token_init_choice});

	return der::wrap(0x60, inner);
}

inline bytes build_neg_token_resp_challenge(const bytes& ntlmssp_challenge) {
	bytes neg_state = der::wrap(0xA0, der::wrap(0x0A, {0x01}));
	bytes supported_mech = der::wrap(0xA1, oid_ntlmssp());
	bytes response_token = der::wrap(0xA2, der::wrap(0x04, ntlmssp_challenge));

	bytes body = der::concat({neg_state, supported_mech, response_token});
	bytes neg_token_resp = der::wrap(0x30, body);

	return der::wrap(0xA1, neg_token_resp);
}

inline bytes build_neg_token_resp_success() {
	bytes neg_state = der::wrap(0xA0, der::wrap(0x0A, {0x00})); // accept-completed(0)
	bytes neg_token_resp = der::wrap(0x30, neg_state);
	return der::wrap(0xA1, neg_token_resp);
}

namespace detail {
	inline void put_u8(bytes& b, std::uint8_t v) {
		b.push_back(v);
	}

	inline void put_u16(bytes& b, std::uint16_t v) {
		put_u8(b, v & 0xFF); put_u8(b, (v >> 8) & 0xFF);
	}

	inline void put_u32(bytes& b, std::uint32_t v) {
		put_u16(b, v & 0xFFFF); put_u16(b, (v >> 16) & 0xFFFF);
	}

	// TODO: unicode
	inline bytes utf16le(const std::string& s) {
		bytes out;
		for (char c : s) put_u16(out, static_cast<std::uint16_t>(static_cast<unsigned char>(c)));
		return out;
	}

	inline void put_av_pair(bytes& out, std::uint16_t av_id, const bytes& value) {
		put_u16(out, av_id);
		put_u16(out, static_cast<std::uint16_t>(value.size()));
		out.insert(out.end(), value.begin(), value.end());
	}

	constexpr std::uint16_t msv_av_eol = 0x0000;
	constexpr std::uint16_t msv_av_nb_computer_name = 0x0001;
	constexpr std::uint16_t msv_av_nb_domain_name = 0x0002;

	inline void assert_eq_or_throw(std::size_t actual, std::size_t expected) {
		if (actual != expected) throw std::runtime_error("ntlm: size mismatch when building a message");
	}
}

inline bytes build_ntlmssp_challenge(const std::array<std::uint8_t, 8>& server_challenge) {
	using namespace detail;

	bytes target_name = utf16le("WORKGROUP");

	bytes target_info;
	put_av_pair(target_info, msv_av_nb_domain_name, utf16le("WORKGROUP")); // TODO: make customizable
	put_av_pair(target_info, msv_av_nb_computer_name, utf16le("WII")); // TODO: make customizable
	put_av_pair(target_info, msv_av_eol, {});

	bytes msg;
	const char sig[8] = {'N', 'T', 'L', 'M', 'S', 'S', 'P', '\0'};

	for (char c : sig)
		put_u8(msg, static_cast<std::uint8_t>(c));

	put_u32(msg, 2);

	std::size_t fixed_header_size = 48;
	std::uint32_t target_name_offset = static_cast<std::uint32_t>(fixed_header_size);
	std::uint32_t target_info_offset = static_cast<std::uint32_t>(fixed_header_size + target_name.size());

	put_u16(msg, static_cast<std::uint16_t>(target_name.size()));
	put_u16(msg, static_cast<std::uint16_t>(target_name.size()));
	put_u32(msg, target_name_offset);

	constexpr std::uint32_t negotiate_unicode = 0x00000001;
	constexpr std::uint32_t request_target = 0x00000004;
	constexpr std::uint32_t negotiate_ntlm = 0x00000200;
	constexpr std::uint32_t negotiate_always_sign = 0x00008000;
	constexpr std::uint32_t target_type_server = 0x00020000;
	constexpr std::uint32_t negotiate_extended_sessionsecurity = 0x00080000;
	constexpr std::uint32_t negotiate_target_info = 0x00800000;
	constexpr std::uint32_t negotiate_128 = 0x20000000;
	constexpr std::uint32_t negotiate_56 = 0x80000000;

	put_u32(msg, negotiate_unicode | request_target | negotiate_ntlm | negotiate_always_sign |
	                 target_type_server | negotiate_extended_sessionsecurity | negotiate_target_info |
	                 negotiate_128 | negotiate_56);

	for (auto b : server_challenge)
		put_u8(msg, b);

	put_u32(msg, 0);
	put_u32(msg, 0);

	put_u16(msg, static_cast<std::uint16_t>(target_info.size()));
	put_u16(msg, static_cast<std::uint16_t>(target_info.size()));
	put_u32(msg, target_info_offset);

	assert_eq_or_throw(msg.size(), fixed_header_size);

	msg.insert(msg.end(), target_name.begin(), target_name.end());
	msg.insert(msg.end(), target_info.begin(), target_info.end());

	return msg;
}

inline std::optional<std::uint32_t> find_ntlmssp_message_type(const std::uint8_t* data, std::size_t len) {
	static const std::uint8_t sig[8] = { 'N', 'T', 'L', 'M', 'S', 'S', 'P', '\0' };

	if (len < 12)
		return std::nullopt;

	for (std::size_t i = 0; i + 12 <= len; ++i) {
		if (std::memcmp(data + i, sig, 8) == 0) {
			std::uint32_t type = static_cast<std::uint32_t>(data[i + 8]) |
			                      (static_cast<std::uint32_t>(data[i + 9]) << 8) |
			                      (static_cast<std::uint32_t>(data[i + 10]) << 16) |
			                      (static_cast<std::uint32_t>(data[i + 11]) << 24);
			return type;
		}
	}

	return std::nullopt;
}

}
