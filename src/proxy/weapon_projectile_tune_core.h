#pragma once
#include "weapon_projectile_family.h"
#include <cstddef>
#include <cstdint>

// The dev-only in-flight tuning file's parser and reload rule (docs/architecture/projectile-look.md section 4 and
// "Implementation (2026-11-08)"): <game>\x3m\projectile_tune.json, at most the seven numbers of the fighter-tracer
// family (weapon_projectile_family.h tune_key_names). Portable and allocation-free (the host test
// verification/analysis/test_weapon_effects_projectile_tune.py compiles it); the file reader around it
// (weapon_projectile_tune.cpp) exists only in an X3M_DEV_TUNING build.
//
// The file is one flat JSON object of "key": number pairs; unknown keys (with a number, string, true, false or null)
// are ignored, a missing key takes the family's baked value (each reload starts from the defaults, so the file states
// every deviation), a value outside its bounds is clamped (one row naming the key). Anything else (not an object, a
// known key whose value is not a number, a nested value, a missing or trailing comma, a leading '+', a bare word, more
// than file_capacity - 1 bytes) refuses the whole file with one row and the last applied values stay.
namespace x3m::weapon_effects::projectile_tune {
namespace pf = x3m::weapon_effects::projectile_family;
constexpr unsigned file_capacity = 1024;   // the reader's fixed buffer
constexpr std::uint32_t poll_ms = 1000;     // the last-write time is checked at most once a second
struct Result {
    bool ok = false;
    unsigned seen = 0, clamped = 0; // bit k: tune_key_names[k] present / clamped
    float raw[pf::tune_keys]{};     // the values as written (seen keys)
};
inline bool space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
// A JSON number at text[*at] (an optional '-', an integer part, an optional fraction and exponent of at most three
// digits); false when none (a leading '+', a bare '.', a word).
inline bool number(const char* t, std::size_t n, std::size_t* at, double* out) noexcept {
    std::size_t i = *at;
    bool neg = false;
    if (i < n && t[i] == '-') {
        neg = true;
        ++i;
    }
    if (!(i < n && t[i] >= '0' && t[i] <= '9')) return false;
    double v = 0.0;
    while (i < n && t[i] >= '0' && t[i] <= '9') v = v * 10.0 + double(t[i++] - '0');
    if (i < n && t[i] == '.') {
        ++i;
        if (!(i < n && t[i] >= '0' && t[i] <= '9')) return false;
        double scale = 0.1;
        while (i < n && t[i] >= '0' && t[i] <= '9') {
            v += double(t[i++] - '0') * scale;
            scale *= 0.1;
        }
    }
    if (i < n && (t[i] == 'e' || t[i] == 'E')) {
        ++i;
        bool eneg = false;
        if (i < n && (t[i] == '-' || t[i] == '+')) eneg = t[i++] == '-';
        int e = 0, ed = 0;
        while (i < n && t[i] >= '0' && t[i] <= '9' && ed < 3) {
            e = e * 10 + (t[i++] - '0');
            ++ed;
        }
        if (!ed) return false;
        for (int k = 0; k < e; ++k) v = eneg ? v / 10.0 : v * 10.0;
    }
    *out = neg ? -v : v;
    *at = i;
    return true;
}
inline bool word(const char* t, std::size_t n, std::size_t* at, const char* w) noexcept {
    std::size_t k = 0;
    while (w[k] && *at + k < n && t[*at + k] == w[k]) ++k;
    if (w[k]) return false;
    *at += k;
    return true;
}
// Parses text into *tune (which the caller set to the family's defaults); *tune is written only when ok. The text must
// be one flat JSON object: members separated by single commas (none missing, none trailing), every value a number, a
// string, true, false or null; a known key's value a number; nothing but whitespace after the closing brace.
inline Result parse(const char* t, std::size_t n, pf::Tune* tune) noexcept {
    Result r;
    if (!t || n >= file_capacity) return r;
    pf::Tune out = *tune;
    std::size_t i = 0;
    const auto ws = [&] {
        while (i < n && space(t[i])) ++i;
    };
    ws();
    if (i >= n || t[i] != '{') return r;
    ++i;
    ws();
    bool empty = i < n && t[i] == '}';
    if (empty) ++i;
    while (!empty) {
        if (i >= n || t[i] != '"') return r; // a key (a missing member after a comma, a trailing comma)
        char key[24];
        unsigned k = 0;
        bool fits = true;
        for (++i; i < n && t[i] != '"'; ++i) {
            if (t[i] == '\\') return r;
            if (k + 1 < sizeof key) key[k++] = t[i];
            else fits = false;
        }
        if (i >= n) return r;
        ++i;
        key[k] = 0;
        ws();
        if (i >= n || t[i] != ':') return r;
        ++i;
        ws();
        unsigned which = pf::tune_keys;
        for (unsigned q = 0; fits && q < pf::tune_keys; ++q) {
            const char* name = pf::tune_key_names[q];
            unsigned c = 0;
            while (name[c] && name[c] == key[c]) ++c;
            if (!name[c] && !key[c]) which = q;
        }
        double v = 0.0;
        if (number(t, n, &i, &v)) {
            if (which < pf::tune_keys) {
                const float raw = float(v);
                float c = raw;
                if (!(c >= pf::tune_min[which])) c = pf::tune_min[which];
                if (c > pf::tune_max[which]) c = pf::tune_max[which];
                r.raw[which] = raw;
                r.seen |= 1u << which;
                if (c != raw) r.clamped |= 1u << which;
                pf::tune_at(out, which) = c;
            }
        } else if (which < pf::tune_keys) {
            return r; // a known key must carry a number
        } else if (i < n && t[i] == '"') { // an unknown key's string
            for (++i; i < n && t[i] != '"'; ++i)
                if (t[i] == '\\') ++i;
            if (i >= n) return r;
            ++i;
        } else if (!word(t, n, &i, "true") && !word(t, n, &i, "false") && !word(t, n, &i, "null")) {
            return r; // a bare word, a nested value, a sign the grammar has not
        }
        ws();
        if (i < n && t[i] == ',') {
            ++i;
            ws();
            continue;
        }
        if (i < n && t[i] == '}') {
            ++i;
            break;
        }
        return r; // a missing comma
    }
    ws();
    if (i != n) return r; // trailing bytes
    r.ok = true;
    *tune = out;
    return r;
}
// The reload rule: called every frame; at most once per poll_ms it looks at the file (exists, its last-write time).
enum class Action { none, reload, absent };
struct Poll {
    bool started = false, present = false;
    std::uint32_t last_tick = 0;
    std::uint64_t write_time = 0;
};
// due: the caller may query the file now (the tick advanced poll_ms since the last query, or the first call).
inline bool due(const Poll& p, std::uint32_t tick) noexcept {
    return !p.started || std::uint32_t(tick - p.last_tick) >= poll_ms;
}
// The query's verdict: reload when the file appeared or its write time changed; absent once when it disappeared.
inline Action decide(Poll& p, std::uint32_t tick, bool exists, std::uint64_t write_time) noexcept {
    const bool first = !p.started;
    p.started = true;
    p.last_tick = tick;
    if (!exists) {
        const bool was = p.present;
        p.present = false;
        return was && !first ? Action::absent : Action::none;
    }
    const bool changed = !p.present || write_time != p.write_time;
    p.present = true;
    p.write_time = write_time;
    return changed ? Action::reload : Action::none;
}
} // namespace x3m::weapon_effects::projectile_tune
