#pragma once
#include "engine_effects_core.h" // json::Cursor (the engine_bodies.json reader)
#include "weapon_effects_core.h"

// The reader of <game>\x3m\weapon_effects.json (tools/effects/weapon_effects_table.py, docs/architecture/
// weapon-effects.md 3.2), reduced to weapon_effects::core::Table. Portable (host tests compile it); runs once at
// initialize, never in the handler. Shape:
//   {"schema": 1, "tool": "...", ..., "tables": {
//      "bullets":  {"rows": N, "sha256": "<16 hex>", "entries": [{"row": i, "id": "SS_BULLET_...", "z_over_xy": e|null,
//                   "texture_rgb": [r, g, b]|null, "style": "<shape>"|null, "tint": [r, g, b]|null,
//                   "calibre": k|null, "hull": "repair"|"tug"|"energy"|"ballistic"|"plasma"|"heavy"|"ion"|null, ...}, ...]},
//      "missiles": {"rows": M, ...}}}
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
};
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
    if (!c.accept('}'))
        for (;;) {
            char key[24];
            bool fits;
            if (!c.string(key, sizeof key, &fits) || !c.expect(':')) return false;
            if (!fits) {
                if (!c.skip_value()) return false;
            } else if (!std::strcmp(key, "row")) {
                if (!c.number(&row)) return false;
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
            } else if (!c.skip_value())
                return false;
            if (c.accept('}')) break;
            if (!c.expect(',')) return false;
        }
    if (!(row >= 0.f) || row >= float(core::table_capacity) || float(unsigned(row)) != row) {
        ++st->dropped;
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
                schema = v == 1.f;
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
                        if ((bullets || missiles) ? !parse_kind(c, t, bullets, st) : !c.skip_value()) {
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
