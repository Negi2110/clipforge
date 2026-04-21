#include "cli.h"
#include "ipc.h"
#include "daemon.h"
#include "logger.h"

#include <iostream>
#include <sstream>
#include <cstdio>
#include <ctime>
#include <iomanip>

// --- ANSI color codes ---
// using constants so colors are easy to change or disable globally
namespace Color {
    const std::string reset   = "\033[0m";
    const std::string bold    = "\033[1m";
    const std::string red     = "\033[31m";
    const std::string green   = "\033[32m";
    const std::string yellow  = "\033[33m";
    const std::string blue    = "\033[34m";
    const std::string magenta = "\033[35m";
    const std::string cyan    = "\033[36m";
    const std::string gray    = "\033[90m";
}

// --- helpers ---

// send one command to daemon and return response
// each call creates fresh connection — server handles one message per connection
static Response send_cmd(const Config& cfg,
                         const std::string& command,
                         const std::string& payload = "") {
    IPCClient client;
    if (!client.connect(cfg.socket_path)) {
        return make_response(1, "Cannot connect to daemon. Is it running?");
    }
    auto resp = client.send(make_message(command, payload));
    client.disconnect();
    return resp;
}

// check if daemon is running and print error if not
static bool require_daemon(const Config& cfg) {
    if (!Daemon::is_running(cfg.pid_path)) {
        std::cerr << Color::red
                  << "Error: daemon is not running. Start it with: clip daemon start"
                  << Color::reset << "\n";
        return false;
    }
    return true;
}

// format unix timestamp to human readable "21 Apr 14:32"
static std::string format_time(long ts) {
    time_t t = static_cast<time_t>(ts);
    struct tm* tm_info = std::localtime(&t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d %b %H:%M", tm_info);
    return std::string(buf);
}

// truncate long content for display — show first line only
static std::string preview(const std::string& content, size_t max = 60) {
    // find first newline — multiline content shows only first line
    auto nl = content.find('\n');
    std::string first_line = (nl != std::string::npos)
                           ? content.substr(0, nl) + " ..."
                           : content;

    if (first_line.size() > max) {
        return first_line.substr(0, max) + "...";
    }
    return first_line;
}

// type badge with color — [URL], [PATH], [TEXT] etc
static std::string type_badge(const std::string& type) {
    if (type == "url")    return Color::cyan    + "[URL] "  + Color::reset;
    if (type == "path")   return Color::yellow  + "[PATH]"  + Color::reset;
    if (type == "secret") return Color::red     + "[SEC] "  + Color::reset;
    return Color::gray + "[TEXT]" + Color::reset;
}

// --- simple JSON field extractor ---
// extracts value for a given key from our hand-rolled JSON
// not a full JSON parser — works for our specific format
static std::string json_get(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos += search.size();

    if (json[pos] == '"') {
        // string value — find closing quote, handle escaped quotes
        pos++;
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                char next = json[pos + 1];
                if (next == '"')  { result += '"';  pos += 2; continue; }
                if (next == '\\') { result += '\\'; pos += 2; continue; }
                if (next == 'n')  { result += '\n'; pos += 2; continue; }
            }
            result += json[pos++];
        }
        return result;
    } else {
        // numeric or boolean value
        auto end = json.find_first_of(",}", pos);
        return json.substr(pos, end - pos);
    }
}

// parse JSON array of items — splits on top-level { } objects
static std::vector<std::string> parse_json_array(const std::string& json) {
    std::vector<std::string> objects;
    if (json.empty() || json[0] != '[') return objects;

    int depth = 0;
    size_t start = 0;

    for (size_t i = 0; i < json.size(); i++) {
        if (json[i] == '{') {
            if (depth == 0) start = i;
            depth++;
        } else if (json[i] == '}') {
            depth--;
            if (depth == 0) {
                objects.push_back(json.substr(start, i - start + 1));
            }
        }
    }
    return objects;
}

