#include "rimau/http/parser.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

int main()
{
    {
        const auto parsed = rimau::http::parse_request(
            "GET /index.html HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: close\r\n"
            "\r\n");

        assert(parsed);
        assert(parsed.request->method == "GET");
        assert(parsed.request->target == "/index.html");
        assert(parsed.request->path == "/index.html");
        assert(parsed.request->version == "HTTP/1.1");
        assert(parsed.request->header("host") == "localhost");
        assert(parsed.request->header("HOST") == "localhost");
    }

    {
        const auto parsed = rimau::http::parse_request(
            "POST /submit%20form?name=rimau+server&tag=c%2B%2B&tag=http HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 13\r\n"
            "\r\n"
            "{\"ok\":true}\n");

        assert(parsed);
        assert(parsed.request->method == "POST");
        assert(parsed.request->path == "/submit form");
        assert(parsed.request->query("name") == "rimau server");
        assert(parsed.request->query("tag") == "c++");
        assert(parsed.request->query_params.at("tag").size() == 2);
        assert(parsed.request->body == "{\"ok\":true}\n");
        assert(parsed.request->is_json());

        auto reader = parsed.request->open_body_reader();
        assert(reader.read_chunk(4) == "{\"ok");
        assert(reader.read_chunk(64) == "\":true}\n");
        assert(reader.eof());
    }

    {
        const auto path = std::filesystem::temp_directory_path() / "rimau-parser-body-reader.tmp";
        {
            std::ofstream output(path, std::ios::binary);
            output << "abcdefghijklmnopqrstuvwxyz";
        }

        rimau::http::Request request;
        request.method = "POST";
        request.target = "/upload";
        request.version = "HTTP/1.1";
        request.body_size_bytes = 26;
        request.body_file = std::make_shared<rimau::http::RequestBodyFile>(path);

        auto reader = request.open_body_reader();
        assert(reader.read_chunk(5) == "abcde");
        assert(reader.bytes_read() == 5);
        assert(reader.read_chunk(7) == "fghijkl");
        assert(reader.read_chunk(64) == "mnopqrstuvwxyz");
        assert(reader.eof());
    }

    {
        const auto parsed = rimau::http::parse_request("GET / HTTP/3\r\nHost: localhost\r\n\r\n");
        assert(!parsed);
        assert(parsed.error == rimau::http::ParseError::unsupported_http_version);
    }

    {
        const auto parsed = rimau::http::parse_request("GET / HTTP/1.1\r\nBrokenHeader\r\n\r\n");
        assert(!parsed);
        assert(parsed.error == rimau::http::ParseError::malformed_header);
    }

    std::cout << "http parser tests passed\n";
    return 0;
}
