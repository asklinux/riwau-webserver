#include "rimau/http/response.hpp"
#include "rimau/protocol/http2_frame.hpp"
#include "rimau/protocol/http2_hpack.hpp"
#include "rimau/protocol/http2_session.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::string bytes_to_string(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.empty()) {
        return {};
    }
    return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

std::string frame_string(rimau::protocol::http2::Frame frame)
{
    return bytes_to_string(rimau::protocol::http2::serialize_frame(frame));
}

std::string settings_frame()
{
    rimau::protocol::http2::Frame frame;
    frame.type = static_cast<std::uint8_t>(rimau::protocol::http2::FrameType::settings);
    frame.stream_id = 0;
    return frame_string(std::move(frame));
}

std::string headers_frame(
    std::uint32_t stream_id,
    const std::vector<rimau::protocol::http2::HeaderField>& headers,
    std::uint8_t flags)
{
    rimau::protocol::http2::Frame frame;
    frame.type = static_cast<std::uint8_t>(rimau::protocol::http2::FrameType::headers);
    frame.flags = flags;
    frame.stream_id = stream_id;
    frame.payload = rimau::protocol::http2::hpack_encode_header_block(headers);
    return frame_string(std::move(frame));
}

std::string raw_headers_frame(std::uint32_t stream_id, std::vector<std::uint8_t> payload, std::uint8_t flags)
{
    rimau::protocol::http2::Frame frame;
    frame.type = static_cast<std::uint8_t>(rimau::protocol::http2::FrameType::headers);
    frame.flags = flags;
    frame.stream_id = stream_id;
    frame.payload = std::move(payload);
    return frame_string(std::move(frame));
}

std::string continuation_frame(std::uint32_t stream_id, std::vector<std::uint8_t> payload, std::uint8_t flags)
{
    rimau::protocol::http2::Frame frame;
    frame.type = static_cast<std::uint8_t>(rimau::protocol::http2::FrameType::continuation);
    frame.flags = flags;
    frame.stream_id = stream_id;
    frame.payload = std::move(payload);
    return frame_string(std::move(frame));
}

std::string ping_frame()
{
    rimau::protocol::http2::Frame frame;
    frame.type = static_cast<std::uint8_t>(rimau::protocol::http2::FrameType::ping);
    frame.stream_id = 0;
    frame.payload = { 0, 1, 2, 3, 4, 5, 6, 7 };
    return frame_string(std::move(frame));
}

std::string data_frame(std::uint32_t stream_id, std::string_view payload, std::uint8_t flags)
{
    rimau::protocol::http2::Frame frame;
    frame.type = static_cast<std::uint8_t>(rimau::protocol::http2::FrameType::data);
    frame.flags = flags;
    frame.stream_id = stream_id;
    frame.payload.assign(payload.begin(), payload.end());
    return frame_string(std::move(frame));
}

void accept_preface(rimau::protocol::http2::ServerSession& session, std::string& buffer)
{
    buffer = std::string(rimau::protocol::http2::client_connection_preface) + settings_frame();
    const auto result = session.accept_preface(buffer, 64 * 1024, true, true);
    assert(result.status == rimau::protocol::http2::PrefaceStatus::accepted);
    assert(result.keep_connection);
    assert(!result.output.empty());
    assert(buffer.empty());
    assert(session.active());
}

} // namespace

