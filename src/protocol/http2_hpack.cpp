#include "rimau/protocol/http2_hpack.hpp"

#include <array>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

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

struct HuffmanCode {
    std::uint32_t code = 0;
    std::uint8_t bits = 0;
};

constexpr std::array<HuffmanCode, 257> huffman_table {{
    { 0x1ff8U, 13 }, { 0x7fffd8U, 23 }, { 0xfffffe2U, 28 }, { 0xfffffe3U, 28 },
    { 0xfffffe4U, 28 }, { 0xfffffe5U, 28 }, { 0xfffffe6U, 28 }, { 0xfffffe7U, 28 },
    { 0xfffffe8U, 28 }, { 0xffffeaU, 24 }, { 0x3ffffffcU, 30 }, { 0xfffffe9U, 28 },
    { 0xfffffeaU, 28 }, { 0x3ffffffdU, 30 }, { 0xfffffebU, 28 }, { 0xfffffecU, 28 },
    { 0xfffffedU, 28 }, { 0xfffffeeU, 28 }, { 0xfffffefU, 28 }, { 0xffffff0U, 28 },
    { 0xffffff1U, 28 }, { 0xffffff2U, 28 }, { 0x3ffffffeU, 30 }, { 0xffffff3U, 28 },
    { 0xffffff4U, 28 }, { 0xffffff5U, 28 }, { 0xffffff6U, 28 }, { 0xffffff7U, 28 },
    { 0xffffff8U, 28 }, { 0xffffff9U, 28 }, { 0xffffffaU, 28 }, { 0xffffffbU, 28 },
    { 0x14U, 6 }, { 0x3f8U, 10 }, { 0x3f9U, 10 }, { 0xffaU, 12 },
    { 0x1ff9U, 13 }, { 0x15U, 6 }, { 0xf8U, 8 }, { 0x7faU, 11 },
    { 0x3faU, 10 }, { 0x3fbU, 10 }, { 0xf9U, 8 }, { 0x7fbU, 11 },
    { 0xfaU, 8 }, { 0x16U, 6 }, { 0x17U, 6 }, { 0x18U, 6 },
    { 0x0U, 5 }, { 0x1U, 5 }, { 0x2U, 5 }, { 0x19U, 6 },
    { 0x1aU, 6 }, { 0x1bU, 6 }, { 0x1cU, 6 }, { 0x1dU, 6 },
    { 0x1eU, 6 }, { 0x1fU, 6 }, { 0x5cU, 7 }, { 0xfbU, 8 },
    { 0x7ffcU, 15 }, { 0x20U, 6 }, { 0xffbU, 12 }, { 0x3fcU, 10 },
    { 0x1ffaU, 13 }, { 0x21U, 6 }, { 0x5dU, 7 }, { 0x5eU, 7 },
    { 0x5fU, 7 }, { 0x60U, 7 }, { 0x61U, 7 }, { 0x62U, 7 },
    { 0x63U, 7 }, { 0x64U, 7 }, { 0x65U, 7 }, { 0x66U, 7 },
    { 0x67U, 7 }, { 0x68U, 7 }, { 0x69U, 7 }, { 0x6aU, 7 },
    { 0x6bU, 7 }, { 0x6cU, 7 }, { 0x6dU, 7 }, { 0x6eU, 7 },
    { 0x6fU, 7 }, { 0x70U, 7 }, { 0x71U, 7 }, { 0x72U, 7 },
    { 0xfcU, 8 }, { 0x73U, 7 }, { 0xfdU, 8 }, { 0x1ffbU, 13 },
    { 0x7fff0U, 19 }, { 0x1ffcU, 13 }, { 0x3ffcU, 14 }, { 0x22U, 6 },
    { 0x7ffdU, 15 }, { 0x3U, 5 }, { 0x23U, 6 }, { 0x4U, 5 },
    { 0x24U, 6 }, { 0x5U, 5 }, { 0x25U, 6 }, { 0x26U, 6 },
    { 0x27U, 6 }, { 0x6U, 5 }, { 0x74U, 7 }, { 0x75U, 7 },
    { 0x28U, 6 }, { 0x29U, 6 }, { 0x2aU, 6 }, { 0x7U, 5 },
    { 0x2bU, 6 }, { 0x76U, 7 }, { 0x2cU, 6 }, { 0x8U, 5 },
    { 0x9U, 5 }, { 0x2dU, 6 }, { 0x77U, 7 }, { 0x78U, 7 },
    { 0x79U, 7 }, { 0x7aU, 7 }, { 0x7bU, 7 }, { 0x7ffeU, 15 },
    { 0x7fcU, 11 }, { 0x3ffdU, 14 }, { 0x1ffdU, 13 }, { 0xffffffcU, 28 },
    { 0xfffe6U, 20 }, { 0x3fffd2U, 22 }, { 0xfffe7U, 20 }, { 0xfffe8U, 20 },
    { 0x3fffd3U, 22 }, { 0x3fffd4U, 22 }, { 0x3fffd5U, 22 }, { 0x7fffd9U, 23 },
    { 0x3fffd6U, 22 }, { 0x7fffdaU, 23 }, { 0x7fffdbU, 23 }, { 0x7fffdcU, 23 },
    { 0x7fffddU, 23 }, { 0x7fffdeU, 23 }, { 0xffffebU, 24 }, { 0x7fffdfU, 23 },
    { 0xffffecU, 24 }, { 0xffffedU, 24 }, { 0x3fffd7U, 22 }, { 0x7fffe0U, 23 },
    { 0xffffeeU, 24 }, { 0x7fffe1U, 23 }, { 0x7fffe2U, 23 }, { 0x7fffe3U, 23 },
    { 0x7fffe4U, 23 }, { 0x1fffdcU, 21 }, { 0x3fffd8U, 22 }, { 0x7fffe5U, 23 },
    { 0x3fffd9U, 22 }, { 0x7fffe6U, 23 }, { 0x7fffe7U, 23 }, { 0xffffefU, 24 },
    { 0x3fffdaU, 22 }, { 0x1fffddU, 21 }, { 0xfffe9U, 20 }, { 0x3fffdbU, 22 },
    { 0x3fffdcU, 22 }, { 0x7fffe8U, 23 }, { 0x7fffe9U, 23 }, { 0x1fffdeU, 21 },
    { 0x7fffeaU, 23 }, { 0x3fffddU, 22 }, { 0x3fffdeU, 22 }, { 0xfffff0U, 24 },
    { 0x1fffdfU, 21 }, { 0x3fffdfU, 22 }, { 0x7fffebU, 23 }, { 0x7fffecU, 23 },
    { 0x1fffe0U, 21 }, { 0x1fffe1U, 21 }, { 0x3fffe0U, 22 }, { 0x1fffe2U, 21 },
    { 0x7fffedU, 23 }, { 0x3fffe1U, 22 }, { 0x7fffeeU, 23 }, { 0x7fffefU, 23 },
    { 0xfffeaU, 20 }, { 0x3fffe2U, 22 }, { 0x3fffe3U, 22 }, { 0x3fffe4U, 22 },
    { 0x7ffff0U, 23 }, { 0x3fffe5U, 22 }, { 0x3fffe6U, 22 }, { 0x7ffff1U, 23 },
    { 0x3ffffe0U, 26 }, { 0x3ffffe1U, 26 }, { 0xfffebU, 20 }, { 0x7fff1U, 19 },
    { 0x3fffe7U, 22 }, { 0x7ffff2U, 23 }, { 0x3fffe8U, 22 }, { 0x1ffffecU, 25 },
    { 0x3ffffe2U, 26 }, { 0x3ffffe3U, 26 }, { 0x3ffffe4U, 26 }, { 0x7ffffdeU, 27 },
    { 0x7ffffdfU, 27 }, { 0x3ffffe5U, 26 }, { 0xfffff1U, 24 }, { 0x1ffffedU, 25 },
    { 0x7fff2U, 19 }, { 0x1fffe3U, 21 }, { 0x3ffffe6U, 26 }, { 0x7ffffe0U, 27 },
    { 0x7ffffe1U, 27 }, { 0x3ffffe7U, 26 }, { 0x7ffffe2U, 27 }, { 0xfffff2U, 24 },
    { 0x1fffe4U, 21 }, { 0x1fffe5U, 21 }, { 0x3ffffe8U, 26 }, { 0x3ffffe9U, 26 },
    { 0xffffffdU, 28 }, { 0x7ffffe3U, 27 }, { 0x7ffffe4U, 27 }, { 0x7ffffe5U, 27 },
    { 0xfffecU, 20 }, { 0xfffff3U, 24 }, { 0xfffedU, 20 }, { 0x1fffe6U, 21 },
    { 0x3fffe9U, 22 }, { 0x1fffe7U, 21 }, { 0x1fffe8U, 21 }, { 0x7ffff3U, 23 },
    { 0x3fffeaU, 22 }, { 0x3fffebU, 22 }, { 0x1ffffeeU, 25 }, { 0x1ffffefU, 25 },
    { 0xfffff4U, 24 }, { 0xfffff5U, 24 }, { 0x3ffffeaU, 26 }, { 0x7ffff4U, 23 },
    { 0x3ffffebU, 26 }, { 0x7ffffe6U, 27 }, { 0x3ffffecU, 26 }, { 0x3ffffedU, 26 },
    { 0x7ffffe7U, 27 }, { 0x7ffffe8U, 27 }, { 0x7ffffe9U, 27 }, { 0x7ffffeaU, 27 },
    { 0x7ffffebU, 27 }, { 0xffffffeU, 28 }, { 0x7ffffecU, 27 }, { 0x7ffffedU, 27 },
    { 0x7ffffeeU, 27 }, { 0x7ffffefU, 27 }, { 0x7fffff0U, 27 }, { 0x3ffffeeU, 26 },
    { 0x3fffffffU, 30 },
}};

