#pragma once
#include "engine_effects_core.h" // json::Cursor (the engine_bodies.json reader)
#include "weapon_effects_core.h"

// The reader of <game>\x3m\weapon_effects.json (tools/effects/weapon_effects_table.py, docs/architecture/
// weapon-effects.md 3.2), reduced to weapon_effects::core::Table. Portable (host tests compile it); runs once at
// initialize, never in the handler. Shape:
//   {"schema": 1|2, "tool": "...", ..., "tables": {
//      "bullets":  {"rows": N, "sha256": "<16 hex>", "entries": [{"row": i, "id": "SS_BULLET_...", "z_over_xy": e|null,
//                   "texture_rgb": [r, g, b]|null, "style": "<shape>"|null, "tint": [r, g, b]|null,
//                   "calibre": k|null, "hull": "repair"|"tug"|"energy"|"ballistic"|"plasma"|"heavy"|"ion"|null,
//                   "mount_tier": "fighter"|"corvette"|"capital"|null, "range_m": m|null, ...}, ...]},
//      "missiles": {"rows": M, ...}}}
// Schema 2 (step 4) adds mount_tier and range_m; a schema 1 table reads as every row a fighter mount of unknown range.
// The optional bolt hand pins (bolt-muzzle-look.md 11.5, schema 2, absent on most rows): "bolt_e_body": E_b in
// [0.01, 2.55], "bolt_w_orb": the orb diameter in w in [0.01, 2.55] (both stored x 100), "bolt_halo": false (no halo; true
// or null pins nothing), "bolt_tint": "texture" (the texture mean even when the light colour is saturated). A pin of the
// wrong JSON type makes the file malformed; a number outside its range (a stored 0 included) or another bolt_tint name
// drops the row (Stats::bolt_rejected), as the generator never writes one.
// The bullet re-shade's textures (any schema; an older generator writes none): "tables": {..., "bullet_textures":
// [{"bytes": n, "head64": "<16 hex>", "fnv1a64": "<16 hex>", "p99": p, ...}, ...]}; an entry with a byte count outside
// 1..2^24, a hash that is not 16 hex digits, a p99 outside (0, 1] or past bullet_texture_capacity is dropped (counted);
// a value of the wrong JSON type makes the file malformed.
// Colours are 8-bit sRGB words (0..255); style is a shape name (tracer, slug, orb, wave, beam). Unknown keys are
// skipped. A row index outside [0, table_capacity) is dropped (counted). false = malformed (the table stays empty):
// besides a syntax error, a row index given twice and any byte other than whitespace after the closing brace.
namespace x3m::weapon_effects::table {
namespace json = x3m::engine_effects::core::json;
namespace core = x3m::weapon_effects::core;
struct Stats {
    unsigned entries = 0, dropped = 0, textures = 0, overrides = 0;
    unsigned schema = 0;
    unsigned hull_keys = 0; // entries carrying the "hull" key (0: a table generated before step 2, or no variant row)
    unsigned mount_keys = 0; // entries carrying "mount_tier" (step 4; 0: a table generated before it)
    unsigned bolt_pins = 0;  // entries carrying a bolt hand pin (bolt_e_body, bolt_w_orb, bolt_halo false, bolt_tint)
    unsigned bolt_rejected = 0; // rows dropped for a bolt pin outside its range (a zero E_b, a w_orb stored as 0, ...)
    unsigned bullet_textures = 0, bullet_textures_dropped = 0; // tables.bullet_textures entries kept / dropped
};
// A 16-hex-digit string into *out; false when the string is anything else.
inline bool parse_hex64(json::Cursor& c, std::uint64_t* out, bool* valid) noexcept {
    char text[24];
    bool fits;
    if (!c.string(text, sizeof text, &fits)) return false;
    *valid = fits && std::strlen(text) == 16;
    std::uint64_t v = 0;
    for (unsigned i = 0; *valid && i < 16; ++i) {
        const char h = text[i];
        const unsigned d = h >= '0' && h <= '9' ? unsigned(h - '0')
                           : h >= 'a' && h <= 'f' ? unsigned(h - 'a' + 10)
                           : h >= 'A' && h <= 'F' ? unsigned(h - 'A' + 10)
                                                  : 16u;
        if (d > 15) *valid = false;
        v = v << 4 | d;
    }
    *out = *valid ? v : 0;
    return true;
}
inline bool parse_bullet_texture(json::Cursor& c, core::Table* t, Stats* st) noexcept {
    if (!c.expect('{')) return false;
    float bytes = 0.f, p99 = 0.f;
    std::uint64_t head = 0, hash = 0;
    bool head_ok = false, hash_ok = false;
    if (!c.accept('}'))
        for (;;) {
            char key[24];
            bool fits;
            if (!c.string(key, sizeof key, &fits) || !c.expect(':')) return false;
            if (fits && !std::strcmp(key, "bytes")) {
                if (!c.number(&bytes)) return false;
            } else if (fits && !std::strcmp(key, "p99")) {
                if (!c.number(&p99)) return false;
            } else if (fits && !std::strcmp(key, "head64")) {
                if (!parse_hex64(c, &head, &head_ok)) return false;
            } else if (fits && !std::strcmp(key, "fnv1a64")) {
                if (!parse_hex64(c, &hash, &hash_ok)) return false;
            } else if (!c.skip_value())
                return false;
            if (c.accept('}')) break;
            if (!c.expect(',')) return false;
        }
    const bool valid = bytes >= 1.f && bytes <= 16777216.f && float(std::uint32_t(bytes)) == bytes && p99 > 0.f &&
                       p99 <= 1.f && head_ok && hash_ok;
    if (!valid || t->bullet_textures >= core::bullet_texture_capacity) {
        ++st->bullet_textures_dropped;
        return true;
    }
    const unsigned i = t->bullet_textures++;
    t->bullet_texture_bytes[i] = std::uint32_t(bytes);
    t->bullet_texture_head[i] = head;
    t->bullet_texture_hash[i] = hash;
    const float q = p99 * 65535.f + 0.5f;
    t->bullet_texture_p99_q[i] = std::uint16_t(q < 1.f ? 1.f : q > 65535.f ? 65535.f : q);
    ++st->bullet_textures;
    return true;
}
inline bool parse_bullet_textures(json::Cursor& c, core::Table* t, Stats* st) noexcept {
    if (!c.expect('[')) return false;
    if (c.accept(']')) return true;
    for (;;) {
        if (!parse_bullet_texture(c, t, st)) return false;
        if (c.accept(']')) return true;
        if (!c.expect(',')) return false;
    }
}
inline bool parse_rgb(json::Cursor& c, std::uint32_t* out, bool* present) noexcept {
    *present = false;
    if (c.literal("null")) return true;
    if (!c.expect('[')) return false;
    float v[3];
    for (unsigned i = 0; i < 3; ++i) {
        if (!c.number(&v[i])) return false;
        if (i < 2 && !c.expect(',')) return false;
    }
    if (!c.expect(']')) return false;
    std::uint32_t w[3];
    for (unsigned i = 0; i < 3; ++i) w[i] = v[i] > 0.f ? (v[i] < 255.f ? std::uint32_t(v[i] + 0.5f) : 255u) : 0u;
    *out = core::pack_rgb(w[0], w[1], w[2]);
    *present = true;
    return true;
}
inline bool shape_from_name(const char* s, std::uint8_t* out) noexcept {
    for (unsigned i = 0; i < core::shape_missile_hit; ++i)
        if (!std::strcmp(s, core::shape_name(i))) {
            *out = std::uint8_t(i);
            return true;
        }
    return false;
}
inline bool parse_entry(json::Cursor& c, core::Table* t, Stats* st) noexcept {
    if (!c.expect('{')) return false;
    float row = -1.f, e = 0.f, calibre = 0.f;
    bool e_present = false, calibre_present = false, texture_present = false, tint_present = false, style_present = false;
    std::uint32_t texture = 0, tint = 0;
    // hull: the hull-hit variant (row_hull_repair / row_hull_tug, step 2) or a class pin (row_hull_class with hull_class,
    // docs/architecture/hull-impact-classes.md section 2)
    std::uint8_t style = 0, hull = 0, hull_class = 0;
    std::uint8_t mount = 0; // step 4: the mount tier (fighter when absent or unknown)
    float range = 0.f;
    bool range_present = false;
    std::uint8_t pin = 0, e_body_q = 0, w_orb_q = 0; // the bolt hand pins (bolt_e_body, bolt_w_orb, bolt_halo, bolt_tint)
    bool bad_pin = false;
    std::uint32_t id_hash = 0, body_hash = 0; // the projectile stage's row identity (0: absent or too long)
    if (!c.accept('}'))
        for (;;) {
            char key[24];
            bool fits;
            if (!c.string(key, sizeof key, &fits) || !c.expect(':')) return false;
            if (!fits) {
                if (!c.skip_value()) return false;
            } else if (!std::strcmp(key, "row")) {
                if (!c.number(&row)) return false;
            } else if (!std::strcmp(key, "id") || !std::strcmp(key, "body")) {
                if (!c.literal("null")) {
                    char name[64];
                    bool name_fits;
                    if (!c.string(name, sizeof name, &name_fits)) return false;
                    (key[0] == 'i' ? id_hash : body_hash) = name_fits ? core::name_hash(name) : 0u;
                }
            } else if (!std::strcmp(key, "z_over_xy")) {
                if (!c.number_or_null(&e, &e_present)) return false;
            } else if (!std::strcmp(key, "calibre")) {
                if (!c.number_or_null(&calibre, &calibre_present)) return false;
            } else if (!std::strcmp(key, "texture_rgb")) {
                if (!parse_rgb(c, &texture, &texture_present)) return false;
            } else if (!std::strcmp(key, "tint")) {
                if (!parse_rgb(c, &tint, &tint_present)) return false;
            } else if (!std::strcmp(key, "style")) {
                if (c.literal("null")) {
                } else {
                    char name[16];
                    bool name_fits;
                    if (!c.string(name, sizeof name, &name_fits)) return false;
                    style_present = name_fits && shape_from_name(name, &style);
                }
            } else if (!std::strcmp(key, "hull")) {
                ++st->hull_keys;
                if (c.literal("null")) {
                } else {
                    char name[16];
                    bool name_fits;
                    if (!c.string(name, sizeof name, &name_fits)) return false;
                    hull = !name_fits                     ? std::uint8_t(0)
                           : !std::strcmp(name, "repair") ? std::uint8_t(core::row_hull_repair)
                           : !std::strcmp(name, "tug")    ? std::uint8_t(core::row_hull_tug)
                                                          : std::uint8_t(0);
                    // The class pins (beam is not pinnable: a bolt pinned beam would hold a spot with no tick stream).
                    for (unsigned c = 0; name_fits && !hull && c < core::hull_class_count; ++c)
                        if (c != core::class_beam && !std::strcmp(name, core::hull_class_name(c))) {
                            hull = core::row_hull_class;
                            hull_class = std::uint8_t(c);
                        }
                }
            } else if (!std::strcmp(key, "mount_tier")) {
                ++st->mount_keys;
                if (!c.literal("null")) {
                    char name[16];
                    bool name_fits;
                    if (!c.string(name, sizeof name, &name_fits)) return false;
                    mount = !name_fits                       ? std::uint8_t(0)
                            : !std::strcmp(name, "corvette") ? std::uint8_t(1)
                            : !std::strcmp(name, "capital")  ? std::uint8_t(2)
                                                             : std::uint8_t(0);
                }
            } else if (!std::strcmp(key, "range_m")) {
                if (!c.number_or_null(&range, &range_present)) return false;
            } else if (!std::strcmp(key, "bolt_e_body") || !std::strcmp(key, "bolt_w_orb")) {
                const bool e_key = key[5] == 'e';
                float v = 0.f;
                bool present = false;
                if (!c.number_or_null(&v, &present)) return false;
                if (present) {
                    const float q = v * 100.f + 0.5f; // stored x 100: a value that rounds to 0 is rejected with the rest
                    if (!(q >= 1.f && q < 256.f)) {
                        bad_pin = true;
                    } else {
                        pin |= e_key ? std::uint8_t(core::pin_e_body) : std::uint8_t(core::pin_w_orb);
                        (e_key ? e_body_q : w_orb_q) = std::uint8_t(q);
                    }
                }
            } else if (!std::strcmp(key, "bolt_halo")) {
                if (c.literal("false")) pin |= core::pin_halo_off;
                else if (!c.literal("true") && !c.literal("null")) return false;
            } else if (!std::strcmp(key, "bolt_tint")) {
                if (!c.literal("null")) {
                    char name[16];
                    bool name_fits;
                    if (!c.string(name, sizeof name, &name_fits)) return false;
                    if (name_fits && !std::strcmp(name, "texture"))
                        pin |= core::pin_tint_texture;
                    else
                        bad_pin = true;
                }
            } else if (!c.skip_value())
                return false;
            if (c.accept('}')) break;
            if (!c.expect(',')) return false;
        }
    if (!(row >= 0.f) || row >= float(core::table_capacity) || float(unsigned(row)) != row) {
        ++st->dropped;
        return true;
    }
    if (bad_pin) { // a bolt pin outside its range: the row is not the generator's, so none of it is used
        ++st->bolt_rejected;
        return true;
    }
    const unsigned i = unsigned(row);
    if (t->bits[i] & core::row_present) return c.ok = false; // a duplicate row: the file is not the generator's
    std::uint8_t bits = core::row_present;
    t->shape[i] = core::shape_from_elongation(e, e_present);
    if (style_present) {
        t->shape[i] = style;
        bits |= core::row_style_override;
    }
    if (texture_present) {
        t->texture_rgb[i] = texture;
        bits |= core::row_texture;
        ++st->textures;
    }
    if (tint_present) {
        t->override_rgb[i] = tint;
        bits |= core::row_tint_override;
    }
    if (calibre_present && calibre >= 0.f && calibre <= 1.f) {
        t->calibre_q[i] = std::uint8_t(calibre * 255.f + 0.5f);
        bits |= core::row_calibre_override;
    }
    bits |= hull;
    t->hull_class[i] = hull_class;
    t->mount_tier[i] = mount;
    t->range_m[i] = range_present && range > 0.f ? std::uint16_t(range < 65535.f ? range + 0.5f : 65535.f) : std::uint16_t(0);
    t->bolt_pin[i] = pin;
    t->bolt_e_body_q[i] = e_body_q;
    t->bolt_w_orb_q[i] = w_orb_q;
    t->id_hash[i] = id_hash;
    t->body_hash[i] = body_hash;
    if (pin) ++st->bolt_pins;
    if (bits & (core::row_style_override | core::row_tint_override | core::row_calibre_override | core::row_hull_repair |
                core::row_hull_tug | core::row_hull_class))
        ++st->overrides;
    t->bits[i] = bits;
    ++st->entries;
    return true;
}
inline bool parse_kind(json::Cursor& c, core::Table* t, bool bullets, Stats* st) noexcept {
    if (!c.expect('{')) return false;
    if (c.accept('}')) return true;
    for (;;) {
        char key[24];
        bool fits;
        if (!c.string(key, sizeof key, &fits) || !c.expect(':')) return false;
        if (fits && !std::strcmp(key, "rows")) {
            float rows = 0.f;
            if (!c.number(&rows) || !(rows >= 0.f) || rows > 65535.f) return false;
            (bullets ? t->rows : t->missile_rows) = std::uint32_t(rows);
        } else if (fits && bullets && !std::strcmp(key, "entries")) {
            if (!c.expect('[')) return false;
            if (!c.accept(']'))
                for (;;) {
                    if (!parse_entry(c, t, st)) return false;
                    if (c.accept(']')) break;
                    if (!c.expect(',')) return false;
                }
        } else if (!c.skip_value())
            return false;
        if (c.accept('}')) return true;
        if (!c.expect(',')) return false;
    }
}
// The whole file into *t (cleared first). On false *t is cleared again (no lookups).
inline bool parse(const char* text, std::size_t length, core::Table* t, Stats* st, std::size_t* fault = nullptr) noexcept {
    *t = core::Table{};
    *st = Stats{};
    json::Cursor c{text, text + length};
    bool ok = c.expect('{');
    bool schema = false;
    if (ok && !c.accept('}'))
        for (;;) {
            char key[24];
            bool fits;
            if (!c.string(key, sizeof key, &fits) || !c.expect(':')) {
                ok = false;
                break;
            }
            if (fits && !std::strcmp(key, "schema")) {
                float v = 0.f;
                if (!c.number(&v)) {
                    ok = false;
                    break;
                }
                st->schema = unsigned(v);
                schema = v == 1.f || v == 2.f; // 2: step 4's mount_tier and range_m
            } else if (fits && !std::strcmp(key, "tables")) {
                if (!c.expect('{')) {
                    ok = false;
                    break;
                }
                if (!c.accept('}'))
                    for (;;) {
                        char kind[16];
                        bool kind_fits;
                        if (!c.string(kind, sizeof kind, &kind_fits) || !c.expect(':')) {
                            ok = false;
                            break;
                        }
                        const bool bullets = kind_fits && !std::strcmp(kind, "bullets");
                        const bool missiles = kind_fits && !std::strcmp(kind, "missiles");
                        const bool textures = kind_fits && !std::strcmp(kind, "bullet_textures");
                        if (textures ? !parse_bullet_textures(c, t, st)
                                     : (bullets || missiles) ? !parse_kind(c, t, bullets, st) : !c.skip_value()) {
                            ok = false;
                            break;
                        }
                        if (c.accept('}')) break;
                        if (!c.expect(',')) {
                            ok = false;
                            break;
                        }
                    }
                if (!ok) break;
            } else if (!c.skip_value()) {
                ok = false;
                break;
            }
            if (c.accept('}')) break;
            if (!c.expect(',')) {
                ok = false;
                break;
            }
        }
    if (ok && c.ok) { // nothing but whitespace after the closing brace
        c.skip_ws();
        if (c.p != c.end) c.ok = false;
    }
    ok = ok && c.ok && schema;
    if (!ok) {
        if (fault) *fault = std::size_t(c.p - text);
        *t = core::Table{};
    }
    return ok;
}
} // namespace x3m::weapon_effects::table
