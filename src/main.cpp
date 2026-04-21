#include <iostream>
#include <thread>
#include <chrono>
#include "config.h"
#include "logger.h"
#include "storage.h"
#include "ipc.h"

int main() {
    Config cfg = load_config();
    Logger::get().init(cfg.log_path, LogLevel::DEBUG);

    Storage storage(cfg.db_path);
    storage.clear_history();
    storage.save_item("https://github.com/Negi2110/clipforge", "url");
    storage.save_item("sudo apt install cmake", "text");

    // start IPC server
    IPCServer server;
    server.start(cfg.socket_path, [&](const Message& msg) -> Response {
        std::string cmd(msg.command);
        if (cmd == "PING") return make_response(0, "PONG");
        if (cmd == "LIST") {
            auto items = storage.get_history(50);
            return make_response(0, serialize_items(items));
        }
        return make_response(1, "Unknown command");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // test PING and LIST on separate connections
    // each send() opens a fresh connection — matches server's one-request-per-thread model
    {
        IPCClient client;
        if (client.connect(cfg.socket_path)) {
            auto resp = client.send(make_message("PING"));
            std::cout << "PING: " << resp.data << "\n";
        }
    }

    {
        IPCClient client;
        if (client.connect(cfg.socket_path)) {
            auto resp = client.send(make_message("LIST"));
            std::cout << "LIST status: " << resp.status << "\n";
            std::cout << "LIST data: "   << resp.data   << "\n";
        }
    }

    return 0;
}