#include <iostream>
#include <string>
#include "config.h"
#include "logger.h"
#include "daemon.h"
#include "cli.h"
#include "tui.h"
int main(int argc, char* argv[]) {
    Config cfg = load_config();
    Logger::get().init(cfg.log_path, LogLevel::DEBUG);
    // no arguments — show history
    if (argc < 2) {
        return cmd_list(cfg, 20);
    }

    std::string cmd(argv[1]);

    // --- daemon control ---
    if (cmd == "daemon") {
        std::string sub = argc > 2 ? argv[2] : "start";

        if (sub == "start") {
            if (Daemon::is_running(cfg.pid_path)) {
                std::cout << "Daemon is already running\n";
                return 1;
            }
            std::cout << "Starting ClipForge daemon...\n";
            Daemon d;
            d.daemonize(cfg);
            d.run(cfg);
            return 0;
        }

        if (sub == "stop") {
            if (!Daemon::is_running(cfg.pid_path)) {
                std::cout << "Daemon is not running\n";
                return 1;
            }
            Daemon::stop(cfg.pid_path);
            std::cout << "Daemon stopped\n";
            return 0;
        }

        if (sub == "status") {
            std::cout << (Daemon::is_running(cfg.pid_path)
                ? "Daemon is running\n"
                : "Daemon is not running\n");
            return 0;
        }
    }

    // --- clipboard commands ---
    if (cmd == "list") {
        int limit = argc > 2 ? std::stoi(argv[2]) : 20;
        return cmd_list(cfg, limit);
    }

    if (cmd == "get") {
        if (argc < 3) { std::cerr << "Usage: clip get <id|name>\n"; return 1; }
        return cmd_get(cfg, argv[2]);
    }

    if (cmd == "search") {
        if (argc < 3) { std::cerr << "Usage: clip search <query>\n"; return 1; }
        return cmd_search(cfg, argv[2]);
    }

    if (cmd == "delete" || cmd == "rm") {
        if (argc < 3) { std::cerr << "Usage: clip delete <id>\n"; return 1; }
        return cmd_delete(cfg, std::stoi(argv[2]));
    }

    if (cmd == "clear") {
        return cmd_clear(cfg);
    }

    if (cmd == "pin") {
        if (argc < 3) { std::cerr << "Usage: clip pin <id>\n"; return 1; }
        return cmd_pin(cfg, std::stoi(argv[2]), true);
    }

    if (cmd == "unpin") {
        if (argc < 3) { std::cerr << "Usage: clip unpin <id>\n"; return 1; }
        return cmd_pin(cfg, std::stoi(argv[2]), false);
    }

    if (cmd == "save") {
        if (argc < 3) { std::cerr << "Usage: clip save <name>\n"; return 1; }
        return cmd_save(cfg, argv[2]);
    }

    if (cmd == "snippets") {
        return cmd_snippets(cfg);
    }

    if (cmd == "stats") {
        return cmd_stats(cfg);
    }
    
    if (cmd == "ui") {
        return run_tui(cfg);
    }
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        print_usage();
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    print_usage();
    return 1;
}