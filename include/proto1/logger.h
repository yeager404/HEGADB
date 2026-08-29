#pragma once
// Simple structured logger — never logs plaintext values or private keys.

#include <chrono>
#include <iostream>
#include <string>

namespace proto1 {

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

inline void log(LogLevel level, const std::string& component,
                const std::string& msg) {
    const char* lvl = "INFO";
    switch (level) {
        case LogLevel::DEBUG: lvl = "DEBUG"; break;
        case LogLevel::INFO:  lvl = "INFO "; break;
        case LogLevel::WARN:  lvl = "WARN "; break;
        case LogLevel::ERROR: lvl = "ERROR"; break;
    }
    std::cerr << "[" << lvl << "] [" << component << "] " << msg << "\n";
}

#define LOG_INFO(comp, msg)  ::proto1::log(::proto1::LogLevel::INFO,  comp, msg)
#define LOG_DEBUG(comp, msg) ::proto1::log(::proto1::LogLevel::DEBUG, comp, msg)
#define LOG_WARN(comp, msg)  ::proto1::log(::proto1::LogLevel::WARN,  comp, msg)
#define LOG_ERROR(comp, msg) ::proto1::log(::proto1::LogLevel::ERROR, comp, msg)

} // namespace proto1
