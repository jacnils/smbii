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

#include "byte_io.hpp"
#include "log.hpp"
#include "ntlm.hpp"
#include "auth.hpp"
#include "ntlmv2.hpp"
#include "signing.hpp"
#include "credentials.hpp"
#include "protocol.hpp"

#include <netkit/tcp/tcp_stream.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <limits>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace smb {

// copypaste and refactor from netkit
inline void fill_random(std::uint8_t* output, std::size_t sz) {
	static std::uint32_t state = 0;

	if (state == 0) {
		state = static_cast<std::uint32_t>(gettime());
		state ^= static_cast<std::uint32_t>(
			reinterpret_cast<std::uintptr_t>(&state));
		state ^= static_cast<std::uint32_t>(
			reinterpret_cast<std::uintptr_t>(output));

		if (state == 0)
			state = 0x9E3779B9u;
	}

	for (std::size_t i = 0; i < sz; ++i) {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;

		if ((i & 7) == 0) {
			state ^= static_cast<std::uint32_t>(gettime());

			if (state == 0)
				state = 0x9E3779B9u;
		}

		output[i] = static_cast<char>(state & 0xFF);
	}
}

}

namespace smb {

inline std::uint64_t unix_time_to_filetime(std::time_t t) {
	constexpr std::uint64_t epoch_diff = 116444736000000000ULL;
	return static_cast<std::uint64_t>(t) * 10000000ULL + epoch_diff;
}

inline std::uint64_t filetime_now() {
	return unix_time_to_filetime(std::time(nullptr));
}

inline constexpr std::uint16_t max_credit_grant = 16;
inline constexpr std::uint32_t max_io_size = 64 * 1024;
inline constexpr std::uint32_t max_message_size = 1024 * 1024;

namespace ntstatus {
inline constexpr std::uint32_t no_such_file = 0xC000000F;
inline constexpr std::uint32_t disk_full = 0xC000007F;
inline constexpr std::uint32_t file_is_a_directory = 0xC00000BA;
inline constexpr std::uint32_t bad_network_name = 0xC00000CC;
inline constexpr std::uint32_t directory_not_empty = 0xC0000101;
inline constexpr std::uint32_t not_a_directory = 0xC0000103;
}

namespace local {
inline constexpr std::uint32_t delete_access = 0x00010000;
inline constexpr std::uint32_t generic_all = 0x10000000;
inline constexpr std::uint32_t maximum_allowed = 0x02000000;
inline constexpr std::uint32_t non_directory_file = 0x00000040;
inline constexpr std::uint32_t delete_on_close = 0x00001000;
	
inline constexpr std::uint32_t write_access_mask = 0x00000002 | 0x00000004 | 0x00000010 | 0x00000040 |
                                                    0x00000100 | 0x00010000 | 0x00040000 | 0x00080000 |
                                                    0x10000000 | 0x40000000;
inline constexpr std::uint32_t full_access = 0x001F01FF;
inline constexpr std::uint32_t read_only_access = 0x001200A9; // FILE_GENERIC_READ | FILE_GENERIC_EXECUTE
inline constexpr std::uint32_t read_only_volume = 0x00080000; // FILE_READ_ONLY_VOLUME
inline constexpr std::uint32_t logon_failure = 0xC000006D;    // STATUS_LOGON_FAILURE

inline bool has_delete_access(std::uint32_t desired_access) {
	return (desired_access & (delete_access | generic_all | maximum_allowed)) != 0;
}
}

inline bool iequals(const std::string& a, const std::string& b) {
	if (a.size() != b.size()) return false;

	for (std::size_t i = 0; i < a.size(); ++i) {
		char x = a[i], y = b[i];
		if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
		if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
		if (x != y) return false;
	}

	return true;
}

struct file_times {
	std::uint64_t creation = 0, access = 0, write = 0, change = 0;
};

struct file_stat {
	bool is_dir = false;
	std::uint64_t size = 0;
	file_times times;
};

inline file_stat stat_path(const std::filesystem::path& p) {
	file_stat out;
	struct stat sb;
	const std::string s = p.string();

	if (::stat(s.c_str(), &sb) == 0) {
		out.is_dir = S_ISDIR(sb.st_mode);
		out.size = out.is_dir ? 0 : static_cast<std::uint64_t>(sb.st_size);

		const std::uint64_t mtime = unix_time_to_filetime(sb.st_mtime);

		out.times.write = mtime;
		out.times.change = mtime;
		out.times.access = sb.st_atime ? unix_time_to_filetime(sb.st_atime) : mtime;
		out.times.creation = sb.st_ctime ? unix_time_to_filetime(sb.st_ctime) : mtime;

		return out;
	}

	std::error_code ec;

	out.is_dir = std::filesystem::is_directory(p, ec);
	out.size = out.is_dir ? 0 : static_cast<std::uint64_t>(std::filesystem::file_size(p, ec));

	if (ec)
		out.size = 0;

	const std::uint64_t now = filetime_now();

	out.times.creation = out.times.access = out.times.write = out.times.change = now;

	return out;
}

struct open_handle {
	std::filesystem::path host_path;
	bool is_directory = false;
	std::FILE* file = nullptr;
	bool read_only = false;
	bool delete_pending = false;
	std::vector<std::filesystem::directory_entry> dir_entries;
	std::uint32_t desired_access = 0;
	std::size_t dir_cursor = 0;
	bool dir_listed_once = false;
};

class connection {
public:
	connection(std::unique_ptr<netkit::tcp::tcp_stream> stream, std::string share_name, std::filesystem::path share_root,
			   bool read_only = false, credentials::info creds = {})
		: stream_(std::move(stream)), share_name_(std::move(share_name)), share_root_(std::move(share_root)),
		  read_only_(read_only), creds_(std::move(creds)) {
		fill_random(server_challenge_.data(), server_challenge_.size());

		std::string peer_desc = "<unknown>";

		try {
			auto peer = stream_->peer();
			peer_desc = peer.get_ip() + ":" + std::to_string(peer.get_port());
		} catch (const std::exception&) {}
		SMB_LOG("connection established, peer=%s%s", peer_desc.c_str(), read_only_ ? " (read-only share)" : "");
	}

