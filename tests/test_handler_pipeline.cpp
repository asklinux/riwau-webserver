#include "rimau/http/response_sink.hpp"
#include "rimau/http/response_builder.hpp"
#include "rimau/http/static_file_handler.hpp"
#include "rimau/http/transaction.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

class CapturingSink final : public rimau::http::ResponseSink {
public:
    void send(rimau::http::Response response, rimau::http::BodyMode body_mode = rimau::http::BodyMode::include) override
    {
        response_ = std::move(response);
        body_mode_ = body_mode;
        sent_ = true;
    }

    bool sent() const noexcept override
    {
        return sent_;
    }

    void send_chunked(
        rimau::http::Response response,
        std::vector<std::string> chunks,
        rimau::http::BodyMode body_mode = rimau::http::BodyMode::include) override
    {
        response_ = std::move(response);
        chunks_ = std::move(chunks);
        body_mode_ = body_mode;
        chunked_ = true;
        sent_ = true;
    }

    const rimau::http::Response& response() const
    {
        assert(response_.has_value());
        return *response_;
    }

    rimau::http::BodyMode body_mode() const
    {
        assert(body_mode_.has_value());
        return *body_mode_;
    }

    bool chunked() const noexcept
    {
        return chunked_;
    }

    const std::vector<std::string>& chunks() const noexcept
    {
        return chunks_;
    }

private:
    bool sent_ = false;
    bool chunked_ = false;
    std::optional<rimau::http::Response> response_;
    std::optional<rimau::http::BodyMode> body_mode_;
    std::vector<std::string> chunks_;
};

class ChunkedHandler final : public rimau::http::RequestHandler {
public:
    void on_request(const rimau::http::Request&, rimau::http::ResponseSink& downstream) override
    {
        rimau::http::ResponseBuilder(downstream)
            .status(200)
            .content_type("text/plain; charset=utf-8")
            .send_chunked({ "hello", " ", "rimau\n" });
    }
};

class ChunkedHandlerFactory final : public rimau::http::RequestHandlerFactory {
public:
    std::unique_ptr<rimau::http::RequestHandler> create(const rimau::http::Request&) const override
    {
        return std::make_unique<ChunkedHandler>();
    }
};

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

std::filesystem::path make_document_root()
{
    const auto root = std::filesystem::temp_directory_path() / "rimau-handler-pipeline-test";
    std::filesystem::create_directories(root);

    std::ofstream index(root / "index.html", std::ios::binary);
    index << "Rimau pipeline";

    return root;
}

} // namespace

int main()
{
    const auto root = make_document_root();
    const rimau::http::StaticFileHandlerFactory factory(root);

    {
        CapturingSink sink;
        rimau::http::Transaction transaction(1, make_request("GET", "/"));
        transaction.dispatch(factory, sink);

        assert(transaction.state() == rimau::http::TransactionState::complete);
        assert(sink.sent());
        assert(sink.response().status == 200);
        assert(sink.response().body == "Rimau pipeline");
        assert(sink.body_mode() == rimau::http::BodyMode::include);
    }

    {
        CapturingSink sink;
        rimau::http::Transaction transaction(2, make_request("HEAD", "/"));
        transaction.dispatch(factory, sink);

        assert(transaction.state() == rimau::http::TransactionState::complete);
        assert(sink.sent());
        assert(sink.response().status == 200);
        assert(sink.response().body == "Rimau pipeline");
        assert(sink.body_mode() == rimau::http::BodyMode::headers_only);
    }

    {
        CapturingSink sink;
        rimau::http::Transaction transaction(3, make_request("POST", "/"));
        transaction.dispatch(factory, sink);

        assert(transaction.state() == rimau::http::TransactionState::complete);
        assert(sink.sent());
        assert(sink.response().status == 200);
        assert(sink.response().headers.at("content-type") == "application/json; charset=utf-8");
        assert(sink.response().body.find("\"method\":\"POST\"") != std::string::npos);
    }

    {
        CapturingSink sink;
        rimau::http::Transaction transaction(4, make_request("OPTIONS", "/"));
        transaction.dispatch(factory, sink);

        assert(transaction.state() == rimau::http::TransactionState::complete);
        assert(sink.sent());
        assert(sink.response().status == 204);
        assert(sink.response().headers.at("allow").find("PATCH") != std::string::npos);
        assert(sink.body_mode() == rimau::http::BodyMode::headers_only);
    }

    {
        CapturingSink sink;
        ChunkedHandlerFactory chunked_factory;
        rimau::http::Transaction transaction(5, make_request("GET", "/chunked"));
        transaction.dispatch(chunked_factory, sink);

        assert(transaction.state() == rimau::http::TransactionState::complete);
        assert(sink.sent());
        assert(sink.chunked());
        assert(sink.response().status == 200);
        assert(sink.chunks().size() == 3);
        assert(sink.chunks()[0] == "hello");
        assert(sink.body_mode() == rimau::http::BodyMode::include);
    }

    std::filesystem::remove_all(root);
    return 0;
}
