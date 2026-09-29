#include "text_density.h"
#include "config.h"
#include "text_density_sites.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "log_tiers.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>

// Six claims through engine_patch (text_density_sites.h), all or none: three entry claims whose thunks call one
// cdecl C function each with every register and EFLAGS saved (the font open 0x0048cdc0, the two block blits
// 0x0048c090 / 0x0048c460), one call redirect (the Materials load call at 0x0048af71: the thunk calls the original
// with the caller's stack exactly as it was, then the C side edits the row flags) and, under X3M_DEBUG=1, two entry
// claims that only record (function, src, dst) triples (the text line 0x0048b2d0, the rect fill 0x0048b0b0). The
// thunks live in this module, which is pinned before any claim goes live; every continuation word is read back before
// the stub is pushed in front of the tail. The four engine helpers the shadow path calls (texture lookup, surface
// allocator and free, the copy leaves) are compared byte for byte before anything is claimed. Per blit into a flagged
// row: one lookup, a 16-entry cache probe and, for a static source, the engine's own leaf with the shadow as the
// source; the shadow is built once per source id (d x nearest neighbour through the engine's plain copy leaf) and
// freed before every device Reset. No x87, no allocation on the blit path.
static_assert(sizeof(void*) == 4, "x86 code patching only");

namespace {
namespace engine_patch = x3m::engine_patch;
namespace sites = x3m::text_density::sites;
enum Index : unsigned { Font = 0, BltBlock, BltAlpha, TextLine, RectFill, claim_count };
struct Claim {
    const char* name;
    std::uintptr_t va;
    unsigned length;
    const unsigned char* expected;
    void (*thunk)();
    std::uintptr_t* continuation;
    bool diagnostic;
};
const Claim claims[claim_count] = {
    {"text_density_font", sites::font_site_va, sites::font_site_length, sites::expected_font_window, x3m_text_density_font_thunk,
     &x3m_text_density_font_continue, false},
    {"text_density_blt_block", sites::blt_block_va, sites::blit_site_length, sites::expected_blit_window,
     x3m_text_density_blt_block_thunk, &x3m_text_density_blt_block_continue, false},
    {"text_density_blt_alpha", sites::blt_alpha_va, sites::blit_site_length, sites::expected_blit_window,
     x3m_text_density_blt_alpha_thunk, &x3m_text_density_blt_alpha_continue, false},
    {"text_density_text_line", sites::text_line_va, sites::text_line_site_length, sites::expected_text_line_window,
     x3m_text_density_text_line_thunk, &x3m_text_density_text_line_continue, true},
    {"text_density_rect_fill", sites::rect_fill_va, sites::rect_fill_site_length, sites::expected_rect_fill_window,
     x3m_text_density_rect_fill_thunk, &x3m_text_density_rect_fill_continue, true}};
engine_patch::Site site_[claim_count];
bool live_[claim_count] = {};
const char* write_[claim_count] = {};
engine_patch::CallSite materials_site_{};
bool materials_live_ = false;
const char* materials_write_ = "none";
bool requested_ = false;      // the setting asks for auto or 2/3 and every check passed: claims in, waiting for the device
bool claimed_ = false;        // every production claim (and the diagnostic ones under debug) active
bool resolved_ = false;       // device_created ran
volatile LONG active_ = 0;    // density > 1 applied: the thunks do their work
bool diagnostic_ = false;     // the two diagnostic claims are in (debug)
sites::Parse mode_ = sites::Parse::automatic;
unsigned value_ = 1, density_ = 1;
double scale_ = 1.0;
const char* state_ = "not_initialized";
const char* failed_site_ = "-";
char setting_[sites::setting_capacity] = "-";
// The config field and the style imm32 (written at device_created, put back by disable()/shutdown()).
std::uint32_t config_before_ = 0;
bool config_written_ = false, style_patched_ = false;
DWORD style_protection_ = 0;
const char* style_write_ = "none";
// The Materials rows. The three static slots (row count, rows, texture table) are re-read on every call as the
// engine's lookup does: the texture table is reallocated in 1000-entry steps when named textures are added
// (0x004b8920), so a captured pointer would go stale.
struct Tables {
    unsigned rows;
    std::uintptr_t rows_base, table;
};
bool materials_loaded_ = false, rows_applied_ = false;
unsigned rows_ = 0, rows_flagged_ = 0, rows_unflagged_ = 0, rows_nofilter_ = 0;
unsigned largest_w_ = 0, largest_h_ = 0; // the largest row to flag at 1x (for the texture-limit check)
unsigned caps_w_ = 0, caps_h_ = 0, caps_limited_d_ = 0; // D3DCAPS9 limits (0 = unknown) and the density they imposed
char missing_[sites::missing_list_capacity] = "-";
// The game directory (wide, with a trailing backslash) for the font existence checks.
wchar_t game_dir_[MAX_PATH + 2] = L"";
unsigned game_dir_length_ = 0;
// The shadow cache: one engine texture object per static source id.
struct Shadow {
    std::int32_t id;
    void* object;
    std::uint32_t bytes;
};
Shadow shadows_[sites::shadow_slots] = {};
unsigned shadow_count_ = 0;
std::uint32_t shadow_bytes_ = 0;
// Sources whose shadow was refused (budget, slots, size, allocation): remembered so later blits pass straight through.
constexpr unsigned refused_slots = 32;
std::int32_t refused_[refused_slots] = {};
unsigned refused_count_ = 0;
SRWLOCK shadow_lock_ = SRWLOCK_INIT;
unsigned shadow_rows_ = 0, font_rows_ = 0; // log rows written (bounded)
constexpr unsigned shadow_row_limit = 64, font_row_limit = 32;
// The on-screen filter override (run 392): the D3D textures of the flagged rows, rebuilt once per Present; the
// stage-0 texture the game bound last; the sampler values a draw replaced.
sites::FilterSet filter_set_{};
volatile std::uintptr_t stage0_ = 0;
unsigned long saved_min_ = 0, saved_mag_ = 0;
bool filter_unavailable_ = false; // GetSamplerState failed once (a pure device): no further attempt
unsigned filter_draws_ = 0, filter_already_linear_ = 0; // draws changed / already linear since the last debug row
bool raised_min_ = false, raised_mag_ = false;           // which axes the current draw raised (restored after it)
bool filter_logged_ = false;
constexpr unsigned filter_report_window = 300;
// The diagnostic set (debug): printed once per distinct (function, src, dst).
sites::DiagnosticKey diagnostic_table_[sites::diagnostic_rows] = {};
unsigned diagnostic_count_ = 0;
const char* const diagnostic_names[4] = {"text_line", "blt_block", "blt_alpha", "rect_fill"};

using CopyFn = void(__cdecl*)(void*, void*, int, int, int, int, int, int);
using FreeFn = int(__cdecl*)(void**);

bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&claimed_), &module) != FALSE &&
           module != nullptr;
}
void printable(const wchar_t* text, DWORD length, char* out) {
    if (!length) {
        out[0] = '-';
        out[1] = 0;
        return;
    }
    if (length >= sites::setting_capacity) {
        out[0] = '?';
        out[1] = 0;
        return;
    }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0;
}
bool code_is(std::uintptr_t at, const unsigned char* expected, unsigned n) {
    unsigned char now[128]{};
    return n <= sizeof now && engine_patch::read_code(at, now, n) && !std::memcmp(now, expected, n);
}
// Every byte window the contract rests on (fail closed); nullptr when all match.
const char* windows_verified() {
    if (!code_is(sites::font_site_va, sites::expected_font_window, sites::font_window_length)) return "font_mismatch";
    if (!code_is(sites::materials_wrapper_va, sites::expected_materials_window, sites::materials_window_length) ||
        !code_is(sites::materials_target_va, sites::expected_materials_callee, sites::materials_callee_length))
        return "materials_mismatch";
    if (!code_is(sites::style_window_va, sites::expected_style_window, sites::style_window_length)) return "style_mismatch";
    if (!code_is(sites::blt_block_va, sites::expected_blit_window, sites::blit_window_length) ||
        !code_is(sites::blt_alpha_va, sites::expected_blit_window, sites::blit_window_length) ||
        !code_is(sites::config_read_va, sites::expected_config_read, sites::config_read_length) ||
        !code_is(sites::row_flag_test_va, sites::expected_row_flag_test, sites::row_flag_test_length) ||
        !code_is(sites::row_count_read_va, sites::expected_row_count_read, sites::row_count_read_length))
        return "blit_mismatch";
    if (!code_is(sites::lookup_va, sites::expected_lookup, sites::lookup_length) ||
        !code_is(sites::alloc_va, sites::expected_alloc, sites::alloc_length) ||
        !code_is(sites::alloc_fields_va, sites::expected_alloc_fields, sites::alloc_fields_length) ||
        !code_is(sites::free_va, sites::expected_free, sites::free_length) ||
        !code_is(sites::copy_va, sites::expected_copy, sites::copy_length) ||
        !code_is(sites::colour_copy_va, sites::expected_colour_copy, sites::colour_copy_length) ||
        !code_is(sites::alpha_leaf_va, sites::expected_alpha_leaf, sites::alpha_leaf_length))
        return "helper_mismatch";
    if (!code_is(sites::text_line_va, sites::expected_text_line_window, sites::text_line_window_length) ||
        !code_is(sites::rect_fill_va, sites::expected_rect_fill_window, sites::rect_fill_window_length))
        return "diagnostic_mismatch";
    if (!code_is(sites::config_default_store_va, sites::expected_config_default_store, sites::config_store_length) ||
        !code_is(sites::config_atol_store_va, sites::expected_config_atol_store, sites::config_store_length) ||
        !code_is(sites::config_flag_test_va, sites::expected_config_flag_test, sites::config_flag_test_length))
        return "config_mismatch";
    return nullptr;
}
// The game directory from the main module's path (documented: GetModuleFileNameW), with a trailing backslash.
bool resolve_game_dir() {
    wchar_t path[MAX_PATH + 2]{};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    DWORD cut = n;
    while (cut && path[cut - 1] != L'\\' && path[cut - 1] != L'/') --cut;
    if (!cut) return false;
    std::memcpy(game_dir_, path, cut * sizeof(wchar_t));
    game_dir_[cut] = 0;
    game_dir_length_ = cut;
    return true;
}
// Whether the loose file `relative` (ASCII, backslashes) exists under the game directory.
bool loose_file_exists(const char* relative) {
    wchar_t full[MAX_PATH + 2]{};
    const unsigned n = unsigned(std::strlen(relative));
    if (!game_dir_length_ || game_dir_length_ + n >= MAX_PATH) return false;
    std::memcpy(full, game_dir_, game_dir_length_ * sizeof(wchar_t));
    for (unsigned i = 0; i < n; ++i) full[game_dir_length_ + i] = wchar_t(static_cast<unsigned char>(relative[i]));
    full[game_dir_length_ + n] = 0;
    const DWORD attributes = GetFileAttributesW(full);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
bool font_pair_exists(const char* name, unsigned size, unsigned d, char abc[sites::font_path_capacity]) {
    char tga[sites::font_path_capacity];
    return sites::font_file(name, size, d, ".abc", abc) && sites::font_file(name, size, d, ".tga", tga) &&
           loose_file_exists(abc) && loose_file_exists(tga);
}
// The gate: every family's pair at density d under <game>\f; the missing names go to missing_.
unsigned fonts_missing(unsigned d) {
    return sites::missing_fonts(d, [](const char* relative) { return loose_file_exists(relative); }, missing_);
}
// ---- the claims ----
const char* install_site(unsigned index) {
    const Claim& c = claims[index];
    write_[index] = "none";
    if (live_[index]) return "already_installed";
    if (!engine_patch::install_window_open()) return "late_claim";
    engine_patch::SiteSpec spec{};
    spec.name = c.name;
    spec.address = c.va;
    spec.length = c.length;
    spec.ret_pop = 0;
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, c.expected, c.length);
    site_[index] = engine_patch::Site{};
    const bool claimed = engine_patch::claim(site_[index], spec);
    if (claimed || site_[index].patched_in || !std::strcmp(site_[index].status, "patch_rolled_back"))
        write_[index] = site_[index].atomic_write ? "atomic" : "plain";
    live_[index] = site_[index].patched_in;
    if (!claimed) return live_[index] ? "rollback_failed" : site_[index].status;
    const char* failure = nullptr;
    unsigned char now[engine_patch::max_prologue]{};
    // The continuation word (the tail) is published before the thunk goes in front of it.
    *c.continuation = reinterpret_cast<std::uintptr_t>(*site_[index].entry);
    if (!*c.continuation || !engine_patch::push_front(site_[index], reinterpret_cast<void*>(c.thunk)))
        failure = "chain_failed";
    else if (!engine_patch::read_code(c.va, now, c.length) || std::memcmp(now, site_[index].patched, 5) ||
             std::memcmp(now + 5, c.expected + 5, c.length - 5) || *site_[index].entry != reinterpret_cast<void*>(c.thunk))
        failure = "readback_failed";
    if (!failure) return "ok";
    engine_patch::restore(site_[index]);
    live_[index] = site_[index].patched_in;
    return live_[index] ? "rollback_failed" : failure;
}
const char* install_materials() {
    materials_write_ = "none";
    if (materials_live_) return "already_installed";
    if (!engine_patch::install_window_open()) return "late_claim";
    x3m_text_density_materials_continue = sites::materials_target_va;
    materials_site_ = engine_patch::CallSite{};
    const bool claimed = engine_patch::claim_call(materials_site_, sites::materials_site_va, sites::materials_target_va,
                                                  reinterpret_cast<void*>(&x3m_text_density_materials_thunk));
    if (claimed || materials_site_.patched_in || !std::strcmp(materials_site_.status, "patch_rolled_back"))
        materials_write_ = materials_site_.atomic_write ? "atomic" : "plain";
    materials_live_ = materials_site_.patched_in;
    if (!claimed) return materials_live_ ? "rollback_failed" : materials_site_.status;
    return "ok";
}
bool restore_all() {
    bool clean = true;
    for (unsigned i = claim_count; i-- > 0;) {
        if (!live_[i]) continue;
        engine_patch::restore(site_[i]);
        live_[i] = site_[i].patched_in;
        clean = clean && !live_[i];
    }
    if (materials_live_) {
        engine_patch::restore_call(materials_site_);
        materials_live_ = materials_site_.patched_in;
        clean = clean && !materials_live_;
    }
    claimed_ = false;
    diagnostic_ = false;
    return clean;
}
bool any_live() {
    for (bool l : live_)
        if (l) return true;
    return materials_live_;
}
// The claims in order: the font open, the two blits, the Materials call, then the two diagnostic entries under
// debug; a failure restores the earlier ones. Every thunk is inert until active_ is raised at device_created.
const char* install_all(bool with_diagnostics) {
    if (!pin_self()) return "pin_failed";
    for (unsigned i = 0; i < claim_count; ++i) {
        if (claims[i].diagnostic) continue;
        const char* r = install_site(i);
        if (std::strcmp(r, "ok")) {
            failed_site_ = claims[i].name;
            restore_all();
            return r;
        }
    }
    if (const char* r = install_materials(); std::strcmp(r, "ok")) {
        failed_site_ = "text_density_materials";
        restore_all();
        return r;
    }
    for (unsigned i = 0; with_diagnostics && i < claim_count; ++i) {
        if (!claims[i].diagnostic) continue;
        const char* r = install_site(i);
        if (std::strcmp(r, "ok")) {
            failed_site_ = claims[i].name;
            restore_all();
            return r;
        }
    }
    claimed_ = true;
    diagnostic_ = with_diagnostics;
    return "ok";
}
// ---- the config field and the style imm32 ----
std::uint32_t* config_density_cell() {
    std::uint32_t config = 0;
    if (!x3m::engine_memory::read(sites::config_slot_va, &config, sizeof config) || !config ||
        config > UINTPTR_MAX - sites::config_density_offset - 4)
        return nullptr;
    std::uint32_t current = 0;
    if (!x3m::engine_memory::read(config + sites::config_density_offset, &current, sizeof current)) return nullptr;
    return reinterpret_cast<std::uint32_t*>(config + sites::config_density_offset);
}
bool write_config(unsigned d) {
    std::uint32_t* cell = config_density_cell();
    if (!cell) return false;
    if (!config_written_) config_before_ = *cell;
    *static_cast<volatile std::uint32_t*>(cell) = d;
    config_written_ = true;
    return *static_cast<volatile std::uint32_t*>(cell) == d;
}
void restore_config() {
    if (!config_written_) return;
    if (std::uint32_t* cell = config_density_cell()) *static_cast<volatile std::uint32_t*>(cell) = config_before_;
    config_written_ = false;
}
// One lock cmpxchg8b of the imm32 (inside its aligned qword), read back; fail closed.
const char* write_style(unsigned d) {
    if (style_patched_) return "ok";
    if (!code_is(sites::style_window_va, sites::expected_style_window, sites::style_window_length)) return "style_mismatch";
    auto* code = reinterpret_cast<unsigned char*>(sites::style_write_va);
    if (!VirtualProtect(code, sites::style_write_length, PAGE_EXECUTE_READWRITE, &style_protection_)) return "style_protect_failed";
    const unsigned char bytes[4] = {static_cast<unsigned char>(d), 0, 0, 0};
    const bool atomic = engine_patch::write_code(sites::style_write_va, bytes, 4);
    style_write_ = atomic ? "atomic" : "plain";
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), code, 4) != FALSE;
    DWORD unused = 0;
    VirtualProtect(code, sites::style_write_length, style_protection_, &unused);
    unsigned char now[4]{};
    if (!flushed || !engine_patch::read_code(sites::style_write_va, now, 4) || std::memcmp(now, bytes, 4)) {
        style_patched_ = true; // whatever is there now is ours to put back
        return "style_readback_failed";
    }
    style_patched_ = true;
    return "ok";
}
void restore_style() {
    if (!style_patched_) return;
    auto* code = reinterpret_cast<unsigned char*>(sites::style_write_va);
    DWORD previous = 0, unused = 0;
    if (!VirtualProtect(code, sites::style_write_length, PAGE_EXECUTE_READWRITE, &previous)) return;
    engine_patch::write_code(sites::style_write_va, sites::expected_style_window + sites::style_write_offset, 4);
    FlushInstructionCache(GetCurrentProcess(), code, 4);
    VirtualProtect(code, sites::style_write_length, previous, &unused);
    style_patched_ = false;
}
// ---- the Materials rows ----
bool read_tables(Tables* t) {
    std::int16_t rows = 0;
    std::uint32_t rows_base = 0, table = 0;
    if (!x3m::engine_memory::read(sites::row_count_va, &rows, sizeof rows) ||
        !x3m::engine_memory::read(sites::rows_slot_va, &rows_base, sizeof rows_base) ||
        !x3m::engine_memory::read(sites::table_slot_va, &table, sizeof table) || rows <= 0 || !rows_base || !table)
        return false;
    const std::uintptr_t rows_end = rows_base + std::uintptr_t(rows) * sites::row_stride,
                         table_end = table + std::uintptr_t(rows) * sites::entry_stride;
    std::uint32_t probe = 0;
    if (rows_end < rows_base || table_end < table || !x3m::engine_memory::read(rows_base, &probe, 4) ||
        !x3m::engine_memory::read(rows_end - 4, &probe, 4) || !x3m::engine_memory::read(table, &probe, 4) ||
        !x3m::engine_memory::read(table_end - 4, &probe, 4))
        return false;
    t->rows = unsigned(rows);
    t->rows_base = rows_base;
    t->table = table;
    return true;
}
std::uint32_t* row_flags(const Tables& t, unsigned id) {
    return reinterpret_cast<std::uint32_t*>(t.rows_base + id * sites::row_stride + sites::row_flags_offset);
}
std::uint32_t* entry_flags(const Tables& t, unsigned id) {
    return reinterpret_cast<std::uint32_t*>(t.table + id * sites::entry_stride + sites::entry_flags_offset);
}
std::uint32_t entry_object(const Tables& t, unsigned id) {
    return *reinterpret_cast<const std::uint32_t*>(t.table + id * sites::entry_stride + sites::entry_object_offset);
}
// Sets MPF_FONTSCALE on the generated writeable rows (clearing MPF_NOFILTERING when s != d) and clears it on the
// file-backed rows, in the row and in the texture-table entry. Refuses when a row to flag already has a texture
// object (0x004f4160 sized it at 1x). nullptr = applied.
const char* apply_rows(unsigned d, double s) {
    Tables t{};
    if (!read_tables(&t)) return "tables_unreadable";
    const bool clear_nofiltering = double(d) != s;
    unsigned existing = 0;
    for (unsigned id = 0; id < t.rows; ++id) {
        const sites::RowPlan plan = sites::plan_row(*row_flags(t, id), clear_nofiltering);
        if (plan.edit == sites::RowEdit::flag && entry_object(t, id)) ++existing;
    }
    if (existing) return "objects_exist";
    rows_ = t.rows;
    rows_flagged_ = rows_unflagged_ = rows_nofilter_ = 0;
    for (unsigned id = 0; id < t.rows; ++id) {
        std::uint32_t* row = row_flags(t, id);
        std::uint32_t* entry = entry_flags(t, id);
        const sites::RowPlan plan = sites::plan_row(*row, clear_nofiltering);
        if (plan.edit == sites::RowEdit::none) continue;
        const std::uint32_t changed = *row ^ plan.flags;
        if (plan.edit == sites::RowEdit::flag) {
            ++rows_flagged_;
            if (changed & sites::mpf_nofiltering) ++rows_nofilter_;
        } else {
            ++rows_unflagged_;
        }
        *row = plan.flags;
        *entry = (*entry & ~changed) | (plan.flags & changed);
    }
    rows_applied_ = true;
    return nullptr;
}
// The largest row the edit would flag (1x size), for the texture-limit check; false when the tables are unreadable.
bool largest_flagged_row(unsigned* w, unsigned* h) {
    Tables t{};
    if (!read_tables(&t)) return false;
    *w = *h = 0;
    for (unsigned id = 0; id < t.rows; ++id) {
        if (sites::plan_row(*row_flags(t, id), true).edit != sites::RowEdit::flag &&
            !((*row_flags(t, id) & sites::mpf_generated) && (*row_flags(t, id) & sites::mpf_writeable)))
            continue;
        const std::int16_t rw = *reinterpret_cast<const std::int16_t*>(t.rows_base + id * sites::row_stride + sites::row_width_offset);
        const std::int16_t rh = *reinterpret_cast<const std::int16_t*>(t.rows_base + id * sites::row_stride + sites::row_height_offset);
        if (rw > 0 && unsigned(rw) > *w) *w = unsigned(rw);
        if (rh > 0 && unsigned(rh) > *h) *h = unsigned(rh);
    }
    return true;
}
// The blit path's raw reads of the three static slots (the engine's lookup reads them the same way; the rows and
// the table exist once the row edit ran, which the blit path requires).
Tables live_tables() {
    Tables t{};
    const std::int16_t rows = *reinterpret_cast<const volatile std::int16_t*>(sites::row_count_va);
    t.rows = rows > 0 ? unsigned(rows) : 0;
    t.rows_base = *reinterpret_cast<const volatile std::uint32_t*>(sites::rows_slot_va);
    t.table = *reinterpret_cast<const volatile std::uint32_t*>(sites::table_slot_va);
    if (!t.rows_base || !t.table) t.rows = 0;
    return t;
}
bool row_fontscale(const Tables& t, unsigned id) {
    return id < t.rows && (*row_flags(t, id) & sites::mpf_fontscale) != 0;
}
// Lowers the density to what the device's texture limits allow for the largest row to flag (the 64x2048 row 8 is
// 6144 texels high at d = 3) and re-gates the fonts for the lowered density; nullptr = fine (density_ may have
// changed), else the refusal reason. Needs the rows (after the Materials load) and runs before any font open or
// texture creation, so a lowered density is as clean as the first choice.
const char* fit_density() {
    if (!largest_flagged_row(&largest_w_, &largest_h_)) return "tables_unreadable";
    const unsigned fitted = sites::caps_density(density_, largest_w_, largest_h_, caps_w_, caps_h_);
    if (fitted == density_) return nullptr;
    if (fitted < 2) return "caps";
    if (fonts_missing(fitted)) return "fonts_missing";
    if (style_patched_ && fitted != 3) restore_style();
    if (!write_config(fitted)) return "config_unwritable";
    caps_limited_d_ = fitted;
    density_ = fitted;
    return nullptr;
}
// Fail closed after the density went in: the config field and the style factor back, every claim restored.
void disable(const char* reason) {
    InterlockedExchange(&active_, 0);
    filter_set_.count = 0;
    restore_config();
    restore_style();
    restore_all();
    density_ = 1;
    state_ = reason;
}
// ---- the shadow cache ----
void* lookup_object(std::int32_t id);
void* alloc_surface(std::uint32_t bpp, int w, int h, std::uint32_t flags);
void free_surface(void** object) {
    reinterpret_cast<FreeFn>(sites::free_va)(object);
}
std::int16_t object_width(const void* object) {
    return *reinterpret_cast<const std::int16_t*>(static_cast<const unsigned char*>(object) + sites::object_width_offset);
}
std::int16_t object_height(const void* object) {
    return *reinterpret_cast<const std::int16_t*>(static_cast<const unsigned char*>(object) + sites::object_height_offset);
}
void shadow_row(std::int32_t src, const char* status, const char* reason, unsigned w, unsigned h, std::uint32_t bytes,
                unsigned long ms) {
    if (shadow_rows_ >= shadow_row_limit) return;
    ++shadow_rows_;
    x3m::log("text_density_shadow src=%ld status=%s reason=%s size=%ux%u density=%u bytes=%lu total=%lu slots=%u ms=%lu", long(src),
             status, reason, w, h, density_, static_cast<unsigned long>(bytes), static_cast<unsigned long>(shadow_bytes_),
             shadow_count_, ms);
}
// The d x nearest-neighbour copy of a static source through the engine's own plain copy leaf: d*W column copies
// from the source (x), then d*H row copies inside the shadow from the last row up (y; every write lands at or below
// the row it reads, so no source row is overwritten before its use). Caller holds the lock.
void* build_shadow(std::int32_t src, void* src_object) {
    const unsigned d = density_;
    const int W = object_width(src_object), H = object_height(src_object);
    std::uint32_t bytes = 0;
    if (W <= 0 || H <= 0 || !sites::shadow_fits(unsigned(W), unsigned(H), d, shadow_bytes_, &bytes)) {
        shadow_row(src, "refused", W <= 0 || H <= 0 ? "empty_source" : "budget", unsigned(W), unsigned(H), bytes, 0);
        return nullptr;
    }
    if (shadow_count_ >= sites::shadow_slots) {
        shadow_row(src, "refused", "slots", unsigned(W), unsigned(H), bytes, 0);
        return nullptr;
    }
    const ULONGLONG begin = GetTickCount64();
    void* shadow = alloc_surface(sites::generated_bits_per_pixel, W * int(d), H * int(d), sites::generated_surface_flags);
    if (!shadow) {
        shadow_row(src, "refused", "alloc_failed", unsigned(W), unsigned(H), bytes, 0);
        return nullptr;
    }
    const CopyFn copy = reinterpret_cast<CopyFn>(sites::copy_va);
    for (int i = 0; i < W; ++i)
        for (unsigned k = 0; k < d; ++k) copy(src_object, shadow, i, 0, i * int(d) + int(k), 0, 1, H);
    for (int j = H - 1; j >= 0; --j)
        for (int l = int(d) - 1; l >= 0; --l) {
            const int row = j * int(d) + l;
            if (row != j) copy(shadow, shadow, 0, j, 0, row, W * int(d), 1);
        }
    shadows_[shadow_count_++] = Shadow{src, shadow, bytes};
    shadow_bytes_ += bytes;
    shadow_row(src, "built", "ok", unsigned(W), unsigned(H), bytes, static_cast<unsigned long>(GetTickCount64() - begin));
    return shadow;
}
bool shadow_refused(std::int32_t src) {
    for (unsigned i = 0; i < refused_count_; ++i)
        if (refused_[i] == src) return true;
    return false;
}
void remember_refused(std::int32_t src) {
    if (refused_count_ < refused_slots) refused_[refused_count_++] = src;
}
void* shadow_for(std::int32_t src) {
    AcquireSRWLockExclusive(&shadow_lock_);
    void* found = nullptr;
    for (unsigned i = 0; i < shadow_count_; ++i)
        if (shadows_[i].id == src) {
            found = shadows_[i].object;
            break;
        }
    if (!found && !shadow_refused(src)) {
        if (void* src_object = lookup_object(src)) found = build_shadow(src, src_object);
        if (!found) remember_refused(src);
    }
    ReleaseSRWLockExclusive(&shadow_lock_);
    return found;
}
void diagnostic_row(unsigned function, std::int32_t src, std::int32_t dst, unsigned dst_flagged, unsigned src_flagged,
                    unsigned src_generated, unsigned handled) {
    if (!x3m::log_tier::cached_debug || function > sites::kind_rect_fill) return;
    if (!sites::diagnostic_first(diagnostic_table_, &diagnostic_count_, sites::DiagnosticKey{std::uint8_t(function), src, dst}))
        return;
    x3m::log("text_density_draw fn=%s src=%ld dst=%ld dst_flagged=%u src_flagged=%u src_generated=%u handled=%u", diagnostic_names[function],
             long(src), long(dst), dst_flagged, src_flagged, src_generated, handled);
}
// The text line (0x0048b2d0: dst, text, font slot, x, y, right, colour, flags): one row per distinct (font, dst).
void text_line_row(std::int32_t font, std::int32_t x, std::int32_t dst, unsigned dst_flagged) {
    if (!x3m::log_tier::cached_debug) return;
    if (!sites::diagnostic_first(diagnostic_table_, &diagnostic_count_, sites::DiagnosticKey{std::uint8_t(sites::kind_text_line), font, dst}))
        return;
    x3m::log("text_density_draw fn=text_line font=%ld x=%ld dst=%ld dst_flagged=%u", long(font), long(x), long(dst), dst_flagged);
}
// Rebuilds the set of flagged rows' D3D textures (validated reads of the entry object and its +0x34) once per Present.
void rebuild_filter_set() {
    sites::FilterSet next{};
    Tables t{};
    if (!read_tables(&t)) {
        filter_set_ = next;
        return;
    }
    for (unsigned id = 0; id < t.rows; ++id) {
        const std::uint32_t flags = *row_flags(t, id);
        if ((flags & (sites::mpf_fontscale | sites::mpf_generated)) != (sites::mpf_fontscale | sites::mpf_generated)) continue;
        std::uint32_t object = 0, texture = 0;
        if (!x3m::engine_memory::read(t.table + id * sites::entry_stride + sites::entry_object_offset, &object, 4) || !object ||
            object > UINTPTR_MAX - 0x64 || !x3m::engine_memory::read(object + sites::object_texture_offset, &texture, 4))
            continue;
        sites::filter_set_insert(&next, texture);
    }
    filter_set_ = next;
}
bool filter_wanted() {
    return active_ != 0 && density_ > 1 && double(density_) != scale_;
}
}

