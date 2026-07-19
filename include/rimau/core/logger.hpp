#pragma once

#include <string_view>

namespace rimau::core {

enum class LogLevel {
    info,
    warning,
    error
};

void log(LogLevel level, std::string_view message);

} // namespace rimau::core
