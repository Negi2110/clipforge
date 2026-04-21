#pragma once
#include <string>
#include <functional>
#include "storage.h"

// fixed sizes guarantee both sides read/write exact byte counts
// no framing problem — receiver always knows sizeof(Message) bytes = one message
#define CF_CMD_LEN      32
#define CF_PAYLOAD_LEN  4096
#define CF_DATA_LEN     65536

// every CLI→daemon request fits in this struct
struct Message {
    char command[CF_CMD_LEN];      // "LIST", "GET", "SEARCH", "DELETE" etc
    char payload[CF_PAYLOAD_LEN];  // arguments — item id, search query, snippet name
    int  payload_len;              // actual bytes used in payload
};

// every daemon→CLI response fits in this struct
struct Response {
    int  status;               // 0 = success, 1 = error
    char data[CF_DATA_LEN];    // response body — JSON encoded items or error message
    int  data_len;             // actual bytes used in data
};

// --- serialization ---
// convert ClipItems to a simple JSON string for transmission
// we write our own — no external library needed for this simple structure
std::string serialize_items(const std::vector<ClipItem>& items);
std::string serialize_snippets(const std::vector<Snippet>& snippets);

// build a Message struct cleanly — sets command and payload, zeroes the rest
Message make_message(const std::string& command, const std::string& payload = "");

// build a Response struct cleanly
Response make_response(int status, const std::string& data);

// --- IPC server (daemon side) ---
class IPCServer {
public:
    IPCServer()  = default;
    ~IPCServer() { stop(); }

    IPCServer(const IPCServer&)            = delete;
    IPCServer& operator=(const IPCServer&) = delete;

    // starts listening on socket_path, calls handler for every incoming message
    // handler receives a Message and returns a Response — pure function, easy to test
    void start(const std::string& socket_path,
               std::function<Response(const Message&)> handler);

    void stop();

private:
    int         server_fd_ = -1;   // listening socket file descriptor
    std::string socket_path_;      // path to socket file — needed for cleanup
};

// --- IPC client (CLI side) ---
class IPCClient {
public:
    IPCClient()  = default;
    ~IPCClient() { disconnect(); }

    IPCClient(const IPCClient&)            = delete;
    IPCClient& operator=(const IPCClient&) = delete;

    bool     connect(const std::string& socket_path);
    void     disconnect();
    Response send(const Message& msg);

    // convenience — try connecting, return false if daemon not running
    bool is_daemon_running(const std::string& socket_path);

private:
    int client_fd_ = -1;
};