	void run() {
		int msg_count = 0;

		try {
			while (true) {
				auto msg = read_message();

				if (!msg) {
					SMB_LOG("connection closed by peer (clean EOF) after %d message(s)", msg_count);
					break;
				}

				++msg_count;

				bool is_smb1_probe = msg->size() >= 4 && static_cast<std::uint8_t>((*msg)[0]) == 0xFF &&
				                      static_cast<std::uint8_t>((*msg)[1]) == 'S' &&
				                      static_cast<std::uint8_t>((*msg)[2]) == 'M' &&
				                      static_cast<std::uint8_t>((*msg)[3]) == 'B';

				if (is_smb1_probe) {
					SMB_LOG("msg #%d: %zu bytes, legacy SMB1-wrapped negotiate probe (proto id 0xFF 'SMB')",
					        msg_count, msg->size());
				} else if (msg->size() >= 4) {
					SMB_LOG("msg #%d: %zu bytes, proto id = %02X %02X %02X %02X", msg_count, msg->size(),
					        static_cast<unsigned>((*msg)[0]), static_cast<unsigned>((*msg)[1]),
					        static_cast<unsigned>((*msg)[2]), static_cast<unsigned>((*msg)[3]));
				} else {
					SMB_LOG("msg #%d: %zu bytes", msg_count, msg->size());
				}

				std::vector<std::byte> response;

				try {
					if (is_smb1_probe) {
						response = handle_smb1_negotiate_probe(*msg);

						SMB_LOG("msg #%d: SMB1 probe -> %s", msg_count,
						        response.empty() ? "no SMB2 dialect offered, nothing we can do"
						                          : "answered with SMB2 negotiate response");
					} else {
						byte_reader peek(msg->data(), msg->size());
						header peeked_hdr = header::parse(peek);
						response = handle_message(*msg);
						std::uint32_t resp_status = 0;

						if (response.size() >= 12)
							resp_status = static_cast<std::uint32_t>(response[8]) | (static_cast<std::uint32_t>(response[9]) << 8) |
							              (static_cast<std::uint32_t>(response[10]) << 16) | (static_cast<std::uint32_t>(response[11]) << 24);

						SMB_LOG("msg #%d: cmd=0x%04X msgid=%llu -> status=0x%08X, %zu bytes", msg_count,
						        static_cast<unsigned>(peeked_hdr.cmd),
						        static_cast<unsigned long long>(peeked_hdr.message_id), resp_status, response.size());
					}
				} catch (const std::exception& e) {
					SMB_LOG("msg #%d: request error: %s", msg_count, e.what());
					response = make_error_response_raw(*msg, status::invalid_parameter);
				}

				if (response.empty() && is_smb1_probe) {
					SMB_LOG("closing connection: no SMB2 support offered");
					break;
				}

				if (!response.empty() && !write_message(response)) {
					SMB_LOG("msg #%d: write_message failed (peer likely closed its read side)", msg_count);
					break;
				}
			}
		} catch (const std::exception& e) {
			SMB_LOG("connection error after %d message(s): %s", msg_count, e.what());
		}

		SMB_LOG("connection ending, %zu handle(s) still open at close", open_files_.size());

		for (auto& [id, h] : open_files_) {
			if (h.file) std::fclose(h.file);
		}
	}

private:
	std::optional<std::vector<std::byte>> read_message() {
		std::uint8_t len_prefix[4];
		if (!read_exact(len_prefix, 4))
			return std::nullopt;

		std::uint32_t len = (static_cast<std::uint32_t>(len_prefix[1]) << 16) |
		                     (static_cast<std::uint32_t>(len_prefix[2]) << 8) |
		                     static_cast<std::uint32_t>(len_prefix[3]);

		if (len == 0 || len > max_message_size)
			throw std::runtime_error("smb2: invalid message length");

		std::vector<std::byte> buf(len);

		if (!read_exact(buf.data(), len))
			return std::nullopt;

		return buf;
	}

	bool read_exact(void* out, std::size_t n) {
		auto* p = static_cast<std::byte*>(out);
		std::size_t have = 0;

		while (have < n) {
			auto r = stream_->read(p + have, n - have);

			if (r.status == netkit::stream::stream_status::eof || r.status == netkit::stream::stream_status::closed)
				return false;

			if (r.status == netkit::stream::stream_status::error)
				throw std::runtime_error("smb2: socket read error");

			have += r.bytes;
		}

		return true;
	}

	bool write_message(const std::vector<std::byte>& msg) {
		std::uint8_t len_prefix[4] = {
			0,
			static_cast<std::uint8_t>((msg.size() >> 16) & 0xFF),
			static_cast<std::uint8_t>((msg.size() >> 8) & 0xFF),
			static_cast<std::uint8_t>(msg.size() & 0xFF),
		};
		auto r1 = stream_->write_all(len_prefix, 4);
		if (r1.status != netkit::stream::stream_status::success)
			return false;

		auto r2 = stream_->write_all(msg.data(), msg.size());

		return r2.status == netkit::stream::stream_status::success;
	}

	static constexpr std::uint32_t smb2_flag_related = 0x00000004;
	static constexpr std::uint64_t invalid_fid = 0xFFFFFFFFFFFFFFFFULL;

	static std::uint16_t le16_at(const std::byte* p) {
		return static_cast<std::uint16_t>(static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8));
	}

