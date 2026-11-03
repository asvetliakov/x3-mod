// FL edition of x3m::text_density (src/proxy/text_density.h; the ap target keeps src/proxy/text_density.cpp): sharp
// in-game text under the UI scale on X3FL.exe (docs/architecture/text-density.md, "Editions"; sites, windows, edits and
// ABIs: text_density_sites_fl.h, verified in docs/reverse-engineering/fl/text-density.md, "Verified 2026-11-03 against
// X3FL.exe"). The option, the density mapping, the font gate, the Materials row plan, the shadow cache, the image plan,
// the on-screen filter and every log row are AP's (src/proxy/text_density_sites.h), each row ending in `edition=fl`.
//
// initialize() (backend-load path, the FL loader block, inside Direct3DCreate9 at 0x0772bc): the setting, X3FL.exe's
// identity (fl::module_base() != 0), every identity window at base + RVA, then the claims in order (font entry, block
// blit, alpha blit, the Materials seam, the image block, the two diagnostic entries under X3M_DEBUG=1), all or none,
// inert until device_created() raises the active flag. The image block crosses a qword: it is written only while the
// Materials row count and the KC VM slot are still zero (the init routine creates the VM at 0x077561 and loads the
// Materials at 0x0777db, both after Direct3DCreate9), else the transaction is refused (late_window). device_created()
// (CreateDevice, still inside the install window): d from ui_scale's scale, the font gate (the four AP families; under
// -L081 also UmePlusCLP{23d}, else umeplusclp_missing), then cfg+0x774 = d, the four cockpit bytes, the style byte at
// d = 3 (after the field), each read back; any failure puts back the style byte first, then the cockpit bytes, the
// field (kept at 3 while the style byte could not go back) and every claim, except that the image block's claim, once
// the KC VM or the rows exist, is not rewritten but stays in place, inert (`image_claim=retained_inert`; the usual case
// for `auto` without a UI scale). The Materials seam then flags the rows (CreateDevice precedes the Materials load).
//
// The thunks (file-scope asm, this module pinned) save EFLAGS, every GPR, XMM0-7, the x87 image (FNSAVE/FRSTOR) and
// MXCSR around one cdecl C side, which saves LastError itself: the engine sees its own state back, apart from the
// documented outputs (the scaled font request; a handled blit's return value; the image block's skip path).
#include "text_density_fl.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_memory.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/log_tiers.h"
#include "executable_identity_fl.h"
#include "text_density_sites_fl.h"
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");

extern "C" {
// The thunks and their words (read by the asm below).
void x3m_fl_text_density_font_thunk();
void x3m_fl_text_density_materials_thunk();
void x3m_fl_text_density_blt_block_thunk();
void x3m_fl_text_density_blt_alpha_thunk();
void x3m_fl_text_density_blt_image_thunk();
void x3m_fl_text_density_text_line_thunk();
void x3m_fl_text_density_rect_fill_thunk();
std::uintptr_t x3m_fl_text_density_font_continue = 0, x3m_fl_text_density_blt_block_continue = 0,
               x3m_fl_text_density_blt_alpha_continue = 0, x3m_fl_text_density_blt_image_continue = 0,
               x3m_fl_text_density_blt_image_return = 0, x3m_fl_text_density_text_line_continue = 0,
               x3m_fl_text_density_rect_fill_continue = 0, x3m_fl_text_density_materials_continue = 0;
// The engine helpers (base + RVA, set at initialize) and the shims that hand them register arguments.
std::uintptr_t x3m_fl_text_density_lookup_fn = 0, x3m_fl_text_density_alloc_fn = 0, x3m_fl_text_density_free_fn = 0;
void* __cdecl x3m_fl_text_density_call_lookup(std::int32_t id);
void* __cdecl x3m_fl_text_density_call_alloc(std::uint32_t bpp, int w, int h, std::uint32_t flags);
int __cdecl x3m_fl_text_density_call_free(void** object);
void __cdecl x3m_fl_text_density_call_wrapper(std::uintptr_t fn, void* src, void* dst, int sx, int sy, int dx, int dy, int w,
                                              int h, int colour);
// Raised once the density is in place: the blit and image thunks take their fast path (straight to the tail) while 0.
volatile LONG x3m_fl_text_density_active = 0;
// The C sides (frame = the thunk's pushad/pushfd frame, text_density_sites_fl.h frame_*).
void __cdecl x3m_fl_text_density_font_open(std::uint32_t* frame);
void __cdecl x3m_fl_text_density_materials_loaded();
std::uint32_t __cdecl x3m_fl_text_density_blit(std::uint32_t* frame, std::uint32_t kind);
std::uint32_t __cdecl x3m_fl_text_density_image_blit(std::uint32_t* frame);
void __cdecl x3m_fl_text_density_diagnostic(std::uint32_t* frame, std::uint32_t kind);
}

