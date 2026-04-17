#pragma once
#include <string>
#include <fstream>
#include <mutex>

// scoped enum prevents collision with system macros — some platforms define DEBUG globally
enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class Logger {
public:
    // singleton access — one instance for the entire program lifetime
    static Logger& get();

    // call once at startup with log_path from Config
    void init(const std::string& log_path, LogLevel min_level = LogLevel::DEBUG);

    void debug(const std::string& msg);
    void info (const std::string& msg);
    void warn (const std::string& msg);
    void error(const std::string& msg);

    // singleton must never be copied — enforce at compile time
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;

    void        log(LogLevel level, const std::string& msg);
    std::string level_to_string(LogLevel level);
    std::string current_timestamp();

    std::ofstream file_;
    LogLevel      min_level_     = LogLevel::DEBUG;
    std::mutex    mutex_;          // protects file_ from concurrent writes
    bool          initialized_   = false;
};

// namespace alias so call sites write Log::info() instead of Logger::get().info()
namespace Log {
    inline void debug(const std::string& m) { Logger::get().debug(m); }
    inline void info (const std::string& m) { Logger::get().info(m);  }
    inline void warn (const std::string& m) { Logger::get().warn(m);  }
    inline void error(const std::string& m) { Logger::get().error(m); }
}