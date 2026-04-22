#pragma once
#include <string>

// detects content type from clipboard content
// stored in DB at save time — not recomputed on display
std::string detect_type(const std::string& content);

// returns true if content looks sensitive — password, token, key
bool is_sensitive(const std::string& content);