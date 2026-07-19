#include "rimau/protocol/http3_frame.hpp"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

int main()
{
    using namespace rimau::protocol::http3;

    const std::vector<std::uint64_t> varint_values {
        0,
        63,
        64,
        16383,
        16384,
        1073741823,
        1073741824,
        4611686018427387903ULL,
    };
    for (const auto value : varint_values) {
        const auto encoded = encode_varint(value);
        const auto decoded = decode_varint(std::string_view(reinterpret_cast<const char*>(encoded.data()), encoded.size()));
        assert(decoded.ok);
        assert(decoded.value == value);
        assert(decoded.consumed == encoded.size());
    }

    bool too_large_failed = false;
    try {
        (void)encode_varint(4611686018427387904ULL);
    } catch (const std::runtime_error&) {
        too_large_failed = true;
    }
    assert(too_large_failed);

    const std::vector<std::uint8_t> incomplete_varint_bytes { 0x40 };
    const auto incomplete_varint = decode_varint(std::string_view(reinterpret_cast<const char*>(incomplete_varint_bytes.data()), incomplete_varint_bytes.size()));
    assert(!incomplete_varint.ok);
    assert(incomplete_varint.incomplete);

    const auto settings_payload = serialize_settings_payload({
        { SettingIdentifier::qpack_max_table_capacity, 0 },
        { SettingIdentifier::qpack_blocked_streams, 16 },
        { SettingIdentifier::max_field_section_size, 65536 },
    });
    const auto settings = parse_settings_payload(settings_payload);
    assert(settings.size() == 3);
    assert(settings[0].identifier == SettingIdentifier::qpack_max_table_capacity);
    assert(settings[1].identifier == SettingIdentifier::qpack_blocked_streams);
    assert(settings[1].value == 16);
    assert(settings[2].identifier == SettingIdentifier::max_field_section_size);
    assert(settings[2].value == 65536);

    Frame settings_frame;
    settings_frame.type = static_cast<std::uint64_t>(FrameType::settings);
    settings_frame.payload = settings_payload;
    const auto settings_wire = serialize_frame(settings_frame);
    const auto parsed_settings = parse_frame(std::string_view(reinterpret_cast<const char*>(settings_wire.data()), settings_wire.size()));
    assert(parsed_settings.ok);
    assert(parsed_settings.consumed == settings_wire.size());
    assert(parsed_settings.frame.type == static_cast<std::uint64_t>(FrameType::settings));
    assert(parse_settings_payload(parsed_settings.frame.payload).size() == 3);

    Frame data_frame;
    data_frame.type = static_cast<std::uint64_t>(FrameType::data);
    data_frame.payload = { 'r', 'i', 'm', 'a', 'u' };
    const auto data_wire = serialize_frame(data_frame);
    const auto parsed_data = parse_frame(std::string_view(reinterpret_cast<const char*>(data_wire.data()), data_wire.size()));
    assert(parsed_data.ok);
    assert(parsed_data.frame.payload == data_frame.payload);

    const auto incomplete_frame = parse_frame(std::string_view(reinterpret_cast<const char*>(data_wire.data()), data_wire.size() - 1));
    assert(!incomplete_frame.ok);
    assert(incomplete_frame.incomplete);

    const std::vector<std::uint8_t> incomplete_settings { 0x06 };
    bool incomplete_settings_failed = false;
    try {
        (void)parse_settings_payload(incomplete_settings);
    } catch (const std::runtime_error&) {
        incomplete_settings_failed = true;
    }
    assert(incomplete_settings_failed);

    return 0;
}
