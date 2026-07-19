#include "rimau/http/response.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

namespace {

rimau::http::Request make_request(std::string method, std::string target)
{
    rimau::http::Request request;
    request.method = std::move(method);
    request.target = std::move(target);
    request.path = request.target;
    request.version = "HTTP/1.1";
    request.headers["host"] = "localhost";
    return request;
}

} // namespace

int main()
{
    {
        auto response = rimau::http::text_response(200, "ok\n");
        const std::string raw = response.to_http_string();

        assert(raw.find("HTTP/1.1 200 OK\r\n") != std::string::npos);
        assert(raw.find("connection: close\r\n") != std::string::npos);
        assert(raw.find("content-length: 3\r\n") != std::string::npos);
    }

    {
        auto response = rimau::http::text_response(200, "ok\n");
        response.headers["connection"] = "keep-alive";
        response.headers["keep-alive"] = "timeout=15, max=99";

        const std::string raw = response.to_http_string();
        assert(raw.find("connection: keep-alive\r\n") != std::string::npos);
        assert(raw.find("keep-alive: timeout=15, max=99\r\n") != std::string::npos);
    }

    {
        auto response = rimau::http::text_response(200, "ok\n");
        response.headers["x-test"] = "safe\r\nInjected: bad";

        rimau::http::Response::SerializationOptions options;
        options.server_header_enabled = false;
        options.default_headers["x-content-type-options"] = "nosniff";

        const std::string raw = response.to_http_string(true, options);
        assert(raw.find("server:") == std::string::npos);
        assert(raw.find("x-content-type-options: nosniff\r\n") != std::string::npos);
        assert(raw.find("x-test: safe  Injected: bad\r\n") != std::string::npos);
    }

    const auto root = std::filesystem::temp_directory_path() / "rimau-http-response-test";
    std::filesystem::create_directories(root);

    {
        {
            std::ofstream file(root / "video.mp4", std::ios::binary);
            file << "0123456789abcdef";
        }

        auto request = make_request("GET", "/video.mp4");
        request.headers["range"] = "bytes=4-7";

        const auto response = rimau::http::file_response(request, root);
        assert(response.status == 206);
        assert(response.headers.at("content-range") == "bytes 4-7/16");
        assert(response.headers.at("content-type") == "video/mp4");
        assert(response.body == "4567");
    }

    {
        {
            std::ofstream file(root / "app.js", std::ios::binary);
            for (int index = 0; index < 128; ++index) {
                file << "console.log('rimau');\n";
            }
        }

        auto request = make_request("GET", "/app.js");
        request.headers["accept-encoding"] = "gzip";

        const auto response = rimau::http::file_response(request, root);
        assert(response.status == 200);
        assert(response.headers.at("content-encoding") == "gzip");
        assert(response.headers.at("vary") == "Accept-Encoding");
        assert(!response.body.empty());
    }

    std::filesystem::remove_all(root);

    std::cout << "http response tests passed\n";
    return 0;
}
