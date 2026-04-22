#include "daemon.h"
#include "logger.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstring>

// static member — lives for entire program lifetime
std::atomic<bool> Daemon::running_(true);

// signal handler must be a free function — POSIX requirement
// only async-signal-safe operations allowed here
// setting an atomic bool is async-signal-safe
static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        Daemon::running_ = false;
    }
}

void Daemon::daemonize(const Config& cfg) {
    // --- first fork ---
    // parent exits so shell gets its prompt back
    pid_t pid = fork();
    if (pid < 0) {
        Log::error("First fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS);  // parent exits

    // --- child continues ---

    // create new session — detach from controlling terminal
    if (setsid() < 0) {
        Log::error("setsid failed");
        exit(EXIT_FAILURE);
    }

    // --- second fork ---
    // session leader forks again — grandchild can never reacquire a terminal
    pid = fork();
    if (pid < 0) {
        Log::error("Second fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS);  // second parent exits

    // --- grandchild — this is the actual daemon ---

    // umask 0 so daemon can create files with any permissions
    umask(0);

    // chdir to root — don't hold references to mountable filesystems
    chdir("/");

    // redirect stdin/stdout/stderr to /dev/null
    // daemon has no terminal — writes to stdout would fail without this
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }

    // register signal handlers
    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);
    // ignore SIGHUP — some terminals send it on close
    signal(SIGHUP,  SIG_IGN);

    write_pidfile(cfg.pid_path);
    Log::info("Daemon started — PID " + std::to_string(getpid()));
}

void Daemon::run(Config& cfg) {
    Storage storage(cfg.db_path);

    // IPC server handles all CLI commands
    IPCServer server;
    server.start(cfg.socket_path, [&](const Message& msg) -> Response {
        return handle_message(msg, storage);
    });

    // clipboard watcher runs in background thread
    std::thread watcher([&]() {
        watch_clipboard(storage);
    });
    watcher.detach();

    Log::info("Daemon running — watching clipboard");

    // main loop — sleeps until signal sets running_ = false
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // graceful shutdown
    Log::info("Daemon shutting down");
    server.stop();
    remove_pidfile(cfg.pid_path);
    Log::info("Daemon stopped cleanly");
}

void Daemon::watch_clipboard(Storage& storage) {
    Log::info("Clipboard watcher started (polling mode)");

    std::string last_content;

    while (running_) {
        // read entire clipboard output at once — not line by line
        // this preserves multiline content as a single item
        FILE* pipe = popen("xclip -selection clipboard -o 2>/dev/null", "r");
        if (!pipe) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        // read ALL output into one string
        char buf[4096] = {0};
        std::string content;
        while (fgets(buf, sizeof(buf), pipe)) {
            content += buf;  // accumulate all lines into one string
        }
        pclose(pipe);

        // strip trailing whitespace
        while (!content.empty() &&
               (content.back() == '\n' || content.back() == '\r' ||
                content.back() == ' '  || content.back() == '\t')) {
            content.pop_back();
        }

        // only save if content changed since last poll
        if (!content.empty() && content != last_content) {
            last_content = content;
            int id = storage.save_item(content);
            if (id > 0) {
                Log::info("Captured [id=" + std::to_string(id) + "]: "
                          + content.substr(0, 60));
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    Log::info("Clipboard watcher stopped");
}

Response Daemon::handle_message(const Message& msg, Storage& storage) {
    std::string cmd(msg.command);
    std::string payload(msg.payload, msg.payload_len);

    Log::debug("IPC command: " + cmd);

    if (cmd == "PING") {
        return make_response(0, "PONG");
    }

    if (cmd == "LIST") {
        int limit = payload.empty() ? 50 : std::stoi(payload);
        return make_response(0, serialize_items(storage.get_history(limit)));
    }

    if (cmd == "GET") {
        if (payload.empty()) return make_response(1, "No id provided");
        auto item = storage.get_item(std::stoi(payload));
        if (!item) return make_response(1, "Item not found");
        return make_response(0, item->content);
    }

    if (cmd == "SEARCH") {
        return make_response(0, serialize_items(storage.search(payload)));
    }

    if (cmd == "DELETE") {
        if (payload.empty()) return make_response(1, "No id provided");
        storage.delete_item(std::stoi(payload));
        return make_response(0, "Deleted");
    }

    if (cmd == "CLEAR") {
        storage.clear_history();
        return make_response(0, "Cleared");
    }

    if (cmd == "PIN") {
        // payload format: "id:1" to pin, "id:0" to unpin
        auto colon = payload.find(':');
        if (colon == std::string::npos) return make_response(1, "Bad payload");
        int  id  = std::stoi(payload.substr(0, colon));
        bool pin = payload.substr(colon + 1) == "1";
        storage.pin_item(id, pin);
        return make_response(0, pin ? "Pinned" : "Unpinned");
    }

    if (cmd == "SAVE") {
        // payload format: "name:content"
        auto colon = payload.find(':');
        if (colon == std::string::npos) return make_response(1, "Bad payload");
        storage.save_snippet(payload.substr(0, colon), payload.substr(colon + 1));
        return make_response(0, "Snippet saved");
    }

    if (cmd == "SNIPS") {
        return make_response(0, serialize_snippets(storage.get_snippets()));
    }

    if (cmd == "STOP") {
        running_ = false;
        return make_response(0, "Daemon stopping");
    }

    return make_response(1, "Unknown command: " + cmd);
}

void Daemon::write_pidfile(const std::string& pid_path) {
    std::ofstream f(pid_path);
    if (f.is_open()) {
        f << getpid() << "\n";
        Log::info("PID file written: " + pid_path);
    } else {
        Log::error("Failed to write PID file: " + pid_path);
    }
}

void Daemon::remove_pidfile(const std::string& pid_path) {
    unlink(pid_path.c_str());
    Log::debug("PID file removed");
}

bool Daemon::is_running(const std::string& pid_path) {
    std::ifstream f(pid_path);
    if (!f.is_open()) return false;

    pid_t pid;
    f >> pid;
    if (pid <= 0) return false;

    // kill(pid, 0) sends no signal — just checks if process exists
    // returns 0 if alive, -1 if not
    return kill(pid, 0) == 0;
}

bool Daemon::stop(const std::string& pid_path) {
    std::ifstream f(pid_path);
    if (!f.is_open()) return false;

    pid_t pid;
    f >> pid;
    if (pid <= 0) return false;

    // SIGTERM = polite shutdown request
    // daemon's signal handler sets running_ = false, main loop exits cleanly
    if (kill(pid, SIGTERM) == 0) {
        Log::info("Sent SIGTERM to daemon PID " + std::to_string(pid));
        return true;
    }
    return false;
}