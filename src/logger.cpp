#include "logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

Logger& Logger::get() {
    // guaranteed initialized once, thread-safe by C++11 standard
    static Logger instance;
    return instance;
}

void Logger::init(const std::string& log_path, LogLevel min_level) {
    min_level_ = min_level;

    // append mode — don't wipe logs from previous daemon run
    file_.open(log_path, std::ios::app);

    if (!file_.is_open()) {
        std::cerr << "[Logger] Failed to open log file: " << log_path << "\n";
        return;
    }

    initialized_ = true;
    info("Logger initialized — ClipForge starting up");
}

void Logger::log(LogLevel level, const std::string& msg) {
    if (level < min_level_) return;

    // RAII lock — releases automatically even if an exception is thrown
    std::lock_guard<std::mutex> lock(mutex_);

    std::string entry = "[" + current_timestamp() + "] "
                      + "[" + level_to_string(level) + "] "
                      + msg + "\n";

    if (initialized_ && file_.is_open()) {
        file_ << entry;
        file_.flush();  // force disk write — don't lose logs on crash
    }

    // errors always visible in terminal, debug only during development
    if (level == LogLevel::ERROR || level == LogLevel::DEBUG) {
        std::cerr << entry;
    }
}

void Logger::debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
void Logger::info (const std::string& msg) { log(LogLevel::INFO,  msg); }
void Logger::warn (const std::string& msg) { log(LogLevel::WARN,  msg); }
void Logger::error(const std::string& msg) { log(LogLevel::ERROR, msg); }

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
    }
    return "?????";
}

std::string Logger::current_timestamp() {
    auto now     = std::time(nullptr);
    auto tm_info = *std::localtime(&now);
    std::ostringstream ss;
    ss << std::put_time(&tm_info, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}