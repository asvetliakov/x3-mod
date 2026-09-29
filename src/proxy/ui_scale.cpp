#include "ui_scale.h"
#include "config.h"
#include "ui_scale_sites.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "log_tiers.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

// Eight sites claimed through engine_patch (ui_scale_sites.h), all or none: a failed claim restores the
// earlier ones; a restore that fails keeps the site registered (patched_unverified) with the data cells
// at the identity, under which every stub reproduces the displaced instruction exactly. The stubs are
// emitted bytes in the never-freed arena reading the data cells below (this module is pinned for the
// process lifetime, as cull_small_parts does): no call into this module, no push, no x87. Per frame only
// present(): the excluded camera refresh (a bounded registry walk of validated reads, patched only) and,
// under debug with the diagnostic claim live, one row.
static_assert(sizeof(void*) == 4, "x86 code patching only");

// The data cells the stubs read (extern "C": fixed names for the emitted absolute operands).
extern "C" {
volatile float x3m_ui_scale_factor = 1.0f;                // A: s
volatile std::uint32_t x3m_ui_scale_inverse16 = 65536;    // B, C, D: round(65536 / s)
volatile std::uint32_t x3m_ui_scale_fixed256 = 256;       // E: round(s * 256)
volatile std::uint32_t x3m_ui_scale_excluded_camera = 0;  // A: the active cockpit's HUD camera (0 = none)
volatile std::uint32_t x3m_ui_scale_scaled = 0;           // A: instances scaled since the last Present
volatile std::uint32_t x3m_ui_scale_excluded = 0;         // A: instances of the excluded camera since the last Present
volatile std::int32_t x3m_ui_scale_mouse_acc[4] = {x3m::ui_scale::sites::accumulator_start, x3m::ui_scale::sites::accumulator_start,
                                                   x3m::ui_scale::sites::accumulator_start, x3m::ui_scale::sites::accumulator_start}; // D1..D4: the 16.16 remainders
const float x3m_ui_scale_anchor_x[4] = {0.0f, -1.0f, 1.0f, -1.0f}; // A: ax by (flags >> 12) & 3
const float x3m_ui_scale_anchor_y[4] = {0.0f, 1.0f, -1.0f, 1.0f};  // A: ay by (flags >> 14) & 3
volatile std::uint32_t x3m_ui_scale_cameras[x3m::ui_scale::sites::camera_rows * 4] = {}; // F: {camera, n2d, nother, 0} x 8
}

