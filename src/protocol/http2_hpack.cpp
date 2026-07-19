#include "rimau/protocol/http2_hpack.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace rimau::protocol::http2 {
namespace {

struct StaticTableEntry {
    std::string_view name;
    std::string_view value;
};

constexpr std::array<StaticTableEntry, 61> static_table {{
    { ":authority", "" },
    { ":method", "GET" },
    { ":method", "POST" },
    { ":path", "/" },
    { ":path", "/index.html" },
    { ":scheme", "http" },
    { ":scheme", "https" },
    { ":status", "200" },
    { ":status", "204" },
    { ":status", "206" },
    { ":status", "304" },
    { ":status", "400" },
    { ":status", "404" },
    { ":status", "500" },
    { "accept-charset", "" },
    { "accept-encoding", "gzip, deflate" },
    { "accept-language", "" },
    { "accept-ranges", "" },
    { "accept", "" },
    { "access-control-allow-origin", "" },
    { "age", "" },
    { "allow", "" },
    { "authorization", "" },
    { "cache-control", "" },
    { "content-disposition", "" },
    { "content-encoding", "" },
    { "content-language", "" },
    { "content-length", "" },
    { "content-location", "" },
    { "content-range", "" },
    { "content-type", "" },
    { "cookie", "" },
    { "date", "" },
    { "etag", "" },
    { "expect", "" },
    { "expires", "" },
    { "from", "" },
    { "host", "" },
    { "if-match", "" },
    { "if-modified-since", "" },
    { "if-none-match", "" },
    { "if-range", "" },
    { "if-unmodified-since", "" },
    { "last-modified", "" },
    { "link", "" },
    { "location", "" },
    { "max-forwards", "" },
    { "proxy-authenticate", "" },
    { "proxy-authorization", "" },
    { "range", "" },
    { "referer", "" },
    { "refresh", "" },
    { "retry-after", "" },
    { "server", "" },
    { "set-cookie", "" },
    { "strict-transport-security", "" },
    { "transfer-encoding", "" },
    { "user-agent", "" },
    { "vary", "" },
    { "via", "" },
    { "www-authenticate", "" },
}};

const StaticTableEntry& static_entry(std::uint32_t index)
{
    if (index == 0 || index > static_table.size()) {
        throw std::runtime_error("HPACK static table index is out of range");
    }
    return static_table[index - 1];
}

std::uint32_t find_exact_static_index(const HeaderField& header)
{
    for (std::size_t index = 0; index < static_table.size(); ++index) {
        if (static_table[index].name == header.name && static_table[index].value == header.value) {
            return static_cast<std::uint32_t>(index + 1);
        }
    }
    return 0;
}

std::uint32_t find_name_static_index(std::string_view name)
{
    for (std::size_t index = 0; index < static_table.size(); ++index) {
        if (static_table[index].name == name) {
            return static_cast<std::uint32_t>(index + 1);
        }
    }
    return 0;
}

void append_all(std::vector<std::uint8_t>& output, const std::vector<std::uint8_t>& input)
{
    output.insert(output.end(), input.begin(), input.end());
}

} // namespace

std::vector<std::uint8_t> hpack_encode_integer(std::uint32_t value, std::uint8_t prefix_bits, std::uint8_t prefix_mask)
{
    if (prefix_bits == 0 || prefix_bits > 8) {
        throw std::runtime_error("invalid HPACK integer prefix width");
    }

    const std::uint32_t max_prefix = (1U << prefix_bits) - 1U;
    std::vector<std::uint8_t> output;
    if (value < max_prefix) {
        output.push_back(prefix_mask | static_cast<std::uint8_t>(value));
        return output;
    }

    output.push_back(prefix_mask | static_cast<std::uint8_t>(max_prefix));
    value -= max_prefix;
    while (value >= 128) {
        output.push_back(static_cast<std::uint8_t>((value % 128) + 128));
        value /= 128;
    }
    output.push_back(static_cast<std::uint8_t>(value));
    return output;
}

