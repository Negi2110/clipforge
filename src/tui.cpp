#include "tui.h"
#include "ipc.h"
#include "daemon.h"
#include "logger.h"

#include <ncurses.h>
#include <string>
#include <vector>
#include <ctime>

// ── data ────────────────────────────────────────────────────────────────────

struct TuiItem {
    int         id;
    std::string content;
    std::string type;
    std::string timestamp;
    bool        pinned;
};

// ── helpers ──────────────────────────────────────────────────────────────────

// reuse the same json_get logic from cli.cpp
static std::string tui_json_get(const std::string& obj, const std::string& key) {
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

static std::vector<std::string> tui_parse_array(const std::string& json) {
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
            if (depth == 0)
                objects.push_back(json.substr(start, i - start + 1));
        }
    }
    return objects;
}

// send one IPC command and return response
static Response tui_send(const Config& cfg,
                         const std::string& cmd,
                         const std::string& payload = "") {
    IPCClient client;
    if (!client.connect(cfg.socket_path))
        return make_response(1, "Cannot connect to daemon");
    auto resp = client.send(make_message(cmd, payload));
    client.disconnect();
    return resp;
}

// load items from daemon into vector
static std::vector<TuiItem> load_items(const Config& cfg, const std::string& query = "") {
    std::vector<TuiItem> items;

    Response resp;
    if (query.empty())
        resp = tui_send(cfg, "LIST", "100");
    else
        resp = tui_send(cfg, "SEARCH", query);

    if (resp.status != 0) return items;

    auto objects = tui_parse_array(std::string(resp.data));
    for (const auto& obj : objects) {
        TuiItem item;
        std::string id_str = tui_json_get(obj, "id");
        item.id        = id_str.empty() ? 0 : std::stoi(id_str);
        item.content   = tui_json_get(obj, "content");
        item.type      = tui_json_get(obj, "type");
        item.timestamp = tui_json_get(obj, "timestamp");
        item.pinned    = tui_json_get(obj, "pinned") == "true";
        items.push_back(item);
    }
    return items;
}

// format timestamp for display
static std::string tui_format_time(const std::string& ts_str) {
    if (ts_str.empty()) return "unknown";
    try {
        long ts = std::stol(ts_str);
        time_t t = static_cast<time_t>(ts);
        struct tm* tm_info = std::localtime(&t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%d %b %H:%M", tm_info);
        return std::string(buf);
    } catch (...) { return "unknown"; }
}

// type indicator character
static std::string type_indicator(const std::string& type) {
    if (type == "url")    return "U";
    if (type == "path")   return "P";
    if (type == "secret") return "S";
    if (type == "code")   return "C";
    return "T";
}

// ── color pairs ──────────────────────────────────────────────────────────────
// define all color pairs in one place
#define CP_HEADER    1   // blue on black — header bar
#define CP_SELECTED  2   // black on white — selected item
#define CP_PINNED    3   // yellow on black — pinned marker
#define CP_TYPE_U    4   // cyan on black — URL
#define CP_TYPE_P    5   // yellow on black — path
#define CP_TYPE_C    6   // magenta on black — code
#define CP_TYPE_S    7   // red on black — secret
#define CP_TYPE_T    8   // white on black — text
#define CP_STATUSBAR 9   // black on white — status bar
#define CP_PREVIEW   10  // white on black — preview panel
#define CP_BORDER    11  // blue on black — borders

static void init_colors() {
    start_color();
    use_default_colors();
    init_pair(CP_HEADER,    COLOR_BLUE,    -1);
    init_pair(CP_SELECTED,  COLOR_BLACK,   COLOR_WHITE);
    init_pair(CP_PINNED,    COLOR_YELLOW,  -1);
    init_pair(CP_TYPE_U,    COLOR_CYAN,    -1);
    init_pair(CP_TYPE_P,    COLOR_YELLOW,  -1);
    init_pair(CP_TYPE_C,    COLOR_MAGENTA, -1);
    init_pair(CP_TYPE_S,    COLOR_RED,     -1);
    init_pair(CP_TYPE_T,    COLOR_WHITE,   -1);
    init_pair(CP_STATUSBAR, COLOR_BLACK,   COLOR_WHITE);
    init_pair(CP_PREVIEW,   COLOR_WHITE,   -1);
    init_pair(CP_BORDER,    COLOR_BLUE,    -1);
}

// get color pair for a type string
static int type_color(const std::string& type) {
    if (type == "url")    return CP_TYPE_U;
    if (type == "path")   return CP_TYPE_P;
    if (type == "code")   return CP_TYPE_C;
    if (type == "secret") return CP_TYPE_S;
    return CP_TYPE_T;
}

// ── drawing functions ────────────────────────────────────────────────────────

// draw the header bar at top of screen
static void draw_header(WINDOW* win, int width, const std::string& search_query) {
    werase(win);
    wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);

    if (search_query.empty()) {
        mvwprintw(win, 0, 0, " ClipForge");
        mvwprintw(win, 0, width - 20, "Press ? for help");
    } else {
        mvwprintw(win, 0, 0, " Search: %s", search_query.c_str());
        mvwprintw(win, 0, width - 14, "ESC to clear");
    }

    wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    wrefresh(win);
}