	static std::uint32_t le32_at(const std::byte* p) {
		return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
			   (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
	}

	static void put_le32_at(std::vector<std::byte>& v, std::size_t pos, std::uint32_t x) {
		for (std::size_t i = 0; i < 4; ++i) v[pos + i] = static_cast<std::byte>((x >> (8 * i)) & 0xFF);
	}

	static bool is_failure(std::uint32_t s) {
		return (s & 0xC0000000u) == 0xC0000000u && s != status::more_processing_required;
	}

	static std::uint16_t grant_credits(const std::byte* hdr) {
		const std::uint16_t asked = le16_at(hdr + 14);
		return std::max<std::uint16_t>(1, std::min<std::uint16_t>(asked, max_credit_grant));
	}

	std::uint64_t resolve_fid(std::uint64_t volatile_id) const {
		return volatile_id == invalid_fid ? compound_fid_ : volatile_id;
	}

	std::vector<std::byte> handle_message(const std::vector<std::byte>& raw) {
		compound_fid_ = invalid_fid;
		compound_have_prev_ = false;
		compound_prev_status_ = status::success;

		std::vector<std::byte> all;
		std::size_t off = 0;
		for (;;) {
			const std::byte* base = raw.data() + off;
			const std::size_t avail = raw.size() - off;

			if (avail < header_size) {
				if (all.empty()) throw std::runtime_error("smb2: message shorter than an SMB2 header");
				break;
			}

			const std::uint32_t next = le32_at(base + 20);
			std::size_t len = avail;

			if (next != 0) {
				if (next < header_size || (next & 7u) != 0 || next > avail) {
					if (all.empty()) throw std::runtime_error("smb2: malformed compound chain");
					break;
				}
				len = next;
			}

			const bool related = off != 0 && (le32_at(base + 16) & smb2_flag_related) != 0;

			if (signing_active_ && !signing::verify(base, len, session_key_)) {
				SMB_LOG("warning: request signature did not verify (cmd=0x%04X) - processing anyway",
				        static_cast<unsigned>(le16_at(base + 12)));
			}

			std::vector<std::byte> resp = handle_one(base, len, related);

			if (!resp.empty() && related) resp[16] |= std::byte{0x04};

			if (next != 0 && !resp.empty()) {
				while (resp.size() % 8 != 0)
					resp.push_back(std::byte{0});

				put_le32_at(resp, 20, static_cast<std::uint32_t>(resp.size()));
			}

			if (signing_active_ && !resp.empty())
				signing::sign(resp, session_key_);

			all.insert(all.end(), resp.begin(), resp.end());

			if (next == 0) break;
			off += len;
		}
		return all;
	}

	std::vector<std::byte> handle_one(const std::byte* data, std::size_t size, bool related) {
		byte_reader r(data, size);
		header req = header::parse(r);

		if (static_cast<std::uint16_t>(req.cmd) == 0x000C) {
			SMB_LOG("CANCEL for msgid=%llu ignored (nothing is ever pending)",
					static_cast<unsigned long long>(req.message_id));
			return {};
		}

		if (related && compound_have_prev_) {
			req.tree_id = compound_tree_id_;
			req.session_id = compound_session_id_;
		}

		header resp_hdr;
		resp_hdr.cmd = req.cmd;
		resp_hdr.message_id = req.message_id;
		resp_hdr.flags = hdr_flags::server_to_redir;
		resp_hdr.tree_id = req.tree_id;
		resp_hdr.session_id = req.session_id;
		resp_hdr.credit_request_response = grant_credits(data);

		if (related && compound_have_prev_ && is_failure(compound_prev_status_) && compound_fid_ == invalid_fid) {
			resp_hdr.status = compound_prev_status_;
			return make_error_response(resp_hdr);
		}

		byte_writer body;
		std::uint32_t result_status = status::success;

		try {
			switch (req.cmd) {
				case command::negotiate: handle_negotiate(r, body); break;
				case command::session_setup: result_status = handle_session_setup(req, r, body, resp_hdr); break;
				case command::logoff: handle_logoff(body); break;
				case command::tree_connect: result_status = handle_tree_connect(r, body, resp_hdr); break;
				case command::tree_disconnect: handle_tree_disconnect(body); break;
				case command::create: result_status = handle_create(r, body); break;
				case command::close_: result_status = handle_close(r, body); break;
				case command::read: result_status = handle_read(r, body); break;
				case command::write: result_status = handle_write(r, body); break;
				case command::query_directory: result_status = handle_query_directory(r, body); break;
				case command::query_info: result_status = handle_query_info(r, body); break;
				case command::echo: body.put_u16(4); body.put_u16(0); break;
				case command::flush: result_status = handle_flush(r, body); break;
				case command::set_info:
					result_status = handle_set_info(r, body);

					if (result_status == status::success) {
						body.put_u16(2);
						body.put_u16(0);
					}

					break;
				default:
					SMB_LOG("unsupported command 0x%04X -> STATUS_NOT_SUPPORTED", static_cast<unsigned>(req.cmd));
					result_status = status::not_supported;
					break;
			}
		} catch (const std::exception& e) {
			SMB_LOG("cmd=0x%04X: request error: %s", static_cast<unsigned>(req.cmd), e.what());
			result_status = status::invalid_parameter;
		}

		resp_hdr.status = result_status;

		compound_tree_id_ = resp_hdr.tree_id;
		compound_session_id_ = resp_hdr.session_id;
		compound_prev_status_ = result_status;
		compound_have_prev_ = true;

		if (result_status != status::success && result_status != status::more_processing_required)
			return make_error_response(resp_hdr);

		byte_writer out;
		resp_hdr.write(out);

		const auto& body_bytes = body.data();

		out.put_bytes(body_bytes.data(), body_bytes.size());

		return out.data();
	}

	std::vector<std::byte> make_error_response(const header& resp_hdr) {
		byte_writer out;

		resp_hdr.write(out);

		out.put_u16(9);
		out.put_u8(0);
		out.put_u8(0);
		out.put_u32(0);
		out.put_u8(0);

		return out.data();
	}

	std::vector<std::byte> make_error_response_raw(const std::byte* data, std::size_t size, std::uint32_t status_code) {
		try {
			byte_reader r(data, size);
			header req = header::parse(r);
			header resp_hdr;
			resp_hdr.cmd = req.cmd;
			resp_hdr.message_id = req.message_id;
			resp_hdr.flags = hdr_flags::server_to_redir;
			resp_hdr.tree_id = req.tree_id;
			resp_hdr.session_id = req.session_id;
			resp_hdr.status = status_code;
			return make_error_response(resp_hdr);
		} catch (const std::exception&) {
			return {};
		}
	}

	std::vector<std::byte> make_error_response_raw(const std::vector<std::byte>& raw, std::uint32_t status_code) {
		return make_error_response_raw(raw.data(), raw.size(), status_code);
	}

	void write_negotiate_body(byte_writer& out, std::uint16_t dialect_revision) {
		out.put_u16(65); // StructureSize
		out.put_u16(creds_.enabled && creds_.has_password ? 0x0001 : 0x0000); // SecurityMode
		out.put_u16(dialect_revision);
		out.put_u16(0);
		std::array<std::uint8_t, 16> server_guid{};
		out.put_bytes(server_guid.data(), 16);
		out.put_u32(0); // Capabilities
		out.put_u32(max_io_size); // MaxTransactSize
		out.put_u32(max_io_size); // MaxReadSize
		out.put_u32(max_io_size); // MaxWriteSize
		out.put_u64(filetime_now()); // SystemTime
		out.put_u64(0); // ServerStartTime

		auto token = ntlm::build_neg_token_init();
		std::size_t sec_buf_offset_field = out.size();

		out.put_u16(0);
		out.put_u16(static_cast<std::uint16_t>(token.size()));
		out.put_u32(0);

		out.patch_u16(sec_buf_offset_field, static_cast<std::uint16_t>(header_size + out.size()));
		for (auto b : token) out.put_u8(b);
	}

	void handle_negotiate(byte_reader& r, byte_writer& out) {
		r.get_u16(); // StructureSize
		std::uint16_t dialect_count = r.get_u16();
		client_negotiate_signing_enabled_ = (r.get_u16() & 0x0001) != 0; // SecurityMode
		r.get_u16(); // Reserved
		r.get_u32(); // Capabilities
		std::array<std::uint8_t, 16> client_guid{};
		r.get_bytes(client_guid.data(), 16);
		r.get_u64(); // ClientStartTime

		bool client_supports_us = false;

		for (int i = 0; i < dialect_count; ++i) {
			if (r.get_u16() == dialect::smb_2_0_2) client_supports_us = true;
		}

		write_negotiate_body(out, client_supports_us ? dialect::smb_2_0_2 : 0);
	}

	std::vector<std::byte> handle_smb1_negotiate_probe(const std::vector<std::byte>& raw) {
		constexpr std::size_t smb1_header_size = 32;
		if (raw.size() < smb1_header_size + 3) return {};

		constexpr uint32_t com_negotiate = 0x72;

		auto byte_at = [&](std::size_t i) {
			return static_cast<std::uint8_t>(raw[i]);
		};

		if (byte_at(4) != com_negotiate)
			return {};

		std::size_t pos = smb1_header_size;
		std::uint8_t word_count = byte_at(pos++);

		pos += static_cast<std::size_t>(word_count) * 2;

		if (pos + 2 > raw.size())
			return {};

		std::uint16_t byte_count = static_cast<std::uint16_t>(byte_at(pos) | (byte_at(pos + 1) << 8));

		pos += 2;

		if (pos + byte_count > raw.size())
			return {};

		bool saw_smb_2_0_2 = false;
		bool saw_smb_2_wildcard = false;

		std::size_t end = pos + byte_count;

		while (pos < end) {
			if (byte_at(pos) != 0x02)
				break;

			++pos;

			std::string dialect_str;

			while (pos < end && byte_at(pos) != 0)
				dialect_str.push_back(static_cast<char>(byte_at(pos++)));

			if (pos < end) ++pos;
			if (dialect_str == "SMB 2.002")
				saw_smb_2_0_2 = true;

			if (dialect_str == "SMB 2.???")
				saw_smb_2_wildcard = true;
		}

		if (!saw_smb_2_0_2 && !saw_smb_2_wildcard)
			return {};

		header resp_hdr;

		resp_hdr.cmd = command::negotiate;
		resp_hdr.message_id = 0;
		resp_hdr.flags = hdr_flags::server_to_redir;

		byte_writer body;
		write_negotiate_body(body, saw_smb_2_wildcard ? dialect::wildcard : dialect::smb_2_0_2);

		byte_writer out;

		resp_hdr.write(out);

		for (auto b : body.data())
			out.put_bytes(&b, 1);

		return out.data();
	}

	std::uint32_t handle_session_setup(header& req, byte_reader& r, byte_writer& out, header& resp_hdr) {
		r.get_u16(); // StructureSize
		r.get_u8();  // Flags
		const bool client_session_signing_enabled = (r.get_u8() & 0x01) != 0; // SecurityMode
		r.get_u32(); // Capabilities
		r.get_u32(); // Channel

		std::uint16_t sec_offset = r.get_u16();
		std::uint16_t sec_len = r.get_u16();

		r.get_u64();

		std::size_t abs_offset = sec_offset;

		const auto* blob = reinterpret_cast<const std::uint8_t*>(r.raw()) + abs_offset;
		auto msg_type = ntlm::find_ntlmssp_message_type(blob, sec_len);

		if (msg_type == 3) {
			if (resp_hdr.session_id == 0)
				resp_hdr.session_id = req.session_id;

			session_established_ = true;
			session_id_ = resp_hdr.session_id;

			const auto auth = ntlm_auth::parse_authenticate(blob, sec_len);

			bool guest = true;
			if (creds_.enabled && creds_.has_password && auth.valid && !auth.anonymous) {
				if (auth.user == creds_.user) {
					const auto v = ntlmv2::verify(auth, creds_.nt_hash, server_challenge_);
					if (v.ok) {
						guest = false;
						session_key_ = v.session_key;
						signing_active_ = client_negotiate_signing_enabled_ && client_session_signing_enabled;
					} else {
						SMB_LOG("SESSION_SETUP: wrong password for user='%s'", auth.user.c_str());
					}
				} else {
					SMB_LOG("SESSION_SETUP: unknown user='%s'", auth.user.c_str());
				}
			} else if (creds_.enabled && creds_.has_password && auth.valid && auth.anonymous) {
				SMB_LOG("SESSION_SETUP: anonymous logon rejected, falling back to guest");
			}

			if (guest && creds_.enabled && creds_.has_password)
				return local::logon_failure;

			SMB_LOG("SESSION_SETUP: user='%s' domain='%s' workstation='%s' -> %s session",
			        auth.user.c_str(), auth.domain.c_str(), auth.workstation.c_str(),
			        guest ? "guest" : (signing_active_ ? "authenticated+signed" : "authenticated"));

			out.put_u16(9);
			out.put_u16(static_cast<std::uint16_t>(guest ? session_flags::is_guest : 0));

			auto token = ntlm::build_neg_token_resp_success();
			std::size_t off_field = out.size();

			out.put_u16(0);
			out.put_u16(static_cast<std::uint16_t>(token.size()));
			out.patch_u16(off_field, static_cast<std::uint16_t>(header_size + out.size()));

			for (auto b : token)
				out.put_u8(b);

			return status::success;
		}

		static std::uint64_t next_session_id{1};
		resp_hdr.session_id = next_session_id++;

		auto challenge_msg = ntlm::build_ntlmssp_challenge(server_challenge_);
		auto token = ntlm::build_neg_token_resp_challenge(challenge_msg);

		out.put_u16(9);
		out.put_u16(0);

		std::size_t off_field = out.size();

		out.put_u16(0);
		out.put_u16(static_cast<std::uint16_t>(token.size()));
		out.patch_u16(off_field, static_cast<std::uint16_t>(header_size + out.size()));

		for (auto b : token)
			out.put_u8(b);

		return status::more_processing_required;
	}

	void handle_logoff(byte_writer& out) {
		session_established_ = false;
		signing_active_ = false;
		session_key_.fill(0);
		out.put_u16(4);
		out.put_u16(0);
	}

	std::uint32_t handle_tree_connect(byte_reader& r, byte_writer& out, header& resp_hdr) {
		r.get_u16();
		r.get_u16();

		std::uint16_t path_offset = r.get_u16();
		std::uint16_t path_len = r.get_u16();

		std::size_t abs_offset = path_offset;
		std::string path = r.get_utf16le_at(abs_offset, path_len);

		auto backslash = path.find_last_of('\\');
		std::string requested_share =
			backslash == std::string::npos ? path : path.substr(backslash + 1);

		if (!iequals(requested_share, share_name_))
			return ntstatus::bad_network_name;

		static std::uint32_t next_tree_id{1};

		tree_id_ = next_tree_id++;
		resp_hdr.tree_id = tree_id_;
		tree_connected_ = true;

		out.put_u16(16);
		out.put_u8(1);
		out.put_u8(0);
		out.put_u32(0);
		out.put_u32(0);
		out.put_u32(read_only_ ? local::read_only_access : local::full_access); // MaximalAccess

		return status::success;
	}

	void handle_tree_disconnect(byte_writer& out) {
		tree_connected_ = false;

		out.put_u16(4);
		out.put_u16(0);
	}

	std::optional<std::filesystem::path> resolve_path(const std::string& client_path) {
		std::string normalized = client_path;

		for (auto& c : normalized) {
			if (c == '\\')
				c = '/';
		}

		std::filesystem::path rel(normalized);

		if (rel.is_absolute() || rel.has_root_name() || rel.has_root_directory())
			return std::nullopt;

		rel = rel.lexically_normal();

		for (const auto& part : rel) {
			if (part == "..")
				return std::nullopt;
		}

		if (!rel.empty() && !rel.has_filename())
			rel = rel.parent_path();

		if (rel.empty() || rel == ".")
			return share_root_;

		return share_root_ / rel;
	}

	static bool ensure_open(open_handle& h) {
		if (h.file)
			return true;

		if (h.is_directory)
			return false;

		const std::string p = h.host_path.string();

		if (h.read_only) {
			h.file = std::fopen(p.c_str(), "rb");
			return h.file != nullptr;
		}

		h.file = std::fopen(p.c_str(), "r+b");

		if (!h.file) {
			h.file = std::fopen(p.c_str(), "rb");
			h.read_only = (h.file != nullptr);
		}

		return h.file != nullptr;
	}

	std::uint32_t handle_create(byte_reader& r, byte_writer& out) {
		r.get_u16(); // StructureSize
		r.get_u8();  // SecurityFlags
		r.get_u8();  // RequestedOplockLevel
		r.get_u32(); // ImpersonationLevel
		r.get_u64(); // SmbCreateFlags
		r.get_u64(); // Reserved

		std::uint32_t desired_access = r.get_u32(); // DesiredAccess

		r.get_u32(); // FileAttributes
		r.get_u32(); // ShareAccess

		std::uint32_t disposition = r.get_u32();
		std::uint32_t options = r.get_u32();
		std::uint16_t name_offset = r.get_u16();
		std::uint16_t name_len = r.get_u16();

		r.get_u32(); // CreateContextsOffset
		r.get_u32(); // CreateContextsLength

		std::size_t abs_offset = name_offset;
		std::string name = name_len > 0 ? r.get_utf16le_at(abs_offset, name_len) : std::string();

		SMB_LOG("CREATE: name='%s' disposition=%u options=0x%08X", name.c_str(), disposition, options);

		auto resolved = resolve_path(name);
		if (!resolved) return status::access_denied;
		const std::filesystem::path target = *resolved;

		compound_fid_ = invalid_fid;

		std::error_code ec;

		const bool exists = std::filesystem::exists(target, ec);
		const bool target_is_dir = exists && std::filesystem::is_directory(target, ec);
		const bool want_directory = (options & create_options::directory_file) != 0;
		const bool want_non_directory = (options & local::non_directory_file) != 0;
		const bool delete_on_close = (options & local::delete_on_close) != 0;

		if (read_only_) {
			const bool would_create = disposition == create_disposition::create ||
				disposition == create_disposition::open_if ||
				disposition == create_disposition::overwrite_if ||
				disposition == create_disposition::supersede;

			if (exists || would_create) {
				const bool write_access = (desired_access & local::write_access_mask) != 0;
				const bool changes_data = disposition == create_disposition::create ||
					disposition == create_disposition::overwrite ||
					disposition == create_disposition::overwrite_if ||
					disposition == create_disposition::supersede ||
					(disposition == create_disposition::open_if && !exists);

				if (write_access || changes_data || delete_on_close) {
					SMB_LOG("CREATE: denied, share is read-only (access=0x%08X disposition=%u)", desired_access, disposition);
					return status::access_denied;
				}
			}
		}

		std::uint32_t action = create_action::opened;

		if (exists) {
			if (target_is_dir && want_non_directory)
				return ntstatus::file_is_a_directory;

			if (!target_is_dir && want_directory)
				return ntstatus::not_a_directory;

			if (disposition == create_disposition::create)
				return status::object_name_collision;
		} else {
			const bool may_create = disposition == create_disposition::create ||
									disposition == create_disposition::open_if ||
									disposition == create_disposition::overwrite_if ||
									disposition == create_disposition::supersede;
			if (!may_create)
				return status::object_name_not_found;

			if (!std::filesystem::is_directory(target.parent_path(), ec))
				return status::object_path_not_found;

			if (want_directory) {
				if (!std::filesystem::create_directory(target, ec) || ec) return status::access_denied;
				action = create_action::created;
			}
		}

		const bool is_dir = want_directory || target_is_dir;

		if (delete_on_close) {
			if (!local::has_delete_access(desired_access)) return status::access_denied;
			if (target == share_root_) return status::access_denied;
			if (is_dir && !std::filesystem::is_empty(target, ec)) return ntstatus::directory_not_empty;
		}

		const std::uint64_t id = next_file_id_++;

		open_handle h;

		h.host_path = target;
		h.desired_access = desired_access;
		h.is_directory = is_dir;
		h.delete_pending = delete_on_close;
		h.read_only = read_only_; // never open for writing on a read-only share

		if (!is_dir) {
			const bool truncate = disposition == create_disposition::overwrite ||
								  disposition == create_disposition::overwrite_if ||
								  disposition == create_disposition::supersede;

			if (!exists || truncate) {
				h.file = std::fopen(target.string().c_str(), "w+b");
				if (!h.file) return status::access_denied;
				action = exists ? create_action::overwritten : create_action::created;
			}
		}

		open_files_.emplace(id, std::move(h));
		compound_fid_ = id;

		const file_stat fst = stat_path(target);

		out.put_u16(89);
		out.put_u8(0);
		out.put_u8(0);
		out.put_u32(action);
		out.put_u64(fst.times.creation);
		out.put_u64(fst.times.access);
		out.put_u64(fst.times.write);
		out.put_u64(fst.times.change);
		out.put_u64(fst.size); // AllocationSize
		out.put_u64(fst.size); // EOF
		out.put_u32(fst.is_dir ? file_attr::directory : file_attr::normal);
		out.put_u32(0);
		out.put_u64(0);
		out.put_u64(id);
		out.put_u32(0);
		out.put_u32(0);

		return status::success;
	}

	bool h_is_directory_safe(std::uint64_t id) {
		auto it = open_files_.find(id);
		return it != open_files_.end() && it->second.is_directory;
	}

	std::uint32_t handle_set_info(byte_reader& r, byte_writer& out) {
		static_cast<void>(out);

		r.get_u16();

		std::uint8_t info_type_v = r.get_u8();
		std::uint8_t info_class = r.get_u8();
		std::uint32_t buffer_length = r.get_u32();
		std::uint16_t buffer_offset = r.get_u16();

		r.get_u16();
		r.get_u32(); // AdditionalInformation
		r.get_u64(); // FileId.Persistent

		std::uint64_t id = resolve_fid(r.get_u64());

		SMB_LOG("SET_INFO: type=0x%02X class=0x%02X len=%u", info_type_v, info_class, buffer_length);

		if (info_type_v != info_type::file)
			return status::not_supported;

		auto it = open_files_.find(id);

		if (it == open_files_.end())
			return status::invalid_handle;

		const std::size_t offset = buffer_offset;
		if (offset > r.size() || buffer_length > r.size() - offset)
			return status::invalid_parameter;

		if (buffer_length == 0)
			return status::invalid_parameter;

		byte_reader info(r.raw() + offset, buffer_length);

		auto& h = it->second;

		std::error_code ec;

		if (read_only_ && (info_class == 10 || info_class == 19 || info_class == 20)) { // rename, allocation, end of file
			SMB_LOG("SET_INFO: denied, share is read-only");
			return status::access_denied;
		}

		switch (info_class) {
		case 4: { // FileBasicInformation
			if (buffer_length < 40)
				return status::invalid_parameter;

			return status::success;
		}

		case 13: { // FileDispositionInformation
			if (buffer_length < 1)
				return status::invalid_parameter;

			const bool delete_file = info.get_u8() != 0;

			if (!delete_file) {
				h.delete_pending = false;
				return status::success;
			}

			if (read_only_ || !local::has_delete_access(h.desired_access))
				return status::access_denied;

			if (h.host_path.lexically_normal() == share_root_.lexically_normal())
				return status::access_denied;

			if (h.is_directory) {
				const bool empty = std::filesystem::is_empty(h.host_path, ec);

				if (!ec && !empty)
					return ntstatus::directory_not_empty;
			}

			h.delete_pending = true;

			return status::success;
		}

		case 19: { // FileAllocationInformation
			if (buffer_length < 8)
				return status::invalid_parameter;

			return status::success;
		}

		case 20: { // FileEndOfFileInformation
			if (buffer_length < 8)
				return status::invalid_parameter;

			if (h.is_directory || !ensure_open(h))
				return status::invalid_parameter;

			if (h.read_only)
				return status::access_denied;

			const auto eof = info.get_u64();

			if (eof > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
				return status::invalid_parameter;

			if (std::fflush(h.file) != 0)
				return status::access_denied;

			std::filesystem::resize_file(h.host_path, eof, ec);

			if (ec) {
				SMB_LOG("SET_INFO: resize failed: %s", ec.message().c_str());
				return status::access_denied;
			}

			if (std::fseek(h.file, 0, SEEK_SET) != 0)
				return status::invalid_parameter;

			return status::success;
		}

		case 10: { // FileRenameInformation
			if (buffer_length < 20)
				return status::invalid_parameter;

			const bool replace_if_exists = info.get_u8() != 0;

			for (int i = 0; i < 7; ++i)
				info.get_u8();

			const auto root_directory = info.get_u64();
			const auto name_length = info.get_u32();

			if (root_directory != 0)
				return status::not_supported;

			if ((name_length & 1) != 0 || name_length > buffer_length - 20)
				return status::invalid_parameter;

			const std::string new_name = info.get_utf16le_at(20, name_length);

			auto resolved = resolve_path(new_name);

			if (!resolved)
				return status::access_denied;

			const std::filesystem::path target = *resolved;

			if (target.lexically_normal() == share_root_.lexically_normal())
				return status::access_denied;

			if (h.host_path.lexically_normal() == share_root_.lexically_normal())
				return status::access_denied;

			if (!std::filesystem::is_directory(target.parent_path(), ec))
				return status::object_path_not_found;

			const bool same_file = iequals(target.lexically_normal().string(), h.host_path.lexically_normal().string());

			const bool target_exists = !same_file && std::filesystem::exists(target, ec);

			if (ec)
				return status::access_denied;

			if (target_exists && !replace_if_exists)
				return status::object_name_collision;

			if (target_exists) {
				const bool source_is_dir = h.is_directory;
				const bool target_is_dir = std::filesystem::is_directory(target, ec);

				if (ec || source_is_dir != target_is_dir)
					return status::access_denied;

				if (target_is_dir && !std::filesystem::is_empty(target, ec))
					return status::access_denied;

				if (ec)
					return status::access_denied;

				std::filesystem::remove(target, ec);
				if (ec)
					return status::access_denied;
			}

			if (h.file) {
				std::fclose(h.file);
				h.file = nullptr;
			}

			std::filesystem::rename(h.host_path, target, ec);

			if (ec) {
				SMB_LOG("  SET_INFO: rename failed: %s", ec.message().c_str());
				return status::access_denied;
			}

			h.host_path = target;

			return status::success;
		}

		default:
			SMB_LOG("  SET_INFO: unsupported file info class 0x%02X", info_class);
			return status::invalid_info_class;
		}
	}
	std::uint32_t handle_close(byte_reader& r, byte_writer& out) {
		r.get_u16();

		const std::uint16_t flags = r.get_u16();

		r.get_u32();
		r.get_u64();

		std::uint64_t id = resolve_fid(r.get_u64());

		auto it = open_files_.find(id);

		if (it == open_files_.end())
			return status::invalid_handle;

		auto& h = it->second;

		if (h.file) {
			std::fclose(h.file);
			h.file = nullptr;
		}

		const bool postquery = (flags & 0x0001) != 0;

		file_stat fst;

		if (postquery)
			fst = stat_path(h.host_path);

		std::uint32_t close_status = status::success;

		std::error_code ec;

		if (h.delete_pending) {
			const bool removed = std::filesystem::remove(h.host_path, ec);

			if (ec) {
				SMB_LOG("  CLOSE: deferred deletion failed: %s", ec.message().c_str());
				close_status = status::access_denied;
			} else if (!removed) {
				SMB_LOG("  CLOSE: delete-pending path was not removed");
			}
		}

		SMB_LOG("  CLOSE: delete_pending=%d path='%s' status=0x%08X",
				h.delete_pending, h.host_path.string().c_str(), close_status);

		open_files_.erase(it);

		out.put_u16(60); // StructureSize
		out.put_u16(postquery ? 1 : 0); // Flags
		out.put_u32(0);

		if (postquery) {
			out.put_u64(fst.times.creation);
			out.put_u64(fst.times.access);
			out.put_u64(fst.times.write);
			out.put_u64(fst.times.change);
			out.put_u64(fst.size); // AllocationSize
			out.put_u64(fst.size); // EndOfFile
			out.put_u32(fst.is_dir ? file_attr::directory : file_attr::normal);
		} else {
			out.put_u64(0); out.put_u64(0); out.put_u64(0); out.put_u64(0);
			out.put_u64(0); // AllocationSize
			out.put_u64(0); // EndOfFile
			out.put_u32(0); // FileAttributes
		}

		return close_status;
	}

	std::uint32_t handle_read(byte_reader& r, byte_writer& out) {
		r.get_u16();
		r.get_u8();
		r.get_u8();

		std::uint32_t length = r.get_u32();
		std::uint64_t offset = r.get_u64();

		r.get_u64();
		std::uint64_t id = resolve_fid(r.get_u64());
		r.get_u32();
		r.get_u32();
		r.get_u32();
		r.get_u16();
		r.get_u16();

		auto it = open_files_.find(id);
		if (it == open_files_.end() || it->second.is_directory)
			return status::invalid_handle;

		if (!ensure_open(it->second))
			return status::access_denied;

		if (length > max_io_size)
			length = max_io_size;

		if (std::fseek(it->second.file, static_cast<long>(offset), SEEK_SET) != 0)
			return status::invalid_parameter;

		std::vector<std::uint8_t> buf(length);
		std::size_t got = std::fread(buf.data(), 1, length, it->second.file);

		if (got == 0)
			return status::end_of_file;

		out.put_u16(17); // StructureSize

		std::size_t data_offset_field = out.size();

		out.put_u8(0);
		out.put_u8(0);

		out.put_u32(static_cast<std::uint32_t>(got));

		out.put_u32(0); // DataRemaining
		out.put_u32(0);

		out.patch_u8(data_offset_field, static_cast<std::uint8_t>(header_size + 16));

		out.put_bytes(buf.data(), got);

		return status::success;
	}

	std::uint32_t handle_write(byte_reader& r, byte_writer& out) {
		r.get_u16();

		std::uint16_t data_offset = r.get_u16();
		std::uint32_t length = r.get_u32();
		std::uint64_t offset = r.get_u64();

		r.get_u64(); // FileId.Persistent

		std::uint64_t id = resolve_fid(r.get_u64());

		r.get_u32(); // Channel
		r.get_u32(); // RemainingBytes
		r.get_u16(); // WriteChannelInfoOffset
		r.get_u16(); // WriteChannelInfoLength
		r.get_u32(); // Flags

		auto it = open_files_.find(id);

		if (it == open_files_.end() || it->second.is_directory)
			return status::invalid_handle;

		if (read_only_)
			return status::access_denied;

		if (!ensure_open(it->second))
			return status::access_denied;

		if (it->second.read_only)
			return status::access_denied;

		std::size_t abs_offset = data_offset;

		if (abs_offset + length > r.size())
			return status::invalid_parameter;

		if (std::fseek(it->second.file, static_cast<long>(offset), SEEK_SET) != 0)
			return status::invalid_parameter;

		std::size_t written = std::fwrite(r.raw() + abs_offset, 1, length, it->second.file);
		std::fflush(it->second.file);

		if (written != length)
			return ntstatus::disk_full;

		out.put_u16(17); // StructureSize
		out.put_u16(0);
		out.put_u32(static_cast<std::uint32_t>(written));
		out.put_u32(0); // Remaining
		out.put_u16(0); // WriteChannelInfoOffset
		out.put_u16(0); // WriteChannelInfoLength

		return status::success;
	}

	static bool wildcard_match(const std::string& pat, const std::string& name) {
		auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };
		std::size_t p = 0, n = 0, star = std::string::npos, mark = 0;

		while (n < name.size()) {
			if (p < pat.size() && (pat[p] == '?' || lower(pat[p]) == lower(name[n]))) {
				++p; ++n;
			} else if (p < pat.size() && pat[p] == '*') {
				star = p++; mark = n;
			} else if (star != std::string::npos) {
				p = star + 1; n = ++mark;
			} else {
				return false;
			}
		}

		while (p < pat.size() && pat[p] == '*')
			++p;

		return p == pat.size();
	}

	static bool encode_dir_entry(byte_writer& e, std::uint8_t info_class, std::uint32_t index, std::uint64_t file_id,
								 const std::string& fname, bool is_dir, std::uint64_t size, const file_times& t) {
		std::uint32_t attrs = is_dir ? file_attr::directory : file_attr::normal;
		std::uint32_t name_bytes = static_cast<std::uint32_t>(fname.size() * 2);

		e.put_u32(0);
		e.put_u32(index);

		if (info_class == 0x0C) {
			e.put_u32(name_bytes);
		} else {
			e.put_u64(t.creation); e.put_u64(t.access); e.put_u64(t.write); e.put_u64(t.change);
			e.put_u64(size);
			e.put_u64(size);
			e.put_u32(attrs);
			e.put_u32(name_bytes);

			if (info_class != file_info_class::directory) {
				e.put_u32(0);

				if (info_class == file_info_class::both_directory || info_class == file_info_class::id_both_directory) {
					e.put_u8(0);
					e.put_u8(0);
					e.put_zeros(24);
					if (info_class == file_info_class::id_both_directory) {
						e.put_u16(0);
						e.put_u64(file_id);
					}
				} else if (info_class == 0x26) {
					e.put_u32(0);
					e.put_u64(file_id);
				} else if (info_class != file_info_class::full_directory) {
					return false;
				}
			}
		}

		e.put_utf16le(fname);
		e.put_zeros((8 - (e.size() % 8)) % 8);

		return true;
	}

	std::uint32_t handle_query_directory(byte_reader& r, byte_writer& out) {
		r.get_u16();

		std::uint8_t info_class = r.get_u8();
		std::uint8_t flags = r.get_u8();

		r.get_u32();
		r.get_u64();

		std::uint64_t id = resolve_fid(r.get_u64());
		std::uint16_t pattern_offset = r.get_u16();
		std::uint16_t pattern_len = r.get_u16();
		std::uint32_t output_buf_len = r.get_u32();

		constexpr std::uint8_t restart_scans = 0x01;
		constexpr std::uint8_t return_single_entry = 0x02;

		std::string pattern = pattern_len > 0 ? r.get_utf16le_at(pattern_offset, pattern_len) : std::string("*");

		if (pattern == "*.*") pattern = "*";

		SMB_LOG("  QUERY_DIRECTORY: class=0x%02X flags=0x%02X pattern='%s' buflen=%u", info_class, flags,
				pattern.c_str(), output_buf_len);

		auto it = open_files_.find(id);
		if (it == open_files_.end() || !it->second.is_directory) return status::invalid_handle;

		auto& h = it->second;

		if (info_class != file_info_class::directory && info_class != file_info_class::full_directory &&
			info_class != file_info_class::both_directory && info_class != file_info_class::id_both_directory &&
			info_class != 0x0C && info_class != 0x26) {
			return status::invalid_info_class;
		}

		bool fresh_scan = false;
		if ((flags & restart_scans) || !h.dir_listed_once) {
			std::error_code ec;
			h.dir_entries.clear();

			for (std::filesystem::directory_iterator di(h.host_path, std::filesystem::directory_options::skip_permission_denied, ec), end;
				 !ec && di != end; di.increment(ec)) {
				if (pattern == "*" || wildcard_match(pattern, di->path().filename().string()))
					h.dir_entries.push_back(*di);
			}

			h.dir_cursor = 0;
			h.dir_listed_once = true;
			fresh_scan = true;

			SMB_LOG("  QUERY_DIRECTORY: scanned %zu matching entr%s", h.dir_entries.size(),
					h.dir_entries.size() == 1 ? "y" : "ies");
		}

		if (h.dir_cursor >= h.dir_entries.size()) {
			if (fresh_scan && pattern.find_first_of("*?<>\"") == std::string::npos)
				return ntstatus::no_such_file;

			return status::no_more_files;
		}

		byte_writer entries;
		std::size_t prev_start = std::string::npos;

		while (h.dir_cursor < h.dir_entries.size()) {
			const auto& entry = h.dir_entries[h.dir_cursor];
			const file_stat fst = stat_path(entry.path());

			byte_writer e;

			if (!encode_dir_entry(e, info_class, static_cast<std::uint32_t>(h.dir_cursor), h.dir_cursor + 1,
								  entry.path().filename().string(), fst.is_dir, fst.size, fst.times))
				return status::invalid_info_class;

			if (entries.size() + e.size() > output_buf_len) {
				if (entries.size() == 0) return status::info_length_mismatch;
				break; // the rest goes in the next reply
			}

			if (prev_start != std::string::npos)
				patch_u32_in(entries, prev_start, static_cast<std::uint32_t>(entries.size() - prev_start));

			prev_start = entries.size();

			for (auto b : e.data())
				entries.put_u8(static_cast<std::uint8_t>(b));

			++h.dir_cursor;

			if (flags & return_single_entry)
				break;
		}

		if (entries.size() == 0)
			return status::no_more_files;

		out.put_u16(9);

		std::size_t off_field = out.size();

		out.put_u16(0);
		out.put_u32(static_cast<std::uint32_t>(entries.size()));
		out.patch_u16(off_field, static_cast<std::uint16_t>(header_size + out.size()));

		for (auto b : entries.data())
			out.put_u8(static_cast<std::uint8_t>(b));

		return status::success;
	}
	static void patch_u32_in(byte_writer& w, std::size_t offset, std::uint32_t v) {
		w.patch_u32(offset, v);
	}

	static std::uint32_t emit_info(byte_writer& out, const byte_writer& info, std::uint32_t output_buf_len) {
		if (info.size() > output_buf_len)
			return status::info_length_mismatch;

		out.put_u16(9);

		const std::size_t off_field = out.size();

		out.put_u16(0);
		out.put_u32(static_cast<std::uint32_t>(info.size()));
		out.patch_u16(off_field, static_cast<std::uint16_t>(header_size + out.size()));

		const auto& bytes = info.data();

		out.put_bytes(bytes.data(), bytes.size());

		return status::success;
	}

	std::uint32_t handle_query_info(byte_reader& r, byte_writer& out) {
		r.get_u16();

		std::uint8_t info_type_v = r.get_u8();
		std::uint8_t info_class = r.get_u8();
		std::uint32_t output_buf_len = r.get_u32();

		r.get_u16(); // InputBufferOffset
		r.get_u16();
		r.get_u32(); // InputBufferLength
		r.get_u32(); // AdditionalInformation
		r.get_u32(); // Flags
		r.get_u64(); // FileId.Persistent

		std::uint64_t id = resolve_fid(r.get_u64());

		SMB_LOG("  QUERY_INFO: type=0x%02X class=0x%02X", info_type_v, info_class);

		auto it = open_files_.find(id);

		if (it == open_files_.end())
			return status::invalid_handle;

		auto& h = it->second;

		if (info_type_v == info_type::filesystem)
			return handle_query_fs_info(info_class, output_buf_len, out);

		if (info_type_v != info_type::file)
			return status::not_supported;

		const file_stat fst = stat_path(h.host_path);
		const std::uint64_t size = fst.size;

		const file_times& t = fst.times;
		const std::uint32_t attrs = h.is_directory ? file_attr::directory : file_attr::normal;

		byte_writer info;

		if (info_class == file_info_class::basic_info) {
			info.put_u64(t.creation); info.put_u64(t.access);
			info.put_u64(t.write); info.put_u64(t.change);
			info.put_u32(attrs);
			info.put_u32(0);
		} else if (info_class == file_info_class::standard_info) {
			info.put_u64(size); // AllocationSize
			info.put_u64(size); // EndOfFile
			info.put_u32(1);    // NumberOfLinks
			info.put_u8(h.delete_pending ? 1 : 0); // DeletePending
			info.put_u8(h.is_directory ? 1 : 0);   // Directory
			info.put_u16(0);
		} else if (info_class == file_info_class::network_open_info) {
			info.put_u64(t.creation); info.put_u64(t.access);
			info.put_u64(t.write); info.put_u64(t.change);
			info.put_u64(size); info.put_u64(size);
			info.put_u32(attrs);
			info.put_u32(0);
		} else if (info_class == 0x23) {
			info.put_u32(attrs);
			info.put_u32(0);
		} else if (info_class == 0x06) { // FileInternalInformation
			info.put_u64(id);
		} else if (info_class == 0x07) { // FileEaInformation
			info.put_u32(0); // EaSize
		} else if (info_class == 0x16) { // FileStreamInformation
			if (!h.is_directory) {
				const std::string stream_name = "::$DATA";

				info.put_u32(0);
				info.put_u32(static_cast<std::uint32_t>(stream_name.size() * 2));
				info.put_u64(size);
				info.put_u64(size);
				info.put_utf16le(stream_name);
			}
		} else if (info_class == file_info_class::all_info) {
			info.put_u64(t.creation); info.put_u64(t.access);
			info.put_u64(t.write); info.put_u64(t.change);
			info.put_u32(attrs); info.put_u32(0);
			info.put_u64(size); info.put_u64(size);
			info.put_u32(1); info.put_u8(h.delete_pending ? 1 : 0); info.put_u8(h.is_directory ? 1 : 0); info.put_u16(0);
			info.put_u64(id);
			info.put_u32(0);
			info.put_u32(read_only_ ? local::read_only_access : local::full_access);
			info.put_u64(0);
			info.put_u32(0);
			info.put_u32(0);

			std::string nm = "\\" + h.host_path.filename().string();

			info.put_u32(static_cast<std::uint32_t>(nm.size() * 2));

			info.put_utf16le(nm);
		} else {
			SMB_LOG("  QUERY_INFO: unsupported file info class 0x%02X", info_class);
			return status::invalid_info_class;
		}

		return emit_info(out, info, output_buf_len);
	}

	std::uint32_t handle_query_fs_info(std::uint8_t info_class, std::uint32_t output_buf_len, byte_writer& out) {
		byte_writer info;

		if (info_class == fs_info_class::attribute) {
			constexpr std::uint32_t file_case_preserved_names = 0x00000002;
			constexpr std::uint32_t file_unicode_on_disk = 0x00000004;

			info.put_u32(file_case_preserved_names | file_unicode_on_disk | (read_only_ ? local::read_only_volume : 0));
			info.put_u32(255);

			std::string fs_name = "NTFS"; // maybe we should be honest?

			info.put_u32(static_cast<std::uint32_t>(fs_name.size() * 2));
			info.put_utf16le(fs_name);
		} else if (info_class == fs_info_class::size || info_class == fs_info_class::full_size) {
			constexpr std::uint64_t bytes_per_sector = 512;

			std::uint64_t total_bytes = 0, free_bytes = 0;
			std::error_code ec;

			auto space = std::filesystem::space(share_root_, ec);

			if (!ec) {
				total_bytes = space.capacity;
				free_bytes = space.available;
			} else {
				total_bytes = 1ull * 1024 * 1024 * 1024;
				free_bytes = 512ull * 1024 * 1024;
			}

			std::uint64_t total_units = total_bytes / bytes_per_sector;
			std::uint64_t free_units = free_bytes / bytes_per_sector;

			info.put_u64(total_units);

			if (info_class == fs_info_class::full_size) {
				info.put_u64(free_units);
				info.put_u64(free_units);
			} else {
				info.put_u64(free_units);
			}

			info.put_u32(1);
			info.put_u32(static_cast<std::uint32_t>(bytes_per_sector));
		} else if (info_class == fs_info_class::volume) {
			std::string label = share_name_;
			info.put_u64(filetime_now()); // VolumeCreationTime
			info.put_u32(0x13371337); // VolumeSerialNumber
			info.put_u32(static_cast<std::uint32_t>(label.size() * 2)); // VolumeLabelLength
			info.put_u8(0);// SupportsObjects
			info.put_u8(0);
			info.put_utf16le(label);
		} else if (info_class == 0x04) { // FileFsDeviceInformation
			info.put_u32(0x00000007); // DeviceType: FILE_DEVICE_DISK
			info.put_u32(0x00000020); // Characteristics: FILE_DEVICE_IS_MOUNTED
		} else if (info_class == 0x0B) { // FileFsSectorSizeInformation (queried by Windows 8 and later)
			info.put_u32(512); // LogicalBytesPerSector
			info.put_u32(512); // PhysicalBytesPerSectorForAtomicity
			info.put_u32(512); // PhysicalBytesPerSectorForPerformance
			info.put_u32(512); // FileSystemEffectiveBytesPerSector
			info.put_u32(0x00000003);
			info.put_u32(0); // ByteOffsetForSectorAlignment
			info.put_u32(0); // ByteOffsetForPartitionAlignment
		} else {
			SMB_LOG("  QUERY_INFO: unsupported filesystem info class 0x%02X", info_class);

			return status::invalid_info_class;
		}

		return emit_info(out, info, output_buf_len);
	}
	std::uint32_t handle_flush(byte_reader& r, byte_writer& out) {
		r.get_u16();
		r.get_u16();
		r.get_u32();
		r.get_u64();

		std::uint64_t id = resolve_fid(r.get_u64());

		auto it = open_files_.find(id);

		if (it == open_files_.end())
			return status::invalid_handle;

		if (it->second.file)
			std::fflush(it->second.file);

		out.put_u16(4); out.put_u16(0);

		return status::success;
	}

	std::unique_ptr<netkit::tcp::tcp_stream> stream_;
	std::string share_name_;
	std::filesystem::path share_root_;
	bool read_only_ = false;
	credentials::info creds_{};

	std::array<std::uint8_t, 8> server_challenge_{};
	bool session_established_ = false;
	std::uint64_t session_id_ = 0;

	bool client_negotiate_signing_enabled_ = false;
	bool signing_active_ = false;
	std::array<std::uint8_t, 16> session_key_{};
	bool tree_connected_ = false;
	std::uint32_t tree_id_ = 0;

	std::unordered_map<std::uint64_t, open_handle> open_files_;
	std::uint64_t next_file_id_ = 1;

	std::uint64_t compound_fid_ = 0xFFFFFFFFFFFFFFFFULL;
	std::uint32_t compound_tree_id_ = 0;
	std::uint64_t compound_session_id_ = 0;
	std::uint32_t compound_prev_status_ = 0;
	bool compound_have_prev_ = false;
};

}