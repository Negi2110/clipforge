#include "cli.h"
#include "ipc.h"
#include "daemon.h"
#include "logger.h"
#include <map>
#include <iostream>
#include <sstream>
#include <cstdio>
#include <ctime>
#include <iomanip>

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

static bool require_daemon(const Config& cfg) {
    if (!Daemon::is_running(cfg.pid_path)) {
        std::cerr << Color::red
                  << "Error: daemon is not running. Start it with: clip daemon start"
                  << Color::reset << "\n";
        return false;
    }
    return true;
}

static std::string format_time(const std::string& ts_str) {
    if (ts_str.empty()) return "unknown";
    try {
        long ts = std::stol(ts_str);
        time_t t = static_cast<time_t>(ts);
        struct tm* tm_info = std::localtime(&t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%d %b %H:%M", tm_info);
        return std::string(buf);
    } catch (...) {
        return "unknown";
    }
}

static std::string preview(const std::string& content, size_t max = 60) {
    auto nl = content.find('\n');
    std::string first_line = (nl != std::string::npos)
                           ? content.substr(0, nl) + " ..."
                           : content;
    if (first_line.size() > max) {
        return first_line.substr(0, max) + "...";
    }
    return first_line;
}

static std::string type_badge(const std::string& type) {
    if (type == "url")    return Color::cyan    + "[URL]  " + Color::reset;
    if (type == "path")   return Color::yellow  + "[PATH] " + Color::reset;
    if (type == "secret") return Color::red     + "[SEC]  " + Color::reset;
    if (type == "code")   return Color::magenta + "[CODE] " + Color::reset;
    return Color::gray + "[TEXT] " + Color::reset;
}

static std::string json_get(const std::string& obj, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = obj.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    if (pos >= obj.size()) return "";

    if (obj[pos] == '"') {
        pos++;
        std::string result;
        while (pos < obj.size()) {
            char c = obj[pos];
            if (c == '\\' && pos + 1 < obj.size()) {
                char next = obj[pos+1];
                if      (next == '"')  { result += '"';  pos += 2; }
                else if (next == '\\') { result += '\\'; pos += 2; }
                else if (next == 'n')  { result += '\n'; pos += 2; }
                else if (next == 'r')  { result += '\r'; pos += 2; }
                else if (next == 't')  { result += '\t'; pos += 2; }
                else                   { result += next; pos += 2; }
            } else if (c == '"') {
                break;
            } else {
                result += c; pos++;
            }
        }
        return result;
    } else {
        auto end = obj.find_first_of(",}", pos);
        if (end == std::string::npos) return obj.substr(pos);
        return obj.substr(pos, end - pos);
    }
}

static std::vector<std::string> parse_json_array(const std::string& json) {
    std::vector<std::string> objects;
    if (json.empty() || json[0] != '[') return objects;

    int depth = 0;
    size_t start = 0;
    bool in_string = false;

    for (size_t i = 0; i < json.size(); i++) {
        if (in_string) {
            if (json[i] == '\\') { i++; continue; }
            if (json[i] == '"')  { in_string = false; }
            continue;
        }
        if (json[i] == '"') { in_string = true; continue; }
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

static bool copy_to_clipboard(const std::string& content) {
    FILE* pipe = popen("wl-copy", "w");
    if (!pipe) {
        pipe = popen("xclip -selection clipboard", "w");
        if (!pipe) return false;
    }
    fwrite(content.c_str(), 1, content.size(), pipe);
    pclose(pipe);
    return true;
}

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
        std::string type      = json_get(obj, "type");
        std::string timestamp = json_get(obj, "timestamp");
        std::string pinned    = json_get(obj, "pinned");
        std::string content   = json_get(obj, "content");

        bool is_pinned = (pinned == "true");
        std::string id_str = std::string(3 - std::min((int)id.size(), 3), ' ') + id;
        // warn user if item is a secret about to expire
std::string expires_str = json_get(obj, "expires_at");
long expires_at = expires_str.empty() ? 0 : std::stol(expires_str);
long now = static_cast<long>(std::time(nullptr));
long remaining = expires_at - now;

std::string expiry_warning;
if (expires_at > 0 && remaining > 0) {
    expiry_warning = Color::red + " [expires in " + std::to_string(remaining) + "s]" + Color::reset;
} else if (expires_at > 0 && remaining <= 0) {
    expiry_warning = Color::red + " [expired]" + Color::reset;
}

std::cout << Color::gray  << "  #" << Color::reset
          << Color::bold  << id_str << Color::reset
          << Color::gray  << "  " << format_time(timestamp) << "  "
          << Color::reset << type_badge(type) << "  "
          << (is_pinned ? Color::yellow + "* " + Color::reset : "  ")
          << preview(content)
          << expiry_warning
          << "\n";
    }

    return 0;
}

int cmd_stats(const Config& cfg) {
    if (!require_daemon(cfg)) return 1;

    auto resp = send_cmd(cfg, "STATS");
    if (resp.status != 0) {
        std::cerr << Color::red << "Error: " << resp.data << Color::reset << "\n";
        return 1;
    }

    // parse key=value response
    std::map<std::string, std::string> values;
    std::istringstream ss(std::string(resp.data, resp.data_len));
    std::string line;
    while (std::getline(ss, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        values[line.substr(0, eq)] = line.substr(eq + 1);
    }

    // format DB size
    long db_bytes = values.count("db_bytes") ? std::stol(values["db_bytes"]) : 0;
    std::string db_size;
    if (db_bytes < 1024)
        db_size = std::to_string(db_bytes) + " B";
    else if (db_bytes < 1024 * 1024)
        db_size = std::to_string(db_bytes / 1024) + " KB";
    else
        db_size = std::to_string(db_bytes / (1024 * 1024)) + " MB";

    // format most active hour
    int hour = values.count("active_hour") ? std::stoi(values["active_hour"]) : 0;
    std::string hour_str = std::to_string(hour) + ":00 - "
                         + std::to_string(hour + 1) + ":00";

    // get type counts
    int urls    = values.count("type_url")    ? std::stoi(values["type_url"])    : 0;
    int paths   = values.count("type_path")   ? std::stoi(values["type_path"])   : 0;
    int codes   = values.count("type_code")   ? std::stoi(values["type_code"])   : 0;
    int secrets = values.count("type_secret") ? std::stoi(values["type_secret"]) : 0;
    int texts   = values.count("type_text")   ? std::stoi(values["type_text"])   : 0;
    int total   = values.count("total")       ? std::stoi(values["total"])       : 1;

    // calculate percentages
    auto pct = [&](int n) {
        return std::to_string(total > 0 ? (n * 100 / total) : 0) + "%";
    };

    std::cout << Color::bold << Color::blue
              << "\n  ClipForge Stats\n"
              << "  ─────────────────────────────────────────────────────────\n"
              << Color::reset;

    std::cout << "  Total items:     " << Color::bold
              << (values.count("total") ? values["total"] : "0")
              << Color::reset << "\n";

    std::cout << "  Pinned:          " << Color::yellow
              << (values.count("pinned") ? values["pinned"] : "0")
              << Color::reset << "\n";

    std::cout << "  Snippets:        " << Color::cyan
              << (values.count("snippets") ? values["snippets"] : "0")
              << Color::reset << "\n";

    std::cout << "  DB size:         " << Color::gray
              << db_size << Color::reset << "\n";

    std::cout << "  Most active:     " << Color::green
              << hour_str << Color::reset << "\n";

    std::cout << "\n";
    std::cout << "  " << Color::cyan    << "[URL]  " << Color::reset
              << urls    << "  " << Color::gray << pct(urls)    << Color::reset << "\n";
    std::cout << "  " << Color::yellow  << "[PATH] " << Color::reset
              << paths   << "  " << Color::gray << pct(paths)   << Color::reset << "\n";
    std::cout << "  " << Color::magenta << "[CODE] " << Color::reset
              << codes   << "  " << Color::gray << pct(codes)   << Color::reset << "\n";
    std::cout << "  " << Color::red     << "[SEC]  " << Color::reset
              << secrets << "  " << Color::gray << pct(secrets) << Color::reset << "\n";
    std::cout << "  " << Color::gray    << "[TEXT] " << Color::reset
              << texts   << "  " << Color::gray << pct(texts)   << Color::reset << "\n";

    std::cout << "\n";
    return 0;
}

int cmd_get(const Config& cfg, const std::string& id_or_name) {
    if (!require_daemon(cfg)) return 1;

    bool is_number = !id_or_name.empty() &&
                     id_or_name.find_first_not_of("0123456789") == std::string::npos;

    Response resp;
    if (is_number) {
        resp = send_cmd(cfg, "GET", id_or_name);
    } else {
        resp = send_cmd(cfg, "SNIPS");
        if (resp.status != 0) {
            std::cerr << Color::red << "Error: " << resp.data << Color::reset << "\n";
            return 1;
        }
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
        std::string type      = json_get(obj, "type");
        std::string timestamp = json_get(obj, "timestamp");
        std::string content   = json_get(obj, "content");

        std::string id_str = std::string(3 - std::min((int)id.size(), 3), ' ') + id;

        std::cout << Color::gray  << "  #" << Color::reset
                  << Color::bold  << id_str << Color::reset
                  << Color::gray  << "  " << format_time(timestamp) << "  "
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
        std::string name    = json_get(obj, "name");
        std::string content = json_get(obj, "content");

        std::cout << Color::bold << "  " << name << Color::reset
                  << Color::gray << "  →  " << Color::reset
                  << preview(content) << "\n";
    }

    return 0;
}



void print_usage() {
    std::cout << Color::bold << "\nClipForge — clipboard manager\n\n" << Color::reset;
    std::cout << Color::bold << "Usage:\n" << Color::reset;
    std::cout << "  clip list              " << Color::gray << "show clipboard history\n"          << Color::reset;
    std::cout << "  clip get <id>          " << Color::gray << "copy item #id to clipboard\n"     << Color::reset;
    std::cout << "  clip get <name>        " << Color::gray << "copy named snippet to clipboard\n" << Color::reset;
    std::cout << "  clip search <query>    " << Color::gray << "search history\n"                 << Color::reset;
    std::cout << "  clip delete <id>       " << Color::gray << "delete item #id\n"                << Color::reset;
    std::cout << "  clip clear             " << Color::gray << "clear all history\n"              << Color::reset;
    std::cout << "  clip pin <id>          " << Color::gray << "pin item (never evicted)\n"       << Color::reset;
    std::cout << "  clip unpin <id>        " << Color::gray << "unpin item\n"                     << Color::reset;
    std::cout << "  clip save <name>       " << Color::gray << "save clipboard as snippet\n"      << Color::reset;
    std::cout << "  clip snippets          " << Color::gray << "list saved snippets\n"            << Color::reset;
    std::cout << "  clip stats             " << Color::gray << "show usage statistics\n"          << Color::reset;
    std::cout << "  clip daemon start      " << Color::gray << "start the daemon\n"               << Color::reset;
    std::cout << "  clip daemon stop       " << Color::gray << "stop the daemon\n"                << Color::reset;
    std::cout << "  clip daemon status     " << Color::gray << "check daemon status\n"            << Color::reset;
    std::cout << "  clip ui                " << Color::gray << "launch interactive TUI\n" << Color::reset;
    std::cout << "\n";
}