namespace {
namespace engine_patch = x3m::engine_patch;
namespace sites = x3m::ui_scale::sites;
constexpr unsigned site_count = 8;
enum Index : unsigned { Width = 0, Height, MainX, MainY, MenuX, MenuY, Cursor, Projection };
struct Claim {
    const char* name;
    std::uintptr_t va;
    unsigned length;
    const unsigned char* expected;
};
constexpr Claim claims[site_count] = {
    {"ui_scale_width", sites::width_site_va, sites::size_site_length, sites::expected_width_site},
    {"ui_scale_height", sites::height_site_va, sites::size_site_length, sites::expected_height_site},
    {"ui_scale_main_x", sites::main_x_site_va, sites::mouse_site_length, sites::expected_main_x_site},
    {"ui_scale_main_y", sites::main_y_site_va, sites::mouse_site_length, sites::expected_main_y_site},
    {"ui_scale_menu_x", sites::menu_x_site_va, sites::mouse_site_length, sites::expected_menu_x_site},
    {"ui_scale_menu_y", sites::menu_y_site_va, sites::mouse_site_length, sites::expected_menu_y_site},
    {"ui_scale_cursor", sites::cursor_site_va, sites::cursor_site_length, sites::expected_cursor_site},
    {"ui_scale_projection", sites::projection_site_va, sites::projection_site_length, sites::expected_projection_site}};
engine_patch::Site site_[site_count];
bool live_[site_count] = {};          // registered (active, or a rollback that failed)
const char* write_[site_count] = {};  // none|atomic|plain
bool patched_ = false;                // every site active
bool requested_ = false;              // a value or auto was given and passed the checks: waiting for the device
bool automatic_ = false;
bool identity_ = false;
bool device_seen_ = false;
double scale_ = sites::scale_off;
unsigned width_ = 0, height_ = 0;
const char* state_ = "not_initialized";
const char* failed_site_ = "-";
char setting_[sites::setting_capacity] = "-";
// The diagnostic claim (X3M_DEBUG=1), independent of the scale.
engine_patch::Site diagnostic_site_{};
bool diagnostic_live_ = false;
const char* diagnostic_ = "none";
const char* diagnostic_write_ = "none";
constexpr unsigned report_window = 300; // Presents per ui_scale_frame row (the other per-frame diagnostics' window)
unsigned window_frames_ = 0;
bool camera_logged_ = false;

std::uint32_t address_of(const volatile void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN), so the stubs'
// absolute operands can never point into freed memory.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
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
    if (!code_is(sites::projection_tail_va, sites::expected_projection_tail, sites::projection_tail_length))
        return "projection_mismatch";
    if (!code_is(sites::caller1_va, sites::expected_caller1, sites::caller1_length) ||
        !code_is(sites::caller2_va, sites::expected_caller2, sites::caller2_length))
        return "caller_mismatch";
    if (!code_is(sites::width_case_va, sites::expected_width_case, sites::size_case_length) ||
        !code_is(sites::height_case_va, sites::expected_height_case, sites::size_case_length))
        return "size_case_mismatch";
    if (!code_is(sites::main_x_window_va, sites::expected_main_x_window, sites::main_x_window_length) ||
        !code_is(sites::main_y_window_va, sites::expected_main_y_window, sites::main_y_window_length) ||
        !code_is(sites::menu_x_window_va, sites::expected_menu_x_window, sites::menu_x_window_length) ||
        !code_is(sites::menu_y_window_va, sites::expected_menu_y_window, sites::menu_y_window_length) ||
        !code_is(sites::menu_callee_va, sites::expected_menu_callee, sites::menu_callee_length))
        return "mouse_mismatch";
    // The cursor window in two parts: the six bytes between them belong to the chase-fire claim.
    if (!code_is(sites::cursor_pre_va, sites::expected_cursor_pre, sites::cursor_pre_length) ||
        !code_is(sites::cursor_post_va, sites::expected_cursor_post, sites::cursor_post_length))
        return "cursor_mismatch";
    return nullptr;
}
void set_cells(double s) {
    x3m_ui_scale_factor = static_cast<float>(s);
    x3m_ui_scale_inverse16 = sites::inverse16(s);
    x3m_ui_scale_fixed256 = sites::fixed256(s);
}
void set_identity() {
    x3m_ui_scale_factor = 1.0f;
    x3m_ui_scale_inverse16 = sites::identity_inverse16;
    x3m_ui_scale_fixed256 = sites::identity_fixed256;
    for (auto& acc : x3m_ui_scale_mouse_acc) acc = sites::accumulator_start;
}
// Emits `length` stub bytes produced by `encode(slot)` followed by the 4-aligned continuation slot; 0
// when the arena is full. The encoder gets the slot's address (the stub's absolute operand).
template <class Encode> std::uintptr_t emit(unsigned length, Encode encode, void*** slot_out) {
    engine_patch::Emitter e(length + 3 + 4);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + length + 3) & ~std::uintptr_t(3);
    unsigned char code[sites::projection_stub_length];
    encode(std::uint32_t(slot), code);
    e.bytes(code, length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    if (!e.finish()) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    return at;
}
std::uintptr_t emit_stub(unsigned index, void*** slot_out) {
    const std::uint32_t inv = address_of(&x3m_ui_scale_inverse16);
    switch (index) {
    case Width:
    case Height:
        return emit(sites::size_stub_length, [&](std::uint32_t slot, unsigned char* out) { sites::encode_size_stub(inv, slot, out); },
                    slot_out);
    case MainX:
    case MainY:
        return emit(
            sites::main_mouse_stub_length,
            [&](std::uint32_t slot, unsigned char* out) {
                // jmp [slot]: install_site stores the instruction after the load in the slot (the tail is bypassed)
                sites::encode_main_mouse_stub(index == MainX ? sites::delta_x_offset : sites::delta_y_offset, inv,
                                              address_of(&x3m_ui_scale_mouse_acc[index - MainX]), slot, out);
            },
            slot_out);
    case MenuX:
    case MenuY:
        return emit(
            sites::menu_mouse_stub_length,
            [&](std::uint32_t slot, unsigned char* out) {
                sites::encode_menu_mouse_stub(index == MenuX ? sites::delta_x_offset : sites::delta_y_offset, inv,
                                              address_of(&x3m_ui_scale_mouse_acc[index - MainX]), slot, out);
            },
            slot_out);
    case Cursor:
        return emit(
            sites::cursor_stub_length,
            [&](std::uint32_t slot, unsigned char* out) { sites::encode_cursor_stub(address_of(&x3m_ui_scale_fixed256), slot, out); },
            slot_out);
    case Projection:
        return emit(
            sites::projection_stub_length,
            [&](std::uint32_t slot, unsigned char* out) {
                sites::encode_projection_stub(address_of(&x3m_ui_scale_factor), address_of(&x3m_ui_scale_excluded_camera),
                                              address_of(&x3m_ui_scale_scaled), address_of(&x3m_ui_scale_excluded),
                                              address_of(x3m_ui_scale_anchor_x), address_of(x3m_ui_scale_anchor_y), slot, out);
            },
            slot_out);
    }
    return 0;
}
// Claims one site and pushes its stub in front of the tail: "ok" or the reason. A failure after the jmp
// went in puts the original bytes back; when even that fails the site stays registered (live_) for
// shutdown(). The stub's continuation word holds the previous chain head (the tail), except for the four
// mouse stubs, whose word holds the instruction after the displaced load (the tail is emitted but never
// entered); the word is read back before the stub goes live.
const char* install_site(unsigned index) {
    const Claim& c = claims[index];
    write_[index] = "none";
    if (live_[index]) return "already_installed";
    if (!engine_patch::install_window_open()) return "late_claim";
    void** slot = nullptr;
    const std::uintptr_t stub = emit_stub(index, &slot);
    if (!stub || !slot) return "arena_full";
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
    // The stub's continuation word: the tail (the displaced instruction, then the jump back), except for the four
    // mouse stubs, which perform the displaced load themselves and continue at the instruction after it.
    const bool mouse = index >= MainX && index <= MenuY;
    void* continuation = mouse ? reinterpret_cast<void*>(c.va + c.length) : *site_[index].entry;
    if (!engine_patch::store_pointer(slot, continuation) || *slot != continuation ||
        !engine_patch::push_front(site_[index], reinterpret_cast<void*>(stub)))
        failure = "chain_failed";
    else if (!engine_patch::read_code(c.va, now, c.length) || std::memcmp(now, site_[index].patched, 5) ||
             std::memcmp(now + 5, c.expected + 5, c.length - 5) ||
             *site_[index].entry != reinterpret_cast<void*>(stub))
        failure = "readback_failed";
    if (!failure) return "ok";
    engine_patch::restore(site_[index]); // judged by the registration it leaves, not the protection result
    live_[index] = site_[index].patched_in;
    return live_[index] ? "rollback_failed" : failure;
}
// Takes every registered site back (only over our jmp); true when none stays registered.
bool restore_all() {
    bool clean = true;
    for (unsigned i = site_count; i-- > 0;) {
        if (!live_[i]) continue;
        engine_patch::restore(site_[i]);
        live_[i] = site_[i].patched_in;
        clean = clean && !live_[i];
    }
    return clean;
}
bool any_live() {
    for (bool l : live_)
        if (l) return true;
    return false;
}
// The eight claims in order (the projection last: nothing visible changes until every input-side site
// is in); on a failure the earlier ones are restored. The cells hold the scale before the first claim so
// a stub that goes live is never wrong; a failed transaction puts the identity back.
const char* install_all(double s) {
    if (!pin_self()) return "pin_failed";
    set_cells(s);
    for (auto& acc : x3m_ui_scale_mouse_acc) acc = sites::accumulator_start;
    x3m_ui_scale_excluded_camera = 0;
    for (unsigned i = 0; i < site_count; ++i) {
        const char* r = install_site(i);
        if (std::strcmp(r, "ok")) {
            failed_site_ = claims[i].name;
            set_identity();
            restore_all();
            return r;
        }
    }
    patched_ = true;
    return "ok";
}
// The diagnostic entry claim: the per-camera counter stub in front of the displaced prologue.
const char* install_diagnostic() {
    diagnostic_write_ = "none";
    if (diagnostic_live_) return "already_installed";
    if (!engine_patch::install_window_open()) return "late_claim";
    if (!code_is(sites::entry_site_va, sites::expected_entry_site, sites::entry_site_length)) return "bytes_mismatch";
    if (!pin_self()) return "pin_failed";
    void** slot = nullptr;
    const std::uintptr_t stub = emit(
        sites::entry_stub_length,
        [&](std::uint32_t s, unsigned char* out) { sites::encode_entry_stub(address_of(x3m_ui_scale_cameras), s, out); }, &slot);
    if (!stub || !slot) return "arena_full";
    engine_patch::SiteSpec spec{};
    spec.name = "ui_scale_entry";
    spec.address = sites::entry_site_va;
    spec.length = sites::entry_site_length;
    spec.ret_pop = 0;
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, sites::expected_entry_site, sites::entry_site_length);
    diagnostic_site_ = engine_patch::Site{};
    const bool claimed = engine_patch::claim(diagnostic_site_, spec);
    if (claimed || diagnostic_site_.patched_in || !std::strcmp(diagnostic_site_.status, "patch_rolled_back"))
        diagnostic_write_ = diagnostic_site_.atomic_write ? "atomic" : "plain";
    diagnostic_live_ = diagnostic_site_.patched_in;
    if (!claimed) return diagnostic_live_ ? "rollback_failed" : diagnostic_site_.status;
    const char* failure = nullptr;
    unsigned char now[engine_patch::max_prologue]{};
    if (!engine_patch::store_pointer(slot, *diagnostic_site_.entry) ||
        !engine_patch::push_front(diagnostic_site_, reinterpret_cast<void*>(stub)))
        failure = "chain_failed";
    else if (!engine_patch::read_code(sites::entry_site_va, now, sites::entry_site_length) ||
             std::memcmp(now, diagnostic_site_.patched, 5) || std::memcmp(now + 5, sites::expected_entry_site + 5, 2))
        failure = "readback_failed";
    if (!failure) return "ok";
    engine_patch::restore(diagnostic_site_);
    diagnostic_live_ = diagnostic_site_.patched_in;
    return diagnostic_live_ ? "rollback_failed" : failure;
}
// The active cockpit through the registry (the bounded walk of the native resolver 0x0041cd20, validated
// reads only), then its HUD camera at +8. 0 when anything is unreadable or implausible.
bool read_u32(std::uintptr_t base, unsigned offset, std::uint32_t* out) {
    return base && base <= UINTPTR_MAX - offset - 4 && x3m::engine_memory::read(base + offset, out, sizeof *out);
}
std::uint32_t hud_camera() {
    std::uint32_t registry = 0, table = 0, handle = 0, bucket[2]{}, link = 0, cockpit = 0, camera = 0;
    if (!read_u32(sites::cockpit_registry_slot_va, 0, &registry) || !read_u32(registry, 0, &table) ||
        !read_u32(registry, 0x10, &handle) || !table || table > UINTPTR_MAX - 8 ||
        !x3m::engine_memory::read(table, bucket, sizeof bucket))
        return 0;
    const std::uint32_t n = bucket[1];
    if (!n || n > 65536 || (n & (n - 1)) || !read_u32(bucket[0], 4 * ((n - 1) & handle), &link)) return 0;
    for (unsigned i = 0; link && i < 32; ++i) {
        std::uint32_t row[3]{};
        if (link > UINTPTR_MAX - 12 || !x3m::engine_memory::read(link, row, sizeof row)) return 0;
        if (row[1] == handle) {
            cockpit = row[2];
            break;
        }
        link = row[0];
    }
    if (!cockpit || (cockpit & 3u) || !read_u32(cockpit, sites::cockpit_hud_camera_offset, &camera)) return 0;
    return (camera & 3u) ? 0 : camera;
}
// Publishes the excluded camera for the next frame; one ui_scale_camera row whenever it changes (0 = none:
// no cockpit yet, or the walk failed, so the HUD scene is scaled until it resolves).
void refresh_excluded_camera(unsigned long long frame) {
    const std::uint32_t camera = hud_camera();
    const std::uint32_t previous = x3m_ui_scale_excluded_camera;
    x3m_ui_scale_excluded_camera = camera;
    if (camera != previous || !camera_logged_) {
        camera_logged_ = true;
        x3m::log("ui_scale_camera frame=%llu excluded_camera=%08lx previous=%08lx", frame, static_cast<unsigned long>(camera),
            static_cast<unsigned long>(previous));
    }
}
}

