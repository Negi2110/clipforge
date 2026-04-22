#include "storage.h"
#include "logger.h"
#include <iostream>
#include <stdexcept>
#include "utils.h"

// --- constructor and destructor ---

Storage::Storage(const std::string& db_path) {
    // SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE:
    // open existing db for read/write, or create it if it doesnt exist
    int rc = sqlite3_open_v2(
        db_path.c_str(),
        &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
        nullptr
    );

    if (rc != SQLITE_OK) {
        // sqlite3_errmsg gives human readable error string
        std::string err = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        // throw so the caller knows construction failed
        // we cannot log here — logger might not be initialized yet
        throw std::runtime_error("Failed to open database: " + err);
    }

    Log::info("Database opened: " + db_path);
    create_tables();
}

Storage::~Storage() {
    // RAII — destructor always runs, connection always closes
    // even if an exception was thrown somewhere else in the program
    if (db_) {
        sqlite3_close(db_);
        Log::info("Database connection closed");
    }
}

// --- private: create tables ---

void Storage::create_tables() {
    // IF NOT EXISTS makes this safe to call every time — idempotent.
    // running this on an existing database does nothing.
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS history (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            content   TEXT    NOT NULL,
            type      TEXT    NOT NULL DEFAULT 'text',
            timestamp INTEGER NOT NULL,
            pinned    INTEGER NOT NULL DEFAULT 0,
            app       TEXT    NOT NULL DEFAULT ''
        );

        CREATE TABLE IF NOT EXISTS snippets (
            id      INTEGER PRIMARY KEY AUTOINCREMENT,
            name    TEXT    NOT NULL UNIQUE,
            content TEXT    NOT NULL,
            created INTEGER NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_history_timestamp
            ON history(timestamp DESC);
    )";

    // sqlite3_exec runs SQL that returns no rows — perfect for CREATE TABLE
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);

    if (rc != SQLITE_OK) {
        std::string err = err_msg;
        sqlite3_free(err_msg);  // must free this manually — C API
        throw std::runtime_error("Failed to create tables: " + err);
    }

    Log::info("Database schema ready");
}

// --- write operations ---

int Storage::save_item(const std::string& content, const std::string& type) {
    if (is_duplicate(content)) {
        Log::debug("Skipping duplicate item");
        return -1;
    }

    // auto-detect type if caller passed default "text"
    // this means daemon's watch_clipboard never needs to think about types
    std::string actual_type = (type == "text") ? detect_type(content) : type;

    const char* sql = "INSERT INTO history (content, type, timestamp) VALUES (?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        Log::error("Failed to prepare insert: " + std::string(sqlite3_errmsg(db_)));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, content.c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, actual_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(std::time(nullptr)));

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        Log::error("Failed to insert item: " + std::string(sqlite3_errmsg(db_)));
        return -1;
    }

    int new_id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    Log::debug("Saved item id=" + std::to_string(new_id) + " type=" + actual_type);
    return new_id;
}

bool Storage::is_duplicate(const std::string& content) {
    // only check the most recently inserted item —
    // copying the same thing twice in a row should not create two entries
    // but copying A, then B, then A again should create a new entry for the second A
    const char* sql = "SELECT content FROM history ORDER BY id DESC LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    bool duplicate = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // sqlite3_column_text returns the text value of column 0
        // reinterpret_cast needed because SQLite returns const unsigned char*
        const char* last = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (last && content == last) {
            duplicate = true;
        }
    }

    sqlite3_finalize(stmt);
    return duplicate;
}

void Storage::delete_item(int id) {
    const char* sql = "DELETE FROM history WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    Log::debug("Deleted item id=" + std::to_string(id));
}

void Storage::clear_history() {
    // DELETE without WHERE deletes all rows but keeps the table structure
    const char* sql = "DELETE FROM history WHERE pinned = 0";
    sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
    Log::info("History cleared (pinned items preserved)");
}

void Storage::pin_item(int id, bool pinned) {
    const char* sql = "UPDATE history SET pinned = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, pinned ? 1 : 0);
    sqlite3_bind_int(stmt, 2, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Storage::evict_old_items(int max_history) {
    // delete oldest unpinned rows when total count exceeds max_history.
    // pinned items are never evicted — user explicitly protected them.
    // subquery counts total rows and calculates how many to delete.
    const char* sql = R"(
        DELETE FROM history
        WHERE pinned = 0
        AND id IN (
            SELECT id FROM history
            WHERE pinned = 0
            ORDER BY id ASC
            LIMIT MAX(0, (SELECT COUNT(*) FROM history) - ?)
        )
    )";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, max_history);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// --- read operations ---

std::vector<ClipItem> Storage::get_history(int limit) {
    const char* sql = R"(
        SELECT id, content, type, timestamp, pinned, app
        FROM history
        ORDER BY id DESC
        LIMIT ?
    )";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, limit);

    std::vector<ClipItem> items;

    // sqlite3_step returns SQLITE_ROW while there are rows to read
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ClipItem item;
        item.id        = sqlite3_column_int(stmt, 0);

        // sqlite3_column_text returns nullptr if the column is NULL
        // always guard against nullptr before constructing std::string
        auto content   = sqlite3_column_text(stmt, 1);
        auto type      = sqlite3_column_text(stmt, 2);
        auto app       = sqlite3_column_text(stmt, 5);

        item.content   = content ? reinterpret_cast<const char*>(content) : "";
        item.type      = type    ? reinterpret_cast<const char*>(type)    : "text";
        item.timestamp = static_cast<time_t>(sqlite3_column_int64(stmt, 3));
        item.pinned    = sqlite3_column_int(stmt, 4) != 0;
        item.app       = app     ? reinterpret_cast<const char*>(app)     : "";

        items.push_back(item);
    }

    sqlite3_finalize(stmt);
    return items;
}

