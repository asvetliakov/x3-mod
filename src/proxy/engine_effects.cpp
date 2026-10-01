#include "engine_effects.h"
#include "capture.h"
#include "config.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "log_tiers.h"
#include <windows.h>
#include <cstring>
#include <new>

// Process-wide state of the engine-effects phase 1a (engine_effects.h). initialize() runs once on the load path;
// begin_frame(), body_for_model() and body() run on the application's render thread only (the Present hook and the
// draw hooks), the only writer and reader of the table and the resolution after initialize.
namespace {
namespace core = x3m::engine_effects::core;
namespace lfc = x3m::lens_flare_cull::core;
core::Mode mode_ = core::Mode::native;
bool identity_ = false, hook_ = false, initialized_ = false;
const char* status_ = "not_initialized";
#ifdef X3M_MOTION_OUTPUT_FIXTURE
bool fixture_identity_ = false;
std::uintptr_t body_global_ = lfc::census::body_global_va;
#else
constexpr std::uintptr_t body_global_ = lfc::census::body_global_va;
#endif
// The table, its index and the resolution: allocated at the first begin_frame (about 260 KB), never freed.
struct State {
    core::BodyTable table;
    core::NameIndex index;
    core::Resolver resolver;
};
State* state_ = nullptr;
bool load_attempted_ = false, table_loaded_ = false;
lfc::Table engine_table_{};
std::uint32_t scanned_dynamic_ = 0, restarts_ = 0;
unsigned body_rows_ = 0;
std::uint32_t logged_mapped_ = 0xffffffffu;
constexpr unsigned body_row_cap = 16;   // engine_effects_resolve rows per process (a game load re-binds the set)
constexpr unsigned check_budget = 32;   // mapped dynamic names re-read per frame (round-robin)
constexpr DWORD table_size_limit = 8u << 20;

// <EXE directory>\x3m\engine_bodies.json, or X3M_ENGINE_BODIES verbatim; 0 / none disables. Reads the whole file.
const char* read_table_file(char** text, DWORD* size, char* path_utf8, unsigned path_cap) {
    *text = nullptr;
    *size = 0;
    path_utf8[0] = 0;
    wchar_t value[8]{};
    const DWORD env = x3m::config::get(L"X3M_ENGINE_BODIES", value, 8);
    if (env && env < 8 && (!lstrcmpW(value, L"0") || !lstrcmpiW(value, L"none"))) return "disabled";
    static wchar_t path[MAX_PATH + 64];
    DWORD length = env ? x3m::config::get(L"X3M_ENGINE_BODIES", path, MAX_PATH) : GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return "path";
    if (!env) {
        DWORD cut = length;
        while (cut && path[cut - 1] != L'\\' && path[cut - 1] != L'/') --cut;
        static const wchar_t leaf[] = L"x3m\\engine_bodies.json";
        if (!cut || cut + sizeof leaf / sizeof leaf[0] >= MAX_PATH + 64) return "path";
        std::memcpy(path + cut, leaf, sizeof leaf);
    }
    if (!WideCharToMultiByte(CP_UTF8, 0, path, -1, path_utf8, int(path_cap), nullptr, nullptr))
        std::memcpy(path_utf8, "(long)", 7);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return "absent";
    const char* reason = nullptr;
    LARGE_INTEGER bytes{};
    if (!GetFileSizeEx(file, &bytes) || bytes.QuadPart <= 0)
        reason = "empty";
    else if (bytes.QuadPart > table_size_limit)
        reason = "too_large";
    else if (!(*text = new (std::nothrow) char[std::size_t(bytes.QuadPart)]))
        reason = "allocation";
    else {
        DWORD read = 0;
        if (ReadFile(file, *text, DWORD(bytes.QuadPart), &read, nullptr) && read == DWORD(bytes.QuadPart))
            *size = read;
        else {
            delete[] *text;
            *text = nullptr;
            reason = "read_failed";
        }
    }
    CloseHandle(file);
    return reason ? reason : "read";
}
void load_table(unsigned long long frame) {
    load_attempted_ = true;
    state_ = new (std::nothrow) State;
    if (!state_) {
        x3m::log("engine_effects_bodies event=rejected reason=allocation bodies=0 frame=%llu", frame);
        return;
    }
    state_->table.count = state_->table.refused = 0;
    state_->resolver.clear();
    char* text = nullptr;
    DWORD size = 0;
    char path[260];
    const char* read = read_table_file(&text, &size, path, sizeof path);
    std::size_t fault = 0;
    bool parsed = false;
    const bool had_text = text != nullptr;
    if (text) {
        parsed = core::parse_body_table(text, size, &state_->table, &fault);
        delete[] text;
    }
    state_->index.build(state_->table);
    table_loaded_ = parsed;
    unsigned steering = 0, named = 0;
    for (unsigned i = 0; i < state_->table.count; ++i) {
        if (state_->table.bodies[i].lists & core::list_smalljet) ++steering;
        if (state_->table.bodies[i].id < 0) ++named;
    }
    // One row: loaded (count may be 0), or rejected / absent / disabled with count 0 (every record unknown_body).
    x3m::log("engine_effects_bodies event=%s reason=%s bodies=%u named=%u smalljet=%u refused=%u bytes=%lu fault=%lu frame=%llu path=\"%s\"",
             parsed ? "loaded" : had_text ? "rejected" : read, parsed ? "ok" : had_text ? "malformed" : read,
             state_->table.count, named, steering, state_->table.refused, static_cast<unsigned long>(size),
             static_cast<unsigned long>(fault), frame, path);
}
lfc::Reader reader() {
    return &x3m::engine_memory::read;
}
// lens_flare_cull.cpp refresh(), generalised: restart when the engine table moved, shrank or re-bound a mapped name;
// scan only the new dynamic slots (and the newest again) when it grew.
void refresh(unsigned long long frame) {
    if (!state_ || !state_->table.count) return;
    const lfc::Table t = lfc::read_table(reader(), body_global_);
    if (!t.valid) return;
    core::Resolver& r = state_->resolver;
    const bool restart = !engine_table_.valid || t.slots != engine_table_.slots || t.fixed != engine_table_.fixed ||
                         t.dynamic < engine_table_.dynamic ||
                         !core::mappings_hold(reader(), t, state_->table, &r, check_budget);
    if (restart) {
        if (engine_table_.valid) ++restarts_;
        r.clear();
        scanned_dynamic_ = 0;
        core::resolve(reader(), t, state_->table, state_->index, &r, 0, std::uint32_t(t.fixed) + std::uint32_t(t.dynamic));
    } else if (std::uint32_t(t.dynamic) > scanned_dynamic_ && r.resolved < state_->table.count) {
        core::resolve(reader(), t, state_->table, state_->index, &r, std::uint32_t(t.fixed) + scanned_dynamic_,
                      std::uint32_t(t.fixed) + std::uint32_t(t.dynamic));
    }
    scanned_dynamic_ = t.dynamic > 0 ? std::uint32_t(t.dynamic) - 1 : 0;
    engine_table_ = t;
    if (r.mapped != logged_mapped_ && body_rows_ < body_row_cap) {
        ++body_rows_;
        logged_mapped_ = r.mapped;
        x3m::log("engine_effects_resolve bodies=%u resolved=%lu mapped=%lu scanned=%lu dynamic_mappings=%u fixed=%ld dynamic=%ld restarts=%lu frame=%llu",
                 state_->table.count, static_cast<unsigned long>(r.resolved), static_cast<unsigned long>(r.mapped),
                 static_cast<unsigned long>(r.scanned), r.dynamic_count, static_cast<long>(t.fixed),
                 static_cast<long>(t.dynamic), static_cast<unsigned long>(restarts_), frame);
    }
}
}