namespace {
namespace engine_patch = x3m::engine_patch;
namespace F = x3m::fl::text_density_fl;
namespace sites = x3m::text_density::sites;
using CopyFn = void(__cdecl*)(void*, void*, int, int, int, int, int, int);

void (*const thunks[F::claim_count])() = {x3m_fl_text_density_font_thunk,      x3m_fl_text_density_blt_block_thunk,
                                          x3m_fl_text_density_blt_alpha_thunk, x3m_fl_text_density_blt_image_thunk,
                                          x3m_fl_text_density_text_line_thunk, x3m_fl_text_density_rect_fill_thunk};
std::uintptr_t* const continuations[F::claim_count] = {
    &x3m_fl_text_density_font_continue,     &x3m_fl_text_density_blt_block_continue, &x3m_fl_text_density_blt_alpha_continue,
    &x3m_fl_text_density_blt_image_continue, &x3m_fl_text_density_text_line_continue, &x3m_fl_text_density_rect_fill_continue};
engine_patch::Site site_[F::claim_count];
bool live_[F::claim_count] = {};
const char* write_[F::claim_count] = {};
engine_patch::CallSite materials_site_{};
bool materials_live_ = false;
const char* materials_write_ = "none";
bool requested_ = false, claimed_ = false, resolved_ = false, diagnostic_ = false;
sites::Parse mode_ = sites::Parse::automatic;
unsigned value_ = 1, density_ = 1;
double scale_ = 1.0;
std::uintptr_t base_ = 0;
CopyFn copy_ = nullptr;
std::uintptr_t colour_fn_ = 0, alpha_fn_ = 0;
const char* state_ = "not_initialized";
const char* failed_site_ = "-";
const char* window_ = "-";
char setting_[sites::setting_capacity] = "-";
std::int32_t language_ = -1;
// The config field and the single-byte edits (owned_ = the byte there is ours; value_now_ = what we wrote).
std::uint32_t config_before_ = 0;
bool config_written_ = false;
bool owned_[F::edit_count] = {};
unsigned char written_[F::edit_count] = {};
const char* edits_failed_ = "-";
// The Materials rows (re-read on every call; the texture table is reallocated as named textures are added).
struct Tables {
    unsigned rows;
    std::uintptr_t rows_base, table;
};
bool materials_loaded_ = false, rows_applied_ = false;
unsigned rows_ = 0, rows_flagged_ = 0, rows_unflagged_ = 0, rows_nofilter_ = 0;
unsigned largest_w_ = 0, largest_h_ = 0;
unsigned caps_w_ = 0, caps_h_ = 0, caps_limited_d_ = 0;
char missing_[sites::missing_list_capacity] = "-";
wchar_t game_dir_[MAX_PATH + 2] = L"";
unsigned game_dir_length_ = 0;
struct Shadow {
    std::int32_t id;
    void* object;
    std::uint32_t bytes;
};
Shadow shadows_[sites::shadow_slots] = {};
unsigned shadow_count_ = 0;
std::uint32_t shadow_bytes_ = 0;
struct ImageShadow {
    void* source;
    void* object;
    std::uint32_t bytes;
    int w, h;
    bool path_unreadable;
    unsigned path_length;
    char path[sites::image_path_capacity];
};
ImageShadow image_shadow_{};
bool image_shadow_keyed_ = false;
constexpr unsigned refused_slots = 32;
std::int32_t refused_[refused_slots] = {};
unsigned refused_count_ = 0;
SRWLOCK shadow_lock_ = SRWLOCK_INIT;
unsigned shadow_rows_ = 0, font_rows_ = 0;
constexpr unsigned shadow_row_limit = 64, font_row_limit = 32;
sites::FilterSet filter_set_{};
volatile std::uintptr_t stage0_ = 0;
unsigned long saved_min_ = 0, saved_mag_ = 0;
bool filter_unavailable_ = false;
unsigned filter_draws_ = 0, filter_already_linear_ = 0;
bool raised_min_ = false, raised_mag_ = false;
bool filter_logged_ = false;
constexpr unsigned filter_report_window = 300;
sites::DiagnosticKey diagnostic_table_[sites::diagnostic_rows] = {};
unsigned diagnostic_count_ = 0;
const char* const diagnostic_names[5] = {"text_line", "blt_block", "blt_alpha", "rect_fill", "blt_image"};
unsigned recorded_passes_ = 0; // blits left to the engine because its recorder was on
bool recorder_logged_ = false;  // the first recorded pass has its own row (at the next Present)
bool image_retained_ = false;   // the image claim stays in place, inert, after the order witness lapsed
#ifdef X3M_TEXT_DENSITY_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
#endif

bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&claimed_), &module) != FALSE &&
           module != nullptr;
}
void printable(const wchar_t* text, DWORD length, char* out) {
    if (!length || length >= sites::setting_capacity) {
        out[0] = length ? '?' : '-';
        out[1] = 0;
        return;
    }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0;
}
bool read_window(std::uintptr_t at, unsigned char* out, unsigned n) {
    return engine_patch::read_code(at, out, n);
}
const char* windows_refused(std::uintptr_t base) {
    const char* why = nullptr;
    const char* window = F::refused_window(base, read_window, &why);
    window_ = window ? window : "-";
    return window ? why : nullptr;
}
bool read_u32(std::uintptr_t at, std::uint32_t* out) {
    return at && at <= UINTPTR_MAX - 4 && x3m::engine_memory::read(at, out, sizeof *out);
}
// ---- the game directory and the font gate (AP's) ----
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
unsigned fonts_missing(unsigned d) {
    return sites::missing_fonts(d, [](const char* relative) { return loose_file_exists(relative); }, missing_);
}
// -L081 (FL's Japanese Fonts table asks LARGE UmePlusCLP 23, which no FL catalogue holds): the density needs the
// UmePlusCLP{23d} pair too; otherwise the LARGE font would open at 1x into d x textures.
bool japanese_missing(unsigned d) {
    if (language_ != F::language_japanese) return false;
    char abc[sites::font_path_capacity] = "";
    if (font_pair_exists(F::japanese_family.name, F::japanese_family.size, d, abc)) return false;
    std::snprintf(missing_, sizeof missing_, "%s%u", F::japanese_family.name, F::japanese_family.size * d);
    return true;
}
const char* font_gate(unsigned d) {
    if (fonts_missing(d)) return "fonts_missing";
    if (japanese_missing(d)) return "umeplusclp_missing";
    return nullptr;
}
// ---- the claims ----
const char* install_site(unsigned index) {
    const F::Claim& c = F::claims[index];
    write_[index] = "none";
    if (live_[index]) return "already_installed";
    if (!engine_patch::install_window_open()) return "late_claim";
    const std::uintptr_t at = base_ + c.rva;
    engine_patch::SiteSpec spec{};
    spec.name = c.name;
    spec.address = at;
    spec.length = c.length;
    spec.ret_pop = 0;
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, c.bytes, c.length);
    site_[index] = engine_patch::Site{};
    const bool claimed = engine_patch::claim(site_[index], spec);
    if (claimed || site_[index].patched_in || !std::strcmp(site_[index].status, "patch_rolled_back"))
        write_[index] = site_[index].atomic_write ? "atomic" : "plain";
    live_[index] = site_[index].patched_in;
    if (!claimed) return live_[index] ? "rollback_failed" : site_[index].status;
    const char* failure = nullptr;
    unsigned char now[engine_patch::max_prologue]{};
    // The continuation word (the tail) is published before the thunk goes in front of it.
    *continuations[index] = reinterpret_cast<std::uintptr_t>(*site_[index].entry);
    if (!*continuations[index] || !engine_patch::push_front(site_[index], reinterpret_cast<void*>(thunks[index])))
        failure = "chain_failed";
    else if (!engine_patch::read_code(at, now, c.length) || std::memcmp(now, site_[index].patched, 5) ||
             std::memcmp(now + 5, c.bytes + 5, c.length - 5) || *site_[index].entry != reinterpret_cast<void*>(thunks[index]))
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
    x3m_fl_text_density_materials_continue = base_ + F::materials_callee_rva;
    materials_site_ = engine_patch::CallSite{};
    const std::uintptr_t at = base_ + F::materials_call_rva;
    const bool claimed = engine_patch::claim_call(materials_site_, at, base_ + F::materials_callee_rva,
                                                  reinterpret_cast<void*>(&x3m_fl_text_density_materials_thunk));
    if (claimed || materials_site_.patched_in || !std::strcmp(materials_site_.status, "patch_rolled_back"))
        materials_write_ = materials_site_.atomic_write ? "atomic" : "plain";
    materials_live_ = materials_site_.patched_in;
    if (!claimed) return materials_live_ ? "rollback_failed" : materials_site_.status;
    unsigned char now[5]{};
    if (engine_patch::read_code(at, now, 5) && !std::memcmp(now, materials_site_.patched, 5)) return "ok";
    engine_patch::restore_call(materials_site_);
    materials_live_ = materials_site_.patched_in;
    return materials_live_ ? "rollback_failed" : "readback_failed";
}
bool before_vm_and_materials();
// Takes every registered claim back (only over our bytes), in reverse order; true when none stays registered apart from
// a retained image claim. The image block crosses a qword, so its restore is a plain write: it is made only while the
// install-time order witness still holds (no KC VM, no Materials rows). Afterwards the claim stays in place, inert (its
// thunk goes straight to the tail while the active word is 0), and image_retained_ says so (`image_claim=retained_inert`).
bool restore_all() {
    bool clean = true;
    for (unsigned i = F::claim_count; i-- > 0;) {
        if (!live_[i]) continue;
        if (i == F::BltImage && !before_vm_and_materials()) {
            image_retained_ = true;
            continue;
        }
        engine_patch::restore(site_[i]);
        live_[i] = site_[i].patched_in;
        if (i == F::BltImage && !live_[i]) image_retained_ = false;
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
// Registered beyond the retained, inert image claim (a claim whose rollback failed, or an owned edit).
bool any_registered();
const char* image_claim() {
    if (!live_[F::BltImage]) return "none";
    if (image_retained_) return "retained_inert";
    return x3m_fl_text_density_active ? "active" : "inert";
}
// The order witness for the qword-crossing image block: neither the KC VM (created at 0x077561) nor the Materials rows
// (0x111755, from 0x0777db) exist yet, so no thread can be inside the KC dispatcher; both run after Direct3DCreate9.
bool before_vm_and_materials() {
    std::uint32_t vm = 1, rows_base = 1;
    std::int16_t rows = 1;
    return read_u32(base_ + F::vm_slot_rva, &vm) && read_u32(base_ + F::rows_slot_rva, &rows_base) &&
           x3m::engine_memory::read(base_ + F::rows_count_rva, &rows, sizeof rows) && !vm && !rows_base && !rows;
}
// The claims in order (the atomic ones first, then the crossing image block behind its order witness, then the
// diagnostic entries under debug); a failure restores the earlier ones. Every thunk is inert until the active flag.
const char* install_all(bool with_diagnostics) {
    if (!pin_self()) return "pin_failed";
    const F::ClaimIndex first[] = {F::Font, F::BltBlock, F::BltAlpha};
    for (F::ClaimIndex i : first) {
        const char* r = install_site(i);
        if (std::strcmp(r, "ok")) {
            failed_site_ = F::claims[i].name;
            restore_all();
            return r;
        }
    }
    if (const char* r = install_materials(); std::strcmp(r, "ok")) {
        failed_site_ = "text_density_materials_fl";
        restore_all();
        return r;
    }
    if (!before_vm_and_materials()) {
        failed_site_ = F::claims[F::BltImage].name;
        restore_all();
        return "late_window";
    }
    if (const char* r = install_site(F::BltImage); std::strcmp(r, "ok")) {
        failed_site_ = F::claims[F::BltImage].name;
        restore_all();
        return r;
    }
    for (unsigned i = 0; with_diagnostics && i < F::claim_count; ++i) {
        if (!F::claims[i].diagnostic) continue;
        const char* r = install_site(i);
        if (std::strcmp(r, "ok")) {
            failed_site_ = F::claims[i].name;
            restore_all();
            return r;
        }
    }
    claimed_ = true;
    diagnostic_ = with_diagnostics;
    return "ok";
}
// ---- the config field (cfg+0x774) and the language (cfg+0x75c) ----
std::uintptr_t config_object() {
    std::uint32_t config = 0;
    if (!read_u32(base_ + F::config_slot_rva, &config) || !config || config > UINTPTR_MAX - 0x7a0) return 0;
    return config;
}
bool read_config_field(std::uint32_t* out) {
    const std::uintptr_t config = config_object();
    return config && engine_patch::read_code(config + F::config_density_offset, reinterpret_cast<unsigned char*>(out), 4);
}
bool write_config(unsigned d) {
    const std::uintptr_t config = config_object();
    std::uint32_t current = 0;
    if (!config || !x3m::engine_memory::read(config + F::config_density_offset, &current, 4)) return false;
    if (!config_written_) config_before_ = current;
    *reinterpret_cast<volatile std::uint32_t*>(config + F::config_density_offset) = d;
    config_written_ = true;
    std::uint32_t now = 0;
    return read_config_field(&now) && now == d;
}
void restore_config() {
    if (!config_written_) return;
    if (const std::uintptr_t config = config_object())
        *reinterpret_cast<volatile std::uint32_t*>(config + F::config_density_offset) = config_before_;
    config_written_ = false;
}
std::int32_t read_language() {
    const std::uintptr_t config = config_object();
    std::int32_t language = -1;
    if (!config || !x3m::engine_memory::read(config + F::config_language_offset, &language, 4)) return -1;
    return language;
}
// ---- the single-byte edits (the cockpit opener, the style factor) ----
bool write_byte(std::uintptr_t at, unsigned char value) {
    auto* code = reinterpret_cast<unsigned char*>(at);
    DWORD previous = 0, unused = 0;
    if (!VirtualProtect(code, 1, PAGE_EXECUTE_READWRITE, &previous)) return false;
    engine_patch::write_code(at, &value, 1); // one byte: always inside one aligned qword (lock cmpxchg8b)
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), code, 1) != FALSE;
    VirtualProtect(code, 1, previous, &unused);
    return flushed;
}
// Puts `value` at edit i: the byte must be the engine's original or our own previous value (fail closed), and reads
// back equal. A byte written but not read back stays owned (restore puts the original back over it).
const char* apply_edit(unsigned i, unsigned char value) {
    const F::Edit& e = F::edits[i];
    const std::uintptr_t at = base_ + e.rva;
    unsigned char now = 0;
    if (!engine_patch::read_code(at, &now, 1)) return "edit_unreadable";
    if (now != e.original && !(owned_[i] && now == written_[i])) return "edit_mismatch";
    if (now == value && (owned_[i] || value == e.original)) return nullptr;
    if (!write_byte(at, value)) return "edit_protect_failed";
    owned_[i] = true;
    written_[i] = value;
    unsigned char back = 0;
    if (!engine_patch::read_code(at, &back, 1) || back != value) return "readback_failed";
    return nullptr;
}
// Only over our byte; the original read back. False when the byte stays ours (a failed restore).
bool restore_edit(unsigned i) {
    if (!owned_[i]) return true;
    const F::Edit& e = F::edits[i];
    const std::uintptr_t at = base_ + e.rva;
    unsigned char now = 0;
    if (!engine_patch::read_code(at, &now, 1)) return false;
    if (now != written_[i]) {
        owned_[i] = false; // not our byte any more (the original back, or someone else's): left alone
        return true;
    }
    if (!write_byte(at, e.original)) return false;
    unsigned char back = 0;
    if (!engine_patch::read_code(at, &back, 1) || back != e.original) return false;
    owned_[i] = false;
    return true;
}
const char* write_cockpit(unsigned d) {
    for (unsigned i = 0; i < F::cockpit_edits; ++i)
        if (const char* r = apply_edit(i, F::edit_value(F::edits[i], d))) {
            edits_failed_ = F::edits[i].name;
            return r;
        }
    return nullptr;
}
bool restore_cockpit() {
    bool clean = true;
    for (unsigned i = F::cockpit_edits; i-- > 0;) clean = restore_edit(i) && clean;
    return clean;
}
// The style factor at d = 3, only while cfg+0x774 already holds 3 (at N = 1 the edited lea gives factor 2).
const char* write_style() {
    std::uint32_t field = 0;
    if (!read_config_field(&field) || field != 3) {
        edits_failed_ = F::edits[F::Style].name;
        return "style_order";
    }
    if (const char* r = apply_edit(F::Style, F::edits[F::Style].d3)) {
        edits_failed_ = F::edits[F::Style].name;
        return r;
    }
    return nullptr;
}
bool restore_style() {
    return restore_edit(F::Style);
}
bool edits_owned() {
    for (bool o : owned_)
        if (o) return true;
    return false;
}
// The style byte first (while the field still says 3), then the cockpit bytes, then the field.
bool restore_edits() {
    const bool style = restore_style();
    const bool cockpit = restore_cockpit();
    if (!owned_[F::Style]) restore_config(); // a style byte that could not go back keeps the field at 3 (factor 3, as written)
    return style && cockpit;
}
bool any_registered() {
    for (unsigned i = 0; i < F::claim_count; ++i)
        if (live_[i] && !(i == F::BltImage && image_retained_)) return true;
    return materials_live_ || edits_owned();
}
// ---- the Materials rows ----
bool read_tables(Tables* t) {
    std::int16_t rows = 0;
    std::uint32_t rows_base = 0, table = 0;
    if (!x3m::engine_memory::read(base_ + F::rows_count_rva, &rows, sizeof rows) || !read_u32(base_ + F::rows_slot_rva, &rows_base) ||
        !read_u32(base_ + F::table_slot_rva, &table) || rows <= 0 || !rows_base || !table)
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
bool largest_flagged_row(unsigned* w, unsigned* h) {
    Tables t{};
    if (!read_tables(&t)) return false;
    *w = *h = 0;
    for (unsigned id = 0; id < t.rows; ++id) {
        const std::uint32_t flags = *row_flags(t, id);
        if (sites::plan_row(flags, true).edit != sites::RowEdit::flag && !((flags & sites::mpf_generated) && (flags & sites::mpf_writeable)))
            continue;
        const std::int16_t rw = *reinterpret_cast<const std::int16_t*>(t.rows_base + id * sites::row_stride + sites::row_width_offset);
        const std::int16_t rh = *reinterpret_cast<const std::int16_t*>(t.rows_base + id * sites::row_stride + sites::row_height_offset);
        if (rw > 0 && unsigned(rw) > *w) *w = unsigned(rw);
        if (rh > 0 && unsigned(rh) > *h) *h = unsigned(rh);
    }
    return true;
}
Tables live_tables() {
    Tables t{};
    const std::int16_t rows = *reinterpret_cast<const volatile std::int16_t*>(base_ + F::rows_count_rva);
    t.rows = rows > 0 ? unsigned(rows) : 0;
    t.rows_base = *reinterpret_cast<const volatile std::uint32_t*>(base_ + F::rows_slot_rva);
    t.table = *reinterpret_cast<const volatile std::uint32_t*>(base_ + F::table_slot_rva);
    if (!t.rows_base || !t.table) t.rows = 0;
    return t;
}
bool row_fontscale(const Tables& t, unsigned id) {
    return id < t.rows && (*row_flags(t, id) & sites::mpf_fontscale) != 0;
}
// Fail closed after the density went in: the style byte, the cockpit bytes, the field, then every claim.
void disable(const char* reason) {
    InterlockedExchange(&x3m_fl_text_density_active, 0);
    filter_set_.count = 0;
    restore_edits();
    restore_all();
    density_ = 1;
    state_ = reason;
}
// Lowers the density to the device's texture limits for the largest row to flag (AP's rule); the cockpit bytes and the
// style factor follow the lowered density. nullptr = fine, else the refusal reason.
const char* fit_density() {
    if (!largest_flagged_row(&largest_w_, &largest_h_)) return "tables_unreadable";
    const unsigned fitted = sites::caps_density(density_, largest_w_, largest_h_, caps_w_, caps_h_);
    if (fitted == density_) return nullptr;
    if (fitted < 2) return "caps";
    if (const char* gate = font_gate(fitted)) return gate;
    if (fitted != 3 && !restore_style()) return "style_failed";
    if (!write_config(fitted)) return "config_unwritable";
    if (const char* r = write_cockpit(fitted)) return r;
    caps_limited_d_ = fitted;
    density_ = fitted;
    return nullptr;
}
// ---- the shadow cache (AP's, through the FL helper ABIs) ----
void* lookup_object(std::int32_t id) {
    return x3m_fl_text_density_call_lookup(id);
}
void free_surface(void** object) {
    x3m_fl_text_density_call_free(object);
}
std::int16_t object_width(const void* object) {
    return *reinterpret_cast<const std::int16_t*>(static_cast<const unsigned char*>(object) + sites::object_width_offset);
}
std::int16_t object_height(const void* object) {
    return *reinterpret_cast<const std::int16_t*>(static_cast<const unsigned char*>(object) + sites::object_height_offset);
}
void shadow_row(std::int32_t src, const char* status, const char* reason, unsigned w, unsigned h, std::uint32_t bytes, unsigned long ms,
                const char* image = "-") {
    if (shadow_rows_ >= shadow_row_limit) return;
    ++shadow_rows_;
    x3m::log("text_density_shadow src=%ld status=%s reason=%s size=%ux%u density=%u bytes=%lu total=%lu slots=%u ms=%lu image=%s "
             "edition=fl",
             long(src), status, reason, w, h, density_, static_cast<unsigned long>(bytes), static_cast<unsigned long>(shadow_bytes_),
             shadow_count_, ms, image);
}
void* make_shadow(std::int32_t src, void* src_object, int W, int H, std::uint32_t* bytes, const char* image) {
    const unsigned d = density_;
    *bytes = 0;
    if (W <= 0 || H <= 0 || !sites::shadow_fits(unsigned(W), unsigned(H), d, shadow_bytes_, bytes)) {
        shadow_row(src, "refused", W <= 0 || H <= 0 ? "empty_source" : "budget", unsigned(W), unsigned(H), *bytes, 0, image);
        return nullptr;
    }
    const ULONGLONG begin = GetTickCount64();
    void* shadow = x3m_fl_text_density_call_alloc(sites::generated_bits_per_pixel, W * int(d), H * int(d), F::generated_surface_flags);
    if (!shadow) {
        shadow_row(src, "refused", "alloc_failed", unsigned(W), unsigned(H), *bytes, 0, image);
        return nullptr;
    }
    for (int i = 0; i < W; ++i)
        for (unsigned k = 0; k < d; ++k) copy_(src_object, shadow, i, 0, i * int(d) + int(k), 0, 1, H);
    for (int j = H - 1; j >= 0; --j)
        for (int l = int(d) - 1; l >= 0; --l) {
            const int row = j * int(d) + l;
            if (row != j) copy_(shadow, shadow, 0, j, 0, row, W * int(d), 1);
        }
    shadow_bytes_ += *bytes;
    shadow_row(src, "built", "ok", unsigned(W), unsigned(H), *bytes, static_cast<unsigned long>(GetTickCount64() - begin), image);
    return shadow;
}
void* build_shadow(std::int32_t src, void* src_object) {
    const int W = object_width(src_object), H = object_height(src_object);
    std::uint32_t bytes = 0;
    if (shadow_count_ >= sites::shadow_slots) {
        shadow_row(src, "refused", "slots", unsigned(W), unsigned(H), bytes, 0);
        return nullptr;
    }
    void* shadow = make_shadow(src, src_object, W, H, &bytes, "-");
    if (shadow) shadows_[shadow_count_++] = Shadow{src, shadow, bytes};
    return shadow;
}
void free_image_shadow() {
    if (image_shadow_.object) {
        free_surface(&image_shadow_.object);
        shadow_bytes_ -= image_shadow_.bytes;
    }
    image_shadow_ = ImageShadow{};
    image_shadow_keyed_ = false;
}
bool read_image_path(char out[sites::image_path_capacity], unsigned* length) {
    std::uint32_t pointer = 0;
    if (!read_u32(base_ + F::image_path_slot_rva, &pointer) || !pointer) return false;
    if (image_shadow_keyed_ && image_shadow_.path_length + 1 <= sites::image_path_capacity &&
        x3m::engine_memory::read(pointer, out, image_shadow_.path_length + 1) && !std::memcmp(out, image_shadow_.path, image_shadow_.path_length + 1)) {
        *length = image_shadow_.path_length;
        return true;
    }
    unsigned n = 0;
    while (n < sites::image_path_capacity) {
        if (!x3m::engine_memory::read(pointer + n, out + n, 1)) return false;
        if (!out[n]) {
            *length = n;
            return true;
        }
        ++n;
    }
    return false;
}
void* image_shadow_for(void* src_object, int W, int H) {
    char path[sites::image_path_capacity];
    unsigned length = 0;
    const bool same_object = image_shadow_keyed_ && image_shadow_.source == src_object && image_shadow_.w == W && image_shadow_.h == H;
    if (same_object && image_shadow_.path_unreadable) return nullptr;
    if (!read_image_path(path, &length)) {
        free_image_shadow();
        image_shadow_.source = src_object;
        image_shadow_.w = W;
        image_shadow_.h = H;
        image_shadow_.path_unreadable = true;
        image_shadow_keyed_ = true;
        shadow_row(-1, "refused", "path_unreadable", unsigned(W), unsigned(H), 0, 0, "?");
        return nullptr;
    }
    if (same_object && image_shadow_.path_length == length && !std::memcmp(image_shadow_.path, path, length + 1)) return image_shadow_.object;
    free_image_shadow();
    image_shadow_.source = src_object;
    image_shadow_.w = W;
    image_shadow_.h = H;
    image_shadow_.path_length = length;
    std::memcpy(image_shadow_.path, path, length + 1);
    image_shadow_keyed_ = true;
    image_shadow_.object = make_shadow(-1, src_object, W, H, &image_shadow_.bytes, path);
    return image_shadow_.object;
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
// The blit recorder (RTBB/RTBA chunks, *0x1aa100 + 0x24 bit 0; AP's blit trace, which AP's shadowed blits skip) runs
// inside the engine's blit: on FL, while it records, the blit is left to the engine so the recording stays complete (an
// unreadable recorder counts as recording: the engine would dereference it itself).
bool recording() {
    const std::uint32_t recorder = *reinterpret_cast<const volatile std::uint32_t*>(base_ + F::recorder_slot_rva);
    unsigned char flags = 0;
    return recorder && (!x3m::engine_memory::read(recorder + F::recorder_flags_offset, &flags, 1) || (flags & F::recorder_bit));
}
void diagnostic_row(unsigned function, std::int32_t src, std::int32_t dst, unsigned dst_flagged, unsigned src_flagged, unsigned src_generated,
                    unsigned handled) {
    if (!x3m::log_tier::cached_debug || function > sites::kind_blt_image) return;
    if (!sites::diagnostic_first(diagnostic_table_, &diagnostic_count_, sites::DiagnosticKey{std::uint8_t(function), src, dst})) return;
    x3m::log("text_density_draw fn=%s src=%ld dst=%ld dst_flagged=%u src_flagged=%u src_generated=%u handled=%u edition=fl",
             diagnostic_names[function], long(src), long(dst), dst_flagged, src_flagged, src_generated, handled);
}
void text_line_row(std::int32_t font, std::int32_t x, std::int32_t dst, unsigned dst_flagged) {
    if (!x3m::log_tier::cached_debug) return;
    if (!sites::diagnostic_first(diagnostic_table_, &diagnostic_count_, sites::DiagnosticKey{std::uint8_t(sites::kind_text_line), font, dst}))
        return;
    x3m::log("text_density_draw fn=text_line font=%ld x=%ld dst=%ld dst_flagged=%u edition=fl", long(font), long(x), long(dst), dst_flagged);
}
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
            object > UINTPTR_MAX - 0x64 || !x3m::engine_memory::read(object + F::object_texture_offset, &texture, 4))
            continue;
        sites::filter_set_insert(&next, texture);
    }
    filter_set_ = next;
}
bool filter_wanted() {
    return x3m_fl_text_density_active != 0 && density_ > 1 && double(density_) != scale_;
}
}