std::size_t hpack_decode_integer(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint8_t prefix_bits, std::uint32_t& value)
{
    if (offset >= bytes.size()) {
        throw std::runtime_error("incomplete HPACK integer");
    }
    if (prefix_bits == 0 || prefix_bits > 8) {
        throw std::runtime_error("invalid HPACK integer prefix width");
    }

    const std::uint32_t max_prefix = (1U << prefix_bits) - 1U;
    value = bytes[offset] & max_prefix;
    ++offset;
    if (value < max_prefix) {
        return offset;
    }

    std::uint32_t multiplier = 0;
    while (true) {
        if (offset >= bytes.size()) {
            throw std::runtime_error("incomplete HPACK integer continuation");
        }

        const std::uint8_t byte = bytes[offset++];
        if (multiplier >= 28 && (byte & 0x7fU) != 0) {
            throw std::runtime_error("HPACK integer overflow");
        }
        value += static_cast<std::uint32_t>(byte & 0x7fU) << multiplier;
        if ((byte & 0x80U) == 0) {
            return offset;
        }
        multiplier += 7;
    }
}

std::vector<std::uint8_t> hpack_encode_string(std::string_view value)
{
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("HPACK string is too large");
    }

    auto output = hpack_encode_integer(static_cast<std::uint32_t>(value.size()), 7, 0x00);
    output.insert(output.end(), value.begin(), value.end());
    return output;
}

std::size_t hpack_decode_string(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::string& value)
{
    if (offset >= bytes.size()) {
        throw std::runtime_error("incomplete HPACK string");
    }
    if ((bytes[offset] & 0x80U) != 0) {
        throw std::runtime_error("HPACK Huffman strings are not implemented yet");
    }

    std::uint32_t size = 0;
    offset = hpack_decode_integer(bytes, offset, 7, size);
    if (bytes.size() - offset < size) {
        throw std::runtime_error("incomplete HPACK string data");
    }

    value.assign(reinterpret_cast<const char*>(bytes.data() + offset), size);
    return offset + size;
}

std::vector<std::uint8_t> hpack_encode_header_block(const std::vector<HeaderField>& headers)
{
    std::vector<std::uint8_t> output;
    for (const auto& header : headers) {
        if (header.name.empty()) {
            throw std::runtime_error("HPACK header name cannot be empty");
        }

        if (const auto exact_index = find_exact_static_index(header); exact_index != 0) {
            append_all(output, hpack_encode_integer(exact_index, 7, 0x80));
            continue;
        }

        const auto name_index = find_name_static_index(header.name);
        append_all(output, hpack_encode_integer(name_index, 4, 0x00));
        if (name_index == 0) {
            append_all(output, hpack_encode_string(header.name));
        }
        append_all(output, hpack_encode_string(header.value));
    }
    return output;
}

std::vector<HeaderField> hpack_decode_header_block(const std::vector<std::uint8_t>& block)
{
    std::vector<HeaderField> headers;
    std::size_t offset = 0;
    while (offset < block.size()) {
        const std::uint8_t byte = block[offset];

        if ((byte & 0x80U) != 0) {
            std::uint32_t index = 0;
            offset = hpack_decode_integer(block, offset, 7, index);
            const auto& entry = static_entry(index);
            headers.push_back(HeaderField { std::string(entry.name), std::string(entry.value) });
            continue;
        }

        if ((byte & 0x40U) != 0) {
            std::uint32_t name_index = 0;
            offset = hpack_decode_integer(block, offset, 6, name_index);

            HeaderField header;
            if (name_index == 0) {
                offset = hpack_decode_string(block, offset, header.name);
            } else {
                header.name = std::string(static_entry(name_index).name);
            }
            offset = hpack_decode_string(block, offset, header.value);
            headers.push_back(std::move(header));
            continue;
        }

        const bool never_indexed = (byte & 0x10U) != 0;
        const std::uint8_t prefix_bits = never_indexed ? 4 : 4;
        std::uint32_t name_index = 0;
        offset = hpack_decode_integer(block, offset, prefix_bits, name_index);

        HeaderField header;
        if (name_index == 0) {
            offset = hpack_decode_string(block, offset, header.name);
        } else {
            header.name = std::string(static_entry(name_index).name);
        }
        offset = hpack_decode_string(block, offset, header.value);
        headers.push_back(std::move(header));
    }
    return headers;
}

} // namespace rimau::protocol::http2