namespace x3m::engine_effects {
void initialize() {
    const DWORD error = GetLastError();
    if (initialized_) {
        SetLastError(error);
        return;
    }
    initialized_ = true;
    // Unset or empty = native; 1..15 characters must be one of the three words; anything else is refused (native).
    wchar_t text[16]{};
    const DWORD length = x3m::config::get(L"X3M_ENGINE_EFFECTS", text, 16);
    char setting[16]{};
    bool printable = length < 16;
    for (DWORD i = 0; i < length && i < 15; ++i) {
        printable = printable && text[i] > 0x20 && text[i] < 0x7f;
        setting[i] = text[i] > 0x20 && text[i] < 0x7f ? char(text[i]) : '?';
    }
    core::Mode parsed = core::Mode::native;
    const bool valid = !length || (printable && core::parse_mode(setting, length, &parsed));
    mode_ = valid ? parsed : core::Mode::native;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    identity_ = fixture_identity_ || object_trace::executable_verified();
#else
    identity_ = object_trace::executable_verified();
#endif
    const bool debug = log_tier::cached_debug || log_tier::debug();
    if (!valid)
        status_ = length >= 16 ? "too_long" : "invalid_setting";
    else if (!core::suppresses(mode_))
        status_ = "native";
    else if (!identity_)
        status_ = "executable_mismatch"; // fail closed: every draw forwarded
    else
        status_ = "armed";
    // The per-draw hook: suppression armed, or the native census under --debug (counts only, no record).
    hook_ = identity_ && valid && (core::suppresses(mode_) || debug);
    log("engine_effects_mode setting=%s mode=%s status=%s identity=%u hook=%u suppress=%u census=%u",
        length ? setting : "-", core::mode_name(mode_), status_, unsigned(identity_), unsigned(hook_),
        unsigned(hook_ && core::suppresses(mode_)), unsigned(hook_ && debug));
    if (mode_ == core::Mode::plumes)
        log("engine_effects_plumes behaves=off phase=1a reason=no_stage_yet"); // the plume stage arrives in phase 2
    SetLastError(error);
}
core::Mode mode() {
    return mode_;
}
bool hook_wanted() {
    return hook_;
}
bool suppress() {
    return hook_ && core::suppresses(mode_);
}
const char* status() {
    return status_;
}
void begin_frame(unsigned long long frame) {
    if (!hook_) return;
    const DWORD error = GetLastError();
    if (!load_attempted_) load_table(frame);
    refresh(frame);
    SetLastError(error);
}
int body_for_model(std::uint32_t model) {
    return state_ ? state_->resolver.entry(model) : -1;
}
const core::Body* body(int index) {
    return state_ && index >= 0 && unsigned(index) < state_->table.count ? &state_->table.bodies[index] : nullptr;
}
Stats stats() {
    Stats s{};
    if (state_) {
        s.bodies = state_->table.count;
        s.refused = state_->table.refused;
        s.resolved = state_->resolver.resolved;
        s.mapped = state_->resolver.mapped;
        s.scanned = state_->resolver.scanned;
    }
    s.restarts = restarts_;
    s.table_loaded = table_loaded_;
    return s;
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
void fixture_identity(bool verified) {
    fixture_identity_ = verified;
}
void fixture_body_global(std::uintptr_t va) {
    body_global_ = va;
    engine_table_ = lfc::Table{};
}
#endif
}
