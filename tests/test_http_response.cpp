#include "rimau/http/response.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

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

    {
        auto response = rimau::http::text_response(200, "");
        response.headers["content-length"] = "999";

        const std::vector<std::string> chunks { "hello", " ", "rimau" };
        const std::string encoded = rimau::http::encode_chunked_body(chunks);
        assert(encoded == "5\r\nhello\r\n1\r\n \r\n5\r\nrimau\r\n0\r\n\r\n");

        const std::string raw = response.to_http_chunked_string(chunks);
        assert(raw.find("HTTP/1.1 200 OK\r\n") != std::string::npos);
        assert(raw.find("transfer-encoding: chunked\r\n") != std::string::npos);
        assert(raw.find("content-length:") == std::string::npos);
        assert(raw.ends_with(encoded));
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
        auto request = make_request("GET", "/video.mp4");
        request.headers["range"] = "bytes=0-1,4-5";

        const auto response = rimau::http::file_response(request, root);
        assert(response.status == 206);
        assert(response.headers.at("content-type").starts_with("multipart/byteranges; boundary=rimau-"));
        assert(response.body.find("Content-Range: bytes 0-1/16\r\n") != std::string::npos);
        assert(response.body.find("\r\n01\r\n") != std::string::npos);
        assert(response.body.find("Content-Range: bytes 4-5/16\r\n") != std::string::npos);
        assert(response.body.find("\r\n45\r\n") != std::string::npos);
    }

    {
        auto request = make_request("GET", "/video.mp4");
        request.headers["range"] = "bytes=0-1";

        const auto ranged = rimau::http::file_response(request, root);
        assert(ranged.status == 206);
        assert(ranged.headers.contains("etag"));

        auto mismatch = make_request("GET", "/video.mp4");
        mismatch.headers["range"] = "bytes=0-1";
        mismatch.headers["if-range"] = "\"not-current\"";
        const auto full = rimau::http::file_response(mismatch, root);
        assert(full.status == 200);
        assert(full.body == "0123456789abcdef");

        auto matched = make_request("GET", "/video.mp4");
        matched.headers["range"] = "bytes=0-1";
        matched.headers["if-range"] = ranged.headers.at("etag");
        const auto partial = rimau::http::file_response(matched, root);
        assert(partial.status == 206);
        assert(partial.body == "01");
    }

    {
        {
            std::ofstream file(root / "home.html", std::ios::binary);
            file << "custom index";
        }
        {
            std::ofstream file(root / "error.html", std::ios::binary);
            file << "custom error";
        }

        rimau::http::StaticFileOptions options;
        options.directory_index = "home.html";
        options.error_page = "error.html";

        auto index_request = make_request("GET", "/");
        const auto index_response = rimau::http::file_response(index_request, root, options);
        assert(index_response.status == 200);
        assert(index_response.body == "custom index");

        auto missing_request = make_request("GET", "/missing.html");
        const auto missing_response = rimau::http::file_response(missing_request, root, options);
        assert(missing_response.status == 404);
        assert(missing_response.body == "custom error");
        assert(missing_response.headers.at("content-type") == "text/html; charset=utf-8");
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
