// The x3m.ini loader and the resolver behind config::get (docs/architecture/config-file.md).
// Documented Win32 only (GetModuleFileNameW, CreateFileW, GetFileSizeEx, ReadFile, MultiByteToWideChar,
// GetEnvironmentVariableW, QueryPerformanceCounter); the grammar is src/config/config_parse.h. load() runs once in
// initialize_log (load_backend's INIT_ONCE, before any device); everything it fills is static storage written before the
// resolver pointer is published and never written again, so config::get takes no lock and allocates nothing.
#include "config.h"
#include "config_load.h"
#include "capture.h"
#include "session_log.h"
#include "../config/config_parse.h"
#include <cstring>
#include <string>

namespace x3m::config {
namespace {
namespace schema = x3m::config::schema;
namespace parse = x3m::config::parse;

enum class Profile : unsigned char { file, none, bare };
constexpr DWORD kPathCapacity = 4096, kSettingCapacity = 1024;
constexpr unsigned kPoolCapacity = parse::file_limit + schema::entry_count; // every value plus its terminator fits

char text_[parse::file_limit + 1];                  // the file, parsed in place (keys, values and issues point into it)
parse::Result result_;
wchar_t pool_[kPoolCapacity];                       // the file's values as UTF-16
const wchar_t* file_value_[schema::entry_count];    // nullptr: the file does not set the entry
wchar_t path_[kPathCapacity];                       // the file chosen (empty: none)
Profile profile_ = Profile::file;
const char* source_ = "none";                       // game | override | none | bare | missing | refused
const char* reason_ = "";                           // why refused
DWORD bytes_ = 0, open_error_ = 0;
unsigned long long us_ = 0;
unsigned bad_utf8_ = 0;

// The environment state of one name: true when the process environment sets it (possibly to an empty value).
bool in_environment(const wchar_t* name) noexcept {
    SetLastError(ERROR_SUCCESS);
    return GetEnvironmentVariableW(name, nullptr, 0) != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
}
void wide_name(const char* env, wchar_t* out, unsigned capacity) noexcept {
    unsigned i = 0;
    for (; env[i] && i + 1 < capacity; ++i) out[i] = wchar_t(static_cast<unsigned char>(env[i]));
    out[i] = 0;
}
// The value an entry resolves to below the environment: the file's, else the profile's base (the default, or the off
// value under bare); a launcher default marker only while the key it marks comes from the defaults.
bool entry_in_environment(int i) noexcept {
    wchar_t name[96];
    wide_name(schema::entries[i].env, name, 96);
    return in_environment(name);
}
parse::Layer layer_of(int i) noexcept {
    return parse::below_environment(i, profile_ == Profile::bare, [](int k) { return file_value_[k] != nullptr; }, entry_in_environment);
}
void below_environment(int i, const wchar_t** wide, const char** narrow) noexcept {
    *wide = nullptr; *narrow = nullptr;
    const parse::Layer layer = layer_of(i);
    if (layer == parse::Layer::file) *wide = file_value_[i];
    else if (layer == parse::Layer::base) *narrow = parse::base_value(i, profile_ == Profile::bare);
}
template <class Char>
DWORD copy_out(const Char* value, wchar_t* out, DWORD capacity, DWORD saved) noexcept {
    DWORD length = 0;
    while (value[length]) ++length;
    if (!length) { if (out && capacity) out[0] = 0; SetLastError(ERROR_SUCCESS); return 0; } // present, empty
    if (!out || capacity <= length) { SetLastError(saved); return length + 1; }
    for (DWORD k = 0; k < length; ++k) out[k] = wchar_t(value[k]);
    out[length] = 0;
    SetLastError(saved);
    return length;
}
DWORD resolve(const wchar_t* name, wchar_t* out, DWORD capacity) noexcept {
    const DWORD saved = GetLastError();
    SetLastError(ERROR_SUCCESS);
    const DWORD n = GetEnvironmentVariableW(name, out, capacity);
    if (n) { SetLastError(saved); return n; }                // the environment wins
    if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) return 0;   // set to an empty value: still the environment's
    const int i = parse::find(name);
    if (i < 0) return 0;                                      // not a schema name: exactly GetEnvironmentVariableW
    const wchar_t* wide = nullptr;
    const char* narrow = nullptr;
    below_environment(i, &wide, &narrow);
    if (wide) return copy_out(wide, out, capacity, saved);
    if (narrow) return copy_out(narrow, out, capacity, saved);
    SetLastError(ERROR_ENVVAR_NOT_FOUND);
    return 0;
}
bool equals(const wchar_t* a, const wchar_t* b) noexcept { return std::wcscmp(a, b) == 0; }
bool absolute(const wchar_t* p) noexcept {
    return p[0] == L'\\' || p[0] == L'/' || (((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z')) && p[1] == L':');
}
// path_ = <module directory>\<name>; false when the module path is unknown or too long.
bool beside_module(HMODULE module, const wchar_t* name) noexcept {
    const DWORD length = GetModuleFileNameW(module, path_, kPathCapacity);
    if (!length || length >= kPathCapacity) { path_[0] = 0; return false; }
    DWORD cut = length;
    while (cut && path_[cut - 1] != L'\\' && path_[cut - 1] != L'/') --cut;
    if (!cut) { path_[0] = 0; return false; }
    const std::size_t name_length = std::wcslen(name);
    if (cut + name_length + 1 > kPathCapacity) { path_[0] = 0; return false; }
    std::memcpy(path_ + cut, name, (name_length + 1) * sizeof(wchar_t));
    return true;
}
void read_file(bool override_path) noexcept {
    const HANDLE file = CreateFileW(path_, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        open_error_ = GetLastError();
        if (open_error_ == ERROR_FILE_NOT_FOUND || open_error_ == ERROR_PATH_NOT_FOUND) source_ = "missing";
        else { source_ = "refused"; reason_ = "open"; }
        return;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) { source_ = "refused"; reason_ = "size_unknown"; CloseHandle(file); return; }
    if (size.QuadPart > LONGLONG(parse::file_limit)) {
        source_ = "refused"; reason_ = "size"; bytes_ = size.QuadPart > 0xffffffffll ? 0xffffffffu : DWORD(size.QuadPart);
        CloseHandle(file); return;
    }
    DWORD total = 0;
    while (total < DWORD(size.QuadPart)) {
        DWORD got = 0;
        if (!ReadFile(file, text_ + total, DWORD(size.QuadPart) - total, &got, nullptr)) { source_ = "refused"; reason_ = "read"; break; }
        if (!got) break;
        total += got;
    }
    CloseHandle(file);
    bytes_ = total;
    if (*reason_) return;
    source_ = override_path ? "override" : "game";
    parse::parse(text_, total, result_);
    // The values as UTF-16 (keys are ASCII; a value that is not valid UTF-8 is dropped as invalid).
    unsigned used = 0;
    for (unsigned i = 0; i < schema::entry_count; ++i) {
        const char* v = result_.value[i];
        if (!v) continue;
        const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, v, -1, pool_ + used, int(kPoolCapacity - used));
        if (n <= 0) {
            ++bad_utf8_; ++result_.invalid; --result_.keys;
            parse::note(result_, result_.line[i], parse::Problem::invalid, schema::entries[i].key, "", int(i));
            result_.value[i] = nullptr;
            continue;
        }
        file_value_[i] = pool_ + used;
        used += unsigned(n);
    }
}

// Row text: printable ASCII only (a blank or anything else becomes '_'), at most `limit` bytes.
std::string printable(const std::string& text, std::size_t limit = 96) {
    std::string out;
    for (char c : text) {
        if (out.size() >= limit) { out += "..."; break; }
        out += static_cast<unsigned char>(c) >= 0x21 && static_cast<unsigned char>(c) <= 0x7e ? c : '_';
    }
    return out.empty() ? std::string("_") : out;
}
std::string shown_value(int entry, const char* value) {
    if (entry >= 0 && schema::entries[entry].type == schema::Type::Path) return printable(session_log::redact_path(value));
    return printable(value);
}
}

void load(HMODULE module) noexcept {
    const DWORD saved = GetLastError();
    LARGE_INTEGER begin{}, end{}, frequency{};
    QueryPerformanceCounter(&begin);
    static wchar_t setting[kSettingCapacity];
    const DWORD n = GetEnvironmentVariableW(L"X3M_CONFIG", setting, kSettingCapacity);
    if (n >= kSettingCapacity) { source_ = "refused"; reason_ = "config_too_long"; }
    else if (n && equals(setting, L"none")) { profile_ = Profile::none; source_ = "none"; }
    else if (n && equals(setting, L"bare")) { profile_ = Profile::bare; source_ = "bare"; }
    else if (!n) {
        if (beside_module(module, L"x3m.ini")) read_file(false);
        else { source_ = "missing"; reason_ = "module_path"; }
    } else if (absolute(setting)) {
        std::memcpy(path_, setting, (n + 1) * sizeof(wchar_t));
        read_file(true);
    } else if (beside_module(module, setting)) read_file(true);
    else { source_ = "refused"; reason_ = "path_too_long"; }
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    us_ = frequency.QuadPart > 0 && end.QuadPart > begin.QuadPart
        ? static_cast<unsigned long long>((end.QuadPart - begin.QuadPart) * 1000000ll / frequency.QuadPart) : 0ull;
    resolver = &resolve; // published last: every table above is complete and never written again
    SetLastError(saved);
}

std::string effective_below_environment() {
    std::string line;
    for (unsigned i = 0; i < schema::entry_count; ++i) {
        if (entry_in_environment(int(i))) continue; // listed with the environment by proxy_options
        const parse::Layer layer = layer_of(int(i));
        if (layer == parse::Layer::none) continue;
        const char* value = layer == parse::Layer::file ? result_.value[i] : parse::base_value(int(i), profile_ == Profile::bare);
        line += ' '; line += schema::entries[i].env; line += '=';
        if (*value) line += shown_value(int(i), value);
        line += layer == parse::Layer::file ? "@file" : profile_ == Profile::bare ? "@bare" : "@default";
    }
    return line;
}

void log_rows() noexcept {
    const DWORD saved = GetLastError();
    try {
        const std::string file = path_[0] ? printable(session_log::redact_wide_path(path_), 400) : std::string("-");
        std::string extra;
        if (*reason_) extra += std::string(" reason=") + reason_;
        if (open_error_ && std::strcmp(source_, "missing")) extra += " error=" + std::to_string(open_error_);
        log("config_open file=%s source=%s keys=%u unknown=%u invalid=%u duplicate=%u env_only=%u renamed=%u bytes=%lu lines=%u us=%llu%s",
            file.c_str(), source_, result_.keys, result_.unknown, result_.invalid, result_.duplicate, result_.env_only, result_.renamed,
            static_cast<unsigned long>(bytes_), result_.lines, us_, extra.c_str());
        for (unsigned k = 0; k < result_.issue_count; ++k) {
            const parse::Issue& issue = result_.issues[k];
            log("config_key key=%s problem=%s line=%u value=%s", printable(issue.key, 64).c_str(), parse::problem_name(issue.problem), issue.line,
                shown_value(issue.entry, issue.value).c_str());
        }
        if (result_.more) log("config_more=%u", result_.more);
        if (!std::strcmp(source_, "game") || !std::strcmp(source_, "override")) {
            // The keys the file set, as the resolver hands them out; those the environment overrides are named after them.
            std::string line, overridden;
            for (unsigned i = 0; i < schema::entry_count; ++i) {
                if (!result_.value[i]) continue;
                wchar_t name[96];
                wide_name(schema::entries[i].env, name, 96);
                if (in_environment(name)) { overridden += overridden.empty() ? "" : ","; overridden += schema::entries[i].key; continue; }
                line += ' '; line += schema::entries[i].key; line += '='; line += shown_value(int(i), result_.value[i]);
            }
            log("config_file%s overridden_by_env=%s", line.c_str(), overridden.empty() ? "-" : overridden.c_str());
        }
    } catch (...) {
        log("config_open rows=unavailable");
    }
    SetLastError(saved);
}
}