int main()
{
    namespace h2 = rimau::protocol::http2;

    {
        h2::ServerSession session;
        std::string buffer;
        accept_preface(session, buffer);

        buffer = headers_frame(
            1,
            {
                { ":method", "GET" },
                { ":scheme", "http" },
                { ":path", "/?search=rimau" },
                { ":authority", "example.test" },
                { "user-agent", "rimau-session-test" },
            },
            h2::flag_end_headers | h2::flag_end_stream);

        const auto result = session.process_input(buffer, 64 * 1024);
        assert(result.keep_connection);
        assert(result.completed_stream);
        assert(!result.error_response);
        assert(result.completed_stream->stream_id == 1);
        assert(result.completed_stream->request.method == "GET");
        assert(result.completed_stream->request.version == "HTTP/2");
        assert(result.completed_stream->request.path == "/");
        assert(result.completed_stream->request.query("search") == "rimau");
        assert(result.completed_stream->request.header("host") == "example.test");
        assert(buffer.empty());
    }

    {
        h2::ServerSession session;
        std::string buffer;
        accept_preface(session, buffer);

        buffer = headers_frame(
            3,
            {
                { ":method", "POST" },
                { ":scheme", "https" },
                { ":path", "/submit" },
                { ":authority", "example.test" },
                { "content-length", "5" },
            },
            h2::flag_end_headers);
        buffer += data_frame(3, "hello", h2::flag_end_stream);

        const auto result = session.process_input(buffer, 64 * 1024);
        assert(result.completed_stream);
        assert(result.completed_stream->stream_id == 3);
        assert(result.completed_stream->request.method == "POST");
        assert(result.completed_stream->request.path == "/submit");
        assert(result.completed_stream->request.body == "hello");
        assert(result.completed_stream->request.body_size() == 5);
        assert(buffer.empty());
    }

    {
        h2::ServerSession session;
        std::string buffer;
        accept_preface(session, buffer);

        buffer = headers_frame(
            5,
            {
                { ":method", "GET" },
                { ":scheme", "http" },
                { ":path", "/" },
            },
            0);

        auto result = session.process_input(buffer, 64 * 1024);
        assert(result.output.empty());

        buffer = ping_frame();
        result = session.process_input(buffer, 64 * 1024);
        assert(!result.output.empty());
        const auto parsed = h2::parse_frame(result.output);
        assert(parsed.ok);
        assert(parsed.frame.type == static_cast<std::uint8_t>(h2::FrameType::goaway));
    }

    {
        h2::ServerSession session;
        std::string buffer;
        accept_preface(session, buffer);

        const auto block = h2::hpack_encode_header_block({
            { ":method", "GET" },
            { ":scheme", "http" },
            { ":path", "/continued" },
            { ":authority", "example.test" },
        });
        buffer = raw_headers_frame(7, { block.begin(), block.begin() + 2 }, h2::flag_end_stream);
        auto result = session.process_input(buffer, 64 * 1024);
        assert(!result.completed_stream);
        assert(result.output.empty());

        buffer = continuation_frame(7, { block.begin() + 2, block.end() }, h2::flag_end_headers | h2::flag_end_stream);
        result = session.process_input(buffer, 64 * 1024);
        assert(result.completed_stream);
        assert(result.completed_stream->stream_id == 7);
        assert(result.completed_stream->request.path == "/continued");
    }

    {
        h2::ServerSession session;
        std::string buffer;
        accept_preface(session, buffer);

        buffer = headers_frame(
            9,
            {
                { ":method", "POST" },
                { ":scheme", "http" },
                { ":path", "/large" },
                { ":authority", "example.test" },
                { "content-length", "70000" },
            },
            h2::flag_end_headers);
        auto result = session.process_input(buffer, 128 * 1024);
        assert(!result.completed_stream);

        buffer = data_frame(9, std::string(2048, 'x'), 0);
        result = session.process_input(buffer, 1024);
        assert(!result.output.empty());
        const auto parsed = h2::parse_frame(result.output);
        assert(parsed.ok);
        assert(parsed.frame.type == static_cast<std::uint8_t>(h2::FrameType::goaway));
    }

    {
        h2::ServerSession session;
        std::string buffer = std::string(h2::client_connection_preface) + settings_frame();
        const auto result = session.accept_preface(buffer, 64 * 1024, false, true);
        assert(result.status == h2::PrefaceStatus::rejected);
        assert(!result.keep_connection);
        const auto parsed = h2::parse_frame(result.output);
        assert(parsed.ok);
        assert(parsed.frame.type == static_cast<std::uint8_t>(h2::FrameType::goaway));
    }

    {
        rimau::http::Response::SerializationOptions options;
        options.server_name = "Rimau Test";
        options.default_headers["x-content-type-options"] = "nosniff";

        const auto wire = h2::serialize_response(
            1,
            rimau::http::text_response(200, "OK\n"),
            rimau::http::BodyMode::include,
            options);
        const auto headers = h2::parse_frame(wire);
        assert(headers.ok);
        assert(headers.frame.type == static_cast<std::uint8_t>(h2::FrameType::headers));
        const auto fields = h2::hpack_decode_header_block(headers.frame.payload);
        bool has_status = false;
        bool has_server = false;
        bool has_security_header = false;
        for (const auto& field : fields) {
            has_status = has_status || (field.name == ":status" && field.value == "200");
            has_server = has_server || (field.name == "server" && field.value == "Rimau Test");
            has_security_header = has_security_header || (field.name == "x-content-type-options" && field.value == "nosniff");
            assert(field.name != "connection");
        }
        assert(has_status);
        assert(has_server);
        assert(has_security_header);

        const auto data = h2::parse_frame(std::string_view(wire).substr(headers.consumed));
        assert(data.ok);
        assert(data.frame.type == static_cast<std::uint8_t>(h2::FrameType::data));
        assert(std::string(reinterpret_cast<const char*>(data.frame.payload.data()), data.frame.payload.size()) == "OK\n");
    }

    return 0;
}