extern "C" {
std::uintptr_t x3m_text_density_font_continue = 0, x3m_text_density_materials_continue = sites::materials_target_va,
               x3m_text_density_materials_return = 0, x3m_text_density_blt_block_continue = 0,
               x3m_text_density_blt_alpha_continue = 0, x3m_text_density_text_line_continue = 0,
               x3m_text_density_rect_fill_continue = 0;
// The engine calls through register conventions (text_density_sites.h): shims in this module's .text.
void* __cdecl x3m_text_density_call_lookup(std::int32_t id);
void* __cdecl x3m_text_density_call_alloc(std::uint32_t bpp, int w, int h, std::uint32_t flags);
void __cdecl x3m_text_density_call_leaf(std::uintptr_t fn, void* dst, void* src, int sx, int sy, int dx, int dy, int w, int h,
                                        int colour);
extern std::uintptr_t x3m_text_density_lookup_fn, x3m_text_density_alloc_fn;

// The font open (0x0048cdc0): EAX = name, ECX = size, [args] cell width, flags, y offset. When the d x pair exists
// under <game>\f the request is scaled in place (the saved ECX and the two stack arguments); otherwise it passes
// unchanged and one row says which file was missing (a missing font would make the open fail and KC store -1).
void __cdecl x3m_text_density_font_open(std::uint32_t* frame) {
    const DWORD error = GetLastError();
    if (active_ && density_ > 1) {
        char name[sites::font_name_capacity + 2] = "";
        const std::uintptr_t text = frame[sites::frame_eax];
        unsigned n = 0;
        bool readable = text != 0;
        while (readable && n <= sites::font_name_capacity) {
            char c = 0;
            if (!x3m::engine_memory::read(text + n, &c, 1)) {
                readable = false;
                break;
            }
            name[n] = c;
            if (!c) break;
            ++n;
        }
        name[sites::font_name_capacity + 1] = 0;
        const unsigned size = frame[sites::frame_ecx];
        char abc[sites::font_path_capacity] = "-";
        const char* status = "invalid";
        if (readable && n <= sites::font_name_capacity && font_pair_exists(name, size, density_, abc)) {
            frame[sites::frame_ecx] = size * density_;
            frame[sites::frame_args + sites::font_arg_cell_width] *= density_;
            frame[sites::frame_args + sites::font_arg_y_offset] *= density_;
            status = "scaled";
        } else if (readable && n <= sites::font_name_capacity && size >= 1 && size <= 255) {
            status = "missing";
            sites::font_file(name, size, density_, ".abc", abc);
        }
        if (font_rows_ < font_row_limit) {
            ++font_rows_;
            x3m::log("text_density_font name=%s size=%u density=%u status=%s file=%s cell_width=%lu y_offset=%lu", readable ? name : "?",
                     size, density_, status, abc, static_cast<unsigned long>(frame[sites::frame_args + sites::font_arg_cell_width]),
                     static_cast<unsigned long>(frame[sites::frame_args + sites::font_arg_y_offset]));
        }
    }
    SetLastError(error);
}
// After 0x004f44a0 returned (the init thread, once per process): the rows exist and no texture object does yet.
void __cdecl x3m_text_density_materials_loaded() {
    const DWORD error = GetLastError();
    materials_loaded_ = true;
    const char* status = "pending";
    const char* reason = "density_unresolved";
    if (rows_applied_) {
        status = "already";
        reason = "-";
    } else if (active_ && density_ > 1) {
        reason = fit_density();
        if (!reason) reason = apply_rows(density_, scale_);
        status = reason ? "refused" : "applied";
        if (!reason) reason = "ok";
        if (std::strcmp(reason, "ok")) disable(reason);
    } else if (resolved_) {
        status = "off";
        reason = "density_1";
    }
    x3m::log("text_density_rows status=%s reason=%s density=%u rows=%u flagged=%u unflagged=%u nofilter_cleared=%u caps_limited_d=%u "
             "largest_row=%ux%u missing=%s",
             status, reason, density_, rows_, rows_flagged_, rows_unflagged_, rows_nofilter_, caps_limited_d_, largest_w_, largest_h_, missing_);
    SetLastError(error);
}
// The two block blits: when the destination is a flagged row and the source is a static unflagged texture, the
// engine's leaf runs with the d x shadow as the source and every coordinate in texels (the destination clip done
// here, in layout units, exactly as 0x0048c090 does it); returns the destination object (handled) or 0 (the original
// runs). A composite destination (negative) recurses through the same entry per part, so it passes.
std::uint32_t __cdecl x3m_text_density_blit(std::uint32_t* frame, std::uint32_t kind) {
    if (!active_ || density_ <= 1 || !rows_applied_) return 0;
    const DWORD error = GetLastError();
    std::uint32_t handled = 0;
    std::int32_t* args = reinterpret_cast<std::int32_t*>(frame + sites::frame_args);
    const std::int32_t src = args[sites::arg_src], dst = args[sites::arg_dst];
    const Tables t = live_tables();
    if (dst >= 0 && src >= 0 && unsigned(dst) < t.rows) {
        const bool dst_flagged = row_fontscale(t, unsigned(dst));
        const bool src_flagged = row_fontscale(t, unsigned(src));
        bool src_generated = false;
        if (unsigned(src) < t.rows) {
            src_generated = (*row_flags(t, unsigned(src)) & sites::mpf_generated) != 0;
        } else {
            // A named texture (a file atlas): its entry flags are the material's, GENERATED never set; the entry is
            // read through the validated reader (the table pointer above is this call's).
            std::uint32_t named = 0;
            const std::int16_t extra = *reinterpret_cast<const volatile std::int16_t*>(sites::named_count_va);
            if (extra > 0 && unsigned(src) < t.rows + unsigned(extra) &&
                x3m::engine_memory::read(t.table + unsigned(src) * sites::entry_stride + sites::entry_flags_offset, &named, 4))
                src_generated = (named & sites::mpf_generated) != 0;
            else
                src_generated = true; // unknown: leave it to the engine
        }
        if (dst_flagged && !src_flagged && !src_generated) {
            void* dst_object = lookup_object(dst);
            void* shadow = dst_object ? shadow_for(src) : nullptr;
            if (dst_object && shadow) {
                const int d = int(density_);
                int sx = args[sites::arg_sx], sy = args[sites::arg_sy], dx = args[sites::arg_dx], dy = args[sites::arg_dy],
                    w = args[sites::arg_w], h = args[sites::arg_h];
                bool draw = true;
                if (args[sites::arg_clip])
                    draw = sites::clip_destination(object_width(dst_object) / d, object_height(dst_object) / d, &sx, &sy, &dx, &dy, &w, &h);
                if (draw) {
                    const int colour = args[sites::arg_colour];
                    if (kind == sites::kind_blt_alpha)
                        x3m_text_density_call_leaf(sites::alpha_leaf_va, dst_object, shadow, sx * d, sy * d, dx * d, dy * d, w * d, h * d, colour);
                    else if (colour >= 0)
                        x3m_text_density_call_leaf(sites::colour_copy_va, dst_object, shadow, sx * d, sy * d, dx * d, dy * d, w * d, h * d, colour);
                    else
                        reinterpret_cast<CopyFn>(sites::copy_va)(shadow, dst_object, sx * d, sy * d, dx * d, dy * d, w * d, h * d);
                }
                handled = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(dst_object));
            }
        }
        diagnostic_row(kind, src, dst, dst_flagged, src_flagged, src_generated, handled ? 1u : 0u);
    }
    SetLastError(error);
    return handled;
}
// The diagnostic entries (debug): the text line (dst = arg0, font slot = arg2, x = arg3) and the rect fill (dst = arg0).
void __cdecl x3m_text_density_diagnostic(std::uint32_t* frame, std::uint32_t kind) {
    if (!x3m::log_tier::cached_debug || kind > sites::kind_rect_fill) return;
    const DWORD error = GetLastError();
    const std::int32_t* args = reinterpret_cast<const std::int32_t*>(frame + sites::frame_args);
    const std::int32_t dst = args[0];
    unsigned dst_flagged = 0;
    if (dst >= 0) {
        Tables t{};
        if (read_tables(&t) && unsigned(dst) < t.rows) dst_flagged = (*row_flags(t, unsigned(dst)) & sites::mpf_fontscale) ? 1u : 0u;
    }
    if (kind == sites::kind_text_line)
        text_line_row(args[2], args[3], dst, dst_flagged); // font slot = arg2, x = arg3
    else
        diagnostic_row(kind, -1, dst, dst_flagged, 0, 0, 0);
    SetLastError(error);
}
std::uintptr_t x3m_text_density_lookup_fn = sites::lookup_va, x3m_text_density_alloc_fn = sites::alloc_va;
}
namespace {
void* lookup_object(std::int32_t id) {
    return x3m_text_density_call_lookup(id);
}
void* alloc_surface(std::uint32_t bpp, int w, int h, std::uint32_t flags) {
    return x3m_text_density_call_alloc(bpp, w, h, flags);
}
}
// The thunks. Entry claims: pushfd/pushad, the C call with a pointer to the saved frame, popad/popfd, then the tail
// (DF cleared for the C ABI and restored by popfd). The blit thunks return the C result in the saved EAX slot: EAX is
// dead at both entries (the verifier proves the first EAX access is a write), so a non-zero result skips the
// original with `ret` (cdecl: the caller pops) and zero continues into the tail; the flags after `test` are dead too
// (the displaced `sub esp,0x24` writes them and `test ecx,ecx` at +7 rewrites them before any reader). The Materials thunk takes
// the game's return address off the stack, calls the original with the stack exactly as the game's call left it,
// then runs the C side and returns with EAX forwarded. The shims hand register arguments to the engine helpers.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_text_density_font_thunk
_x3m_text_density_font_thunk:
    pushfd
    pushad
    cld
    mov eax, esp
    push eax
    call _x3m_text_density_font_open
    add esp, 4
    popad
    popfd
    jmp dword ptr [_x3m_text_density_font_continue]
    .p2align 4
    .globl _x3m_text_density_materials_thunk
