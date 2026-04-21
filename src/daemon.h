#pragma once
#include <atomic>
#include <string>
#include "config.h"
#include "storage.h"
#include "ipc.h"

class Daemon {
public:
    Daemon()  = default;
    ~Daemon() = default;

    Daemon(const Daemon&)            = delete;
    Daemon& operator=(const Daemon&) = delete;

    // forks twice, creates new session, redirects stdio to /dev/null
    // writes PID file — returns only in the grandchild (actual daemon)
    void daemonize(const Config& cfg);

    // starts clipboard watcher thread and IPC server
    // blocks in main loop until shutdown signal received
    void run(Config& cfg);

    // checks pidfile — returns true if a daemon is already running
    static bool is_running(const std::string& pid_path);

    // sends SIGTERM to PID in pidfile
    static bool stop(const std::string& pid_path);

    // global flag — signal handler sets this to false to trigger shutdown
    // static so free-function signal handler can reach it
    static std::atomic<bool> running_;

private:
    void     write_pidfile(const std::string& pid_path);
    void     remove_pidfile(const std::string& pid_path);
    void     watch_clipboard(Storage& storage);
    Response handle_message(const Message& msg, Storage& storage);
};