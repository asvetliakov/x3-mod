#pragma once
#include "weapon_projectile_family.h"
#include <cstddef>
#include <cstdint>

// The dev-only in-flight tuning file's parser and reload rule (docs/architecture/projectile-look.md section 4 and
// "Implementation (2026-11-08)"): <game>\x3m\projectile_tune.json, at most the eight numbers of the fighter-tracer
// family (weapon_projectile_family.h tune_key_names). Portable and allocation-free (the host test
// verification/analysis/test_weapon_effects_projectile_tune.py compiles it); the file reader around it
// (weapon_projectile_tune.cpp) exists only in an X3M_DEV_TUNING build.
//
// The file is one flat JSON object of "key": number pairs; unknown keys are ignored, a missing key takes the family's
// baked value (each reload starts from the defaults, so the file states every deviation), a value outside its bounds is
// clamped (one row naming the key). Anything else (not an object, a known key whose value is not a number, a nested
// value, more than file_capacity - 1 bytes) refuses the whole file and the last applied values stay.
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
// A JSON number at text[*at] (no exponent beyond 2 digits, no hex); false when none.
inline bool number(const char* t, std::size_t n, std::size_t* at, double* out) noexcept {
    std::size_t i = *at;
    bool neg = false, digits = false;
    if (i < n && (t[i] == '-' || t[i] == '+')) neg = t[i++] == '-';
    double v = 0.0;
    while (i < n && t[i] >= '0' && t[i] <= '9') {
        v = v * 10.0 + double(t[i++] - '0');
        digits = true;
    }
    if (i < n && t[i] == '.') {
        ++i;
        double scale = 0.1;
        while (i < n && t[i] >= '0' && t[i] <= '9') {
            v += double(t[i++] - '0') * scale;
            scale *= 0.1;
            digits = true;
        }
    }
    if (!digits) return false;
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
// Parses text into *tune (which the caller set to the family's defaults); *tune is written only when ok.
inline Result parse(const char* t, std::size_t n, pf::Tune* tune) noexcept {
    Result r;
    if (!t || n >= file_capacity) return r;
    pf::Tune out = *tune;
    std::size_t i = 0;
    while (i < n && space(t[i])) ++i;
    if (i >= n || t[i] != '{') return r;
    ++i;
    for (;;) {
        while (i < n && (space(t[i]) || t[i] == ',')) ++i;
        if (i >= n) return r;
        if (t[i] == '}') break;
        if (t[i] != '"') return r;
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
        while (i < n && space(t[i])) ++i;
        if (i >= n || t[i] != ':') return r;
        ++i;
        while (i < n && space(t[i])) ++i;
        unsigned which = pf::tune_keys;
        for (unsigned q = 0; fits && q < pf::tune_keys; ++q) {
            const char* name = pf::tune_key_names[q];
            unsigned c = 0;
            while (name[c] && name[c] == key[c]) ++c;
            if (!name[c] && !key[c]) which = q;
        }
        double v = 0.0;
        if (!number(t, n, &i, &v)) {
            if (which < pf::tune_keys) return r; // a known key must carry a number
            // an unknown key: a string, true/false/null or a number is skipped; a nested value refuses the file
            if (i < n && t[i] == '"') {
                for (++i; i < n && t[i] != '"'; ++i)
                    if (t[i] == '\\') ++i;
                if (i >= n) return r;
                ++i;
            } else {
                while (i < n && t[i] >= 'a' && t[i] <= 'z') ++i;
            }
            continue;
        }
        if (which >= pf::tune_keys) continue;
        const float raw = float(v);
        float c = raw;
        if (!(c >= pf::tune_min[which])) c = pf::tune_min[which];
        if (c > pf::tune_max[which]) c = pf::tune_max[which];
        r.raw[which] = raw;
        r.seen |= 1u << which;
        if (c != raw) r.clamped |= 1u << which;
        pf::tune_at(out, which) = c;
    }
    ++i;
    while (i < n && space(t[i])) ++i;
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
