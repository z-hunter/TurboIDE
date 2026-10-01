#pragma once

#include <cctype>
#include <string>

namespace gdbmi {
inline std::string stringAttribute(const std::string &record, const std::string &name) {
    const std::string key = name + "=\"";
    size_t pos = record.find(key);
    if (pos == std::string::npos) return {};
    pos += key.size();
    std::string value;
    for (; pos < record.size(); ++pos) {
        const char ch = record[pos];
        if (ch == '"') break;
        if (ch != '\\' || pos + 1 == record.size()) { value += ch; continue; }
        const char escaped = record[++pos];
        switch (escaped) {
        case 'a': value += '\a'; break;
        case 'b': value += '\b'; break;
        case 'f': value += '\f'; break;
        case 'n': value += '\n'; break;
        case 'r': value += '\r'; break;
        case 't': value += '\t'; break;
        case 'v': value += '\v'; break;
        case 'x': {
            unsigned byte = 0;
            bool found = false;
            while (pos + 1 < record.size() && std::isxdigit(static_cast<unsigned char>(record[pos + 1]))) {
                const char digit = record[++pos];
                byte = byte * 16 + (digit <= '9' ? digit - '0' :
                    (digit | 32) - 'a' + 10);
                found = true;
            }
            value += found ? static_cast<char>(byte) : 'x';
            break;
        }
        default:
            if (escaped >= '0' && escaped <= '7') {
                unsigned byte = static_cast<unsigned>(escaped - '0');
                for (int i = 0; i < 2 && pos + 1 < record.size() &&
                     record[pos + 1] >= '0' && record[pos + 1] <= '7'; ++i)
                    byte = byte * 8 + static_cast<unsigned>(record[++pos] - '0');
                value += static_cast<char>(byte);
            } else value += escaped;
        }
    }
    return value;
}

inline size_t recordEnd(const std::string &text, size_t start) {
    bool quoted = false, escaped = false;
    int depth = 0;
    for (size_t i = start; i < text.size(); ++i) {
        const char ch = text[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
        } else if (ch == '"') quoted = true;
        else if (ch == '{') ++depth;
        else if (ch == '}' && --depth == 0) return i + 1;
    }
    return std::string::npos;
}
}
