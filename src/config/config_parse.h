#pragma once
// The x3m.ini grammar (docs/architecture/config-file.md, section 1): whole-line ';' / '#' comments, [section] lines
// that only group, `key = value` with key [A-Za-z0-9_-]+ (lower-cased, '-' read as '_', the environment name without
// X3M_), the value trimmed with one matching pair of double quotes removed, no escapes and no trailing comments.
// Bounds: 64 KiB per file (the caller refuses a larger one), 512 bytes per line (a longer line is refused alone); a
// line holding a NUL byte is refused alone (problem=nul), never cut at it. The value is checked against the generated
// schema (type, the site's accepted intervals, per-position element ranges, list counts, choices); a bool also takes
// on/off/true/false/yes/no and is normalised to 1/0, a list loses the blanks around its commas; every other value
// reaches the read site as written. An empty value is invalid except for a string or path and a key whose default is
// empty. Pure C++17 with no Windows header, no allocation and no CRT number parsing (numbers are read here with plain
// double arithmetic, SSE2 in the DLL): shared by the DLL's resolver (src/proxy/config.cpp) and the host test
// (verification/analysis/test_config_schema.py). The text is parsed in place: keys and values are NUL-terminated inside
// it.
#include "config_schema_inc.h"

namespace x3m::config::parse {
constexpr unsigned file_limit = 65536, line_limit = 512, issue_limit = 32, key_limit = 64;
enum class Problem : unsigned char { unknown, invalid, duplicate, too_long, env_only, syntax, renamed, nul };
inline const char* problem_name(Problem p) noexcept {
    switch (p) {
    case Problem::unknown: return "unknown";
    case Problem::invalid: return "invalid";
    case Problem::duplicate: return "duplicate";
    case Problem::too_long: return "too_long";
    case Problem::env_only: return "env_only";
    case Problem::syntax: return "syntax";
    case Problem::renamed: return "renamed";
    case Problem::nul: return "nul";
    }
    return "unknown";
}
struct Issue {
    unsigned line = 0;
    Problem problem = Problem::unknown;
    const char* key = "";   // NUL-terminated inside the text (or a literal)
    const char* value = ""; // idem
    int entry = -1;         // the schema entry, when known
};
struct Result {
    const char* value[schema::entry_count] = {}; // the checked value (NUL-terminated inside the text), nullptr = not
                                                 // set
    unsigned line[schema::entry_count] = {};
    unsigned keys = 0;                                                           // entries the file sets
    unsigned unknown = 0, invalid = 0, duplicate = 0, env_only = 0, renamed = 0; // invalid includes too_long, syntax
                                                                                 // and nul
    unsigned lines = 0;
    Issue issues[issue_limit] = {};
    unsigned issue_count = 0, more = 0; // more: issues past the first issue_limit
};

// Lexicographic compare of a name (char or wchar_t, ASCII expected) with a schema string, as unsigned values.
template <class Char> inline int compare(const Char* a, const char* b) noexcept {
    for (;; ++a, ++b) {
        const unsigned x = unsigned(*a), y = unsigned(static_cast<unsigned char>(*b));
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}
// The entry of an environment name (X3M_...), -1 when the schema has none: binary search over the sorted table.
template <class Char> inline int find(const Char* env) noexcept {
    if (!env) return -1;
    int lo = 0, hi = int(schema::entry_count) - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const int c = compare(env, schema::entries[mid].env);
        if (!c) return mid;
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return -1;
}
inline bool equals_word(const char* a, const char* b, unsigned n) noexcept { // a (NUL-terminated) == b[0..n)
    unsigned i = 0;
    for (; i < n; ++i)
        if (a[i] != b[i]) return false;
    return a[n] == 0;
}
inline char lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}
inline bool equals_folded(const char* a, const char* word) noexcept { // case-insensitive, word lower-case
    for (; *word; ++a, ++word)
        if (lower(*a) != *word) return false;
    return *a == 0;
}
// One of the '|'-separated words of `choices`.
inline bool in_choices(const char* value, const char* choices) noexcept {
    for (const char* at = choices; *at;) {
        const char* end = at;
        while (*end && *end != '|') ++end;
        if (end > at && equals_word(value, at, unsigned(end - at))) return true;
        at = *end ? end + 1 : end;
    }
    return false;
}
// [+-]digits[.digits][(e|E)[+-]digits] (at least one mantissa digit; integer: digits only after the sign). Returns the
// end of the number or nullptr.
inline const char* number(const char* p, bool integer, double* out) noexcept {
    bool negative = false;
    if (*p == '+' || *p == '-') {
        negative = *p == '-';
        ++p;
    }
    double value = 0.0;
    unsigned digits = 0;
    for (; *p >= '0' && *p <= '9'; ++p, ++digits) value = value * 10.0 + double(*p - '0');
    if (!integer && *p == '.') {
        double scale = 0.1;
        for (++p; *p >= '0' && *p <= '9'; ++p, ++digits) {
            value += double(*p - '0') * scale;
            scale *= 0.1;
        }
    }
    if (!digits) return nullptr;
    if (!integer && (*p == 'e' || *p == 'E')) {
        ++p;
        bool down = false;
        if (*p == '+' || *p == '-') {
            down = *p == '-';
            ++p;
        }
        unsigned exponent = 0, exponent_digits = 0;
        for (; *p >= '0' && *p <= '9'; ++p, ++exponent_digits)
            if (exponent < 1000) exponent = exponent * 10 + unsigned(*p - '0');
        if (!exponent_digits) return nullptr;
        if (exponent > 400) exponent = 400;
        for (unsigned i = 0; i < exponent; ++i) value = down ? value * 0.1 : value * 10.0;
    }
    *out = negative ? -value : value;
    return p;
}
// v inside one of `count` intervals from `first` (no interval: unchecked).
inline bool in_intervals(unsigned first, unsigned count, double v) noexcept {
    if (!count) return true;
    for (unsigned k = 0; k < count; ++k) {
        const schema::Interval& r = schema::intervals[first + k];
        if ((r.min_open ? v > r.min : v >= r.min) && v <= r.max) return true;
    }
    return false;
}
inline bool in_range(const schema::Entry& e, double v) noexcept {
    return in_intervals(e.range_first, e.range_count, v);
}
// Element `position` of a list: its own ranges when the entry has per-position ranges, else the entry's range.
inline bool element_in_range(const schema::Entry& e, unsigned position, double v) noexcept {
    if (!e.element_count) return in_range(e, v);
    if (position >= e.element_count) return false;
    const schema::ElementRange& r = schema::element_ranges[e.element_first + position];
    return in_intervals(r.first, r.count, v);
}
// Checks (and normalises in place) one value of an entry; false = invalid.
inline bool check(const schema::Entry& e, char* value) noexcept {
    using schema::Type;
    // An empty value is invalid (the default stays), except where empty is itself the documented setting: a string or
    // path ("" = the site's own default, e.g. log_file, pause_key) and a key whose default is empty (hdr_ev_manual).
    if (!*value) return e.type == Type::String || e.type == Type::Path || (e.default_value && !*e.default_value);
    switch (e.type) {
    case Type::Bool: {
        const bool on = !value[1] && value[0] == '1'
                            ? true
                            : equals_folded(value, "on") || equals_folded(value, "true") || equals_folded(value, "yes");
        const bool off = !value[1] && value[0] == '0' ? true
                                                      : equals_folded(value, "off") || equals_folded(value, "false") ||
                                                            equals_folded(value, "no");
        if (!on && !off) return false;
        value[0] = on ? '1' : '0';
        value[1] = 0;
        return true;
    }
    case Type::Int:
    case Type::Float: {
        if (in_choices(value, e.choices)) return true;
        double v = 0.0;
        const char* end = number(value, e.type == Type::Int, &v);
        return end && !*end && in_range(e, v);
    }
    case Type::IntList:
    case Type::FloatList: {
        if (in_choices(value, e.choices)) return true;
        // Blanks around the commas go (the sites read the launcher's compact form).
        char* write = value;
        for (const char* read = value; *read; ++read) {
            if (*read == ' ' || *read == '\t') continue;
            *write++ = *read;
        }
        *write = 0;
        unsigned count = 0;
        for (const char* at = value;;) {
            double v = 0.0;
            const char* end = number(at, e.type == Type::IntList, &v);
            if (!end || !element_in_range(e, count, v)) return false;
            ++count;
            if (!*end) break;
            if (*end != ',' || count >= 8) return false;
            at = end + 1;
        }
        return !e.count_mask || (e.count_mask >> count & 1u);
    }
    case Type::Enum: return in_choices(value, e.choices);
    case Type::String:
    case Type::Path: return true;
    }
    return false;
}
inline void note(Result& r, unsigned line, Problem problem, const char* key, const char* value, int entry) noexcept {
    if (r.issue_count < issue_limit)
        r.issues[r.issue_count++] = Issue{line, problem, key, value, entry};
    else
        ++r.more;
}
inline bool blank(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}
inline bool key_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}
// The entry (or alias target) of a normalised key; *renamed is set for an alias.
inline int find_key(const char* key, bool* renamed) noexcept {
    char env[4 + key_limit + 1] = {'X', '3', 'M', '_'};
    unsigned n = 0;
    for (; key[n]; ++n) {
        if (n >= key_limit) return -1;
        const char c = key[n];
        env[4 + n] = c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
    }
    env[4 + n] = 0;
    *renamed = false;
    const int i = find(env);
    if (i >= 0) return i;
    for (unsigned a = 0; a < schema::alias_count; ++a)
        if (schema::aliases[a].key && compare(key, schema::aliases[a].key) == 0) {
            *renamed = true;
            return int(schema::aliases[a].entry);
        }
    return -1;
}
// Where an entry's value comes from when the environment does not set it (config::get, docs/architecture/config-file.md
// section 3): the file, else the profile's base (the default, or the off value under X3M_CONFIG=bare). A launcher
// default marker (marker_of >= 0) takes its default only while the key it marks itself comes from the defaults: when
// the file or the environment sets that key the marker is absent. file_sets(i) and in_environment(i) answer for entry
// i.
enum class Layer : unsigned char { none, file, base };
template <class FileSets, class InEnvironment>
inline Layer below_environment(int i, bool bare, FileSets file_sets, InEnvironment in_environment) noexcept {
    if (file_sets(i)) return Layer::file;
    const schema::Entry& e = schema::entries[i];
    const char* base = bare ? e.off_value : e.default_value;
    if (!base) return Layer::none;
    if (!bare && e.marker_of >= 0 && (file_sets(e.marker_of) || in_environment(e.marker_of))) return Layer::none;
    return Layer::base;
}
inline const char* base_value(int i, bool bare) noexcept {
    return bare ? schema::entries[i].off_value : schema::entries[i].default_value;
}
// Parses `text` (size bytes, text[size] writable) in place into `r`.
inline void parse(char* text, unsigned size, Result& r) noexcept {
    text[size] = 0;
    char* const limit = text + size;
    char* at = text;
    if (size >= 3 && static_cast<unsigned char>(at[0]) == 0xef && static_cast<unsigned char>(at[1]) == 0xbb &&
        static_cast<unsigned char>(at[2]) == 0xbf)
        at += 3;
    for (unsigned n = 1; at < limit; ++n) {
        char* end = at;
        while (end < limit && *end != '\n') ++end;
        char* line = at;
        at = end < limit ? end + 1 : limit;
        *end = 0;
        r.lines = n;
        if (unsigned(end - line) > line_limit) {
            ++r.invalid;
            note(r, n, Problem::too_long, "", "", -1);
            continue;
        }
        { // A NUL inside the line (end is where this line's '\n' was): the line is refused, never read up to the NUL.
            const char* scan = line;
            while (scan < end && *scan) ++scan;
            if (scan < end) {
                ++r.invalid;
                note(r, n, Problem::nul, "", "", -1);
                continue;
            }
        }
        while (blank(*line)) ++line;
        char* stop = line;
        while (*stop) ++stop;
        while (stop > line && blank(stop[-1])) --stop;
        *stop = 0;
        if (!*line || *line == ';' || *line == '#') continue;
        if (*line == '[') {
            if (stop[-1] != ']') {
                ++r.invalid;
                note(r, n, Problem::syntax, "", line, -1);
            }
            continue;
        }
        char* equals = line;
        while (*equals && *equals != '=') ++equals;
        char* key_end = equals;
        while (key_end > line && blank(key_end[-1])) --key_end;
        bool key_ok = *equals == '=' && key_end > line;
        for (char* k = line; key_ok && k < key_end; ++k) key_ok = key_char(*k);
        if (!key_ok) {
            ++r.invalid;
            note(r, n, Problem::syntax, "", line, -1);
            continue;
        }
        *key_end = 0;
        for (char* k = line; *k; ++k) *k = *k == '-' ? '_' : lower(*k);
        char* value = equals + 1;
        while (blank(*value)) ++value;
        char* value_end = value;
        while (*value_end) ++value_end;
        if (value_end - value >= 2 && *value == '"' && value_end[-1] == '"') {
            ++value;
            *--value_end = 0;
        }
        bool renamed = false;
        const int i = find_key(line, &renamed);
        if (i < 0) {
            ++r.unknown;
            note(r, n, Problem::unknown, line, value, -1);
        } else if (schema::entries[i].flags & schema::env_only) {
            ++r.env_only;
            note(r, n, Problem::env_only, line, value, i);
        } else if (!check(schema::entries[i], value)) {
            ++r.invalid;
            note(r, n, Problem::invalid, line, value, i);
        } else {
            if (renamed) {
                ++r.renamed;
                note(r, n, Problem::renamed, line, schema::entries[i].key, i);
            }
            if (r.value[i]) {
                ++r.duplicate;
                note(r, n, Problem::duplicate, line, value, i);
            } else
                ++r.keys;
            r.value[i] = value;
            r.line[i] = n;
        }
    }
}
}
