#include "ipc.h"
#include "logger.h"

#include <sys/socket.h>   // socket, bind, listen, accept, connect
#include <sys/un.h>       // sockaddr_un — Unix domain socket address structure
#include <unistd.h>       // close, read, write
#include <cstring>        // memset, memcpy, strncpy
#include <stdexcept>
#include <thread>
#include <sstream>
#include <iomanip>
#include <ctime>

// --- serialization ---

std::string serialize_items(const std::vector<ClipItem>& items) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < items.size(); i++) {
        const auto& item = items[i];

        // escape content for JSON
        std::string escaped;
        for (char c : item.content) {
            if      (c == '"')  escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else if (c == '\r') escaped += "\\r";
            else if (c == '\t') escaped += "\\t";
            else escaped += c;
        }

        // put type and timestamp BEFORE content
        // so json_get finds them before hitting escaped content
        ss << "{"
           << "\"id\":"         << item.id                           << ","
           << "\"type\":\""     << item.type                         << "\","
           << "\"timestamp\":"  << static_cast<long>(item.timestamp) << ","
           << "\"pinned\":"     << (item.pinned ? "true" : "false")  << ","
           << "\"content\":\""  << escaped                           << "\""
           << "}";

        if (i + 1 < items.size()) ss << ",";
    }
    ss << "]";
    return ss.str();
}

std::string serialize_snippets(const std::vector<Snippet>& snippets) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < snippets.size(); i++) {
        const auto& s = snippets[i];

        std::string escaped;
        for (char c : s.content) {
            if (c == '"')  escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else escaped += c;
        }

        ss << "{"
           << "\"id\":"       << s.id      << ","
           << "\"name\":\""   << s.name    << "\","
           << "\"content\":\"" << escaped  << "\""
           << "}";

        if (i + 1 < snippets.size()) ss << ",";
    }
    ss << "]";
    return ss.str();
}

// --- message helpers ---

Message make_message(const std::string& command, const std::string& payload) {
    Message msg;
    // zero the entire struct first — no garbage bytes in unused space
    memset(&msg, 0, sizeof(msg));
    // strncpy copies at most n bytes — prevents buffer overflow
    strncpy(msg.command, command.c_str(), CF_CMD_LEN - 1);
    if (!payload.empty()) {
        size_t len = std::min(payload.size(), (size_t)CF_PAYLOAD_LEN - 1);
        memcpy(msg.payload, payload.c_str(), len);
        msg.payload_len = static_cast<int>(len);
    }
    return msg;
}

Response make_response(int status, const std::string& data) {
    Response resp;
    memset(&resp, 0, sizeof(resp));
    resp.status = status;
    size_t len = std::min(data.size(), (size_t)CF_DATA_LEN - 1);
    memcpy(resp.data, data.c_str(), len);
    resp.data_len = static_cast<int>(len);
    return resp;
}

// --- IPCServer ---

void IPCServer::start(const std::string& socket_path,
                      std::function<Response(const Message&)> handler) {
    socket_path_ = socket_path;

    // AF_UNIX = Unix domain socket (file-based, same machine only)
    // SOCK_STREAM = reliable ordered byte stream (like TCP but local)
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    // remove stale socket file from previous run
    // if daemon crashed without cleanup, old socket file blocks bind()
    unlink(socket_path.c_str());

    // sockaddr_un is the address structure for Unix sockets
    // equivalent of sockaddr_in for TCP/IP but uses a file path instead of IP:port
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    // bind — associate this socket with the file path
    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        throw std::runtime_error("Failed to bind socket: " + socket_path);
    }

    // listen — mark socket as passive (accepts incoming connections)
    // 5 = backlog, max pending connections waiting to be accepted
    if (listen(server_fd_, 5) < 0) {
        throw std::runtime_error("Failed to listen on socket");
    }

    Log::info("IPC server listening on " + socket_path);

    // run accept loop in a background thread so daemon can do other work
    // lambda captures handler and this by reference
    std::thread([this, handler]() {
        while (server_fd_ >= 0) {
            // accept blocks until a client connects
            // returns a new fd for this specific connection
            int client_fd = accept(server_fd_, nullptr, nullptr);
            if (client_fd < 0) break;  // server_fd_ was closed — time to exit

            // handle each client in its own thread
            // so multiple CLI invocations don't block each other
            std::thread([client_fd, handler]() {
                Message msg;
                // read exactly sizeof(Message) bytes — fixed size is the protocol
                ssize_t n = read(client_fd, &msg, sizeof(msg));
                if (n == sizeof(Message)) {
                    Response resp = handler(msg);
                    write(client_fd, &resp, sizeof(resp));
                }
                close(client_fd);
            }).detach();  // detach — thread cleans itself up when done
        }
    }).detach();
}

void IPCServer::stop() {
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
        unlink(socket_path_.c_str());  // remove the socket file from filesystem
        Log::info("IPC server stopped");
    }
}

// --- IPCClient ---

bool IPCClient::connect(const std::string& socket_path) {
    client_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd_ < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    // connect — attempt to reach the daemon's listening socket
    // fails immediately with ECONNREFUSED if daemon is not running
    if (::connect(client_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(client_fd_);
        client_fd_ = -1;
        return false;
    }

    return true;
}

void IPCClient::disconnect() {
    if (client_fd_ >= 0) {
        close(client_fd_);
        client_fd_ = -1;
    }
}

Response IPCClient::send(const Message& msg) {
    Response resp;
    memset(&resp, 0, sizeof(resp));

    if (client_fd_ < 0) {
        return make_response(1, "Not connected to daemon");
    }

    // write full message struct as raw bytes
    write(client_fd_, &msg, sizeof(msg));

    // block until daemon writes back exactly sizeof(Response) bytes
    ssize_t n = read(client_fd_, &resp, sizeof(resp));
    if (n != sizeof(Response)) {
        return make_response(1, "Invalid response from daemon");
    }

    return resp;
}

bool IPCClient::is_daemon_running(const std::string& socket_path) {
    // try connecting — if it fails daemon is not running
    if (!connect(socket_path)) return false;
    // send PING and expect any response
    auto msg  = make_message("PING");
    auto resp = send(msg);
    disconnect();
    return resp.status == 0;
}