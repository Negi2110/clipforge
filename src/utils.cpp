#include "utils.h"
#include <regex>
#include <algorithm>

std::string detect_type(const std::string& content) {
    if (content.empty()) return "text";

    // --- URL ---
    // must start with http:// or https://
    if (content.substr(0, 7) == "http://" ||
        content.substr(0, 8) == "https://") {
        return "url";
    }

    // --- file path ---
    // starts with / or ~/ and contains no spaces (paths with spaces are rare in practice)
    if ((content[0] == '/' || content.substr(0, 2) == "~/") &&
        content.find(' ') == std::string::npos &&
        content.find('\n') == std::string::npos) {
        return "path";
    }

    // --- secret detection ---
    // hex string: only 0-9 a-f A-F, length 16-128
    // matches API keys, hashes, tokens
    static const std::regex hex_re("^[0-9a-fA-F]{16,128}$");
    if (std::regex_match(content, hex_re)) {
        return "secret";
    }

    // base64 string: alphanumeric + / + = padding, length 20-200
    // matches JWT tokens, encoded keys, passwords
    static const std::regex b64_re("^[A-Za-z0-9+/]{20,200}={0,2}$");
    if (std::regex_match(content, b64_re)) {
        return "secret";
    }

    // --- code detection ---
    // multiline content with code-like characters
    bool has_newline    = content.find('\n') != std::string::npos;
    bool has_semicolon  = content.find(';')  != std::string::npos;
    bool has_brace      = content.find('{')  != std::string::npos ||
                          content.find('}')  != std::string::npos;
    bool has_indent     = content.find("    ") != std::string::npos ||
                          content.find('\t')   != std::string::npos;

    if (has_newline && (has_semicolon || has_brace || has_indent)) {
        return "code";
    }

    return "text";
}

bool is_sensitive(const std::string& content) {
    return detect_type(content) == "secret";
}