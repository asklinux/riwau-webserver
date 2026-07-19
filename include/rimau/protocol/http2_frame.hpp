#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rimau::protocol::http2 {

inline constexpr std::string_view client_connection_preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
inline constexpr std::uint32_t default_max_frame_size = 16 * 1024;
inline constexpr std::uint32_t max_allowed_frame_size = 16 * 1024 * 1024 - 1;

enum class FrameType : std::uint8_t {
    data = 0x0,
    headers = 0x1,
    priority = 0x2,
    rst_stream = 0x3,
    settings = 0x4,
    push_promise = 0x5,
    ping = 0x6,
    goaway = 0x7,
    window_update = 0x8,
    continuation = 0x9
};

enum class SettingIdentifier : std::uint16_t {
    header_table_size = 0x1,
    enable_push = 0x2,
    max_concurrent_streams = 0x3,
    initial_window_size = 0x4,
    max_frame_size = 0x5,
    max_header_list_size = 0x6
};

struct Setting {
    SettingIdentifier identifier;
    std::uint32_t value = 0;
};

struct Frame {
    std::uint8_t type = 0;
    std::uint8_t flags = 0;
    std::uint32_t stream_id = 0;
    std::vector<std::uint8_t> payload;
};

struct ParseResult {
    bool ok = false;
    bool incomplete = false;
    std::size_t consumed = 0;
    Frame frame;
    std::string error;
};

bool has_client_connection_preface(std::string_view bytes);
std::vector<std::uint8_t> serialize_frame(const Frame& frame, std::uint32_t max_frame_size = default_max_frame_size);
ParseResult parse_frame(std::string_view bytes, std::uint32_t max_frame_size = default_max_frame_size);

std::vector<std::uint8_t> serialize_settings_payload(const std::vector<Setting>& settings);
std::vector<Setting> parse_settings_payload(const std::vector<std::uint8_t>& payload);

} // namespace rimau::protocol::http2
