#pragma once

#include <hyprland/src/debug/log/Logger.hpp>

#include <format>
#include <utility>

namespace hypr_radiant::log {

inline constexpr auto PREFIX = "hypr-radiant";

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    Log::logger->log(Log::INFO, "[{}] {}", PREFIX, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    Log::logger->log(Log::WARN, "[{}] {}", PREFIX, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    Log::logger->log(Log::ERR, "[{}] {}", PREFIX, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace hypr_radiant::log
