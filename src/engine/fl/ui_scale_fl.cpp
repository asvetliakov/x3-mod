// FL edition of x3m::ui_scale (src/proxy/ui_scale.h; the ap target keeps src/proxy/ui_scale.cpp): the UI scale option on
// X3FL.exe (docs/architecture/ui-scale.md, "Editions"; sites, windows and stubs: ui_scale_sites_fl.h, verified in
// docs/reverse-engineering/fl/ui-scale.md, "Verified 2026-11-03 against X3FL.exe"). The scale, its three fixed-point
// forms, the parser and the `auto` mapping are AP's (src/proxy/ui_scale_sites.h).
//
// initialize() (backend-load path, the FL loader block): the setting, X3FL.exe's identity (fl::module_base() != 0) and
// every identity window at base + RVA with its fixups bound to base; nothing is claimed. device_created(): `auto`
// resolved from the back buffer, the windows checked again, then the eleven claims in order (B, C, D1, D2, E1, E2, G, H,
// I, J, the projection A last), each a five-byte jmp inside one aligned qword (engine_patch: one lock cmpxchg8b), inside
// the install window (a device created after the first Present is refused, late_claim). All or none: a failed claim
// restores the earlier ones (only over our jmp) and puts the identity cells back; a restore that fails keeps the site
// registered (patched_unverified) with the cells at the identity, under which every stub reproduces the displaced
// instructions exactly (A within one ulp for P[12]/P[13]). The stubs are emitted bytes in the never-freed arena reading
// the data cells below (this module is pinned): no call, no x87; the projection stub keeps EFLAGS, ECX, XMM1 and MXCSR,
// the others only touch registers and flags that are dead at their site. Per frame only present(): the excluded camera
// (a bounded registry walk of validated reads, patched only) and, under --debug, one ui_scale_frame row every 300
// Presents. after_reset() re-derives `auto` into the cells (no code is rewritten). shutdown() is for the fixture (the FL
// loader's DLL_PROCESS_DETACH unload calls are AP only). LastError is saved and restored around every entry point.
#include "ui_scale_fl.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_memory.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/log_tiers.h"
#include "executable_identity_fl.h"
#include "ui_scale_sites_fl.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace engine_patch = x3m::engine_patch;
namespace sites = x3m::ui_scale::sites;
namespace F = x3m::fl::ui_scale_fl;
// The data cells the stubs read (aligned dwords; written only between frames or before the claims).
volatile float factor_ = 1.0f;                               // A: s
volatile std::uint32_t inverse16_ = sites::identity_inverse16; // B, C, D, I: round(65536 / s)
volatile std::uint32_t fixed256_ = sites::identity_fixed256;   // E, G, H, J: round(256 s)
volatile std::uint32_t excluded_camera_ = 0;                 // A: the active cockpit's HUD camera (0 = none)
volatile std::uint32_t scaled_ = 0, excluded_ = 0;            // A: instances since the last report
volatile std::int32_t acc_[4] = {sites::accumulator_start, sites::accumulator_start, sites::accumulator_start,
                                 sites::accumulator_start}; // D1 x, D1 y, D2 x, D2 y: the 16.16 remainders
const float anchor_x_[4] = {sites::anchor_x_table[0], sites::anchor_x_table[1], sites::anchor_x_table[2],
                            sites::anchor_x_table[3]};
const float anchor_y_[4] = {sites::anchor_y_table[0], sites::anchor_y_table[1], sites::anchor_y_table[2],
                            sites::anchor_y_table[3]};

engine_patch::Site site_[F::site_count];
bool live_[F::site_count] = {};         // registered (active, or a rollback that failed)
const char* write_[F::site_count] = {}; // none|atomic|plain
bool patched_ = false;                  // every site active
bool requested_ = false;                // a value or auto was given and passed the checks: waiting for the device
bool automatic_ = false;
bool device_seen_ = false;
bool debug_ = false; // --debug: the counter row every report_window Presents
double scale_ = sites::scale_off;
std::uintptr_t base_ = 0;
const char* state_ = "not_initialized";
const char* failed_site_ = "-";
const char* window_ = "-";
char setting_[sites::setting_capacity] = "-";
constexpr unsigned report_window = 300; // Presents per ui_scale_frame row (AP's window)
unsigned window_frames_ = 0;
bool camera_logged_ = false;
#ifdef X3M_UI_SCALE_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
#endif