// copy content to clipboard via wl-copy
static bool copy_to_clipboard(const std::string& content) {
    FILE* pipe = popen("wl-copy", "w");
    if (!pipe) {
        // fallback to xclip for X11
        pipe = popen("xclip -selection clipboard", "w");
        if (!pipe) return false;
    }
    fwrite(content.c_str(), 1, content.size(), pipe);
    pclose(pipe);
    return true;
}

// --- CLI commands ---

int cmd_list(const Config& cfg, int limit) {
    if (!require_daemon(cfg)) return 1;

    auto resp = send_cmd(cfg, "LIST", std::to_string(limit));
    if (resp.status != 0) {
        std::cerr << Color::red << "Error: " << resp.data << Color::reset << "\n";
        return 1;
    }

    auto items = parse_json_array(std::string(resp.data));
    if (items.empty()) {
        std::cout << Color::gray << "No clipboard history yet." << Color::reset << "\n";
        return 0;
    }

    std::cout << Color::bold << Color::blue
              << "  #   Time        Type    Content\n"
              << "  ─────────────────────────────────────────────────────────\n"
              << Color::reset;

    for (const auto& obj : items) {
        std::string id        = json_get(obj, "id");
        std::string content   = json_get(obj, "content");
        std::string type      = json_get(obj, "type");
        std::string timestamp = json_get(obj, "timestamp");
        std::string pinned    = json_get(obj, "pinned");

        bool is_pinned = (pinned == "true");

        // right-align id in 3 chars
        std::string id_str = std::string(3 - std::min((int)id.size(), 3), ' ') + id;

        std::cout << Color::gray   << "  #" << Color::reset
                  << Color::bold   << id_str << Color::reset
                  << Color::gray   << "  " << format_time(std::stol(timestamp)) << "  "
                  << Color::reset  << type_badge(type) << "  "
                  << (is_pinned ? Color::yellow + "* " + Color::reset : "  ")
                  << preview(content)
                  << "\n";
    }

    return 0;
}

int cmd_get(const Config& cfg, const std::string& id_or_name) {
    if (!require_daemon(cfg)) return 1;

    // check if argument is a number (history id) or string (snippet name)
    bool is_number = !id_or_name.empty() &&
                     id_or_name.find_first_not_of("0123456789") == std::string::npos;

    Response resp;
    if (is_number) {
        resp = send_cmd(cfg, "GET", id_or_name);
    } else {
        // treat as snippet name
        resp = send_cmd(cfg, "SNIPS");
        if (resp.status != 0) {
            std::cerr << Color::red << "Error: " << resp.data << Color::reset << "\n";
            return 1;
        }
        // find snippet by name in response
        auto snips = parse_json_array(std::string(resp.data));
        std::string content;
        for (const auto& s : snips) {
            if (json_get(s, "name") == id_or_name) {
                content = json_get(s, "content");
                break;
            }
        }
        if (content.empty()) {
            std::cerr << Color::red << "Snippet not found: " << id_or_name
                      << Color::reset << "\n";
            return 1;
        }
        if (copy_to_clipboard(content)) {
            std::cout << Color::green << "Copied snippet '"
                      << id_or_name << "' to clipboard"
                      << Color::reset << "\n";
        }
        return 0;
    }

    if (resp.status != 0) {
        std::cerr << Color::red << "Error: " << resp.data << Color::reset << "\n";
        return 1;
    }

    // copy content to clipboard
    std::string content(resp.data, resp.data_len);
    if (copy_to_clipboard(content)) {
        std::cout << Color::green << "Copied item #" << id_or_name
                  << " to clipboard" << Color::reset << "\n";
        std::cout << Color::gray  << preview(content) << Color::reset << "\n";
    } else {
        std::cerr << Color::red << "Failed to copy to clipboard" << Color::reset << "\n";
        return 1;
    }

    return 0;
}

