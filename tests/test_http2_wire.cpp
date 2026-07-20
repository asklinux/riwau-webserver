#include "rimau/protocol/http2_frame.hpp"
#include "rimau/protocol/http2_hpack.hpp"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

int main()
{
    using namespace rimau::protocol::http2;

    assert(has_client_connection_preface(std::string(client_connection_preface) + "extra"));
    assert(!has_client_connection_preface("PRI * HTTP/2.0\r\n\r\n"));

    const auto settings_payload = serialize_settings_payload({
        { SettingIdentifier::max_concurrent_streams, 100 },
        { SettingIdentifier::initial_window_size, 65535 },
    });
    const auto settings = parse_settings_payload(settings_payload);
    assert(settings.size() == 2);
    assert(settings[0].identifier == SettingIdentifier::max_concurrent_streams);
    assert(settings[0].value == 100);
    assert(settings[1].identifier == SettingIdentifier::initial_window_size);
    assert(settings[1].value == 65535);

    Frame settings_frame;
    settings_frame.type = static_cast<std::uint8_t>(FrameType::settings);
    settings_frame.stream_id = 0;
    settings_frame.payload = settings_payload;
    const auto settings_wire = serialize_frame(settings_frame);
    const auto parsed_settings = parse_frame(std::string_view(reinterpret_cast<const char*>(settings_wire.data()), settings_wire.size()));
    assert(parsed_settings.ok);
    assert(!parsed_settings.incomplete);
    assert(parsed_settings.consumed == settings_wire.size());
    assert(parsed_settings.frame.type == static_cast<std::uint8_t>(FrameType::settings));
    assert(parsed_settings.frame.stream_id == 0);
    assert(parsed_settings.frame.payload == settings_payload);

    const std::vector<HeaderField> headers {
        { ":method", "GET" },
        { ":scheme", "https" },
        { ":path", "/" },
        { ":authority", "example.test" },
        { "user-agent", "rimau-test" },
    };
    const auto header_block = hpack_encode_header_block(headers);
    const auto decoded_headers = hpack_decode_header_block(header_block);
    assert(decoded_headers.size() == headers.size());
    for (std::size_t index = 0; index < headers.size(); ++index) {
        assert(decoded_headers[index].name == headers[index].name);
        assert(decoded_headers[index].value == headers[index].value);
    }

    Frame headers_frame;
    headers_frame.type = static_cast<std::uint8_t>(FrameType::headers);
    headers_frame.flags = 0x4;
    headers_frame.stream_id = 1;
    headers_frame.payload = header_block;
    const auto headers_wire = serialize_frame(headers_frame);
    const auto parsed_headers = parse_frame(std::string_view(reinterpret_cast<const char*>(headers_wire.data()), headers_wire.size()));
    assert(parsed_headers.ok);
    assert(parsed_headers.frame.stream_id == 1);
    assert(parsed_headers.frame.flags == 0x4);
    assert(hpack_decode_header_block(parsed_headers.frame.payload)[3].value == "example.test");

    const std::vector<std::uint8_t> incremental_literal_authority {
        0x41,
        0x0d,
        'r',
        'i',
        'm',
        'a',
        'u',
        '.',
        'e',
        'x',
        'a',
        'm',
        'p',
        'l',
        'e',
    };
    const auto decoded_incremental_literal = hpack_decode_header_block(incremental_literal_authority);
    assert(decoded_incremental_literal.size() == 1);
    assert(decoded_incremental_literal[0].name == ":authority");
    assert(decoded_incremental_literal[0].value == "rimau.example");

    std::string decoded_huffman;
    const std::vector<std::uint8_t> huffman_www_example {
        0x8c, 0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff,
    };
    const auto huffman_consumed = hpack_decode_string(huffman_www_example, 0, decoded_huffman);
    assert(huffman_consumed == huffman_www_example.size());
    assert(decoded_huffman == "www.example.com");

    HpackDecoder decoder;
    const std::vector<std::uint8_t> indexed_authority_huffman {
        0x41, 0x8c, 0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff,
    };
    const auto decoded_dynamic_insert = decoder.decode_header_block(indexed_authority_huffman);
    assert(decoded_dynamic_insert.size() == 1);
    assert(decoded_dynamic_insert[0].name == ":authority");
    assert(decoded_dynamic_insert[0].value == "www.example.com");
    assert(decoder.table_size() > 0);
    const auto decoded_dynamic_reference = decoder.decode_header_block({ 0xbe });
    assert(decoded_dynamic_reference.size() == 1);
    assert(decoded_dynamic_reference[0].name == ":authority");
    assert(decoded_dynamic_reference[0].value == "www.example.com");

    const auto incomplete = parse_frame(std::string_view(reinterpret_cast<const char*>(headers_wire.data()), 8));
    assert(!incomplete.ok);
    assert(incomplete.incomplete);

    Frame invalid_headers;
    invalid_headers.type = static_cast<std::uint8_t>(FrameType::headers);
    invalid_headers.stream_id = 0;
    invalid_headers.payload = header_block;
    bool invalid_headers_failed = false;
    try {
        (void)serialize_frame(invalid_headers);
    } catch (const std::runtime_error&) {
        invalid_headers_failed = true;
    }
    assert(invalid_headers_failed);

    Frame invalid_window_update;
    invalid_window_update.type = static_cast<std::uint8_t>(FrameType::window_update);
    invalid_window_update.stream_id = 1;
    invalid_window_update.payload = { 0, 0, 0, 0 };
    bool invalid_window_failed = false;
    try {
        (void)serialize_frame(invalid_window_update);
    } catch (const std::runtime_error&) {
        invalid_window_failed = true;
    }
    assert(invalid_window_failed);

    return 0;
}
