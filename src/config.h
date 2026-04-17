#pragma once
#include <string>

// single source of truth for all runtime settings.
// every module reads paths and limits from here — nothing is hardcoded.
struct Config {
    std::string db_path;
    std::string socket_path;
    std::string pid_path;
    std::string log_path;
    int max_history;
    int sensitive_timeout_seconds;
};

Config      load_config();
void        save_default_config(const std::string& config_path);
std::string get_config_path();