int cmd_search(const Config& cfg, const std::string& query) {
    if (!require_daemon(cfg)) return 1;

    auto resp = send_cmd(cfg, "SEARCH", query);
    if (resp.status != 0) {
        std::cerr << Color::red << "Error: " << resp.data << Color::reset << "\n";
        return 1;
    }

    auto items = parse_json_array(std::string(resp.data));
    if (items.empty()) {
        std::cout << Color::gray << "No results for: " << query << Color::reset << "\n";
        return 0;
    }

    std::cout << Color::bold << Color::blue
              << "  Search results for: '" << query << "'\n"
              << "  ─────────────────────────────────────────────────────────\n"
              << Color::reset;

    for (const auto& obj : items) {
        std::string id        = json_get(obj, "id");
        std::string content   = json_get(obj, "content");
        std::string type      = json_get(obj, "type");
        std::string timestamp = json_get(obj, "timestamp");

        std::string id_str = std::string(3 - std::min((int)id.size(), 3), ' ') + id;

        std::cout << Color::gray  << "  #" << Color::reset
                  << Color::bold  << id_str << Color::reset
                  << Color::gray  << "  " << format_time(std::stol(timestamp)) << "  "
                  << Color::reset << type_badge(type) << "  "
                  << preview(content) << "\n";
    }

    return 0;
}

int cmd_delete(const Config& cfg, int id) {
    if (!require_daemon(cfg)) return 1;

    auto resp = send_cmd(cfg, "DELETE", std::to_string(id));
    if (resp.status != 0) {
        std::cerr << Color::red << "Error: " << resp.data << Color::reset << "\n";
        return 1;
    }

    std::cout << Color::green << "Deleted item #" << id << Color::reset << "\n";
    return 0;
}

int cmd_clear(const Config& cfg) {
    if (!require_daemon(cfg)) return 1;

    // confirm before wiping — destructive operation
    std::cout << Color::yellow
              << "Clear all clipboard history? Pinned items will be kept. (y/N): "
              << Color::reset;

    std::string answer;
    std::getline(std::cin, answer);

    if (answer != "y" && answer != "Y") {
        std::cout << "Cancelled.\n";
        return 0;
    }

    auto resp = send_cmd(cfg, "CLEAR");
    if (resp.status != 0) {
        std::cerr << Color::red << "Error: " << resp.data << Color::reset << "\n";
        return 1;
    }

    std::cout << Color::green << "History cleared." << Color::reset << "\n";
    return 0;
}

int cmd_pin(const Config& cfg, int id, bool pin) {
    if (!require_daemon(cfg)) return 1;

    std::string payload = std::to_string(id) + ":" + (pin ? "1" : "0");
    auto resp = send_cmd(cfg, "PIN", payload);

    if (resp.status != 0) {
        std::cerr << Color::red << "Error: " << resp.data << Color::reset << "\n";
        return 1;
    }

    std::cout << Color::green
              << (pin ? "Pinned" : "Unpinned") << " item #" << id
              << Color::reset << "\n";
    return 0;
}

int cmd_save(const Config& cfg, const std::string& name) {
    if (!require_daemon(cfg)) return 1;

    // read current clipboard content
    FILE* pipe = popen("wl-paste --no-newline 2>/dev/null", "r");
    if (!pipe) {
        pipe = popen("xclip -selection clipboard -o 2>/dev/null", "r");
        if (!pipe) {
            std::cerr << Color::red << "Failed to read clipboard" << Color::reset << "\n";
            return 1;
        }
    }

    char buf[4096] = {0};
    std::string content;
    while (fgets(buf, sizeof(buf), pipe)) {
        content += buf;
    }
    pclose(pipe);

    if (content.empty()) {
        std::cerr << Color::red << "Clipboard is empty" << Color::reset << "\n";
        return 1;
    }

    auto resp = send_cmd(cfg, "SAVE", name + ":" + content);
    if (resp.status != 0) {
        std::cerr << Color::red << "Error: " << resp.data << Color::reset << "\n";
        return 1;
    }

    std::cout << Color::green << "Saved snippet '" << name << "'"
              << Color::reset << "\n";
    return 0;
}

