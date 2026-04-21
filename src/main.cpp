#include <iostream>
#include "config.h"
#include "logger.h"
#include "storage.h"

int main() {
    Config cfg = load_config();
    Logger::get().init(cfg.log_path, LogLevel::DEBUG);

    Log::info("ClipForge v0.1 starting");

    Storage storage(cfg.db_path);

    // clear old test data from previous runs so we start fresh
    storage.clear_history();

    // save test items — third one is consecutive duplicate, should be rejected
    storage.save_item("https://github.com/Negi2110/clipforge", "url");
    storage.save_item("sudo apt install cmake", "text");
    storage.save_item("sudo apt install cmake", "text");  // consecutive — should be skipped
    storage.save_item("/home/aman/projects/clipforge", "path");

    // read back — should show 3 items not 4
    auto items = storage.get_history(10);
    std::cout << "\n--- History (" << items.size() << " items) ---\n";
    for (auto& item : items) {
        std::cout << "#" << item.id
                  << " [" << item.type << "] "
                  << item.content << "\n";
    }

    // search test
    auto results = storage.search("apt");
    std::cout << "\n--- Search 'apt' (" << results.size() << " results) ---\n";
    for (auto& item : results) {
        std::cout << "#" << item.id << " " << item.content << "\n";
    }

    // duplicate check — last saved item is path, so checking path should return yes
    std::cout << "\n--- Duplicate check ---\n";
    std::cout << "Is path duplicate: "
              << (storage.is_duplicate("/home/aman/projects/clipforge") ? "yes" : "no")
              << "\n";

    // snippet test
    storage.save_snippet("sshprod", "ssh user@192.168.1.100 -p 22");
    auto snip = storage.get_snippet("sshprod");
    if (snip) {
        std::cout << "\n--- Snippet 'sshprod' ---\n";
        std::cout << snip->content << "\n";
    }

    return 0;
}