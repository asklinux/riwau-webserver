#include "rimau/core/logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace rimau::core {
namespace {

std::mutex log_mutex;

const char* level_name(LogLevel level)
{
    switch (level) {
    case LogLevel::info:
        return "INFO";
    case LogLevel::warning:
        return "WARN";
    case LogLevel::error:
        return "ERROR";
    }

    return "INFO";
}

std::string timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time {};
#if defined(_WIN32)
    localtime_s(&local_time, &now_time);
#else
    localtime_r(&now_time, &local_time);
#endif

    std::ostringstream output;
    output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

} // namespace

void log(LogLevel level, std::string_view message)
{
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cerr << timestamp() << " [" << level_name(level) << "] " << message << '\n';
}

} // namespace rimau::core