struct HuffmanNode {
    int child[2] { -1, -1 };
    int symbol = -1;
};

std::vector<HuffmanNode> build_huffman_tree()
{
    std::vector<HuffmanNode> nodes(1);
    for (std::size_t symbol = 0; symbol < huffman_table.size(); ++symbol) {
        const auto code = huffman_table[symbol].code;
        const auto bits = huffman_table[symbol].bits;
        int node = 0;
        for (std::uint8_t bit_index = 0; bit_index < bits; ++bit_index) {
            const auto shift = static_cast<std::uint8_t>(bits - bit_index - 1);
            const int bit = static_cast<int>((code >> shift) & 0x1U);
            if (nodes[node].child[bit] < 0) {
                nodes[node].child[bit] = static_cast<int>(nodes.size());
                nodes.push_back(HuffmanNode {});
            }
            node = nodes[node].child[bit];
        }
        if (nodes[node].symbol >= 0) {
            throw std::runtime_error("duplicate HPACK Huffman code");
        }
        nodes[node].symbol = static_cast<int>(symbol);
    }
    return nodes;
}

const std::vector<HuffmanNode>& huffman_tree()
{
    static const auto tree = build_huffman_tree();
    return tree;
}

std::string hpack_decode_huffman(std::string_view encoded)
{
    const auto& tree = huffman_tree();
    std::string output;
    output.reserve(encoded.size());
    int node = 0;
    std::uint32_t pending_code = 0;
    std::uint8_t pending_bits = 0;

    for (const unsigned char byte : encoded) {
        for (int bit_index = 7; bit_index >= 0; --bit_index) {
            const int bit = static_cast<int>((byte >> bit_index) & 0x1U);
            const int next = tree[node].child[bit];
            if (next < 0) {
                throw std::runtime_error("invalid HPACK Huffman code");
            }
            node = next;
            pending_code = (pending_code << 1U) | static_cast<std::uint32_t>(bit);
            ++pending_bits;

            if (tree[node].symbol >= 0) {
                if (tree[node].symbol == 256) {
                    throw std::runtime_error("HPACK Huffman EOS symbol is not allowed in string data");
                }
                output.push_back(static_cast<char>(tree[node].symbol));
                node = 0;
                pending_code = 0;
                pending_bits = 0;
            }
        }
    }

    if (pending_bits != 0) {
        if (pending_bits > 7 || pending_code != ((1U << pending_bits) - 1U)) {
            throw std::runtime_error("invalid HPACK Huffman padding");
        }
    }

    return output;
}

