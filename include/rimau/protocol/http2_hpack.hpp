#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rimau::protocol::http2 {

struct HeaderField {
    std::string name;
    std::string value;
};

std::vector<std::uint8_t> hpack_encode_integer(std::uint32_t value, std::uint8_t prefix_bits, std::uint8_t prefix_mask);
std::size_t hpack_decode_integer(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint8_t prefix_bits, std::uint32_t& value);

std::vector<std::uint8_t> hpack_encode_string(std::string_view value);
std::size_t hpack_decode_string(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::string& value);

std::vector<std::uint8_t> hpack_encode_header_block(const std::vector<HeaderField>& headers);
std::vector<HeaderField> hpack_decode_header_block(const std::vector<std::uint8_t>& block);

} // namespace rimau::protocol::http2
