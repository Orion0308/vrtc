#pragma once
// Logging and the one way to stop: hard rule 11 says a failed window, context, shader or data file stops the
// program with a clear message rather than continuing half-initialised.
#include <format>
#include <string_view>
#include <utility>

namespace vrtc::log {
    enum class Level { Info, Warn, Error };

    // Thread-safe; one line per call.
    void Write(Level level, std::string_view message);

    [[noreturn]] void FatalMessage(std::string_view message);

    template<class... Args>
    void Info(std::format_string<Args...> fmt, Args &&... args) {
        Write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
    }

    template<class... Args>
    void Warn(std::format_string<Args...> fmt, Args &&... args) {
        Write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
    }

    template<class... Args>
    void Error(std::format_string<Args...> fmt, Args &&... args) {
        Write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
    }

    // Logs and exits the process. Never returns.
    template<class... Args>
    [[noreturn]] void Fatal(std::format_string<Args...> fmt, Args &&... args) {
        FatalMessage(std::format(fmt, std::forward<Args>(args)...));
    }
}
