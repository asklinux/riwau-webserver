#include "rimau/protocol/http3_frame.hpp"

#include <limits>
#include <stdexcept>

namespace rimau::protocol::http3 {
namespace {

std::uint8_t byte_at(std::string_view bytes, std::size_t offset)
{
    return static_cast<std::uint8_t>(bytes[offset]);
}

void append_all(std::vector<std::uint8_t>& output, const std::vector<std::uint8_t>& input)
{
    output.insert(output.end(), input.begin(), input.end());
}

} // namespace

std::vector<std::uint8_t> encode_varint(std::uint64_t value)
{
    std::vector<std::uint8_t> output;
    if (value <= 0x3fULL) {
        output.push_back(static_cast<std::uint8_t>(value));
        return output;
    }
    if (value <= 0x3fffULL) {
        output.push_back(static_cast<std::uint8_t>(0x40U | ((value >> 8) & 0x3fU)));
        output.push_back(static_cast<std::uint8_t>(value & 0xffU));
        return output;
    }
    if (value <= 0x3fffffffULL) {
        output.push_back(static_cast<std::uint8_t>(0x80U | ((value >> 24) & 0x3fU)));
        output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
        output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
        output.push_back(static_cast<std::uint8_t>(value & 0xffU));
        return output;
    }
    if (value <= 0x3fffffffffffffffULL) {
        output.push_back(static_cast<std::uint8_t>(0xc0U | ((value >> 56) & 0x3fU)));
        output.push_back(static_cast<std::uint8_t>((value >> 48) & 0xffU));
        output.push_back(static_cast<std::uint8_t>((value >> 40) & 0xffU));
        output.push_back(static_cast<std::uint8_t>((value >> 32) & 0xffU));
        output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffU));
        output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
        output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
        output.push_back(static_cast<std::uint8_t>(value & 0xffU));
        return output;
    }

    throw std::runtime_error("HTTP/3 QUIC varint value exceeds 62 bits");
}

VarIntResult decode_varint(std::string_view bytes, std::size_t offset)
{
    VarIntResult result;
    if (offset >= bytes.size()) {
        result.incomplete = true;
        result.error = "incomplete QUIC varint";
        return result;
    }

    const auto first = byte_at(bytes, offset);
    const std::size_t length = static_cast<std::size_t>(1U << (first >> 6));
    if (bytes.size() - offset < length) {
        result.incomplete = true;
        result.error = "incomplete QUIC varint";
        return result;
    }

    std::uint64_t value = first & 0x3fU;
    for (std::size_t index = 1; index < length; ++index) {
        value = (value << 8) | byte_at(bytes, offset + index);
    }

    result.ok = true;
    result.consumed = length;
    result.value = value;
    return result;
}

std::vector<std::uint8_t> serialize_frame(const Frame& frame)
{
    auto output = encode_varint(frame.type);
    append_all(output, encode_varint(frame.payload.size()));
    output.insert(output.end(), frame.payload.begin(), frame.payload.end());
    return output;
}

ParseResult parse_frame(std::string_view bytes)
{
    ParseResult result;
    const auto type = decode_varint(bytes);
    if (!type.ok) {
        result.incomplete = type.incomplete;
        result.error = type.error;
        return result;
    }

    const auto length = decode_varint(bytes, type.consumed);
    if (!length.ok) {
        result.incomplete = length.incomplete;
        result.error = length.error;
        return result;
    }

    const auto payload_offset = type.consumed + length.consumed;
    if (length.value > std::numeric_limits<std::size_t>::max() || bytes.size() - payload_offset < length.value) {
        result.incomplete = true;
        result.error = "incomplete HTTP/3 frame payload";
        return result;
    }

    result.frame.type = type.value;
    result.frame.payload.reserve(static_cast<std::size_t>(length.value));
    for (std::size_t index = 0; index < static_cast<std::size_t>(length.value); ++index) {
        result.frame.payload.push_back(byte_at(bytes, payload_offset + index));
    }
    result.consumed = payload_offset + static_cast<std::size_t>(length.value);
    result.ok = true;
    return result;
}

std::vector<std::uint8_t> serialize_settings_payload(const std::vector<Setting>& settings)
{
    std::vector<std::uint8_t> payload;
    for (const auto& setting : settings) {
        append_all(payload, encode_varint(static_cast<std::uint64_t>(setting.identifier)));
        append_all(payload, encode_varint(setting.value));
    }
    return payload;
}

std::vector<Setting> parse_settings_payload(const std::vector<std::uint8_t>& payload)
{
    std::vector<Setting> settings;
    const std::string_view bytes(reinterpret_cast<const char*>(payload.data()), payload.size());
    std::size_t offset = 0;
    while (offset < payload.size()) {
        const auto identifier = decode_varint(bytes, offset);
        if (!identifier.ok) {
            throw std::runtime_error("incomplete HTTP/3 SETTINGS identifier");
        }
        offset += identifier.consumed;

        const auto value = decode_varint(bytes, offset);
        if (!value.ok) {
            throw std::runtime_error("incomplete HTTP/3 SETTINGS value");
        }
        offset += value.consumed;

        settings.push_back(Setting {
            static_cast<SettingIdentifier>(identifier.value),
            value.value,
        });
    }
    return settings;
}

} // namespace rimau::protocol::http3
