#include "config.h"
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <cstdlib>
#include <sys/stat.h>

std::string get_config_path() {
    const char* home = std::getenv("HOME");
    return std::string(home) + "/.config/clipforge/config.ini";
}

// static = private to this translation unit, no other file can call this
static void ensure_dir(const std::string& path) {
    mkdir(path.c_str(), 0755);
}

void save_default_config(const std::string& config_path) {
    const char* home = std::getenv("HOME");
    std::string data_dir = std::string(home) + "/.local/share/clipforge";

    ensure_dir(std::string(home) + "/.config/clipforge");
    ensure_dir(data_dir);

    std::ofstream f(config_path);
    if (!f.is_open()) {
        std::cerr << "Error: could not create config file at " << config_path << "\n";
        return;
    }

    f << "# ClipForge configuration\n";
    f << "# Edit these values then restart the daemon\n\n";
    f << "db_path="                    << data_dir << "/history.db\n";
    f << "socket_path="                << "/tmp/clipforge.sock\n";
    f << "pid_path=" << data_dir << "/clipforge.pid\n";    
    f << "log_path="                   << data_dir << "/clipforge.log\n";
    f << "max_history="                << "1000\n";
    f << "sensitive_timeout_seconds="  << "30\n";
}

Config load_config() {
    std::string config_path = get_config_path();

    std::ifstream test(config_path);
    if (!test.good()) {
        save_default_config(config_path);
    }
    test.close();

    // unordered_map so line order in the file doesn't matter
    std::unordered_map<std::string, std::string> values;
    std::ifstream f(config_path);
    std::string line;

    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        values[line.substr(0, eq)] = line.substr(eq + 1);
    }

    Config cfg;
    const char* home = std::getenv("HOME");
    std::string data_dir = std::string(home) + "/.local/share/clipforge";

    cfg.db_path     = values.count("db_path")     ? values["db_path"]     : data_dir + "/history.db";
    cfg.socket_path = values.count("socket_path") ? values["socket_path"] : "/tmp/clipforge.sock";
    cfg.pid_path    = values.count("pid_path")    ? values["pid_path"]    : "/tmp/clipforge.pid";
    cfg.log_path    = values.count("log_path")    ? values["log_path"]    : data_dir + "/clipforge.log";

    // stoi only called when key exists — stoi on empty string throws
    cfg.max_history = values.count("max_history")
        ? std::stoi(values["max_history"]) : 1000;
    cfg.sensitive_timeout_seconds = values.count("sensitive_timeout_seconds")
        ? std::stoi(values["sensitive_timeout_seconds"]) : 30;

    return cfg;
}