#pragma once
#include <string_view>

namespace bth {
// API objects are flat. Bound cJSON recursion before parsing untrusted input;
// reject NULs that would silently truncate credentials in C string consumers.
// cJSON still validates the full JSON grammar after this preflight.
inline bool valid_flat_json_body(std::string_view body) noexcept {
    unsigned depth = 0;
    bool quoted = false;
    bool object = false;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '\0') return false;
        if (quoted) {
            if (c == '\\') {
                if (++i == body.size() || body[i] == '\0') return false;
                if (body.substr(i, 5) == "u0000") return false;
            } else if (c == '"') {
                quoted = false;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == '{') {
            if (++depth > 1) return false;
            object = true;
        } else if (c == '}') {
            if (depth == 0) return false;
            --depth;
        } else if (c == '[' || c == ']') {
            return false;
        }
    }
    return object && depth == 0 && !quoted;
}

inline bool parse_relay_command(std::string_view body, bool& on) noexcept {
    if (body == "true") { on = true; return true; }
    if (body == "false") { on = false; return true; }
    return false;
}
}  // namespace bth
