#include "rimau/http/transaction.hpp"

#include "rimau/http/response_builder.hpp"

#include <exception>
#include <utility>

namespace rimau::http {

void RequestHandler::on_error(const std::string& message, ResponseSink& downstream)
{
    ResponseBuilder(downstream)
        .status(500)
        .content_type("text/plain; charset=utf-8")
        .body(message + "\n")
        .send();
}

Transaction::Transaction(std::uint64_t id, Request request)
    : id_(id)
    , request_(std::move(request))
{
}

std::uint64_t Transaction::id() const noexcept
{
    return id_;
}

const Request& Transaction::request() const noexcept
{
    return request_;
}

TransactionState Transaction::state() const noexcept
{
    return state_;
}

void Transaction::dispatch(const RequestHandlerFactory& factory, ResponseSink& downstream)
{
    state_ = TransactionState::handling;

    try {
        auto handler = factory.create(request_);
        if (!handler) {
            ResponseBuilder(downstream)
                .status(500)
                .content_type("text/plain; charset=utf-8")
                .body("No request handler available\n")
                .send();
            state_ = TransactionState::failed;
            return;
        }

        handler->on_request(request_, downstream);
        handler->on_complete();

        if (!downstream.sent()) {
            handler->on_error("Request handler completed without sending a response", downstream);
            state_ = TransactionState::failed;
            return;
        }

        state_ = TransactionState::complete;
    } catch (const std::exception& error) {
        ResponseBuilder(downstream)
            .status(500)
            .content_type("text/plain; charset=utf-8")
            .body(std::string("Request handler failed: ") + error.what() + "\n")
            .send();
        state_ = TransactionState::failed;
    } catch (...) {
        ResponseBuilder(downstream)
            .status(500)
            .content_type("text/plain; charset=utf-8")
            .body("Request handler failed with an unknown error\n")
            .send();
        state_ = TransactionState::failed;
    }
}

} // namespace rimau::http
