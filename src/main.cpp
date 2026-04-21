#include <iostream>
#include <thread>
#include <chrono>
#include "config.h"
#include "logger.h"
#include "storage.h"
#include "ipc.h"
#include "daemon.h"

// helper — connects, sends one message, returns response, disconnects
static Response send_command(const std::string& socket_path,
                             const std::string& cmd,
                             const std::string& payload = "") {
    IPCClient client;
    if (!client.connect(socket_path)) {
        return make_response(1, "Failed to connect to daemon");
    }
    auto resp = client.send(make_message(cmd, payload));
    client.disconnect();
    return resp;
}

int main(int argc, char* argv[]) {
    Config cfg = load_config();
    Logger::get().init(cfg.log_path, LogLevel::DEBUG);

    if (argc > 1) {
        std::string arg(argv[1]);

        if (arg == "daemon") {
            if (Daemon::is_running(cfg.pid_path)) {
                std::cout << "Daemon is already running\n";
                return 1;
            }
            std::cout << "Starting daemon...\n";
            Daemon d;
            d.daemonize(cfg);
            d.run(cfg);
            return 0;
        }

        if (arg == "stop") {
            if (!Daemon::is_running(cfg.pid_path)) {
                std::cout << "Daemon is not running\n";
                return 1;
            }
            Daemon::stop(cfg.pid_path);
            std::cout << "Daemon stopped\n";
            return 0;
        }

        if (arg == "status") {
            std::cout << (Daemon::is_running(cfg.pid_path)
                ? "Daemon is running\n"
                : "Daemon is not running\n");
            return 0;
        }
    }

    // no args — show history
    if (!Daemon::is_running(cfg.pid_path)) {
        std::cout << "Daemon is not running. Start with: ./build/clipforge daemon\n";
        return 1;
    }

   // separate connection per command — small delay between connections
    auto ping = send_command(cfg.socket_path, "PING");
    std::cout << "PING: " << ping.data << "\n\n";

    // small delay to ensure server thread finishes before next connection
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto list = send_command(cfg.socket_path, "LIST", "10");
    std::cout << "Recent clipboard items:\n" << list.data << "\n";

    return 0;
}