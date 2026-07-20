#pragma once

#include <cstddef>
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

class HpackDecoder {
public:
    void set_max_table_size(std::size_t bytes);
    std::size_t max_table_size() const noexcept;
    std::size_t table_size() const noexcept;
    void reset();

    std::vector<HeaderField> decode_header_block(const std::vector<std::uint8_t>& block);

private:
    HeaderField indexed_header(std::uint32_t index) const;
    std::string indexed_name(std::uint32_t index) const;
    void add_dynamic(HeaderField header);
    void evict_dynamic();

    std::size_t max_table_size_ = 4096;
    std::size_t table_size_ = 0;
    std::vector<HeaderField> dynamic_table_;
};

} // namespace rimau::protocol::http2
