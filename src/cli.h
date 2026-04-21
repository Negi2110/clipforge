#pragma once
#include <string>
#include "config.h"

// all CLI commands live here
// each function connects to daemon, sends command, prints result
// returns 0 on success, 1 on failure — used as process exit code

int cmd_list   (const Config& cfg, int limit = 50);
int cmd_get    (const Config& cfg, const std::string& id_or_name);
int cmd_search (const Config& cfg, const std::string& query);
int cmd_delete (const Config& cfg, int id);
int cmd_clear  (const Config& cfg);
int cmd_pin    (const Config& cfg, int id, bool pin);
int cmd_save   (const Config& cfg, const std::string& name);
int cmd_snippets(const Config& cfg);
int cmd_stats  (const Config& cfg);

// print usage instructions
void print_usage();