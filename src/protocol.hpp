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

#include <array>
#include <cstdint>

namespace smb {

enum class command : std::uint16_t {
	negotiate = 0x0000,
	session_setup = 0x0001,
	logoff = 0x0002,
	tree_connect = 0x0003,
	tree_disconnect = 0x0004,
	create = 0x0005,
	close_ = 0x0006,
	flush = 0x0007,
	read = 0x0008,
	write = 0x0009,
	lock = 0x000A,
	ioctl = 0x000B,
	cancel = 0x000C,
	echo = 0x000D,
	query_directory = 0x000E,
	change_notify = 0x000F,
	query_info = 0x0010,
	set_info = 0x0011,
	oplock_break = 0x0012,
};

namespace status {
	constexpr std::uint32_t success = 0x00000000;
	constexpr std::uint32_t more_processing_required = 0xC0000016;
	constexpr std::uint32_t invalid_parameter = 0xC000000D;
	constexpr std::uint32_t no_such_file = 0xC000000F;
	constexpr std::uint32_t object_name_not_found = 0xC0000034;
	constexpr std::uint32_t object_name_collision = 0xC0000035;
	constexpr std::uint32_t object_path_not_found = 0xC000003A;
	constexpr std::uint32_t not_supported = 0xC00000BB;
	constexpr std::uint32_t access_denied = 0xC0000022;
	constexpr std::uint32_t end_of_file = 0xC0000011;
	constexpr std::uint32_t no_more_files = 0x80000006;
	constexpr std::uint32_t invalid_smb = 0xC0000018;
	constexpr std::uint32_t file_is_a_directory = 0xC00000BA;
	constexpr std::uint32_t not_a_directory = 0xC0000103;
	constexpr std::uint32_t invalid_handle = 0xC0000008;
	constexpr std::uint32_t directory_not_empty = 0xC0000101;
	constexpr std::uint32_t invalid_info_class = 0xC0000003;
	constexpr std::uint32_t info_length_mismatch = 0xC0000004;
}

namespace hdr_flags {
	constexpr std::uint32_t server_to_redir = 0x00000001;
}

namespace dialect {
	constexpr std::uint16_t smb_2_0_2 = 0x0202;
	constexpr std::uint16_t smb_2_1 = 0x0210;
	constexpr std::uint16_t wildcard = 0x02FF;
}

namespace session_flags {
	constexpr std::uint16_t is_guest = 0x0001;
}

namespace create_disposition {
	constexpr std::uint32_t supersede = 0;
	constexpr std::uint32_t open = 1;
	constexpr std::uint32_t create = 2;
	constexpr std::uint32_t open_if = 3;
	constexpr std::uint32_t overwrite = 4;
	constexpr std::uint32_t overwrite_if = 5;
}

namespace create_options {
	constexpr std::uint32_t directory_file = 0x00000001;
}

namespace file_attr {
	constexpr std::uint32_t readonly = 0x00000001;
	constexpr std::uint32_t directory = 0x00000010;
	constexpr std::uint32_t archive = 0x00000020;
	constexpr std::uint32_t normal = 0x00000080;
}

namespace create_action {
	constexpr std::uint32_t superseded = 0;
	constexpr std::uint32_t opened = 1;
	constexpr std::uint32_t created = 2;
	constexpr std::uint32_t overwritten = 3;
}

namespace file_info_class {
	constexpr std::uint8_t directory = 0x01;
	constexpr std::uint8_t full_directory = 0x02;
	constexpr std::uint8_t both_directory = 0x03;
	constexpr std::uint8_t basic_info = 0x04;
	constexpr std::uint8_t standard_info = 0x05;
	constexpr std::uint8_t internal_info = 0x06;
	constexpr std::uint8_t id_both_directory = 0x25;
	constexpr std::uint8_t network_open_info = 0x22;
	constexpr std::uint8_t all_info = 0x12;
	constexpr std::uint8_t attribute_tag_info = 0x23;
}

namespace info_type {
	constexpr std::uint8_t file = 0x01;
	constexpr std::uint8_t filesystem = 0x02;
}

namespace fs_info_class {
	constexpr std::uint8_t volume = 1;
	constexpr std::uint8_t size = 3;
	constexpr std::uint8_t attribute = 5;
	constexpr std::uint8_t full_size = 7;
}

constexpr std::size_t header_size = 64;
constexpr std::array<std::uint8_t, 4> protocol_id = {0xFE, 'S', 'M', 'B'};

struct header {
	std::uint16_t credit_charge = 0;
	std::uint32_t status = 0;
	command cmd = command::negotiate;
	std::uint16_t credit_request_response = 1;
	std::uint32_t flags = 0;
	std::uint32_t next_command = 0;
	std::uint64_t message_id = 0;
	std::uint32_t tree_id = 0;
	std::uint64_t session_id = 0;
	std::array<std::uint8_t, 16> signature{};

	static header parse(byte_reader& r) {
		header h;
		std::array<std::uint8_t, 4> proto{};
		r.get_bytes(proto.data(), 4);
		r.get_u16();
		h.credit_charge = r.get_u16();
		h.status = r.get_u32();
		h.cmd = static_cast<command>(r.get_u16());
		h.credit_request_response = r.get_u16();
		h.flags = r.get_u32();
		h.next_command = r.get_u32();
		h.message_id = r.get_u64();
		r.get_u32();
		h.tree_id = r.get_u32();
		h.session_id = r.get_u64();
		r.get_bytes(h.signature.data(), 16);
		return h;
	}

	void write(byte_writer& w) const {
		w.put_bytes(protocol_id.data(), 4);
		w.put_u16(64);
		w.put_u16(credit_charge);
		w.put_u32(status);
		w.put_u16(static_cast<std::uint16_t>(cmd));
		w.put_u16(credit_request_response);
		w.put_u32(flags);
		w.put_u32(next_command);
		w.put_u64(message_id);
		w.put_u32(0); // Reserved
		w.put_u32(tree_id);
		w.put_u64(session_id);
		w.put_bytes(signature.data(), 16);
	}
};

}
