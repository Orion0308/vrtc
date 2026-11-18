#include "Log.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace {
    std::mutex gLogMutex;

    const char *Prefix(vrtc::log::Level level) {
        switch (level) {
            case vrtc::log::Level::Info: return "[info ] ";
            case vrtc::log::Level::Warn: return "[warn ] ";
            case vrtc::log::Level::Error: return "[error] ";
        }
        return "";
    }
}

void vrtc::log::Write(Level level, std::string_view message) {
    std::string line;
    line.reserve(message.size() + 10);
    line += Prefix(level);
    line += message;
    line += '\n';
    std::lock_guard lock(gLogMutex);
    std::fputs(line.c_str(), stderr);
    std::fflush(stderr);
}

void vrtc::log::FatalMessage(std::string_view message) {
    Write(Level::Error, message);
    Write(Level::Error, "fatal: stopping");
    std::exit(EXIT_FAILURE);
}