// draw the list panel — left side
static void draw_list(WINDOW* win,
                      const std::vector<TuiItem>& items,
                      int selected,
                      int scroll_offset,
                      int height,
                      int width) {
    werase(win);

    // draw border
    wattron(win, COLOR_PAIR(CP_BORDER));
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " History ");
    wattroff(win, COLOR_PAIR(CP_BORDER));

    int list_height = height - 2;  // subtract border rows
    int visible_start = scroll_offset;
    int visible_end   = std::min((int)items.size(), scroll_offset + list_height);

    for (int i = visible_start; i < visible_end; i++) {
        int row = i - visible_start + 1;  // +1 for top border
        const auto& item = items[i];

        bool is_selected = (i == selected);

        if (is_selected) {
            wattron(win, COLOR_PAIR(CP_SELECTED) | A_BOLD);
        }

        // clear the row first
        mvwhline(win, row, 1, ' ', width - 2);

        // pinned marker
        if (item.pinned) {
            wattron(win, COLOR_PAIR(CP_PINNED));
            mvwprintw(win, row, 1, "*");
            wattroff(win, COLOR_PAIR(CP_PINNED));
        } else {
            mvwprintw(win, row, 1, " ");
        }

        // type indicator with color
        if (!is_selected) {
            wattron(win, COLOR_PAIR(type_color(item.type)));
        }
        mvwprintw(win, row, 2, "%s", type_indicator(item.type).c_str());
        if (!is_selected) {
            wattroff(win, COLOR_PAIR(type_color(item.type)));
        }

        // id
        mvwprintw(win, row, 4, "#%-4d", item.id);

        // content preview — truncate to fit width
        std::string preview = item.content;
        // show first line only
        auto nl = preview.find('\n');
        if (nl != std::string::npos) preview = preview.substr(0, nl) + "~";

        int max_content = width - 12;
        if ((int)preview.size() > max_content)
            preview = preview.substr(0, max_content - 1) + "…";

        mvwprintw(win, row, 9, "%s", preview.c_str());

        if (is_selected) {
            wattroff(win, COLOR_PAIR(CP_SELECTED) | A_BOLD);
        }
    }

    // scroll indicator on right border
    if ((int)items.size() > list_height) {
        int scroll_pct = (scroll_offset * 100) / ((int)items.size() - list_height);
        int indicator_row = 1 + (scroll_pct * (list_height - 1) / 100);
        wattron(win, COLOR_PAIR(CP_BORDER));
        mvwprintw(win, indicator_row, width - 1, "█");
        wattroff(win, COLOR_PAIR(CP_BORDER));
    }

    wrefresh(win);
}

