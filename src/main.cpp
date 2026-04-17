#include <iostream>
#include "config.h"

int main() {
    // load_config() either reads the existing file
    // or creates it fresh on first run
    Config cfg = load_config();

    std::cout << "ClipForge v0.1\n\n";
    std::cout << "Config loaded:\n";
    std::cout << "  DB path:     " << cfg.db_path     << "\n";
    std::cout << "  Socket:      " << cfg.socket_path << "\n";
    std::cout << "  PID file:    " << cfg.pid_path    << "\n";
    std::cout << "  Log file:    " << cfg.log_path    << "\n";
    std::cout << "  Max history: " << cfg.max_history << "\n";
    std::cout << "  Secret TTL:  " << cfg.sensitive_timeout_seconds << "s\n";

    return 0;
}