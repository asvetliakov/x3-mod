#pragma once
#include <cstddef>
#include <cstdint>

// X3M_ENGINE_EFFECTS=native|off|plumes (ini engine_effects): the one parser shared by the draw-path module
// (engine_effects_core.h) and the redirect module (engine_effects_sites.h), so both read the same word the same way.
// Exactly "native", "off" or "plumes" in lower case and nothing else; mixed case, padding or another word is refused
// (each module then logs reason invalid_setting). No Windows dependency: the host tests compile it.
namespace x3m::engine_effects::option {
enum class Mode : std::uint8_t { native = 0, off = 1, plumes = 2 };
inline const char* mode_name(Mode m) noexcept {
    return m == Mode::off ? "off" : m == Mode::plumes ? "plumes" : "native";
}
// The first n characters of text (narrow or wide).
template <class Char> inline bool parse_mode(const Char* text, std::size_t n, Mode* out) noexcept {
    static const char* const words[3] = {"native", "off", "plumes"};
    if (!text) return false;
    for (unsigned w = 0; w < 3; ++w) {
        std::size_t len = 0;
        while (words[w][len]) ++len;
        if (n != len) continue;
        bool same = true;
        for (std::size_t i = 0; i < n && same; ++i) same = text[i] == Char(words[w][i]);
        if (same) {
            *out = Mode(w);
            return true;
        }
    }
    return false;
}
// A NUL-terminated text.
template <class Char> inline bool parse_mode(const Char* text, Mode* out) noexcept {
    if (!text) return false;
    std::size_t n = 0;
    while (text[n] && n < 16) ++n;
    if (text[n]) return false; // 16 or more characters
    return parse_mode(text, n, out);
}
// The resolved mode of the setting's first n characters, the one rule for every reader that asks what mode is in force
// (engine_effects.cpp's own option and both editions' small-parts far-jet gate, so they cannot diverge): n == 0 (unset
// or empty) is plumes, the default since Run 123 A; a word parse_mode accepts is that mode; anything else (another
// word, mixed case, 16 or more characters) is refused: native, *valid false (fail closed).
template <class Char> inline Mode resolve_mode(const Char* text, std::size_t n, bool* valid = nullptr) noexcept {
    Mode m = Mode::plumes;
    const bool ok = n == 0 || (n < 16 && parse_mode(text, n, &m));
    if (valid) *valid = ok;
    return ok ? m : Mode::native;
}
}
