#include "rimau/http/http1_session.hpp"
#include "rimau/http/parser.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::string> seed_corpus()
{
    return {
        "GET / HTTP/1.1\r\nHost: example.test\r\n\r\n",
        "HEAD /index.html HTTP/1.1\r\nHost: example.test\r\nConnection: close\r\n\r\n",
        "POST /submit HTTP/1.1\r\nHost: example.test\r\nContent-Length: 11\r\n\r\nhello world",
        "POST /chunk HTTP/1.1\r\nHost: example.test\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n",
        "GET /bad HTTP/1.1\nHost: example.test\r\n\r\n",
        "GET /bad HTTP/1.1\r\nBrokenHeader\r\n\r\n",
        "POST /bad HTTP/1.1\r\nHost: example.test\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx",
        "POST /bad HTTP/1.1\r\nHost: example.test\r\nContent-Length: x\r\n\r\nx",
        "POST /bad HTTP/1.1\r\nHost: example.test\r\nTransfer-Encoding: gzip\r\n\r\nx",
        "GET /large HTTP/1.1\r\nHost: example.test\r\nX-Long: 123456789012345678901234567890\r\n\r\n",
        "\r\n",
        "",
    };
}

char random_byte(std::mt19937_64& rng)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
        " :;,.?=/\\'\"%+-_*#&|\r\n\t\0";
    std::uniform_int_distribution<std::size_t> pick(0, sizeof(alphabet) - 2);
    return alphabet[pick(rng)];
}

std::string mutate(std::string value, std::mt19937_64& rng)
{
    std::uniform_int_distribution<int> mutation_count(1, 8);
    std::uniform_int_distribution<int> mutation_kind(0, 5);

    for (int mutation = 0; mutation < mutation_count(rng); ++mutation) {
        const int kind = mutation_kind(rng);
        if (kind == 0 && !value.empty()) {
            std::uniform_int_distribution<std::size_t> pick(0, value.size() - 1);
            value[pick(rng)] = random_byte(rng);
        } else if (kind == 1 && value.size() < 512) {
            std::uniform_int_distribution<std::size_t> pick(0, value.size());
            value.insert(value.begin() + static_cast<std::ptrdiff_t>(pick(rng)), random_byte(rng));
        } else if (kind == 2 && !value.empty()) {
            std::uniform_int_distribution<std::size_t> pick(0, value.size() - 1);
            value.erase(value.begin() + static_cast<std::ptrdiff_t>(pick(rng)));
        } else if (kind == 3) {
            value.append("GET /smuggled HTTP/1.1\r\nHost: smuggled.test\r\n\r\n");
        } else if (kind == 4 && value.size() > 4) {
            std::uniform_int_distribution<std::size_t> pick(0, value.size() - 1);
            value.resize(pick(rng));
        } else if (kind == 5 && value.size() < 512) {
            value.append("\r\n0\r\n\r\n");
        }

        if (value.size() > 768) {
            value.resize(768);
        }
    }

    return value;
}

void assert_parser_invariants(const rimau::http::ParseResult& parsed)
{
    if (!parsed) {
        assert(!parsed.message.empty() || parsed.error != rimau::http::ParseError::none);
        return;
    }

    assert(!parsed.request->method.empty());
    assert(!parsed.request->target.empty());
    assert(!parsed.request->version.empty());
}

void assert_frame_invariants(const rimau::http::Http1FrameResult& frame, std::string_view input)
{
    if (frame.state == rimau::http::Http1FrameState::complete) {
        assert(frame.request);
        assert(frame.consumed > 0);
        assert(frame.consumed <= input.size());
        assert(!frame.raw_request.empty());
    } else if (frame.state == rimau::http::Http1FrameState::header_complete) {
        assert(frame.request);
        assert(frame.waiting_for_body);
        assert(frame.content_length || frame.chunked);
    } else if (frame.state == rimau::http::Http1FrameState::error) {
        assert(frame.error_status >= 400);
        assert(!frame.error_message.empty());
    } else {
        assert(frame.state == rimau::http::Http1FrameState::incomplete);
    }
}

void exercise_input(std::string_view input)
{
    assert_parser_invariants(rimau::http::parse_request(input));

    const rimau::http::Http1FrameOptions small_options { 128 };
    assert_frame_invariants(rimau::http::next_http1_request_frame(input, small_options), input);

    const rimau::http::Http1FrameOptions normal_options { 1024 };
    assert_frame_invariants(rimau::http::next_http1_request_frame(input, normal_options), input);
}

} // namespace

int main()
{
    auto corpus = seed_corpus();
    std::mt19937_64 rng(0x72696d617566757aULL);

    for (const auto& seed : corpus) {
        exercise_input(seed);
    }

    for (int iteration = 0; iteration < 2500; ++iteration) {
        std::uniform_int_distribution<std::size_t> pick(0, corpus.size() - 1);
        auto sample = mutate(corpus[pick(rng)], rng);
        exercise_input(sample);
        if (iteration % 17 == 0) {
            corpus.push_back(std::move(sample));
        }
    }

    std::cout << "http parser/framing fuzz smoke passed\n";
    return 0;
}
