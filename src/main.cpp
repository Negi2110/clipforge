#include <iostream>
#include "config.h"
#include "logger.h"

int main() {
    Config cfg = load_config();
    Logger::get().init(cfg.log_path, LogLevel::DEBUG);

    std::cout << "ClipForge v0.1\n";

    Log::debug("Config loaded from disk");
    Log::info ("ClipForge started successfully");
    Log::warn ("This is a warning");
    Log::error("This is an error");
    Log::info ("DB path: " + cfg.db_path);

    return 0;
}