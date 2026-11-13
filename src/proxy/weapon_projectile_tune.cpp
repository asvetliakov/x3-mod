// The dev-only tuning file reader (weapon_projectile_tune.h). Compiled only with X3M_DEV_TUNING (CMakeLists.txt).
#include "weapon_projectile_tune.h"
#include "capture.h"
#include "weapon_projectile_tune_core.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace x3m::weapon_effects::projectile_tune {
namespace {
Poll poll_{};
wchar_t path_[MAX_PATH + 64]{};
bool path_known_ = false, path_failed_ = false;
char buffer_[file_capacity];
// The unknown key names already logged this session (one row each; past the capacity no further row).
constexpr unsigned logged_capacity = 32;
char logged_[logged_capacity][key_capacity]{};
unsigned logged_count_ = 0;
void log_unknown(const Result& r) {
    const unsigned named = r.unknown < max_unknown ? r.unknown : max_unknown;
    for (unsigned u = 0; u < named; ++u) {
        const char* name = r.unknown_name[u];
        bool seen = false;
        for (unsigned i = 0; i < logged_count_ && !seen; ++i) seen = !std::strcmp(logged_[i], name);
        if (seen || logged_count_ >= logged_capacity) continue;
        std::memcpy(logged_[logged_count_++], name, key_capacity);
        log("projectile_tune unknown key=%s (ignored)", name);
    }
}
bool resolve_path() {
    if (path_known_ || path_failed_) return path_known_;
    const DWORD length = GetModuleFileNameW(nullptr, path_, MAX_PATH);
    DWORD cut = length && length < MAX_PATH ? length : 0;
    while (cut && path_[cut - 1] != L'\\' && path_[cut - 1] != L'/') --cut;
    static const wchar_t leaf[] = L"x3m\\projectile_tune.json";
    if (!cut || cut + sizeof leaf / sizeof leaf[0] >= MAX_PATH + 64) {
        path_failed_ = true;
        return false;
    }
    std::memcpy(path_ + cut, leaf, sizeof leaf);
    path_known_ = true;
    return true;
}
void log_values(const char* source, const pf::Tune& t, unsigned seen) {
    char keys[256] = "", values[256] = "";
    unsigned kn = 0, vn = 0;
    pf::Tune copy = t;
    for (unsigned k = 0; k < pf::tune_keys; ++k) {
        const int a = std::snprintf(keys + kn, sizeof keys - kn, "%s%s%s", kn ? "," : "", pf::tune_key_names[k],
                                    (seen >> k) & 1u ? "" : "*");
        const int b = std::snprintf(values + vn, sizeof values - vn, "%s%.3g", vn ? "," : "", double(pf::tune_at(copy, k)));
        if (a > 0 && unsigned(a) < sizeof keys - kn) kn += unsigned(a);
        if (b > 0 && unsigned(b) < sizeof values - vn) vn += unsigned(b);
    }
    log("projectile_tune reloaded source=%s keys=%s values=%s", source, keys, values);
}
}
bool poll(pf::Tune* tune) noexcept {
    const DWORD tick = GetTickCount();
    if (!tune || !due(poll_, tick)) return false;
    const DWORD error = GetLastError();
    bool changed = false;
    if (resolve_path()) {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        const bool exists = GetFileAttributesExW(path_, GetFileExInfoStandard, &a) != FALSE &&
                            !(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        const std::uint64_t written =
            exists ? (std::uint64_t(a.ftLastWriteTime.dwHighDateTime) << 32 | a.ftLastWriteTime.dwLowDateTime) : 0;
        const Action act = decide(poll_, tick, exists, written);
        if (act == Action::absent) {
            *tune = pf::baked;
            log_values("defaults", *tune, 0);
            changed = true;
        } else if (act == Action::reload) {
            DWORD read = 0;
            const char* why = nullptr;
            if (a.nFileSizeHigh || a.nFileSizeLow >= file_capacity) why = "too_large";
            else {
                HANDLE file = CreateFileW(path_, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE) why = "open";
                else {
                    if (!ReadFile(file, buffer_, file_capacity - 1, &read, nullptr)) why = "read";
                    CloseHandle(file);
                }
            }
            pf::Tune next = pf::baked;
            const Result r = why ? Result{} : parse(buffer_, read, &next);
            if (!r.ok) {
                log("projectile_tune refused reason=%s bytes=%lu", why ? why : "malformed",
                    static_cast<unsigned long>(why ? a.nFileSizeLow : read));
            } else {
                for (unsigned k = 0; k < pf::tune_keys; ++k)
                    if ((r.clamped >> k) & 1u)
                        log("projectile_tune clamped key=%s value=%g applied=%g min=%g max=%g", pf::tune_key_names[k],
                            double(r.raw[k]), double(pf::tune_at(next, k)), double(pf::tune_min[k]),
                            double(pf::tune_max[k]));
                log_unknown(r);
                *tune = next;
                log_values("file", *tune, r.seen);
                changed = true;
            }
        }
    }
    SetLastError(error);
    return changed;
}
} // namespace x3m::weapon_effects::projectile_tune