std::size_t entry_size(const HeaderField& header)
{
    return header.name.size() + header.value.size() + 32;
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
    const bool huffman = (bytes[offset] & 0x80U) != 0;

    std::uint32_t size = 0;
    offset = hpack_decode_integer(bytes, offset, 7, size);
    if (bytes.size() - offset < size) {
        throw std::runtime_error("incomplete HPACK string data");
    }

    const std::string_view encoded(reinterpret_cast<const char*>(bytes.data() + offset), size);
    value = huffman ? hpack_decode_huffman(encoded) : std::string(encoded);
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
    HpackDecoder decoder;
    return decoder.decode_header_block(block);
}

void HpackDecoder::set_max_table_size(std::size_t bytes)
{
    max_table_size_ = bytes;
    evict_dynamic();
}

std::size_t HpackDecoder::max_table_size() const noexcept
{
    return max_table_size_;
}

std::size_t HpackDecoder::table_size() const noexcept
{
    return table_size_;
}

void HpackDecoder::reset()
{
    dynamic_table_.clear();
    table_size_ = 0;
}

HeaderField HpackDecoder::indexed_header(std::uint32_t index) const
{
    if (index == 0) {
        throw std::runtime_error("HPACK index 0 is invalid");
    }
    if (index <= static_table.size()) {
        const auto& entry = static_entry(index);
        return HeaderField { std::string(entry.name), std::string(entry.value) };
    }

    const auto dynamic_index = index - static_cast<std::uint32_t>(static_table.size()) - 1U;
    if (dynamic_index >= dynamic_table_.size()) {
        throw std::runtime_error("HPACK dynamic table index is out of range");
    }
    return dynamic_table_[dynamic_index];
}

