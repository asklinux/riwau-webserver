#include "rimau/protocol/http2_frame.hpp"

#include <limits>
#include <stdexcept>

namespace rimau::protocol::http2 {
namespace {

std::uint32_t read_u24(std::string_view bytes)
{
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) << 16)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 8)
        | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2]));
}

std::uint32_t read_u31(std::string_view bytes)
{
    return ((static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) & 0x7fU) << 24)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 16)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 8)
        | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3]));
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24)
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8)
        | static_cast<std::uint32_t>(bytes[offset + 3]);
}

void write_u24(std::vector<std::uint8_t>& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void write_u32(std::vector<std::uint8_t>& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

std::string validate_frame(const Frame& frame, std::uint32_t max_frame_size)
{
    if (max_frame_size > max_allowed_frame_size) {
        return "max_frame_size exceeds HTTP/2 maximum";
    }

    if (frame.payload.size() > max_frame_size) {
        return "HTTP/2 frame payload exceeds configured maximum frame size";
    }

    const auto type = static_cast<FrameType>(frame.type);
    switch (type) {
    case FrameType::data:
    case FrameType::headers:
    case FrameType::priority:
    case FrameType::rst_stream:
    case FrameType::push_promise:
    case FrameType::continuation:
        if (frame.stream_id == 0) {
            return "HTTP/2 stream frame must use a non-zero stream id";
        }
        break;
    case FrameType::settings:
    case FrameType::ping:
    case FrameType::goaway:
        if (frame.stream_id != 0) {
            return "HTTP/2 connection frame must use stream id 0";
        }
        break;
    case FrameType::window_update:
        break;
    }

    if (type == FrameType::settings) {
        constexpr std::uint8_t ack_flag = 0x1;
        if ((frame.flags & ack_flag) != 0 && !frame.payload.empty()) {
            return "HTTP/2 SETTINGS ack frame must not contain a payload";
        }
        if ((frame.payload.size() % 6) != 0) {
            return "HTTP/2 SETTINGS payload length must be a multiple of 6";
        }
    } else if (type == FrameType::ping) {
        if (frame.payload.size() != 8) {
            return "HTTP/2 PING payload length must be 8";
        }
    } else if (type == FrameType::rst_stream) {
        if (frame.payload.size() != 4) {
            return "HTTP/2 RST_STREAM payload length must be 4";
        }
    } else if (type == FrameType::goaway) {
        if (frame.payload.size() < 8) {
            return "HTTP/2 GOAWAY payload length must be at least 8";
        }
    } else if (type == FrameType::window_update) {
        if (frame.payload.size() != 4) {
            return "HTTP/2 WINDOW_UPDATE payload length must be 4";
        }
        if ((read_u32(frame.payload, 0) & 0x7fffffffU) == 0) {
            return "HTTP/2 WINDOW_UPDATE increment must be non-zero";
        }
    }

    return {};
}

} // namespace

bool has_client_connection_preface(std::string_view bytes)
{
    return bytes.size() >= client_connection_preface.size()
        && bytes.substr(0, client_connection_preface.size()) == client_connection_preface;
}

std::vector<std::uint8_t> serialize_frame(const Frame& frame, std::uint32_t max_frame_size)
{
    if (frame.stream_id > 0x7fffffffU) {
        throw std::runtime_error("HTTP/2 stream id exceeds 31 bits");
    }

    const auto validation_error = validate_frame(frame, max_frame_size);
    if (!validation_error.empty()) {
        throw std::runtime_error(validation_error);
    }

    std::vector<std::uint8_t> output;
    output.reserve(9 + frame.payload.size());
    write_u24(output, static_cast<std::uint32_t>(frame.payload.size()));
    output.push_back(frame.type);
    output.push_back(frame.flags);
    write_u32(output, frame.stream_id & 0x7fffffffU);
    output.insert(output.end(), frame.payload.begin(), frame.payload.end());
    return output;
}

ParseResult parse_frame(std::string_view bytes, std::uint32_t max_frame_size)
{
    ParseResult result;
    if (bytes.size() < 9) {
        result.incomplete = true;
        result.error = "incomplete HTTP/2 frame header";
        return result;
    }

    if (max_frame_size > max_allowed_frame_size) {
        result.error = "max_frame_size exceeds HTTP/2 maximum";
        return result;
    }

    const auto length = read_u24(bytes.substr(0, 3));
    if (length > max_frame_size) {
        result.error = "HTTP/2 frame payload exceeds configured maximum frame size";
        return result;
    }
    if (bytes.size() < 9 + static_cast<std::size_t>(length)) {
        result.incomplete = true;
        result.error = "incomplete HTTP/2 frame payload";
        return result;
    }

    result.frame.type = static_cast<std::uint8_t>(bytes[3]);
    result.frame.flags = static_cast<std::uint8_t>(bytes[4]);
    result.frame.stream_id = read_u31(bytes.substr(5, 4));
    result.frame.payload.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        result.frame.payload.push_back(static_cast<std::uint8_t>(bytes[9 + index]));
    }
    result.consumed = 9 + length;

    const auto validation_error = validate_frame(result.frame, max_frame_size);
    if (!validation_error.empty()) {
        result.error = validation_error;
        return result;
    }

    result.ok = true;
    return result;
}

std::vector<std::uint8_t> serialize_settings_payload(const std::vector<Setting>& settings)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(settings.size() * 6);
    for (const auto& setting : settings) {
        const auto identifier = static_cast<std::uint16_t>(setting.identifier);
        payload.push_back(static_cast<std::uint8_t>((identifier >> 8) & 0xffU));
        payload.push_back(static_cast<std::uint8_t>(identifier & 0xffU));
        write_u32(payload, setting.value);
    }
    return payload;
}

std::vector<Setting> parse_settings_payload(const std::vector<std::uint8_t>& payload)
{
    if ((payload.size() % 6) != 0) {
        throw std::runtime_error("HTTP/2 SETTINGS payload length must be a multiple of 6");
    }

    std::vector<Setting> settings;
    settings.reserve(payload.size() / 6);
    for (std::size_t offset = 0; offset < payload.size(); offset += 6) {
        const auto identifier = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(payload[offset]) << 8) | static_cast<std::uint16_t>(payload[offset + 1]));
        settings.push_back(Setting {
            static_cast<SettingIdentifier>(identifier),
            read_u32(payload, offset + 2),
        });
    }
    return settings;
}

} // namespace rimau::protocol::http2
