#pragma once

#include "rimau/http/request.hpp"
#include "rimau/http/request_handler_factory.hpp"
#include "rimau/http/response_sink.hpp"

#include <cstdint>

namespace rimau::http {

enum class TransactionState {
    idle,
    handling,
    complete,
    failed
};

class Transaction {
public:
    Transaction(std::uint64_t id, Request request);

    std::uint64_t id() const noexcept;
    const Request& request() const noexcept;
    TransactionState state() const noexcept;

    void dispatch(const RequestHandlerFactory& factory, ResponseSink& downstream);

private:
    std::uint64_t id_;
    Request request_;
    TransactionState state_ = TransactionState::idle;
};

} // namespace rimau::http