extern "C" {
// The font entry (base+0x0a8990): ECX = name, EDX = cell width, [args] size, flags, y offset. When the d x pair exists
// under <game>\f the request is scaled in place (the saved EDX and the two stack words; the entry formats "%s%d" with
// the stack size after this); otherwise it passes unchanged and one row names the missing file.
void __cdecl x3m_fl_text_density_font_open(std::uint32_t* frame) {
    const DWORD error = GetLastError();
    if (x3m_fl_text_density_active && density_ > 1) {
        char name[sites::font_name_capacity + 2] = "";
        const std::uintptr_t text = frame[F::frame_ecx];
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
        std::uint32_t* args = frame + F::frame_args;
        const unsigned size = args[F::font_arg_size];
        char abc[sites::font_path_capacity] = "-";
        const char* status = "invalid";
        if (readable && n <= sites::font_name_capacity && font_pair_exists(name, size, density_, abc)) {
            args[F::font_arg_size] = size * density_;
            frame[F::frame_edx] *= density_;
            args[F::font_arg_y_offset] *= density_;
            status = "scaled";
        } else if (readable && n <= sites::font_name_capacity && size >= 1 && size <= 255) {
            status = "missing";
            sites::font_file(name, size, density_, ".abc", abc);
        }
        if (font_rows_ < font_row_limit) {
            ++font_rows_;
            x3m::log("text_density_font name=%s size=%u density=%u status=%s file=%s cell_width=%lu y_offset=%lu edition=fl",
                     readable ? name : "?", size, density_, status, abc, static_cast<unsigned long>(frame[F::frame_edx]),
                     static_cast<unsigned long>(args[F::font_arg_y_offset]));
        }
    }
    SetLastError(error);
}
// The Materials seam (the call after the Materials load at base+0x0a69fb; the init thread, once): the rows exist and no
// texture object does yet.
void __cdecl x3m_fl_text_density_materials_loaded() {
    const DWORD error = GetLastError();
    materials_loaded_ = true;
    const char* status = "pending";
    const char* reason = "density_unresolved";
    if (rows_applied_) {
        status = "already";
        reason = "-";
    } else if (x3m_fl_text_density_active && density_ > 1) {
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
             "largest_row=%ux%u missing=%s image_claim=%s edition=fl",
             status, reason, density_, rows_, rows_flagged_, rows_unflagged_, rows_nofilter_, caps_limited_d_, largest_w_, largest_h_, missing_,
             image_claim());
    SetLastError(error);
}
// The two block blits (ECX = src id, EDX = dst id, sx sy dx dy w h colour clip): a static unflagged source into a flagged
// destination row is drawn here from its d x shadow with every coordinate in texels (the engine's destination clip done
// first, in layout units); returns the destination object (the engine's own return) or 0 (the original runs).
std::uint32_t __cdecl x3m_fl_text_density_blit(std::uint32_t* frame, std::uint32_t kind) {
    if (!x3m_fl_text_density_active || density_ <= 1) return 0;
    const DWORD error = GetLastError();
    std::uint32_t handled = 0;
    const std::int32_t* args = reinterpret_cast<const std::int32_t*>(frame + F::frame_args);
    const std::int32_t src = std::int32_t(frame[F::frame_ecx]), dst = std::int32_t(frame[F::frame_edx]);
    const Tables t = live_tables();
    if (dst >= 0 && src >= 0 && unsigned(dst) < t.rows) {
        const bool dst_flagged = row_fontscale(t, unsigned(dst));
        const bool src_flagged = row_fontscale(t, unsigned(src));
        bool src_generated = false;
        if (unsigned(src) < t.rows) {
            src_generated = (*row_flags(t, unsigned(src)) & sites::mpf_generated) != 0;
        } else {
            std::uint32_t named = 0;
            const std::int16_t extra = *reinterpret_cast<const volatile std::int16_t*>(base_ + F::named_count_rva);
            if (extra > 0 && unsigned(src) < t.rows + unsigned(extra) &&
                x3m::engine_memory::read(t.table + unsigned(src) * sites::entry_stride + sites::entry_flags_offset, &named, 4))
                src_generated = (named & sites::mpf_generated) != 0;
            else
                src_generated = true; // unknown: leave it to the engine
        }
        if (dst_flagged && !src_flagged && !src_generated) {
            if (recording()) {
                ++recorded_passes_;
            } else {
                void* dst_object = lookup_object(dst);
                void* shadow = dst_object ? shadow_for(src) : nullptr;
                if (dst_object && shadow) {
                    const int d = int(density_);
                    int sx = args[F::arg_sx], sy = args[F::arg_sy], dx = args[F::arg_dx], dy = args[F::arg_dy], w = args[F::arg_w],
                        h = args[F::arg_h];
                    bool draw = true;
                    if (args[F::arg_clip])
                        draw = sites::clip_destination(object_width(dst_object) / d, object_height(dst_object) / d, &sx, &sy, &dx, &dy, &w, &h);
                    if (draw) {
                        const int colour = args[F::arg_colour];
                        if (kind == sites::kind_blt_alpha)
                            x3m_fl_text_density_call_wrapper(alpha_fn_, shadow, dst_object, sx * d, sy * d, dx * d, dy * d, w * d, h * d, colour);
                        else if (colour >= 0)
                            x3m_fl_text_density_call_wrapper(colour_fn_, shadow, dst_object, sx * d, sy * d, dx * d, dy * d, w * d, h * d, colour);
                        else
                            copy_(shadow, dst_object, sx * d, sy * d, dx * d, dy * d, w * d, h * d);
                    }
                    handled = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(dst_object));
                }
            }
        }
        diagnostic_row(kind, src, dst, dst_flagged, src_flagged, src_generated, handled ? 1u : 0u);
    }
    SetLastError(error);
    return handled;
}
// KC's TexBltImage block (base+0x0b22f4): EDX = the engine's factor (1, or cfg+0x774 for a flagged destination row),
// ESI = the record (dst +1, six words from +0xb, stride 5), [EBP-0x50] the image object, [EBP-0x20] the destination
// object (both null-checked by the engine just before). AP's plan: with the d x shadow of the image every coordinate x N,
// else the source rectangle unscaled at the x N destination position, clamped to both surfaces. Returns 1 (the thunk
// skips the engine's block) or 0 (the block runs unchanged).
std::uint32_t __cdecl x3m_fl_text_density_image_blit(std::uint32_t* frame) {
    if (!x3m_fl_text_density_active || density_ <= 1) return 0;
    const DWORD error = GetLastError();
    std::uint32_t handled = 0;
    const unsigned N = frame[F::frame_edx];
    const std::uintptr_t record = frame[F::frame_esi], ebp = frame[F::frame_ebp];
    std::uint32_t src_word = 0, dst_word = 0;
    if (record && ebp && read_u32(ebp + std::uintptr_t(std::intptr_t(F::image_src_frame_offset)), &src_word) &&
        read_u32(ebp + std::uintptr_t(std::intptr_t(F::image_dst_frame_offset)), &dst_word) && src_word && dst_word &&
        (N == 1 || N == density_)) {
        void* src_object = reinterpret_cast<void*>(std::uintptr_t(src_word));
        void* dst_object = reinterpret_cast<void*>(std::uintptr_t(dst_word));
        std::int32_t dst = 0;
        std::memcpy(&dst, reinterpret_cast<const void*>(record + sites::image_record_dst_offset), 4);
        int raw[6];
        for (unsigned i = 0; i < 6; ++i) {
            std::uint16_t word = 0; // the engine pushes the 16-bit product zero-extended (movzx eax,cx)
            std::memcpy(&word, reinterpret_cast<const void*>(record + sites::image_record_coords_offset + i * sites::image_record_coord_stride), 2);
            raw[i] = word;
        }
        const int SW = object_width(src_object), SH = object_height(src_object), DW = object_width(dst_object), DH = object_height(dst_object);
        void* shadow = nullptr;
        std::uint32_t src_flags = 0;
        std::memcpy(&src_flags, static_cast<const unsigned char*>(src_object) + sites::object_flags_offset, 4);
        if (N > 1 && sites::image_shadow_compatible(src_flags)) {
            AcquireSRWLockExclusive(&shadow_lock_);
            shadow = image_shadow_for(src_object, SW, SH);
            ReleaseSRWLockExclusive(&shadow_lock_);
        }
        const sites::ImagePlan plan = sites::plan_image_blit(N, density_, shadow != nullptr, SW, SH, DW, DH, raw);
        if (plan.draw) copy_(plan.shadow ? shadow : src_object, dst_object, plan.sx, plan.sy, plan.dx, plan.dy, plan.w, plan.h);
        handled = 1;
        diagnostic_row(sites::kind_blt_image, -1, dst, N > 1 ? 1u : 0u, 0, 0, plan.shadow ? 1u : 0u);
    }
    SetLastError(error);
    return handled;
}
// The diagnostic entries (debug): the text line (ECX = dst, [args] font slot, x) and the rect fill (ECX = dst).
void __cdecl x3m_fl_text_density_diagnostic(std::uint32_t* frame, std::uint32_t kind) {
    if (!x3m::log_tier::cached_debug || kind > sites::kind_rect_fill) return;
    const DWORD error = GetLastError();
    const std::int32_t* args = reinterpret_cast<const std::int32_t*>(frame + F::frame_args);
    const std::int32_t dst = std::int32_t(frame[F::frame_ecx]);
    unsigned dst_flagged = 0;
    if (dst >= 0) {
        Tables t{};
        if (read_tables(&t) && unsigned(dst) < t.rows) dst_flagged = (*row_flags(t, unsigned(dst)) & sites::mpf_fontscale) ? 1u : 0u;
    }
    if (kind == sites::kind_text_line)
        text_line_row(args[F::text_arg_font], args[F::text_arg_x], dst, dst_flagged);
    else
        diagnostic_row(kind, -1, dst, dst_flagged, 0, 0, 0);
    SetLastError(error);
}
}