_x3m_text_density_materials_thunk:
    pop dword ptr [_x3m_text_density_materials_return]
    call dword ptr [_x3m_text_density_materials_continue]
    pushfd
    pushad
    cld
    call _x3m_text_density_materials_loaded
    popad
    popfd
    jmp dword ptr [_x3m_text_density_materials_return]
    .p2align 4
    .globl _x3m_text_density_blt_block_thunk
_x3m_text_density_blt_block_thunk:
    pushfd
    pushad
    cld
    mov eax, esp
    push 1
    push eax
    call _x3m_text_density_blit
    add esp, 8
    mov dword ptr [esp+28], eax
    popad
    popfd
    test eax, eax
    jnz 1f
    jmp dword ptr [_x3m_text_density_blt_block_continue]
1:  ret
    .p2align 4
    .globl _x3m_text_density_blt_alpha_thunk
_x3m_text_density_blt_alpha_thunk:
    pushfd
    pushad
    cld
    mov eax, esp
    push 2
    push eax
    call _x3m_text_density_blit
    add esp, 8
    mov dword ptr [esp+28], eax
    popad
    popfd
    test eax, eax
    jnz 2f
    jmp dword ptr [_x3m_text_density_blt_alpha_continue]
2:  ret
    .p2align 4
    .globl _x3m_text_density_text_line_thunk