// draw the preview panel — right side
static void draw_preview(WINDOW* win,
                         const std::vector<TuiItem>& items,
                         int selected,
                         int height,
                         int width) {
    werase(win);

    wattron(win, COLOR_PAIR(CP_BORDER));
    box(win, 0, 0);
    wattroff(win, COLOR_PAIR(CP_BORDER));

    if (items.empty() || selected >= (int)items.size()) {
        mvwprintw(win, 0, 2, " Preview ");
        wrefresh(win);
        return;
    }

    const auto& item = items[selected];

    // header with type and time
    std::string header = " " + item.type + "  " + tui_format_time(item.timestamp) + " ";
    wattron(win, COLOR_PAIR(CP_BORDER));
    mvwprintw(win, 0, 2, "%s", header.c_str());
    wattroff(win, COLOR_PAIR(CP_BORDER));

    // content — word wrapped
    wattron(win, COLOR_PAIR(CP_PREVIEW));
    int row = 1;
    int col = 1;
    int max_col = width - 2;
    int max_row = height - 2;

    for (char c : item.content) {
        if (row > max_row) break;

        if (c == '\n') {
            row++;
            col = 1;
            continue;
        }

        if (col > max_col) {
            row++;
            col = 1;
            if (row > max_row) break;
        }

        mvwaddch(win, row, col, c);
        col++;
    }

    wattroff(win, COLOR_PAIR(CP_PREVIEW));
    wrefresh(win);
}

// draw the status bar at bottom
static void draw_statusbar(WINDOW* win, int width,
                           const std::vector<TuiItem>& items,
                           int selected, const std::string& message) {
    werase(win);
    wattron(win, COLOR_PAIR(CP_STATUSBAR));

    // fill entire bar
    mvwhline(win, 0, 0, ' ', width);

    if (!message.empty()) {
        // show temporary message (copy confirmation etc)
        mvwprintw(win, 0, 1, "%s", message.c_str());
    } else {
        // show keyboard shortcuts
        mvwprintw(win, 0, 1,
    "j/k:move  Enter:copy  d:delete  p:pin  /:search  h:help  q:quit");

        // show position
        if (!items.empty()) {
            std::string pos = std::to_string(selected + 1) + "/"
                            + std::to_string(items.size());
            mvwprintw(win, 0, width - (int)pos.size() - 1, "%s", pos.c_str());
        }
    }

    wattroff(win, COLOR_PAIR(CP_STATUSBAR));
    wrefresh(win);
}

// ── main TUI loop ─────────────────────────────────────────────────────────────