// The thunks. X3M_FL_TD_ENTER: pushfd/pushad (the frame, kept in EBX), a 16-aligned block below it with XMM0-7, the
// x87 image (FNSAVE, which leaves the FPU initialised for the C side) and MXCSR, DF cleared; X3M_FL_TD_LEAVE restores the
// three and ESP. The blit and image thunks test the active word first (pushfd/popfd around the compare) and go straight
// to the tail while it is 0. A handled blit returns the C result in EAX with `ret` (the caller pops the eight words); a
// handled image block keeps EFLAGS, reserves the engine's 0x20 bytes with `lea` and resumes at base+0x0b2353. Every
// other path leaves the engine's registers and flags exactly as they were. The Materials thunk sits on the call at
// base+0x0a69fb and enters the original callee with the game's stack as the call left it.
asm(R"(
    .intel_syntax noprefix
    .text
    .macro X3M_FL_TD_ENTER
    pushfd
    pushad
    mov ebx, esp
    sub esp, 256
    and esp, -16
    movdqa xmmword ptr [esp], xmm0
    movdqa xmmword ptr [esp+16], xmm1
    movdqa xmmword ptr [esp+32], xmm2
    movdqa xmmword ptr [esp+48], xmm3
    movdqa xmmword ptr [esp+64], xmm4
    movdqa xmmword ptr [esp+80], xmm5
    movdqa xmmword ptr [esp+96], xmm6
    movdqa xmmword ptr [esp+112], xmm7
    fnsave [esp+128]
    stmxcsr dword ptr [esp+240]
    cld
    .endm
    .macro X3M_FL_TD_LEAVE
    ldmxcsr dword ptr [esp+240]
    frstor [esp+128]
    movdqa xmm0, xmmword ptr [esp]
    movdqa xmm1, xmmword ptr [esp+16]
    movdqa xmm2, xmmword ptr [esp+32]
    movdqa xmm3, xmmword ptr [esp+48]
    movdqa xmm4, xmmword ptr [esp+64]
    movdqa xmm5, xmmword ptr [esp+80]
    movdqa xmm6, xmmword ptr [esp+96]
    movdqa xmm7, xmmword ptr [esp+112]
    mov esp, ebx
    .endm
    .p2align 4
    .globl _x3m_fl_text_density_font_thunk
_x3m_fl_text_density_font_thunk:
    X3M_FL_TD_ENTER
    push ebx
    call _x3m_fl_text_density_font_open
    add esp, 4
    X3M_FL_TD_LEAVE
    popad
    popfd
    jmp dword ptr [_x3m_fl_text_density_font_continue]
    .p2align 4
    .globl _x3m_fl_text_density_materials_thunk
_x3m_fl_text_density_materials_thunk:
    X3M_FL_TD_ENTER
    call _x3m_fl_text_density_materials_loaded
    X3M_FL_TD_LEAVE
    popad
    popfd
    jmp dword ptr [_x3m_fl_text_density_materials_continue]
    .p2align 4
    .globl _x3m_fl_text_density_blt_block_thunk
_x3m_fl_text_density_blt_block_thunk:
    pushfd
    cmp dword ptr [_x3m_fl_text_density_active], 0
    jne 1f
    popfd
    jmp dword ptr [_x3m_fl_text_density_blt_block_continue]
1:  popfd
    X3M_FL_TD_ENTER
    push 1
    push ebx
    call _x3m_fl_text_density_blit
    add esp, 8
    X3M_FL_TD_LEAVE
    test eax, eax
    jz 2f
    mov dword ptr [esp+28], eax
    popad
    popfd
    ret
2:  popad
    popfd
    jmp dword ptr [_x3m_fl_text_density_blt_block_continue]
    .p2align 4
    .globl _x3m_fl_text_density_blt_alpha_thunk
_x3m_fl_text_density_blt_alpha_thunk:
    pushfd
    cmp dword ptr [_x3m_fl_text_density_active], 0
    jne 3f
    popfd
    jmp dword ptr [_x3m_fl_text_density_blt_alpha_continue]
3:  popfd
    X3M_FL_TD_ENTER
    push 2
    push ebx
    call _x3m_fl_text_density_blit
    add esp, 8
    X3M_FL_TD_LEAVE
    test eax, eax
    jz 4f
    mov dword ptr [esp+28], eax
    popad
    popfd
    ret
4:  popad
    popfd
    jmp dword ptr [_x3m_fl_text_density_blt_alpha_continue]
    .p2align 4
    .globl _x3m_fl_text_density_blt_image_thunk
_x3m_fl_text_density_blt_image_thunk:
    pushfd
    cmp dword ptr [_x3m_fl_text_density_active], 0
    jne 5f
    popfd
    jmp dword ptr [_x3m_fl_text_density_blt_image_continue]
5:  popfd
    X3M_FL_TD_ENTER
    push ebx
    call _x3m_fl_text_density_image_blit
    add esp, 4
    X3M_FL_TD_LEAVE
    test eax, eax
    jz 6f
    mov dword ptr [esp+28], eax
    popad
    popfd
    lea esp, [esp-32]
    jmp dword ptr [_x3m_fl_text_density_blt_image_return]
6:  popad
    popfd
    jmp dword ptr [_x3m_fl_text_density_blt_image_continue]
    .p2align 4
    .globl _x3m_fl_text_density_text_line_thunk
_x3m_fl_text_density_text_line_thunk:
    X3M_FL_TD_ENTER
    push 0
    push ebx
    call _x3m_fl_text_density_diagnostic
    add esp, 8
    X3M_FL_TD_LEAVE
    popad
    popfd
    jmp dword ptr [_x3m_fl_text_density_text_line_continue]
    .p2align 4
    .globl _x3m_fl_text_density_rect_fill_thunk
_x3m_fl_text_density_rect_fill_thunk:
    X3M_FL_TD_ENTER
    push 3
    push ebx
    call _x3m_fl_text_density_diagnostic
    add esp, 8
    X3M_FL_TD_LEAVE
    popad
    popfd
    jmp dword ptr [_x3m_fl_text_density_rect_fill_continue]
    .p2align 4
    .globl _x3m_fl_text_density_call_lookup
_x3m_fl_text_density_call_lookup:
    mov ecx, dword ptr [esp+4]
    jmp dword ptr [_x3m_fl_text_density_lookup_fn]
    .p2align 4
    .globl _x3m_fl_text_density_call_alloc
_x3m_fl_text_density_call_alloc:
    push dword ptr [esp+16]
    push dword ptr [esp+8]
    mov ecx, dword ptr [esp+16]
    mov edx, dword ptr [esp+20]
    call dword ptr [_x3m_fl_text_density_alloc_fn]
    add esp, 8
    ret
    .p2align 4
    .globl _x3m_fl_text_density_call_free
_x3m_fl_text_density_call_free:
    mov ecx, dword ptr [esp+4]
    jmp dword ptr [_x3m_fl_text_density_free_fn]
    .p2align 4
    .globl _x3m_fl_text_density_call_wrapper
_x3m_fl_text_density_call_wrapper:
    push ebp
    mov ebp, esp
    push dword ptr [ebp+44]
    push dword ptr [ebp+40]
    push dword ptr [ebp+36]
    push dword ptr [ebp+32]
    push dword ptr [ebp+28]
    push dword ptr [ebp+24]
    push dword ptr [ebp+20]
    mov ecx, dword ptr [ebp+12]
    mov edx, dword ptr [ebp+16]
    call dword ptr [ebp+8]
    add esp, 28
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
#ifdef X3M_TEXT_DENSITY_FL_FIXTURE
    const std::uintptr_t base = fixture_base_ ? fixture_base_ : x3m::fl::module_base();
#else
    const std::uintptr_t base = x3m::fl::module_base();
#endif
    unsigned value = 1;
    const sites::Parse parsed = length >= sites::setting_capacity ? sites::Parse::invalid : sites::parse_setting(text, &value);
    const char* mode = "off";
    const bool debug = log_tier::debug();
    window_ = "-";
    if (length >= sites::setting_capacity)
        state_ = "too_long";
    else if (parsed == sites::Parse::invalid)
        state_ = "invalid_setting";
    else if (parsed == sites::Parse::value && value == 1)
        state_ = "off";
    else if (!base)
        state_ = "executable_mismatch";
    else if (const char* refused = windows_refused(base))
        state_ = refused;
    else if (!resolve_game_dir())
        state_ = "no_game_dir";
    else {
        base_ = base;
        copy_ = reinterpret_cast<CopyFn>(base + F::copy_rva);
        colour_fn_ = base + F::colour_rva;
        alpha_fn_ = base + F::alpha_rva;
        x3m_fl_text_density_lookup_fn = base + F::lookup_rva;
        x3m_fl_text_density_alloc_fn = base + F::alloc_rva;
        x3m_fl_text_density_free_fn = base + F::free_rva;
        x3m_fl_text_density_blt_image_return = base + F::image_return_rva;
        mode_ = parsed;
        value_ = value;
        mode = parsed == sites::Parse::automatic ? "auto" : "fixed";
        state_ = install_all(debug);
        requested_ = !std::strcmp(state_, "ok");
        if (requested_) state_ = "pending"; // resolved and applied at CreateDevice
    }
    char writes[F::claim_count * 8 + 8] = "";
    for (unsigned i = 0; i < F::claim_count; ++i) {
        if (i) std::strncat(writes, ",", sizeof writes - std::strlen(writes) - 1);
        std::strncat(writes, write_[i] ? write_[i] : "none", sizeof writes - std::strlen(writes) - 1);
    }
    std::strncat(writes, ",", sizeof writes - std::strlen(writes) - 1);
    std::strncat(writes, materials_write_, sizeof writes - std::strlen(writes) - 1);
    const bool refused = !requested_ && std::strcmp(state_, "off");
    log("text_density setting=%s status=%s reason=%s mode=%s value=%u diagnostic=%u failed_site=%s window=%s writes=%s base=0x%08lx "
        "site_rva=0x%06lx edition=fl",
        setting_, requested_ ? "pending" : refused ? "refused" : "off", state_, mode, value_, diagnostic_ ? 1u : 0u, failed_site_, window_,
        writes, static_cast<unsigned long>(base), static_cast<unsigned long>(F::claims[F::Font].rva));
    SetLastError(error);
    return false;
}
bool device_created(double scale, IDirect3DDevice9* device) {
    const DWORD error = GetLastError();
    if (!requested_ || resolved_) {
        SetLastError(error);
        return x3m_fl_text_density_active != 0;
    }
    resolved_ = true;
    scale_ = scale > 0 ? scale : 1.0;
    D3DCAPS9 limits{};
    if (device && SUCCEEDED(device->GetDeviceCaps(&limits))) {
        caps_w_ = limits.MaxTextureWidth;
        caps_h_ = limits.MaxTextureHeight;
    }
    density_ = sites::density_for(mode_, value_, scale_);
    language_ = read_language();
    const char* reason = "ok";
    char rows_text[48] = "pending";
    if (density_ <= 1) {
        reason = "density_1";
        restore_all();
        density_ = 1;
    } else if (!engine_patch::install_window_open()) {
        reason = "late_claim"; // a device after the first Present: nothing is written
        restore_all();
        density_ = 1;
    } else if (const char* gate = font_gate(density_)) {
        // The gate (AP's four families, plus -L081's LARGE font): without every d x pair the engine would open 1x fonts
        // into d x textures. Nothing is flagged, the field is 1, the claims go back.
        reason = gate;
        write_config(1);
        restore_all();
        density_ = 1;
    } else if (!write_config(density_)) {
        reason = "config_unwritable";
        disable(reason);
    } else if (const char* r = write_cockpit(density_)) {
        reason = !std::strcmp(r, "readback_failed") ? "cockpit_readback_failed" : "cockpit_failed";
        disable(reason);
    } else if (density_ == 3 && write_style()) {
        reason = "style_failed";
        disable(reason);
    } else {
        InterlockedExchange(&x3m_fl_text_density_active, 1);
        if (materials_loaded_) {
            const char* r = fit_density();
            if (!r) r = apply_rows(density_, scale_);
            if (r) {
                reason = r;
                disable(reason);
            } else {
                std::snprintf(rows_text, sizeof rows_text, "%u/%u/%u", rows_flagged_, rows_unflagged_, rows_nofilter_);
            }
        }
    }
    const bool applied = x3m_fl_text_density_active != 0;
    state_ = reason;
    char fonts[80] = "";
    for (unsigned i = 0; i < 4; ++i) {
        const sites::FontFamily& f = sites::font_families[i];
        char abc[sites::font_path_capacity];
        const bool present = density_ > 1 && font_pair_exists(f.name, f.size, density_, abc);
        char one[24];
        std::snprintf(one, sizeof one, "%s%s%u", i ? "," : "", present ? f.name : "-", present ? f.size * density_ : 0u);
        std::strncat(fonts, one, sizeof fonts - std::strlen(fonts) - 1);
    }
    std::uint32_t field = 0;
    read_config_field(&field);
    log("text_density_install density=%u scale=%.4f status=%s reason=%s mode=%s config_before=%lu config=%lu language=%ld cockpit=%s "
        "style=%s edit_failed=%s rows=%s fonts=%s missing=%s caps_limited_d=%u max_texture=%ux%u image_claim=%s arena_used=%u edition=fl",
        density_, scale_, applied ? "patched" : any_registered() ? "patched_unverified" : !std::strcmp(reason, "density_1") ? "off" : "refused",
        reason, mode_ == sites::Parse::automatic ? "auto" : "fixed", static_cast<unsigned long>(config_before_),
        static_cast<unsigned long>(field), long(language_), owned_[F::CockpitSize] ? "patched" : "stock", owned_[F::Style] ? "patched" : "stock",
        edits_failed_, rows_text, fonts, missing_, caps_limited_d_, caps_w_, caps_h_, image_claim(), engine_patch::arena_used());
    SetLastError(error);
    return applied;
}
void before_reset() {
    if (!x3m_fl_text_density_active) return;
    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&shadow_lock_);
    const unsigned count = shadow_count_ + (image_shadow_.object ? 1u : 0u);
    const std::uint32_t bytes = shadow_bytes_;
    for (unsigned i = 0; i < shadow_count_; ++i)
        if (shadows_[i].object) free_surface(&shadows_[i].object);
    free_image_shadow();
    shadow_count_ = 0;
    shadow_bytes_ = 0;
    refused_count_ = 0;
    ReleaseSRWLockExclusive(&shadow_lock_);
    if (count) log("text_density_reset shadows=%u bytes=%lu recorded_passes=%u edition=fl", count, static_cast<unsigned long>(bytes), recorded_passes_);
    SetLastError(error);
}
bool filter_hooks_wanted() {
    return filter_wanted();
}
void bound_texture(unsigned stage, IDirect3DBaseTexture9* texture) {
    if (stage == 0) stage0_ = reinterpret_cast<std::uintptr_t>(texture);
}
bool draw_filter_override(IDirect3DDevice9* device, SamplerGet get, SamplerSet set) {
    if (!filter_set_.count || !device || !get || !set) return false;
    const std::uintptr_t texture = stage0_;
    if (!texture || !sites::filter_set_contains(filter_set_, texture)) return false;
    unsigned long min_filter = 0, mag_filter = 0;
    if (get(device, 0, D3DSAMP_MINFILTER, &min_filter) < 0 || get(device, 0, D3DSAMP_MAGFILTER, &mag_filter) < 0) {
        filter_set_.count = 0;
        if (!filter_unavailable_) {
            filter_unavailable_ = true;
            log("text_density_filter status=unavailable reason=get_sampler_state_failed textures=0 edition=fl");
        }
        return false;
    }
    const bool raise_min = sites::filter_needs_raise(min_filter), raise_mag = sites::filter_needs_raise(mag_filter);
    if (!filter_logged_) {
        filter_logged_ = true;
        unsigned long aniso = 0;
        if (get(device, 0, D3DSAMP_MAXANISOTROPY, &aniso) < 0) aniso = 0;
        log("text_density_filter status=active min_before=%lu mag_before=%lu aniso=%lu raised=%s textures=%u edition=fl",
            static_cast<unsigned long>(min_filter), static_cast<unsigned long>(mag_filter), aniso,
            raise_min && raise_mag ? "both" : raise_min ? "min" : raise_mag ? "mag" : "none", filter_set_.count);
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
    if (recorded_passes_ && !recorder_logged_) {
        // The engine's blit recorder was on for a blit the module would have drawn (the engine drew its 1x source at
        // the d x position): one row naming the frame, so a flight that shows small icons or text can be matched.
        recorder_logged_ = true;
        const DWORD error = GetLastError();
        log("text_density_recorder first_pass frame=%llu recorded_passes=%u edition=fl", frame, recorded_passes_);
        SetLastError(error);
    }
    if (!filter_wanted() || filter_unavailable_) {
        filter_set_.count = 0;
        return;
    }
    const DWORD error = GetLastError();
    rebuild_filter_set();
    if (x3m::log_tier::cached_debug && frame % filter_report_window == 0) {
        log("text_density_filter_frame frame=%llu textures=%u overridden=%u already_linear=%u edition=fl", frame, filter_set_.count,
            filter_draws_, filter_already_linear_);
        filter_draws_ = 0;
        filter_already_linear_ = 0;
    }
    SetLastError(error);
}
// For the fixture and a dynamic unload (the FL loader calls no unload hooks): the style byte, the cockpit bytes, the
// field, then every claim, only over our bytes.
// The image claim follows restore_all's rule: restored only while the order witness holds, else kept in place, inert.
bool shutdown() {
    if (!any_live() && !config_written_ && !edits_owned()) return true;
    const DWORD error = GetLastError();
    InterlockedExchange(&x3m_fl_text_density_active, 0);
    filter_set_.count = 0;
    const bool edits = restore_edits();
    const bool clean = restore_all() && edits;
    state_ = clean ? "restored" : "restore_failed";
    log("text_density_restore status=%s registered=%u image_claim=%s edition=fl", state_, any_registered() ? 1u : 0u, image_claim());
    SetLastError(error);
    return !any_live() && !edits_owned();
}
const char* state() {
    return state_;
}
unsigned density() {
    return x3m_fl_text_density_active ? density_ : 1;
}
bool patched() {
    return any_registered();
}
#ifdef X3M_TEXT_DENSITY_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
void fixture_reset() {
    if (any_live() || edits_owned()) return;
    InterlockedExchange(&x3m_fl_text_density_active, 0);
    requested_ = claimed_ = resolved_ = diagnostic_ = false;
    mode_ = sites::Parse::automatic;
    value_ = density_ = 1;
    scale_ = 1.0;
    base_ = 0;
    state_ = "not_initialized";
    failed_site_ = window_ = edits_failed_ = "-";
    language_ = -1;
    config_written_ = false;
    config_before_ = 0;
    materials_loaded_ = rows_applied_ = false;
    rows_ = rows_flagged_ = rows_unflagged_ = rows_nofilter_ = largest_w_ = largest_h_ = 0;
    caps_w_ = caps_h_ = caps_limited_d_ = 0;
    std::strcpy(missing_, "-");
    shadow_count_ = 0;
    shadow_bytes_ = 0;
    image_shadow_ = ImageShadow{};
    image_shadow_keyed_ = false;
    refused_count_ = 0;
    shadow_rows_ = font_rows_ = 0;
    filter_set_ = sites::FilterSet{};
    filter_unavailable_ = filter_logged_ = false;
    diagnostic_count_ = 0;
    recorded_passes_ = 0;
    recorder_logged_ = false;
    materials_write_ = "none";
    for (auto& w : write_) w = nullptr;
}
#endif
}
