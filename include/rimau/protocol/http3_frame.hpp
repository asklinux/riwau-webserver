#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rimau::protocol::http3 {

enum class FrameType : std::uint64_t {
    data = 0x0,
    headers = 0x1,
    cancel_push = 0x3,
    settings = 0x4,
    push_promise = 0x5,
    goaway = 0x7,
    max_push_id = 0xd
};

enum class SettingIdentifier : std::uint64_t {
    qpack_max_table_capacity = 0x1,
    max_field_section_size = 0x6,
    qpack_blocked_streams = 0x7
};

struct Setting {
    SettingIdentifier identifier;
    std::uint64_t value = 0;
};

struct VarIntResult {
    bool ok = false;
    bool incomplete = false;
    std::size_t consumed = 0;
    std::uint64_t value = 0;
    std::string error;
};

struct Frame {
    std::uint64_t type = 0;
    std::vector<std::uint8_t> payload;
};

struct ParseResult {
    bool ok = false;
    bool incomplete = false;
    std::size_t consumed = 0;
    Frame frame;
    std::string error;
};

std::vector<std::uint8_t> encode_varint(std::uint64_t value);
VarIntResult decode_varint(std::string_view bytes, std::size_t offset = 0);

std::vector<std::uint8_t> serialize_frame(const Frame& frame);
ParseResult parse_frame(std::string_view bytes);

std::vector<std::uint8_t> serialize_settings_payload(const std::vector<Setting>& settings);
std::vector<Setting> parse_settings_payload(const std::vector<std::uint8_t>& payload);

} // namespace rimau::protocol::http3
