#include "rimau/http/http1_session.hpp"

#include <cassert>
#include <iostream>

int main()
{
    const rimau::http::Http1FrameOptions options { 1024 };

    {
        const auto frame = rimau::http::next_http1_request_frame(
            "GET /one HTTP/1.1\r\n"
            "Host: example.test\r\n"
            "\r\n"
            "GET /two HTTP/1.1\r\n"
            "Host: example.test\r\n"
            "\r\n",
            options);

        assert(frame.state == rimau::http::Http1FrameState::complete);
        assert(frame.request);
        assert(frame.request->path == "/one");
        assert(frame.raw_request == "GET /one HTTP/1.1\r\nHost: example.test\r\n\r\n");
        assert(frame.consumed == frame.raw_request.size());
    }

    {
        const auto frame = rimau::http::next_http1_request_frame(
            "POST /submit HTTP/1.1\r\n"
            "Host: example.test\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "hello",
            options);

        assert(frame.state == rimau::http::Http1FrameState::header_complete);
        assert(frame.request);
        assert(frame.request->path == "/submit");
        assert(frame.waiting_for_body);
    }

    {
        const auto frame = rimau::http::next_http1_request_frame(
            "POST /submit HTTP/1.1\r\n"
            "Host: example.test\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "hello world",
            options);

        assert(frame.state == rimau::http::Http1FrameState::complete);
        assert(frame.raw_request.ends_with("hello world"));
    }

    {
        const auto frame = rimau::http::next_http1_request_frame(
            "POST /chunk HTTP/1.1\r\n"
            "Host: example.test\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "5\r\nhello\r\n"
            "6\r\n world\r\n"
            "0\r\n"
            "X-Trailer: ignored\r\n"
            "\r\n"
            "GET /next HTTP/1.1\r\n"
            "Host: example.test\r\n"
            "\r\n",
            options);

        assert(frame.state == rimau::http::Http1FrameState::complete);
        assert(frame.request);
        assert(frame.request->path == "/chunk");
        assert(frame.raw_request.ends_with("hello world"));
        assert(frame.consumed < 150);
    }

    {
        const auto frame = rimau::http::next_http1_request_frame(
            "GET /bad HTTP/1.1\n"
            "Host: example.test\r\n"
            "\r\n",
            options);

        assert(frame.state == rimau::http::Http1FrameState::error);
        assert(frame.error_status == 400);
    }

    {
        const auto frame = rimau::http::next_http1_request_frame(
            "POST /bad HTTP/1.1\r\n"
            "Host: example.test\r\n"
            "Content-Length: 1\r\n"
            "Content-Length: 1\r\n"
            "\r\n"
            "x",
            options);

        assert(frame.state == rimau::http::Http1FrameState::error);
        assert(frame.error_status == 400);
    }

    {
        const auto frame = rimau::http::next_http1_request_frame(
            "GET /large HTTP/1.1\r\n"
            "Host: example.test\r\n"
            "X-Long: 1234567890\r\n"
            "\r\n",
            rimau::http::Http1FrameOptions { 32 });

        assert(frame.state == rimau::http::Http1FrameState::error);
        assert(frame.error_status == 431);
    }

    std::cout << "http1 session tests passed\n";
    return 0;
}