std::uint32_t address_of(const volatile void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}
// Pins this module for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN), so the stubs' absolute
// operands can never point into freed memory.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
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
// Every identity window at base (fail closed); nullptr when all match, else the reason (window_ names the window).
const char* windows_refused(std::uintptr_t base) {
    const char* why = nullptr;
    const char* window = F::refused_window(base, read_window, &why);
    window_ = window ? window : "-";
    return window ? why : nullptr;
}
void set_cells(double s) {
    factor_ = static_cast<float>(s);
    inverse16_ = sites::inverse16(s);
    fixed256_ = sites::fixed256(s);
}
void reset_remainders() {
    for (auto& acc : acc_) acc = sites::accumulator_start;
}
void set_identity() {
    factor_ = 1.0f;
    inverse16_ = sites::identity_inverse16;
    fixed256_ = sites::identity_fixed256;
    reset_remainders();
}
// Emits the stub `encode(slot, out)` writes (exactly `length` bytes) followed by its 4-aligned continuation slot; 0
// when the arena is full or the encoder disagrees with the length.
template <class Encode> std::uintptr_t emit(unsigned length, Encode encode, void*** slot_out) {
    engine_patch::Emitter e(length + 3 + 4);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + length + 3) & ~std::uintptr_t(3);
    unsigned char code[F::max_stub_length];
    std::memset(code, 0xcc, sizeof code);
    const unsigned n = encode(std::uint32_t(slot), code); // n != length: the block stays int3 and nothing is claimed
    e.bytes(code, length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    if (!e.finish() || n != length) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    return at;
}
std::uintptr_t emit_stub(unsigned index, void*** slot_out) {
    const std::uint32_t inv = address_of(&inverse16_), fixed = address_of(&fixed256_);
    switch (index) {
    case F::Width:
    case F::Height:
        return emit(
            F::size_stub_length,
            [&](std::uint32_t slot, unsigned char* out) {
                return F::encode_size_stub(index == F::Width ? 4 : 6, inv, slot, out);
            },
            slot_out);
    case F::DeltaDirect:
    case F::DeltaMapped: {
        const unsigned k = 2 * (index - F::DeltaDirect);
        return emit(
            F::delta_stub_length,
            [&](std::uint32_t slot, unsigned char* out) {
                return F::encode_delta_stub(inv, address_of(&acc_[k]), address_of(&acc_[k + 1]), slot, out);
            },
            slot_out);
    }
    case F::CursorX:
    case F::CursorY:
        return emit(
            F::cursor_stub_length, [&](std::uint32_t slot, unsigned char* out) { return F::encode_cursor_stub(fixed, slot, out); },
            slot_out);
    case F::OverlayIcon:
        return emit(
            F::overlay_stub_length, [&](std::uint32_t slot, unsigned char* out) { return F::encode_overlay_stub(fixed, slot, out); },
            slot_out);
    case F::CursorAim:
        return emit(
            F::aim_stub_length, [&](std::uint32_t slot, unsigned char* out) { return F::encode_aim_stub(fixed, slot, out); },
            slot_out);
    case F::MapLabel:
        return emit(
            F::map_label_stub_length,
            [&](std::uint32_t slot, unsigned char* out) { return F::encode_map_label_stub(inv, slot, out); }, slot_out);
    case F::MapPick:
        return emit(
            F::map_pick_stub_length,
            [&](std::uint32_t slot, unsigned char* out) { return F::encode_map_pick_stub(fixed, slot, out); }, slot_out);
    case F::Projection:
        return emit(
            F::projection_stub_length,
            [&](std::uint32_t slot, unsigned char* out) {
                return F::encode_projection_stub(address_of(&factor_), address_of(&excluded_camera_), address_of(&scaled_),
                                                 address_of(&excluded_), address_of(anchor_x_), address_of(anchor_y_), slot,
                                                 out);
            },
            slot_out);
    }
    return 0;
}
// Claims one site and pushes its stub in front of the tail: "ok" or the reason. A failure after the jmp went in puts
// the original bytes back; when even that fails the site stays registered (live_) for shutdown(). The stub's
// continuation word holds the tail (D, E, A: the displaced instruction, then the jump back) or, for the bypass stubs (B,
// C, G, H, I, J), base + rva + length (the tail is emitted but never entered); the word is read back before the stub
// goes live.
const char* install_site(unsigned index) {
    const F::Site& c = F::site_table[index];
    write_[index] = "none";
    if (live_[index]) return "already_installed";
    if (!engine_patch::install_window_open()) return "late_claim";
    void** slot = nullptr;
    const std::uintptr_t stub = emit_stub(index, &slot);
    if (!stub || !slot) return "arena_full";
    const std::uintptr_t at = base_ + c.rva;
    unsigned char expected[sizeof c.bytes];
    F::expected_site(c, base_, expected);
    engine_patch::SiteSpec spec{};
    spec.name = c.name;
    spec.address = at;
    spec.length = c.length;
    spec.ret_pop = 0;
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, expected, c.length);
    site_[index] = engine_patch::Site{};
    const bool claimed = engine_patch::claim(site_[index], spec);
    if (claimed || site_[index].patched_in || !std::strcmp(site_[index].status, "patch_rolled_back"))
        write_[index] = site_[index].atomic_write ? "atomic" : "plain";
    live_[index] = site_[index].patched_in;
    if (!claimed) return live_[index] ? "rollback_failed" : site_[index].status;
    const char* failure = nullptr;
    unsigned char now[engine_patch::max_prologue]{};
    void* continuation = c.bypass ? reinterpret_cast<void*>(at + c.length) : *site_[index].entry;
    if (!engine_patch::store_pointer(slot, continuation) || *slot != continuation ||
        !engine_patch::push_front(site_[index], reinterpret_cast<void*>(stub)))
        failure = "chain_failed";
    else if (!engine_patch::read_code(at, now, c.length) || std::memcmp(now, site_[index].patched, 5) ||
             std::memcmp(now + 5, expected + 5, c.length - 5) || *site_[index].entry != reinterpret_cast<void*>(stub))
        failure = "readback_failed";
    if (!failure) return "ok";
    engine_patch::restore(site_[index]); // judged by the registration it leaves, not the protection result
    live_[index] = site_[index].patched_in;
    return live_[index] ? "rollback_failed" : failure;
}
// Takes every registered site back (only over our jmp), in reverse order; true when none stays registered.
bool restore_all() {
    bool clean = true;
    for (unsigned i = F::site_count; i-- > 0;) {
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
// The eleven claims in order (the projection last: nothing visible changes until every input-side site and the map
// pair are in); a failure restores the earlier ones. The cells hold the scale before the first claim so a stub that
// goes live is never wrong; a failed transaction puts the identity back.
const char* install_all(double s) {
    if (!pin_self()) return "pin_failed";
    if (const char* why = windows_refused(base_)) return why; // checked again: the claims rest on them
    set_cells(s);
    reset_remainders();
    excluded_camera_ = 0;
    for (unsigned i = 0; i < F::site_count; ++i) {
        const char* r = install_site(i);
        if (std::strcmp(r, "ok")) {
            failed_site_ = F::site_table[i].name;
            set_identity();
            restore_all();
            return r;
        }
    }
    patched_ = true;
    return "ok";
}
bool read_u32(std::uintptr_t at, std::uint32_t* out) {
    return at && !(at & 3u) && at <= UINTPTR_MAX - 4 && x3m::engine_memory::read(at, out, sizeof *out);
}
// The active cockpit through the registry (the bounded walk of the native resolver base+0x01e420, validated reads only),
// then its HUD camera at +8, admitted only while that camera is attached to the cockpit's own HUD scene (+4; the camera's
// +0x1c, which attach writes and detach and the scene destructor clear): a stale +8 after the cockpit teardown (which
// clears only +4) publishes 0. 0 when anything is unreadable or implausible.
std::uint32_t hud_camera() {
    std::uint32_t registry = 0, table = 0, handle = 0, bucket[2]{}, link = 0, cockpit = 0, scene = 0, camera = 0, attached = 0;
    if (!read_u32(base_ + F::registry_slot_rva, &registry) || !read_u32(registry, &table) ||
        !read_u32(registry + 0x10, &handle) || !handle || !table || (table & 3u) ||
        !x3m::engine_memory::read(table, bucket, sizeof bucket))
        return 0;
    const std::uint32_t n = bucket[1];
    if (!n || n > 65536 || (n & (n - 1)) || !read_u32(bucket[0] + 4 * ((n - 1) & handle), &link)) return 0;
    for (unsigned i = 0; link && i < 32; ++i) {
        std::uint32_t row[3]{};
        if ((link & 3u) || link > UINTPTR_MAX - 12 || !x3m::engine_memory::read(link, row, sizeof row)) return 0;
        if (row[1] == handle) {
            cockpit = row[2];
            break;
        }
        link = row[0];
    }
    if (!cockpit) return 0; // no row for the active handle (explicit; read_u32 would also refuse the low address)
    if (!read_u32(cockpit + F::cockpit_hud_scene_offset, &scene) || !read_u32(cockpit + F::cockpit_hud_camera_offset, &camera) ||
        !scene || !read_u32(camera + F::camera_scene_offset, &attached) || attached != scene)
        return 0;
    return camera;
}
// Publishes the excluded camera for the next frame; one ui_scale_camera row whenever it changes (0 = none: no cockpit
// yet, a detached camera or a walk that failed, so the HUD scene is scaled until it resolves).
void refresh_excluded_camera(unsigned long long frame) {
    const std::uint32_t camera = hud_camera();
    const std::uint32_t previous = excluded_camera_;
    excluded_camera_ = camera;
    if (camera != previous || !camera_logged_) {
        camera_logged_ = true;
        x3m::log("ui_scale_camera frame=%llu excluded_camera=%08lx previous=%08lx edition=fl", frame,
                 static_cast<unsigned long>(camera), static_cast<unsigned long>(previous));
    }
}
}

namespace x3m::ui_scale {
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_ || requested_) {
        SetLastError(error);
        return patched_;
    }
    wchar_t text[sites::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_UI_SCALE", text, sites::setting_capacity);
    printable(text, length, setting_);
#ifdef X3M_UI_SCALE_FL_FIXTURE
    const std::uintptr_t base = fixture_base_ ? fixture_base_ : x3m::fl::module_base();
#else
    const std::uintptr_t base = x3m::fl::module_base();
#endif
    double s = sites::scale_off;
    const sites::Parse parsed = length >= sites::setting_capacity ? sites::Parse::invalid : sites::parse_setting(text, &s);
    const char* mode = "off";
    window_ = "-";
    if (length >= sites::setting_capacity)
        state_ = "too_long";
    else if (parsed == sites::Parse::invalid)
        state_ = "invalid_setting";
    else if (parsed == sites::Parse::off || (parsed == sites::Parse::value && s == sites::scale_off))
        state_ = "off";
    else if (parsed == sites::Parse::value && !sites::in_range(s))
        state_ = "out_of_range";
    else if (!base)
        state_ = "executable_mismatch";
    else if (const char* refused = windows_refused(base))
        state_ = refused;
    else {
        base_ = base;
        requested_ = true;
        automatic_ = parsed == sites::Parse::automatic;
        scale_ = automatic_ ? sites::scale_off : s;
        mode = automatic_ ? "auto" : "fixed";
        state_ = "pending"; // resolved and claimed at CreateDevice
    }
    debug_ = requested_ && log_tier::debug();
    const bool refused = !requested_ && std::strcmp(state_, "off");
    log("ui_scale setting=%s status=%s reason=%s mode=%s scale=%.4f diagnostic=%s window=%s base=0x%08lx site_rva=0x%06lx edition=fl",
        setting_, requested_ ? "pending" : refused ? "refused" : "off", state_, mode, scale_,
        debug_ ? "counters" : "none", window_, static_cast<unsigned long>(base),
        static_cast<unsigned long>(F::site_table[F::Projection].rva));
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
    char writes[F::site_count * 8] = "";
    for (unsigned i = 0; i < F::site_count; ++i) {
        if (i) std::strncat(writes, ",", sizeof writes - std::strlen(writes) - 1);
        std::strncat(writes, write_[i] ? write_[i] : "none", sizeof writes - std::strlen(writes) - 1);
    }
    log("ui_scale_install width=%u height=%u scale=%.4f status=%s reason=%s failed_site=%s window=%s mode=%s virtual=%lux%lu inverse16=%lu fixed256=%lu writes=%s arena_used=%u edition=fl",
        width, height, scale_, applied ? "patched" : registered ? "patched_unverified" : !std::strcmp(reason, "off_auto") ? "off" : "refused",
        reason, failed_site_, window_, automatic_ ? "auto" : "fixed",
        static_cast<unsigned long>(sites::virtual_size(width & 0x7fffu, inverse16_)),
        static_cast<unsigned long>(sites::virtual_size(height & 0x7fffu, inverse16_)), static_cast<unsigned long>(inverse16_),
        static_cast<unsigned long>(fixed256_), writes, engine_patch::arena_used());
    SetLastError(error);
    return applied;
}
void after_reset(unsigned width, unsigned height) {
    if (!patched_ || !automatic_) return;
    const DWORD error = GetLastError();
    const double s = sites::auto_scale(height);
    if (s != scale_) {
        // Data cells only (the code stays claimed): a scale of 1 leaves every stub at the identity.
        scale_ = s;
        set_cells(s);
        reset_remainders();
        log("ui_scale_reset width=%u height=%u scale=%.4f virtual=%lux%lu inverse16=%lu fixed256=%lu edition=fl", width, height, s,
            static_cast<unsigned long>(sites::virtual_size(width & 0x7fffu, inverse16_)),
            static_cast<unsigned long>(sites::virtual_size(height & 0x7fffu, inverse16_)), static_cast<unsigned long>(inverse16_),
            static_cast<unsigned long>(fixed256_));
    }
    SetLastError(error);
}
void present(unsigned long long frame) {
    if (!patched_) return;
    const DWORD error = GetLastError();
    refresh_excluded_camera(frame); // the render thread reads it on the next frame
    ++window_frames_;
    if (debug_ && frame % report_window == 0) {
        // The stub and this Present run on the engine's render thread: read the window's sums, then clear.
        log("ui_scale_frame frame=%llu frames=%u scale=%.4f scaled=%lu excluded=%lu excluded_camera=%08lx cameras=- edition=fl",
            frame, window_frames_, scale_, static_cast<unsigned long>(scaled_), static_cast<unsigned long>(excluded_),
            static_cast<unsigned long>(excluded_camera_));
        scaled_ = 0;
        excluded_ = 0;
        window_frames_ = 0;
    }
    SetLastError(error);
}
bool shutdown() {
    if (!any_live()) return true;
    const DWORD error = GetLastError();
    set_identity();
    const bool clean = restore_all();
    patched_ = false;
    state_ = clean ? "restored" : "restore_failed";
    log("ui_scale_restore status=%s registered=%u diagnostic=none edition=fl", state_, any_live() ? 1u : 0u);
    SetLastError(error);
    return !any_live();
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
    return false; // the AP entry counter (F) is not ported; the projection stub's counters stand in under --debug
}
std::uint32_t excluded_camera() {
    return excluded_camera_;
}
unsigned scaled_count() {
    return scaled_;
}
unsigned excluded_count() {
    return excluded_;
}
#ifdef X3M_UI_SCALE_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
void fixture_reset() {
    if (any_live()) return;
    patched_ = requested_ = automatic_ = device_seen_ = debug_ = camera_logged_ = false;
    scale_ = sites::scale_off;
    base_ = 0;
    state_ = "not_initialized";
    failed_site_ = window_ = "-";
    window_frames_ = 0;
    scaled_ = excluded_ = 0;
    excluded_camera_ = 0;
    set_identity();
    for (auto& w : write_) w = nullptr;
}
#endif
}