std::string HpackDecoder::indexed_name(std::uint32_t index) const
{
    return indexed_header(index).name;
}

void HpackDecoder::add_dynamic(HeaderField header)
{
    const auto size = entry_size(header);
    if (size > max_table_size_) {
        reset();
        return;
    }

    table_size_ += size;
    dynamic_table_.insert(dynamic_table_.begin(), std::move(header));
    evict_dynamic();
}

void HpackDecoder::evict_dynamic()
{
    while (table_size_ > max_table_size_ && !dynamic_table_.empty()) {
        table_size_ -= entry_size(dynamic_table_.back());
        dynamic_table_.pop_back();
    }
    if (max_table_size_ == 0) {
        reset();
    }
}

std::vector<HeaderField> HpackDecoder::decode_header_block(const std::vector<std::uint8_t>& block)
{
    std::vector<HeaderField> headers;
    std::size_t offset = 0;
    bool regular_header_seen = false;
    while (offset < block.size()) {
        const std::uint8_t byte = block[offset];

        if ((byte & 0x80U) != 0) {
            std::uint32_t index = 0;
            offset = hpack_decode_integer(block, offset, 7, index);
            headers.push_back(indexed_header(index));
            regular_header_seen = true;
            continue;
        }

        if ((byte & 0xe0U) == 0x20U) {
            if (regular_header_seen) {
                throw std::runtime_error("HPACK dynamic table size update must precede header fields");
            }
            std::uint32_t new_size = 0;
            offset = hpack_decode_integer(block, offset, 5, new_size);
            set_max_table_size(new_size);
            continue;
        }

        if ((byte & 0x40U) != 0) {
            std::uint32_t name_index = 0;
            offset = hpack_decode_integer(block, offset, 6, name_index);

            HeaderField header;
            if (name_index == 0) {
                offset = hpack_decode_string(block, offset, header.name);
            } else {
                header.name = indexed_name(name_index);
            }
            offset = hpack_decode_string(block, offset, header.value);
            add_dynamic(header);
            headers.push_back(std::move(header));
            regular_header_seen = true;
            continue;
        }

        std::uint8_t prefix_bits = 4;
        std::uint32_t name_index = 0;
        offset = hpack_decode_integer(block, offset, prefix_bits, name_index);

        HeaderField header;
        if (name_index == 0) {
            offset = hpack_decode_string(block, offset, header.name);
        } else {
            header.name = indexed_name(name_index);
        }
        offset = hpack_decode_string(block, offset, header.value);
        headers.push_back(std::move(header));
        regular_header_seen = true;
    }
    return headers;
}

} // namespace rimau::protocol::http2
