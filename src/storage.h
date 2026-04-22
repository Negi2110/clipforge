#pragma once
#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>

// represents one row from the history table
struct ClipItem {
    int         id;
    std::string content;
    std::string type;        // "text", "url", "path", "secret"
    time_t      timestamp;
    bool        pinned;
    std::string app;         // which app triggered the copy (future use)
};

// represents one row from the snippets table
struct Snippet {
    int         id;
    std::string name;
    std::string content;
    time_t      created;
};

class Storage {
public:
    // constructor opens the database, creates tables if they dont exist
    // destructor closes the connection — RAII
    explicit Storage(const std::string& db_path);
    ~Storage();

    // Storage should never be copied — it owns a database connection
    Storage(const Storage&)            = delete;
    Storage& operator=(const Storage&) = delete;

    // --- write operations ---
// type is auto-detected if not provided
    int save_item(const std::string& content, const std::string& type = "text");
    void delete_item(int id);
    void clear_history();
    void pin_item(int id, bool pinned);
    void evict_old_items(int max_history);  // delete oldest unpinned rows over the limit

    // --- read operations ---
    std::vector<ClipItem>    get_history(int limit = 50);
    std::optional<ClipItem>  get_item(int id);
    std::vector<ClipItem>    search(const std::string& query);
    bool                     is_duplicate(const std::string& content);

    // --- snippet operations ---
    void                  save_snippet(const std::string& name, const std::string& content);
    void                  delete_snippet(const std::string& name);
    std::vector<Snippet>  get_snippets();
    std::optional<Snippet> get_snippet(const std::string& name);

private:
    void create_tables();   // called once in constructor to set up schema

    sqlite3* db_ = nullptr; // raw SQLite connection — owned by this class
};