std::optional<ClipItem> Storage::get_item(int id) {
    const char* sql = R"(
        SELECT id, content, type, timestamp, pinned, app
        FROM history WHERE id = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, id);

    // std::optional — represents a value that may or may not exist.
    // returning nullptr would require a pointer, returning a sentinel id=-1 is ugly.
    // optional is the clean C++17 way to say "might not have a value"
    std::optional<ClipItem> result;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ClipItem item;
        item.id        = sqlite3_column_int(stmt, 0);
        auto content   = sqlite3_column_text(stmt, 1);
        auto type      = sqlite3_column_text(stmt, 2);
        auto app       = sqlite3_column_text(stmt, 5);
        item.content   = content ? reinterpret_cast<const char*>(content) : "";
        item.type      = type    ? reinterpret_cast<const char*>(type)    : "text";
        item.timestamp = static_cast<time_t>(sqlite3_column_int64(stmt, 3));
        item.pinned    = sqlite3_column_int(stmt, 4) != 0;
        item.app       = app     ? reinterpret_cast<const char*>(app)     : "";
        result = item;
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<ClipItem> Storage::search(const std::string& query) {
    // LIKE with % wildcards — %query% means "contains query anywhere"
    // case insensitive by default for ASCII in SQLite
    const char* sql = R"(
        SELECT id, content, type, timestamp, pinned, app
        FROM history
        WHERE content LIKE ?
        ORDER BY id DESC
        LIMIT 50
    )";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    // wrap query in % wildcards for contains search
    std::string pattern = "%" + query + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<ClipItem> items;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ClipItem item;
        item.id        = sqlite3_column_int(stmt, 0);
        auto content   = sqlite3_column_text(stmt, 1);
        auto type      = sqlite3_column_text(stmt, 2);
        auto app       = sqlite3_column_text(stmt, 5);
        item.content   = content ? reinterpret_cast<const char*>(content) : "";
        item.type      = type    ? reinterpret_cast<const char*>(type)    : "text";
        item.timestamp = static_cast<time_t>(sqlite3_column_int64(stmt, 3));
        item.pinned    = sqlite3_column_int(stmt, 4) != 0;
        item.app       = app     ? reinterpret_cast<const char*>(app)     : "";
        items.push_back(item);
    }

    sqlite3_finalize(stmt);
    return items;
}

// --- snippet operations ---

void Storage::save_snippet(const std::string& name, const std::string& content) {
    // INSERT OR REPLACE handles the case where a snippet with this name already exists
    // it replaces the old row — effectively an upsert
    const char* sql = R"(
        INSERT OR REPLACE INTO snippets (name, content, created)
        VALUES (?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(std::time(nullptr)));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    Log::info("Saved snippet: " + name);
}

void Storage::delete_snippet(const std::string& name) {
    const char* sql = "DELETE FROM snippets WHERE name = ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    Log::info("Deleted snippet: " + name);
}

std::vector<Snippet> Storage::get_snippets() {
    const char* sql = "SELECT id, name, content, created FROM snippets ORDER BY name ASC";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    std::vector<Snippet> snippets;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Snippet s;
        s.id      = sqlite3_column_int(stmt, 0);
        auto name = sqlite3_column_text(stmt, 1);
        auto cont = sqlite3_column_text(stmt, 2);
        s.name    = name ? reinterpret_cast<const char*>(name) : "";
        s.content = cont ? reinterpret_cast<const char*>(cont) : "";
        s.created = static_cast<time_t>(sqlite3_column_int64(stmt, 3));
        snippets.push_back(s);
    }

    sqlite3_finalize(stmt);
    return snippets;
}

std::optional<Snippet> Storage::get_snippet(const std::string& name) {
    const char* sql = "SELECT id, name, content, created FROM snippets WHERE name = ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<Snippet> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Snippet s;
        s.id      = sqlite3_column_int(stmt, 0);
        auto nm   = sqlite3_column_text(stmt, 1);
        auto cont = sqlite3_column_text(stmt, 2);
        s.name    = nm   ? reinterpret_cast<const char*>(nm)   : "";
        s.content = cont ? reinterpret_cast<const char*>(cont) : "";
        s.created = static_cast<time_t>(sqlite3_column_int64(stmt, 3));
        result = s;
    }

    sqlite3_finalize(stmt);
    return result;
}