namespace x3m::ui_scale {
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_ || requested_ || diagnostic_live_) {
        SetLastError(error);
        return patched_;
    }
    wchar_t text[sites::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_UI_SCALE", text, sites::setting_capacity);
    printable(text, length, setting_);
    identity_ = object_trace::executable_verified();
    double s = sites::scale_off;
    const sites::Parse parsed = length >= sites::setting_capacity ? sites::Parse::invalid : sites::parse_setting(text, &s);
    const char* mode = "off";
    if (length >= sites::setting_capacity)
        state_ = "too_long";
    else if (parsed == sites::Parse::invalid)
        state_ = "invalid_setting";
    else if (parsed == sites::Parse::off || (parsed == sites::Parse::value && s == sites::scale_off))
        state_ = "off";
    else if (parsed == sites::Parse::value && !sites::in_range(s))
        state_ = "out_of_range";
    else if (!identity_)
        state_ = "executable_mismatch";
    else if (const char* mismatch = windows_verified())
        state_ = mismatch;
    else {
        requested_ = true;
        automatic_ = parsed == sites::Parse::automatic;
        scale_ = automatic_ ? sites::scale_off : s;
        mode = automatic_ ? "auto" : "fixed";
        state_ = "pending"; // resolved and claimed at CreateDevice
    }
    // The per-camera counter (open item 1 of the note): --debug only, and only when the option asks for a scale.
    if (log_tier::debug() && requested_) {
        diagnostic_ = install_diagnostic();
        if (!std::strcmp(diagnostic_, "ok")) diagnostic_ = "active";
    }
    const bool refused = !requested_ && std::strcmp(state_, "off");
    log("ui_scale setting=%s status=%s reason=%s mode=%s scale=%.4f diagnostic=%s diagnostic_write=%s site=%08lx", setting_,
        requested_ ? "pending" : refused ? "refused" : "off", state_, mode, scale_, diagnostic_, diagnostic_write_,
        static_cast<unsigned long>(sites::projection_site_va));
    SetLastError(error);
    return false;
}
bool device_created(unsigned width, unsigned height) {
    const DWORD error = GetLastError();
    if (!requested_ || device_seen_) {
        SetLastError(error);
        return patched_;
    }
    device_seen_ = true;
    width_ = width;
    height_ = height;
    if (automatic_) scale_ = sites::auto_scale(height);
    const char* reason = "ok";
    if (scale_ == sites::scale_off)
        reason = "off_auto"; // 1080 lines or fewer: nothing to do, nothing patched
    else if (!engine_patch::install_window_open())
        reason = "late_claim";
    else
        reason = install_all(scale_);
    const bool applied = patched_;
    if (!applied) scale_ = sites::scale_off;
    if (applied) refresh_excluded_camera(0); // usually absent this early (no cockpit yet): one row says so
    state_ = reason;
    const bool registered = any_live();
    char writes[site_count * 8] = "";
    for (unsigned i = 0; i < site_count; ++i) {
        if (i) std::strncat(writes, ",", sizeof writes - std::strlen(writes) - 1);
        std::strncat(writes, write_[i] ? write_[i] : "none", sizeof writes - std::strlen(writes) - 1);
    }
    log("ui_scale_install width=%u height=%u scale=%.4f status=%s reason=%s failed_site=%s mode=%s virtual=%lux%lu inverse16=%lu fixed256=%lu writes=%s arena_used=%u",
        width, height, scale_, applied ? "patched" : registered ? "patched_unverified" : !std::strcmp(reason, "off_auto") ? "off" : "refused",
        reason, failed_site_, automatic_ ? "auto" : "fixed",
        static_cast<unsigned long>(sites::virtual_size(width & 0x7fffu, x3m_ui_scale_inverse16)),
        static_cast<unsigned long>(sites::virtual_size(height & 0x7fffu, x3m_ui_scale_inverse16)),
        static_cast<unsigned long>(x3m_ui_scale_inverse16), static_cast<unsigned long>(x3m_ui_scale_fixed256), writes,
        engine_patch::arena_used());
    SetLastError(error);
    return applied;
}
void after_reset(unsigned width, unsigned height) {
    if (!patched_ || !automatic_) return;
    const DWORD error = GetLastError();
    const double s = sites::auto_scale(height);
    width_ = width;
    height_ = height;
    if (s != scale_) {
        // Data cells only (the code stays claimed): a scale of 1 leaves every stub at the identity.
        scale_ = s;
        set_cells(s);
        for (auto& acc : x3m_ui_scale_mouse_acc) acc = sites::accumulator_start;
        log("ui_scale_reset width=%u height=%u scale=%.4f virtual=%lux%lu inverse16=%lu fixed256=%lu", width, height, s,
            static_cast<unsigned long>(sites::virtual_size(width & 0x7fffu, x3m_ui_scale_inverse16)),
            static_cast<unsigned long>(sites::virtual_size(height & 0x7fffu, x3m_ui_scale_inverse16)),
            static_cast<unsigned long>(x3m_ui_scale_inverse16), static_cast<unsigned long>(x3m_ui_scale_fixed256));
    }
    SetLastError(error);
}
void present(unsigned long long frame) {
    if (!patched_ && !diagnostic_live_) return;
    const DWORD error = GetLastError();
    if (patched_) refresh_excluded_camera(frame); // the render thread reads it on the next frame
    ++window_frames_;
    if (diagnostic_live_ && log_tier::cached_debug && frame % report_window == 0) {
        // The stub and this Present run on the same thread (the engine's render thread): read the window's sums,
        // then clear. Counts are summed over the window; a camera row keeps its slot until the print.
        char cameras[8 * 40] = "";
        for (unsigned r = 0; r < sites::camera_rows; ++r) {
            volatile std::uint32_t* row = x3m_ui_scale_cameras + 4 * r;
            if (!row[0] && !row[1] && !row[2]) continue;
            char one[40];
            std::snprintf(one, sizeof one, "%s%08lx:%lu/%lu", cameras[0] ? "," : "", static_cast<unsigned long>(row[0]),
                          static_cast<unsigned long>(row[1]), static_cast<unsigned long>(row[2]));
            std::strncat(cameras, one, sizeof cameras - std::strlen(cameras) - 1);
            row[0] = 0;
            row[1] = 0;
            row[2] = 0;
        }
        log("ui_scale_frame frame=%llu frames=%u scale=%.4f scaled=%lu excluded=%lu excluded_camera=%08lx cameras=%s", frame,
            window_frames_, scale_, static_cast<unsigned long>(x3m_ui_scale_scaled), static_cast<unsigned long>(x3m_ui_scale_excluded),
            static_cast<unsigned long>(x3m_ui_scale_excluded_camera), cameras[0] ? cameras : "-");
        x3m_ui_scale_scaled = 0;
        x3m_ui_scale_excluded = 0;
        window_frames_ = 0;
    }
    SetLastError(error);
}
bool shutdown() {
    if (!any_live() && !diagnostic_live_) return true;
    const DWORD error = GetLastError();
    set_identity();
    const bool clean = restore_all();
    patched_ = false;
    const char* diagnostic = "none";
    if (diagnostic_live_) {
        engine_patch::restore(diagnostic_site_);
        diagnostic = diagnostic_site_.status;
        diagnostic_live_ = diagnostic_site_.patched_in;
    }
    state_ = clean ? "restored" : "restore_failed";
    // One row straight to the log's OS handle: this runs inside DllMain (dynamic unload), where the
    // capture lock must not be taken. The stubs and tails stay callable in the arena.
    const HANDLE handle = log_handle();
    if (handle && handle != INVALID_HANDLE_VALUE) {
        char line[200];
        const int n = std::snprintf(line, sizeof line, "ui_scale_restore status=%s registered=%u diagnostic=%s\n", state_,
                                    (any_live() || diagnostic_live_) ? 1u : 0u, diagnostic);
        DWORD written = 0;
        if (n > 0 && unsigned(n) < sizeof line) WriteFile(handle, line, DWORD(n), &written, nullptr);
    }
    SetLastError(error);
    return !any_live() && !diagnostic_live_;
}
const char* state() {
    return state_;
}
double scale() {
    return patched_ ? scale_ : sites::scale_off;
}
bool patched() {
    return any_live();
}
bool diagnostic_patched() {
    return diagnostic_live_;
}
}