_x3m_text_density_text_line_thunk:
    pushfd
    pushad
    cld
    mov eax, esp
    push 0
    push eax
    call _x3m_text_density_diagnostic
    add esp, 8
    popad
    popfd
    jmp dword ptr [_x3m_text_density_text_line_continue]
    .p2align 4
    .globl _x3m_text_density_rect_fill_thunk
_x3m_text_density_rect_fill_thunk:
    pushfd
    pushad
    cld
    mov eax, esp
    push 3
    push eax
    call _x3m_text_density_diagnostic
    add esp, 8
    popad
    popfd
    jmp dword ptr [_x3m_text_density_rect_fill_continue]
    .p2align 4
    .globl _x3m_text_density_call_lookup
_x3m_text_density_call_lookup:
    mov ecx, dword ptr [esp+4]
    jmp dword ptr [_x3m_text_density_lookup_fn]
    .p2align 4
    .globl _x3m_text_density_call_alloc
_x3m_text_density_call_alloc:
    push ebp
    mov ebp, esp
    push dword ptr [ebp+20]
    push dword ptr [ebp+16]
    push dword ptr [ebp+12]
    mov eax, dword ptr [ebp+8]
    call dword ptr [_x3m_text_density_alloc_fn]
    add esp, 12
    pop ebp
    ret
    .p2align 4
    .globl _x3m_text_density_call_leaf