int run_tui(const Config& cfg) {
    if (!Daemon::is_running(cfg.pid_path)) {
        fprintf(stderr, "Error: daemon is not running. Start with: clip daemon start\n");
        return 1;
    }

    // initialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);          // hide cursor
    set_escdelay(50);     // faster ESC response

    if (!has_colors()) {
        endwin();
        fprintf(stderr, "Terminal does not support colors\n");
        return 1;
    }

    init_colors();

    // get terminal dimensions
    int term_height, term_width;
    getmaxyx(stdscr, term_height, term_width);

    // layout:
    // row 0          = header bar (1 line)
    // rows 1..H-2    = list panel (left) + preview panel (right)
    // row H-1        = status bar (1 line)
    int header_height  = 1;
    int statusbar_height = 1;
    int panel_height   = term_height - header_height - statusbar_height;
    int list_width     = term_width / 2;
    int preview_width  = term_width - list_width;

    // create windows
    WINDOW* header_win  = newwin(header_height, term_width, 0, 0);
    WINDOW* list_win    = newwin(panel_height, list_width, header_height, 0);
    WINDOW* preview_win = newwin(panel_height, preview_width, header_height, list_width);
    WINDOW* status_win  = newwin(statusbar_height, term_width, term_height - 1, 0);

    // load initial data
    std::vector<TuiItem> items = load_items(cfg);
    int selected      = 0;
    int scroll_offset = 0;
    int list_height   = panel_height - 2;  // subtract borders
    std::string search_query;
    std::string status_message;
    int message_timer = 0;  // countdown to clear message

    // initial draw
    draw_header(header_win, term_width, search_query);
    draw_list(list_win, items, selected, scroll_offset, panel_height, list_width);
    draw_preview(preview_win, items, selected, panel_height, preview_width);
    draw_statusbar(status_win, term_width, items, selected, status_message);

    // ── main event loop ──────────────────────────────────────────────────────
    bool running = true;
    while (running) {
        int ch = wgetch(list_win);

        // clear status message after a few keystrokes
        if (message_timer > 0) {
            message_timer--;
            if (message_timer == 0) {
                status_message = "";
            }
        }

        switch (ch) {
            // ── navigation ──────────────────────────────────────────────────
            case 'j':
            case KEY_DOWN:
                if (selected < (int)items.size() - 1) {
                    selected++;
                    // scroll down if selected goes below visible area
                    if (selected >= scroll_offset + list_height) {
                        scroll_offset++;
                    }
                }
                break;

            case 'k':
            case KEY_UP:
                if (selected > 0) {
                    selected--;
                    // scroll up if selected goes above visible area
                    if (selected < scroll_offset) {
                        scroll_offset--;
                    }
                }
                break;

            case 'g':  // go to top
                selected = 0;
                scroll_offset = 0;
                break;

            case 'G':  // go to bottom
                selected = (int)items.size() - 1;
                scroll_offset = std::max(0, (int)items.size() - list_height);
                break;

            // ── quit ────────────────────────────────────────────────────────
            case 'q':
            case 'Q':
                running = false;
                break;

            // ── reload ──────────────────────────────────────────────────────
            case 'r':
            case KEY_F(5): {
                items = load_items(cfg, search_query);
                selected = std::min(selected, (int)items.size() - 1);
                if (selected < 0) selected = 0;
                scroll_offset = std::min(scroll_offset,
                    std::max(0, (int)items.size() - list_height));
                status_message = "Refreshed";
                message_timer  = 3;
                break;
            }

            // ── search ──────────────────────────────────────────────────────
            case '/': {
                // enter search mode — show cursor and read input
                curs_set(1);
                echo();

                char query_buf[256] = {0};
                // draw search prompt in status bar
                werase(status_win);
                wattron(status_win, COLOR_PAIR(CP_STATUSBAR));
                mvwhline(status_win, 0, 0, ' ', term_width);
                mvwprintw(status_win, 0, 1, "Search: ");
                wattroff(status_win, COLOR_PAIR(CP_STATUSBAR));
                wrefresh(status_win);

                // read search query
                mvwgetnstr(status_win, 0, 9, query_buf, 255);

                noecho();
                curs_set(0);

                search_query = std::string(query_buf);
                items = load_items(cfg, search_query);
                selected = 0;
                scroll_offset = 0;

                draw_header(header_win, term_width, search_query);
                break;
            }

            case 27:  // ESC — clear search
                search_query = "";
                items = load_items(cfg);
                selected = 0;
                scroll_offset = 0;
                draw_header(header_win, term_width, search_query);
                break;

            // ── help ────────────────────────────────────────────────────────
            case 'h':
            case '?': {
                werase(status_win);
                wattron(status_win, COLOR_PAIR(CP_STATUSBAR));
                mvwhline(status_win, 0, 0, ' ', term_width);
                mvwprintw(status_win, 0, 1,
                    "j/k:move  Enter:copy  d:delete  p:pin  /:search  r:refresh  g/G:top/bottom  q:quit");
                wattroff(status_win, COLOR_PAIR(CP_STATUSBAR));
                wrefresh(status_win);
                wgetch(list_win);  // wait for any key
                break;
            }

            // ── terminal resize ─────────────────────────────────────────────
            case KEY_RESIZE: {
                getmaxyx(stdscr, term_height, term_width);
                panel_height  = term_height - header_height - statusbar_height;
                list_width    = term_width / 2;
                preview_width = term_width - list_width;
                list_height   = panel_height - 2;

                wresize(header_win,  header_height,    term_width);
                wresize(list_win,    panel_height,      list_width);
                wresize(preview_win, panel_height,      preview_width);
                wresize(status_win,  statusbar_height,  term_width);

                mvwin(preview_win, header_height, list_width);
                mvwin(status_win,  term_height - 1, 0);

                clearok(stdscr, TRUE);
                break;
            }

// ── copy to clipboard ────────────────────────────────────────
            case '\n':
            case KEY_ENTER: {
                if (items.empty()) break;
                const auto& item = items[selected];

                // send GET to daemon — returns raw content
                auto resp = tui_send(cfg, "GET", std::to_string(item.id));
                if (resp.status == 0) {
                    // pipe content to wl-copy or xclip
                    std::string content(resp.data, resp.data_len);
                    FILE* pipe = popen("wl-copy 2>/dev/null || xclip -selection clipboard", "w");
                    if (pipe) {
                        fwrite(content.c_str(), 1, content.size(), pipe);
                        pclose(pipe);
                        status_message = "✓ Copied to clipboard: " + content.substr(0, 40);
                        if (content.size() > 40) status_message += "...";
                    } else {
                        status_message = "✗ Failed to copy";
                    }
                } else {
                    status_message = "✗ Error: " + std::string(resp.data);
                }
                message_timer = 4;
                break;
            }

            // ── delete item ──────────────────────────────────────────────
            case 'd': {
                if (items.empty()) break;
                int id = items[selected].id;

                auto resp = tui_send(cfg, "DELETE", std::to_string(id));
                if (resp.status == 0) {
                    status_message = "✓ Deleted item #" + std::to_string(id);
                    // reload items and adjust selection
                    items = load_items(cfg, search_query);
                    if (selected >= (int)items.size())
                        selected = std::max(0, (int)items.size() - 1);
                    scroll_offset = std::min(scroll_offset,
                        std::max(0, (int)items.size() - list_height));
                } else {
                    status_message = "✗ Failed to delete";
                }
                message_timer = 4;
                break;
            }

            // ── pin / unpin ──────────────────────────────────────────────
            case 'p': {
                if (items.empty()) break;
                auto& item = items[selected];
                bool new_pin = !item.pinned;

                std::string payload = std::to_string(item.id)
                                    + ":" + (new_pin ? "1" : "0");
                auto resp = tui_send(cfg, "PIN", payload);

                if (resp.status == 0) {
                    item.pinned = new_pin;  // update local state immediately
                    status_message = new_pin
                        ? "✓ Pinned item #" + std::to_string(item.id)
                        : "✓ Unpinned item #" + std::to_string(item.id);
                } else {
                    status_message = "✗ Failed to pin";
                }
                message_timer = 4;
                break;
            }

            // ── yank (copy without moving) ───────────────────────────────
            case 'y': {
                if (items.empty()) break;
                const auto& item = items[selected];
                FILE* pipe = popen("wl-copy 2>/dev/null || xclip -selection clipboard", "w");
                if (pipe) {
                    fwrite(item.content.c_str(), 1, item.content.size(), pipe);
                    pclose(pipe);
                    status_message = "✓ Yanked: " + item.content.substr(0, 40);
                    if (item.content.size() > 40) status_message += "...";
                } else {
                    status_message = "✗ Failed to yank";
                }
                message_timer = 4;
                break;
            }

            default:
                break;
        }

        // redraw all panels after every keypress
        draw_header(header_win, term_width, search_query);
        draw_list(list_win, items, selected, scroll_offset, panel_height, list_width);
        draw_preview(preview_win, items, selected, panel_height, preview_width);
        draw_statusbar(status_win, term_width, items, selected, status_message);
    }

    // cleanup — restore terminal
    delwin(header_win);
    delwin(list_win);
    delwin(preview_win);
    delwin(status_win);
    endwin();

    return 0;
}