int cmd_snippets(const Config& cfg) {
    if (!require_daemon(cfg)) return 1;

    auto resp = send_cmd(cfg, "SNIPS");
    if (resp.status != 0) {
        std::cerr << Color::red << "Error: " << resp.data << Color::reset << "\n";
        return 1;
    }

    auto items = parse_json_array(std::string(resp.data));
    if (items.empty()) {
        std::cout << Color::gray << "No snippets saved yet." << Color::reset << "\n";
        return 0;
    }

    std::cout << Color::bold << Color::blue
              << "  Saved snippets\n"
              << "  ─────────────────────────────────────────────────────────\n"
              << Color::reset;

    for (const auto& obj : items) {
        std::string id      = json_get(obj, "id");
        std::string name    = json_get(obj, "name");
        std::string content = json_get(obj, "content");

        std::cout << Color::bold   << "  " << name << Color::reset
                  << Color::gray   << "  →  " << Color::reset
                  << preview(content) << "\n";
    }

    return 0;
}

int cmd_stats(const Config& cfg) {
    if (!require_daemon(cfg)) return 1;

    // for now just show list count — full stats in step 20
    auto resp = send_cmd(cfg, "LIST", "1000");
    if (resp.status != 0) {
        std::cerr << Color::red << "Error: " << resp.data << Color::reset << "\n";
        return 1;
    }

    auto items = parse_json_array(std::string(resp.data));

    std::cout << Color::bold << Color::blue
              << "  ClipForge Stats\n"
              << "  ─────────────────────────────────────────────────────────\n"
              << Color::reset;
    std::cout << "  Total items:  " << Color::bold << items.size()
              << Color::reset << "\n";

    // count by type
    int urls = 0, paths = 0, texts = 0, secrets = 0;
    for (const auto& obj : items) {
        std::string type = json_get(obj, "type");
        if (type == "url")    urls++;
        else if (type == "path")   paths++;
        else if (type == "secret") secrets++;
        else texts++;
    }

    std::cout << "  URLs:         " << Color::cyan   << urls    << Color::reset << "\n";
    std::cout << "  Paths:        " << Color::yellow << paths   << Color::reset << "\n";
    std::cout << "  Text:         " << Color::gray   << texts   << Color::reset << "\n";
    std::cout << "  Secrets:      " << Color::red    << secrets << Color::reset << "\n";

    return 0;
}

void print_usage() {
    std::cout << Color::bold << "\nClipForge — clipboard manager\n\n" << Color::reset;
    std::cout << Color::bold << "Usage:\n" << Color::reset;
    std::cout << "  clip list              " << Color::gray << "show clipboard history\n"         << Color::reset;
    std::cout << "  clip get <id>          " << Color::gray << "copy item #id to clipboard\n"    << Color::reset;
    std::cout << "  clip get <name>        " << Color::gray << "copy named snippet to clipboard\n"<< Color::reset;
    std::cout << "  clip search <query>    " << Color::gray << "search history\n"                << Color::reset;
    std::cout << "  clip delete <id>       " << Color::gray << "delete item #id\n"               << Color::reset;
    std::cout << "  clip clear             " << Color::gray << "clear all history\n"             << Color::reset;
    std::cout << "  clip pin <id>          " << Color::gray << "pin item (never evicted)\n"      << Color::reset;
    std::cout << "  clip unpin <id>        " << Color::gray << "unpin item\n"                    << Color::reset;
    std::cout << "  clip save <name>       " << Color::gray << "save clipboard as snippet\n"     << Color::reset;
    std::cout << "  clip snippets          " << Color::gray << "list saved snippets\n"           << Color::reset;
    std::cout << "  clip stats             " << Color::gray << "show usage statistics\n"         << Color::reset;
    std::cout << "  clip daemon start      " << Color::gray << "start the daemon\n"              << Color::reset;
    std::cout << "  clip daemon stop       " << Color::gray << "stop the daemon\n"               << Color::reset;
    std::cout << "  clip daemon status     " << Color::gray << "check daemon status\n"           << Color::reset;
    std::cout << "\n";
}