_x3m_text_density_call_leaf:
    push ebp
    mov ebp, esp
    push edi
    push dword ptr [ebp+44]
    push dword ptr [ebp+40]
    push dword ptr [ebp+36]
    push dword ptr [ebp+32]
    push dword ptr [ebp+28]
    push dword ptr [ebp+24]
    push dword ptr [ebp+20]
    mov edi, dword ptr [ebp+16]
    mov eax, dword ptr [ebp+12]
    call dword ptr [ebp+8]
    add esp, 28
    pop edi
    pop ebp
    ret
    .att_syntax
)");

namespace x3m::text_density {
bool initialize() {
    const DWORD error = GetLastError();
    if (requested_ || claimed_) {
        SetLastError(error);
        return claimed_;
    }
    wchar_t text[sites::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_TEXT_DENSITY", text, sites::setting_capacity);
    printable(text, length, setting_);
    unsigned value = 1;
    const sites::Parse parsed = length >= sites::setting_capacity ? sites::Parse::invalid : sites::parse_setting(text, &value);
    const char* mode = "off";
    const bool debug = log_tier::debug();
    if (length >= sites::setting_capacity)
        state_ = "too_long";
    else if (parsed == sites::Parse::invalid)
        state_ = "invalid_setting";
    else if (parsed == sites::Parse::value && value == 1)
        state_ = "off";
    else if (!object_trace::executable_verified())
        state_ = "executable_mismatch";
    else if (const char* mismatch = windows_verified())
        state_ = mismatch;
    else if (!resolve_game_dir())
        state_ = "no_game_dir";
    else {
        mode_ = parsed;
        value_ = value;
        mode = parsed == sites::Parse::automatic ? "auto" : "fixed";
        state_ = install_all(debug);
        requested_ = !std::strcmp(state_, "ok");
        if (requested_) state_ = "pending"; // resolved and applied at CreateDevice
    }
    char writes[claim_count * 8 + 8] = "";
    for (unsigned i = 0; i < claim_count; ++i) {
        if (i) std::strncat(writes, ",", sizeof writes - std::strlen(writes) - 1);
        std::strncat(writes, write_[i] ? write_[i] : "none", sizeof writes - std::strlen(writes) - 1);
    }
    std::strncat(writes, ",", sizeof writes - std::strlen(writes) - 1);
    std::strncat(writes, materials_write_, sizeof writes - std::strlen(writes) - 1);
    const bool refused = !requested_ && std::strcmp(state_, "off");
    log("text_density setting=%s status=%s reason=%s mode=%s value=%u diagnostic=%u failed_site=%s writes=%s site=%08lx", setting_,
        requested_ ? "pending" : refused ? "refused" : "off", state_, mode, value_, diagnostic_ ? 1u : 0u, failed_site_, writes,
        static_cast<unsigned long>(sites::font_site_va));
    SetLastError(error);
    return false;
}
bool device_created(double scale, IDirect3DDevice9* device) {
    const DWORD error = GetLastError();
    if (!requested_ || resolved_) {
        SetLastError(error);
        return active_ != 0;
    }
    resolved_ = true;
    scale_ = scale > 0 ? scale : 1.0;
    D3DCAPS9 limits{}; // the texture size limits (documented GetDeviceCaps); 0 = unknown when the query fails
    if (device && SUCCEEDED(device->GetDeviceCaps(&limits))) {
        caps_w_ = limits.MaxTextureWidth;
        caps_h_ = limits.MaxTextureHeight;
    }
    density_ = sites::density_for(mode_, value_, scale_);
    const char* reason = "ok";
    const char* rows = "pending";
    char rows_text[48] = "pending";
    if (density_ <= 1) {
        reason = "density_1"; // no UI scale: nothing changes; the inert claims go back
        restore_all();
        density_ = 1;
    } else if (fonts_missing(density_)) {
        // The gate: without every d x pair the engine would open its 1x fonts into d x textures (1/d text, 1/d line
        // step). Nothing is flagged, the field is 1 (a user's own -fontscale would break the same way), the claims go back.
        reason = "fonts_missing";
        write_config(1);
        restore_all();
        density_ = 1;
    } else if (!write_config(density_)) {
        reason = "config_unwritable";
        disable(reason);
    } else if (density_ != 2 && std::strcmp(write_style(density_), "ok")) {
        reason = "style_failed";
        disable(reason);
    } else {
        InterlockedExchange(&active_, 1);
        if (materials_loaded_) {
            const char* r = fit_density();
            if (!r) r = apply_rows(density_, scale_);
            if (r) {
                reason = r;
                disable(reason);
            } else {
                std::snprintf(rows_text, sizeof rows_text, "%u/%u/%u", rows_flagged_, rows_unflagged_, rows_nofilter_);
                rows = rows_text;
            }
        }
    }
    const bool applied = active_ != 0;
    state_ = reason;
    // Which of the four families the loose f\ directory holds at this density (both files of the pair).
    char fonts[64] = "";
    for (unsigned i = 0; i < 4; ++i) {
        const sites::FontFamily& f = sites::font_families[i];
        char abc[sites::font_path_capacity];
        const bool present = density_ > 1 && font_pair_exists(f.name, f.size, density_, abc);
        char one[24];
        std::snprintf(one, sizeof one, "%s%s%u", i ? "," : "", present ? f.name : "-", present ? f.size * density_ : 0u);
        std::strncat(fonts, one, sizeof fonts - std::strlen(fonts) - 1);
    }
    log("text_density_install density=%u scale=%.4f status=%s reason=%s mode=%s config_before=%lu style=%s style_write=%s rows=%s fonts=%s "
        "missing=%s caps_limited_d=%u max_texture=%ux%u arena_used=%u",
        density_, scale_, applied ? "patched" : any_live() ? "patched_unverified" : !std::strcmp(reason, "density_1") ? "off" : "refused", reason,
        mode_ == sites::Parse::automatic ? "auto" : "fixed", static_cast<unsigned long>(config_before_), style_patched_ ? "patched" : "stock",
        style_write_, rows, fonts, missing_, caps_limited_d_, caps_w_, caps_h_, engine_patch::arena_used());
    SetLastError(error);
    return applied;
}
void before_reset() {
    if (!active_) return;
    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&shadow_lock_);
    const unsigned count = shadow_count_;
    const std::uint32_t bytes = shadow_bytes_;
    for (unsigned i = 0; i < shadow_count_; ++i)
        if (shadows_[i].object) free_surface(&shadows_[i].object);
    shadow_count_ = 0;
    shadow_bytes_ = 0;
    refused_count_ = 0; // budget and slot refusals are retried after the Reset emptied the cache
    ReleaseSRWLockExclusive(&shadow_lock_);
    if (count) log("text_density_reset shadows=%u bytes=%lu", count, static_cast<unsigned long>(bytes));
    SetLastError(error);
}
bool filter_hooks_wanted() {
    return filter_wanted();
}
void bound_texture(unsigned stage, IDirect3DBaseTexture9* texture) {
    if (stage == 0) stage0_ = reinterpret_cast<std::uintptr_t>(texture);
}
// Before a submitted draw: one pointer probe of the sorted set; a hit reads the two sampler values (documented
// GetSamplerState) and sets LINEAR where they differ. Same thread as present() (the render thread), no lock.
bool draw_filter_override(IDirect3DDevice9* device, SamplerGet get, SamplerSet set) {
    if (!filter_set_.count || !device || !get || !set) return false;
    const std::uintptr_t texture = stage0_;
    if (!texture || !sites::filter_set_contains(filter_set_, texture)) return false;
    unsigned long min_filter = 0, mag_filter = 0;
    if (get(device, 0, D3DSAMP_MINFILTER, &min_filter) < 0 || get(device, 0, D3DSAMP_MAGFILTER, &mag_filter) < 0) {
        // A pure device has no sampler state to read: one row, then no further attempt (the set is dropped).
        filter_set_.count = 0;
        if (!filter_unavailable_) {
            filter_unavailable_ = true;
            log("text_density_filter status=unavailable reason=get_sampler_state_failed textures=0");
        }
        return false;
    }
    const bool raise_min = sites::filter_needs_raise(min_filter), raise_mag = sites::filter_needs_raise(mag_filter);
    if (!filter_logged_) {
        // Once: what the engine set on the first text-quad draw, MAXANISOTROPY included (read, never changed).
        filter_logged_ = true;
        unsigned long aniso = 0;
        if (get(device, 0, D3DSAMP_MAXANISOTROPY, &aniso) < 0) aniso = 0;
        log("text_density_filter status=active min_before=%lu mag_before=%lu aniso=%lu raised=%s textures=%u", static_cast<unsigned long>(min_filter),
            static_cast<unsigned long>(mag_filter), aniso, raise_min && raise_mag ? "both" : raise_min ? "min" : raise_mag ? "mag" : "none",
            filter_set_.count);
    }
    if (!raise_min && !raise_mag) {
        ++filter_already_linear_;
        return false;
    }
    raised_min_ = raise_min;
    raised_mag_ = raise_mag;
    saved_min_ = min_filter;
    saved_mag_ = mag_filter;
    if (raise_min) set(device, 0, D3DSAMP_MINFILTER, sites::d3d_texf_linear);
    if (raise_mag) set(device, 0, D3DSAMP_MAGFILTER, sites::d3d_texf_linear);
    ++filter_draws_;
    return true;
}
void draw_filter_restore(IDirect3DDevice9* device, SamplerSet set) {
    if (!device || !set) return;
    if (raised_min_) set(device, 0, D3DSAMP_MINFILTER, saved_min_);
    if (raised_mag_) set(device, 0, D3DSAMP_MAGFILTER, saved_mag_);
}
void present(unsigned long long frame) {
    if (!filter_wanted() || filter_unavailable_) {
        filter_set_.count = 0; // a stale set never overrides after a refusal or an unavailable device
        return;
    }
    const DWORD error = GetLastError();
    rebuild_filter_set();
    if (x3m::log_tier::cached_debug && frame % filter_report_window == 0) {
        log("text_density_filter_frame frame=%llu textures=%u overridden=%u already_linear=%u", frame, filter_set_.count, filter_draws_,
            filter_already_linear_);
        filter_draws_ = 0;
        filter_already_linear_ = 0;
    }
    SetLastError(error);
}
bool shutdown() {
    if (!any_live() && !config_written_ && !style_patched_) return true;
    const DWORD error = GetLastError();
    InterlockedExchange(&active_, 0);
    filter_set_.count = 0;
    restore_config();
    restore_style();
    const bool clean = restore_all();
    state_ = clean ? "restored" : "restore_failed";
    // One row straight to the log's OS handle: this runs inside DllMain (dynamic unload), where the capture lock
    // must not be taken. The thunks stay callable (this module is pinned) and the tails stay in the arena.
    const HANDLE handle = log_handle();
    if (handle && handle != INVALID_HANDLE_VALUE) {
        char line[160];
        const int n = std::snprintf(line, sizeof line, "text_density_restore status=%s registered=%u\n", state_, any_live() ? 1u : 0u);
        DWORD written = 0;
        if (n > 0 && unsigned(n) < sizeof line) WriteFile(handle, line, DWORD(n), &written, nullptr);
    }
    SetLastError(error);
    return !any_live();
}
const char* state() {
    return state_;
}
unsigned density() {
    return active_ ? density_ : 1;
}
bool patched() {
    return any_live();
}
}
