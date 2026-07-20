#pragma once

#include "rimau/http/request_handler.hpp"
#include "rimau/http/request_handler_factory.hpp"
#include "rimau/http/response.hpp"

#include <filesystem>
#include <memory>

namespace rimau::http {

class StaticFileHandler final : public RequestHandler {
public:
    explicit StaticFileHandler(std::filesystem::path document_root, StaticFileOptions options = {});

    void on_request(const Request& request, ResponseSink& downstream) override;

private:
    std::filesystem::path document_root_;
    StaticFileOptions options_;
};

class StaticFileHandlerFactory final : public RequestHandlerFactory {
public:
    explicit StaticFileHandlerFactory(std::filesystem::path document_root, StaticFileOptions options = {});

    std::unique_ptr<RequestHandler> create(const Request& request) const override;

private:
    std::filesystem::path document_root_;
    StaticFileOptions options_;
};

} // namespace rimau::http
