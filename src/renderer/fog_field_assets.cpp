#include "fog_field_assets.h"
#include "fog_field_assets_metadata_inc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../proxy/config.h"
#else
#include <sys/stat.h>
#endif

namespace x3m::renderer::fog_field {
namespace {
constexpr std::uint8_t magic[8] = {'X', '3', 'F', 'O', 'G', 'P', 'K'};
constexpr std::uint32_t version = 1, header_size = 56;
constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
constexpr std::uint64_t fnv_prime = 1099511628211ull;
constexpr std::int32_t hresult(std::uint32_t value) noexcept {
    return static_cast<std::int32_t>(value);
}

std::uint32_t u32(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::uint64_t u64(const std::uint8_t* p) noexcept {
    return std::uint64_t(u32(p)) | (std::uint64_t(u32(p + 4)) << 32);
}
std::int32_t status_hresult(Status status) noexcept {
    switch (status) {
    case Status::InvalidArgument:
    case Status::InvalidProfile: return hresult(0x80070057u);
    case Status::AllocationFailed: return hresult(0x8007000eu);
    case Status::Truncated: return hresult(0x80070026u);
    case Status::ChecksumMismatch: return hresult(0x80070017u);
    default: return hresult(0x8007000bu);
    }
}
Result fail(Status status, std::vector<std::uint16_t>& output, std::int32_t hr = 0) noexcept {
    output.clear();
    return {status, hr ? hr : status_hresult(status), 0};
}
std::uint64_t append_zeros(std::uint64_t hash, std::size_t count) noexcept {
    auto factor = fnv_prime;
    while (count) {
        if (count & 1u) hash *= factor;
        factor *= factor;
        count >>= 1;
    }
    return hash;
}
bool valid_half(std::uint16_t word) noexcept {
    return (word & 0x8000u) == 0 && (word & 0x7c00u) != 0x7c00u;
}
} // namespace

const ProfileInfo* profile_info(Profile profile) noexcept {
    for (const auto& info : kProfiles)
        if (info.profile == profile) return &info;
    return nullptr;
}

Result decode_packet(const std::uint8_t* packet, std::size_t size, const ProfileInfo& expected,
                     std::vector<std::uint16_t>& output) noexcept {
    output.clear();
    const auto expected_size = std::uint64_t(expected.width) * expected.height * expected.texel_bytes;
    if (!packet || size < header_size || expected.profile == Profile::None || expected.texel_bytes != 8 ||
        expected.decoded_bytes == 0 || expected.decoded_bytes % expected.texel_bytes != 0 ||
        expected_size != expected.decoded_bytes || expected.decoded_fnv1a == 0)
        return fail(Status::InvalidArgument, output);
    if (!std::equal(magic, magic + 8, packet)) return fail(Status::BadMagic, output);
    const auto packet_version = u32(packet + 8), packet_header = u32(packet + 12);
    if (packet_version != version) return fail(Status::WrongVersion, output);
    if (packet_header != header_size) return fail(Status::MetadataMismatch, output);
    const auto profile = u32(packet + 16), recipe = u32(packet + 20);
    const auto width = u32(packet + 24), height = u32(packet + 28), texel = u32(packet + 32);
    const auto decoded = u32(packet + 36), run_count = u32(packet + 40);
    const auto declared_checksum = u64(packet + 44);
    const auto reserved = u32(packet + 52);
    if (profile != static_cast<std::uint32_t>(expected.profile) || recipe != expected.recipe_id ||
        width != expected.width || height != expected.height || texel != expected.texel_bytes ||
        decoded != expected.decoded_bytes || declared_checksum != expected.decoded_fnv1a || reserved != 0)
        return fail(Status::MetadataMismatch, output);
    if (run_count == 0 || run_count > decoded / expected.texel_bytes) return fail(Status::InvalidRun, output);
    try {
        output.resize(expected.decoded_bytes / 2);
    } catch (...) {
        return fail(Status::AllocationFailed, output);
    }
    std::size_t cursor = header_size, texel_index = 0;
    std::uint64_t hash = fnv_offset;
    const std::size_t total_texels = expected.decoded_bytes / expected.texel_bytes;
    for (std::uint32_t run = 0; run < run_count; ++run) {
        if (size - cursor < 4) return fail(Status::Truncated, output);
        const auto encoded = u32(packet + cursor);
        cursor += 4;
        const bool literal = (encoded & 0x80000000u) != 0;
        const std::size_t count = encoded & 0x7fffffffu;
        if (count == 0 || count > total_texels - texel_index) return fail(Status::InvalidRun, output);
        if (literal) {
            if (count > (size - cursor) / expected.texel_bytes) return fail(Status::Truncated, output);
            for (std::size_t i = 0; i < count * 4; ++i) {
                const auto low = packet[cursor + 2 * i], high = packet[cursor + 2 * i + 1];
                const auto word = std::uint16_t(low) | (std::uint16_t(high) << 8);
                if (!valid_half(word)) return fail(Status::InvalidHalf, output);
                output[texel_index * 4 + i] = word;
                hash = (hash ^ low) * fnv_prime;
                hash = (hash ^ high) * fnv_prime;
            }
            cursor += count * expected.texel_bytes;
        } else {
            hash = append_zeros(hash, count * expected.texel_bytes);
        }
        texel_index += count;
    }
    if (texel_index != total_texels) return fail(Status::Truncated, output);
    if (cursor != size) return fail(Status::TrailingBytes, output);
    if (hash != expected.decoded_fnv1a) return fail(Status::ChecksumMismatch, output);
    return {Status::Ok, 0, expected.decoded_bytes};
}

Result decode_from_resource(void* module, Profile profile, std::vector<std::uint16_t>& output) noexcept {
    const auto* expected = profile_info(profile);
    if (!module) return fail(Status::InvalidArgument, output);
    if (!expected) return fail(Status::InvalidProfile, output);
#ifdef _WIN32
    const auto instance = static_cast<HMODULE>(module);
    const auto resource = FindResourceW(instance, MAKEINTRESOURCEW(expected->resource_id), MAKEINTRESOURCEW(10));
    if (!resource) return fail(Status::ResourceNotFound, output, HRESULT_FROM_WIN32(GetLastError()));
    const auto bytes = SizeofResource(instance, resource);
    if (!bytes) return fail(Status::ResourceLoadFailed, output, HRESULT_FROM_WIN32(GetLastError()));
    const auto loaded = LoadResource(instance, resource);
    if (!loaded) return fail(Status::ResourceLoadFailed, output, HRESULT_FROM_WIN32(GetLastError()));
    const auto* data = static_cast<const std::uint8_t*>(LockResource(loaded));
    if (!data) return fail(Status::ResourceLockFailed, output, HRESULT_FROM_WIN32(GetLastError()));
    return decode_packet(data, bytes, *expected, output);
#else
    return fail(Status::ResourceLoadFailed, output);
#endif
}

const char* status_name(Status status) noexcept {
    switch (status) {
    case Status::Ok: return "ok";
    case Status::InvalidArgument: return "invalid_argument";
    case Status::InvalidProfile: return "invalid_profile";
    case Status::ResourceNotFound: return "resource_not_found";
    case Status::ResourceLoadFailed: return "read_failed";
    case Status::ResourceLockFailed: return "resource_lock_failed";
    case Status::BadMagic: return "packet_bad_magic";
    case Status::WrongVersion: return "packet_version";
    case Status::MetadataMismatch: return "packet_metadata";
    case Status::Truncated: return "packet_truncated";
    case Status::InvalidRun: return "packet_run";
    case Status::TrailingBytes: return "packet_trailing_bytes";
    case Status::InvalidHalf: return "packet_half";
    case Status::ChecksumMismatch: return "packet_checksum";
    case Status::AllocationFailed: return "allocation";
    }
    return "unknown";
}

// ---- Data-driven families (fog-family-data.md, "Implementation") ----
namespace {
constexpr std::uint8_t family_magic[8] = {'X', '3', 'F', 'O', 'G', 'F', 'A', 'M'};
constexpr std::uint32_t atlas_width = 1560, atlas_height = 1430, atlas_texel = 8;
constexpr std::uint32_t atlas_decoded = atlas_width * atlas_height * atlas_texel;
constexpr std::uint64_t max_packet_bytes = header_size + std::uint64_t(atlas_decoded);
constexpr std::uint64_t max_file_bytes = family_file_header_bytes + std::uint64_t(family_table_max_bytes) +
                                         family_file_max_rows * max_packet_bytes;

std::uint64_t fnv64(const std::uint8_t* p, std::size_t n) noexcept {
    std::uint64_t hash = fnv_offset;
    for (std::size_t i = 0; i < n; ++i) hash = (hash ^ p[i]) * fnv_prime;
    return hash;
}
float f32(const std::uint8_t* p) noexcept {
    float v;
    const auto bits = u32(p);
    std::memcpy(&v, &bits, 4);
    return v;
}
bool unit(float v) noexcept {
    return std::isfinite(v) && v >= 0.f && v <= 1.f;
}

// One open file per load or packet read, closed by the destructor, so a regenerating tool
// never meets a held handle.
struct FamilyFile {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD error = 0;
    explicit FamilyFile(const wchar_t* path) noexcept {
        handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            error = GetLastError();
        else if (GetFileType(handle) != FILE_TYPE_DISK) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            error = ERROR_ACCESS_DENIED;
        }
    }
    ~FamilyFile() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
    FamilyFile(const FamilyFile&) = delete;
    FamilyFile& operator=(const FamilyFile&) = delete;
    bool open() const noexcept { return handle != INVALID_HANDLE_VALUE; }
    bool missing() const noexcept { return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }
    bool size(std::uint64_t& out) const noexcept {
        LARGE_INTEGER value{};
        if (!GetFileSizeEx(handle, &value) || value.QuadPart < 0) return false;
        out = std::uint64_t(value.QuadPart);
        return true;
    }
    bool read(std::uint64_t offset, void* out, std::size_t bytes) const noexcept {
        if (offset > std::uint64_t(std::numeric_limits<LONGLONG>::max())) return false;
        LARGE_INTEGER at{};
        at.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle, at, nullptr, FILE_BEGIN)) return false;
        auto* cursor = static_cast<std::uint8_t*>(out);
        while (bytes) {
            const DWORD chunk = bytes > (1u << 24) ? DWORD(1u << 24) : DWORD(bytes);
            DWORD done = 0;
            if (!ReadFile(handle, cursor, chunk, &done, nullptr) || done != chunk) return false;
            cursor += done;
            bytes -= done;
        }
        return true;
    }
#else
    // Host builds (tests only): stdio on regular files.
    std::FILE* handle = nullptr;
    bool absent = false;
    explicit FamilyFile(const char* path) noexcept {
        struct stat info{};
        if (::stat(path, &info) != 0) {
            absent = true;
            return;
        }
        if (S_ISREG(info.st_mode)) handle = std::fopen(path, "rb");
    }
    ~FamilyFile() {
        if (handle) std::fclose(handle);
    }
    FamilyFile(const FamilyFile&) = delete;
    FamilyFile& operator=(const FamilyFile&) = delete;
    bool open() const noexcept { return handle != nullptr; }
    bool missing() const noexcept { return absent; }
    bool size(std::uint64_t& out) const noexcept {
        if (fseeko(handle, 0, SEEK_END) != 0) return false;
        const auto end = ftello(handle);
        if (end < 0) return false;
        out = std::uint64_t(end);
        return true;
    }
    bool read(std::uint64_t offset, void* out, std::size_t bytes) const noexcept {
        if (offset > std::uint64_t(std::numeric_limits<off_t>::max()) || fseeko(handle, off_t(offset), SEEK_SET) != 0)
            return false;
        return std::fread(out, 1, bytes, handle) == bytes;
    }
#endif
};

void disable(FamilyTable& table, FamilyRow& row, const char* reason) noexcept {
    const char* expected = nullptr;
    if (row.disabled.compare_exchange_strong(expected, reason, std::memory_order_relaxed)) ++table.rows_disabled;
}
void disable_packet(FamilyTable& table, std::uint32_t packet, const char* reason) noexcept {
    for (std::uint32_t i = 0; i < table.families; ++i)
        if (table.rows[i].packet == packet) disable(table, table.rows[i], reason);
}
bool compiled_name(const char* name) noexcept {
    for (const auto& family : family_profiles)
        if (!std::strcmp(name, family.family)) return true;
    return false;
}
// 1..31 printable ASCII bytes, NUL-terminated, zero padding after the terminator (the sampler's
// 32-byte span and printable rule, fog-family-data.md §1). '"' and '\' are refused: the sampler
// escapes them in the name it compares, so such a row could never match.
bool valid_name(const std::uint8_t* p) noexcept {
    std::size_t length = 0;
    while (length < 32 && p[length]) {
        if (p[length] < 0x20 || p[length] > 0x7e || p[length] == '"' || p[length] == '\\') return false;
        ++length;
    }
    if (length == 0 || length > 31) return false;
    for (std::size_t i = length; i < 32; ++i)
        if (p[i]) return false;
    return true;
}
void reject(FamilyTable& table, const char* reason) noexcept {
    table.status = FamilyFileStatus::Rejected;
    table.reason = reason;
    table.families = table.packets = table.rows_disabled = 0;
}
// Header and table buffer for the one load a process makes (render thread): no allocation.
std::uint8_t table_buffer[family_file_header_bytes + family_table_max_bytes];
} // namespace

void load_family_file(const family_path_char* path, FamilyTable& table) noexcept {
    table.families = table.packets = table.rows_disabled = 0;
    table.file_bytes = 0;
    if (!path || !*path) {
        table.status = FamilyFileStatus::Absent;
        table.reason = "absent";
        return;
    }
    FamilyFile file(path);
    if (!file.open()) {
        if (file.missing()) {
            table.status = FamilyFileStatus::Absent;
            table.reason = "absent";
        } else
            reject(table, "open_failed");
        return;
    }
    std::uint64_t file_size = 0;
    if (!file.size(file_size)) return reject(table, "size_failed");
    table.file_bytes = file_size;
    if (file_size < family_file_header_bytes) return reject(table, "truncated_header");
    if (file_size > max_file_bytes) return reject(table, "oversized");
    auto* h = table_buffer;
    if (!file.read(0, h, family_file_header_bytes)) return reject(table, "read_failed");
    if (!std::equal(family_magic, family_magic + 8, h)) return reject(table, "bad_magic");
    if (u32(h + 8) != family_file_version) return reject(table, "version");
    if (u32(h + 12) != family_file_header_bytes) return reject(table, "header_size");
    if (u32(h + 16) != qualified_recipe_id) return reject(table, "recipe");
    const auto families = u32(h + 20), packets = u32(h + 24);
    // 0 families with 0 packets is the empty table the tool installs when the compiled profiles
    // cover every family: loaded, nothing added (fog-family-data.md, "Mod flow").
    if (families > family_file_max_rows) return reject(table, "family_count");
    if (families ? (packets == 0 || packets > families) : packets != 0) return reject(table, "packet_count");
    if (u32(h + 28) != family_row_bytes || u32(h + 32) != family_packet_row_bytes) return reject(table, "row_size");
    const auto table_offset = u32(h + 36), table_bytes = u32(h + 40);
    if (table_offset != family_file_header_bytes) return reject(table, "table_offset");
    if (table_bytes != families * family_row_bytes + packets * family_packet_row_bytes)
        return reject(table, "table_bytes");
    if (u32(h + 44) != 0) return reject(table, "reserved");
    if (u64(h + 56) != file_size) return reject(table, "file_size");
    const std::uint64_t table_end = std::uint64_t(table_offset) + table_bytes;
    if (table_end > file_size) return reject(table, "table_past_eof");
    auto* t = table_buffer + family_file_header_bytes;
    if (!file.read(table_offset, t, table_bytes)) return reject(table, "read_failed");
    if (fnv64(t, table_bytes) != u64(h + 48)) return reject(table, "table_checksum");

    table.families = families;
    table.packets = packets;
    const auto* packet_base = t + families * family_row_bytes;
    for (std::uint32_t j = 0; j < packets; ++j) {
        const auto* p = packet_base + j * family_packet_row_bytes;
        auto& packet = table.packet_rows[j];
        packet.offset = u64(p);
        packet.size = u64(p + 8);
        packet.width = u32(p + 16);
        packet.height = u32(p + 20);
        packet.texel_bytes = u32(p + 24);
        packet.decoded_bytes = u32(p + 28);
        packet.decoded_fnv1a = u64(p + 32);
        packet.profile = u32(p + 40);
    }
    for (std::uint32_t i = 0; i < families; ++i) {
        const auto* r = t + i * family_row_bytes;
        auto& row = table.rows[i];
        std::memcpy(row.name, r, 32);
        row.name[31] = 0; // never an unterminated array, even for a rejected name
        row.profile = u32(r + 32);
        row.packet = u32(r + 36);
        row.base_sigma = f32(r + 40);
        row.occupancy = f32(r + 44);
        for (unsigned c = 0; c < 3; ++c) row.chroma[c] = f32(r + 48 + 4 * c);
        for (unsigned k = 0; k < 12; ++k) row.colours[k / 3][k % 3] = f32(r + 60 + 4 * k);
        row.flags = u32(r + 108);
        const char* reason = nullptr;
        if (!valid_name(r)) {
            row.name[0] = 0;
            reason = "name";
        } else if (compiled_name(row.name))
            reason = "name_compiled";
        else if (row.profile != family_name_id(row.name))
            reason = "profile_id";
        else if (row.packet >= packets)
            reason = "packet_index";
        else if (!std::isfinite(row.base_sigma) || !(row.base_sigma > 0.f) || row.base_sigma > 1e-4f)
            reason = "sigma";
        else if (!std::isfinite(row.occupancy) || row.occupancy < .01f || row.occupancy > .5f)
            reason = "occupancy";
        else if (!unit(row.chroma[0]) || !unit(row.chroma[1]) || !unit(row.chroma[2]))
            reason = "chroma";
        else if (row.flags & ~(family_flag_background_palette | family_flag_override))
            reason = "flags";
        else
            for (unsigned k = 0; k < 12 && !reason; ++k)
                if (!unit(row.colours[k / 3][k % 3])) reason = "colour";
        // Against every earlier row, disabled or not: a name or id appears at most once, so find()
        // and row() can never resolve one family to two rows.
        for (std::uint32_t e = 0; !reason && e < i; ++e) {
            if (!std::strcmp(table.rows[e].name, row.name))
                reason = "name_duplicate";
            else if (table.rows[e].profile == row.profile)
                reason = "profile_id_duplicate";
        }
        if (reason) disable(table, row, reason);
    }
    for (std::uint32_t j = 0; j < packets; ++j) {
        const auto& packet = table.packet_rows[j];
        const auto* p = packet_base + j * family_packet_row_bytes;
        // The packet header carries the first family's id; that row may be disabled on its own.
        bool used = false, owner = false;
        for (std::uint32_t i = 0; i < families; ++i)
            if (table.rows[i].packet == j) {
                used = used || !table.rows[i].disabled.load(std::memory_order_relaxed);
                owner = owner || table.rows[i].profile == packet.profile;
            }
        if (!used) continue;
        const char* reason = nullptr;
        if (packet.width != atlas_width || packet.height != atlas_height || packet.texel_bytes != atlas_texel ||
            packet.decoded_bytes != atlas_decoded)
            reason = "packet_dimensions";
        else if (packet.decoded_fnv1a == 0)
            reason = "packet_checksum_zero";
        else if (!owner || !is_file_profile(static_cast<Profile>(packet.profile)) || u32(p + 44) != 0)
            reason = "packet_profile";
        else if (packet.size <= header_size || packet.size > max_packet_bytes)
            reason = "packet_size";
        else if (packet.offset < table_end || packet.offset > file_size || packet.size > file_size - packet.offset)
            reason = "packet_offset";
        else {
            // The packet's own header must agree with its row; the run data and the decoded
            // checksum are the decoder's, at the family switch.
            std::uint8_t head[header_size];
            if (!file.read(packet.offset, head, header_size))
                reason = "read_failed";
            else if (!std::equal(magic, magic + 8, head) || u32(head + 8) != version || u32(head + 12) != header_size ||
                     u32(head + 16) != packet.profile || u32(head + 20) != qualified_recipe_id ||
                     u32(head + 24) != packet.width || u32(head + 28) != packet.height ||
                     u32(head + 32) != packet.texel_bytes || u32(head + 36) != packet.decoded_bytes ||
                     u64(head + 44) != packet.decoded_fnv1a || u32(head + 52) != 0)
                reason = "packet_header";
        }
        if (reason) disable_packet(table, j, reason);
    }
    table.status = FamilyFileStatus::Loaded;
    table.reason = "ok";
}

bool family_profile_info(const FamilyTable& table, Profile profile, ProfileInfo& info) noexcept {
    const auto* row = table.row(profile);
    if (!row || row->disabled.load(std::memory_order_relaxed) || row->packet >= table.packets) return false;
    const auto& packet = table.packet_rows[row->packet];
    info = ProfileInfo{static_cast<Profile>(packet.profile),
                       qualified_recipe_id,
                       packet.width,
                       packet.height,
                       packet.texel_bytes,
                       packet.decoded_bytes,
                       row->base_sigma,
                       packet.decoded_fnv1a,
                       0};
    return true;
}

Result decode_family_packet(FamilyTable& table, const family_path_char* path, Profile profile,
                            std::vector<std::uint16_t>& output) noexcept {
    ProfileInfo info{};
    if (!path || !family_profile_info(table, profile, info)) return fail(Status::InvalidProfile, output);
    const auto index = table.row(profile)->packet;
    auto& packet = table.packet_rows[index];
    Result result{};
    {
        std::vector<std::uint8_t> bytes;
        bool allocated = true;
        try {
            bytes.resize(static_cast<std::size_t>(packet.size)); // validated <= 56 + decoded bytes at load
        } catch (...) {
            allocated = false;
        }
        if (!allocated)
            result = fail(Status::AllocationFailed, output);
        else {
            FamilyFile file(path);
            std::uint64_t size = 0;
            if (!file.open() || !file.size(size) || size != table.file_bytes ||
                !file.read(packet.offset, bytes.data(), bytes.size()))
                result = fail(Status::ResourceLoadFailed, output);
            else
                result = decode_packet(bytes.data(), bytes.size(), info, output);
        }
    } // the read buffer goes before the caller uploads the decoded field
    // One retry for an allocation failure (transient memory pressure); any other failure, or a
    // second allocation failure, disables the packet's rows.
    if (!result && (result.status != Status::AllocationFailed ||
                    packet.allocation_failures.fetch_add(1, std::memory_order_relaxed) > 0))
        disable_packet(table, index, status_name(result.status));
    return result;
}

namespace {
FamilyTable global_table;
std::atomic<int> global_state{0};        // 0 not loaded, 1 loading, 2 loaded
family_path_char* global_path = nullptr; // never freed: the table is process-lifetime
char global_path_utf8[260] = "";
} // namespace

bool load_family_table() noexcept {
    int expected = 0;
    if (!global_state.compare_exchange_strong(expected, 1, std::memory_order_acquire)) return false;
#ifdef _WIN32
    const DWORD saved_error = GetLastError();
    wchar_t value[8] = L"";
    const DWORD env = x3m::config::get(L"X3M_FOG_FAMILIES", value, 8);
    if (env && env < 8 && (!lstrcmpW(value, L"0") || !lstrcmpiW(value, L"none"))) {
        global_table.status = FamilyFileStatus::Disabled;
        global_table.reason = "env_disabled";
    } else {
        // The override verbatim, else <directory of the EXE>\x3m\fog-families.bin.
        DWORD capacity = MAX_PATH + 64, length = 0;
        wchar_t* buffer = nullptr;
        for (;;) {
            buffer = new (std::nothrow) wchar_t[capacity];
            if (!buffer) break;
            length = env ? x3m::config::get(L"X3M_FOG_FAMILIES", buffer, capacity)
                         : GetModuleFileNameW(nullptr, buffer, capacity);
            if (length && length + 32 < capacity) break;
            delete[] buffer;
            buffer = nullptr;
            if (!length || capacity >= 32768 + 64) break;
            capacity = std::min<DWORD>(capacity * 2, 32768 + 64);
        }
        if (buffer && !env) {
            DWORD cut = length;
            while (cut && buffer[cut - 1] != L'\\' && buffer[cut - 1] != L'/') --cut;
            static const wchar_t leaf[] = L"x3m\\fog-families.bin";
            for (DWORD i = 0; i < sizeof(leaf) / sizeof(leaf[0]); ++i) buffer[cut + i] = leaf[i];
        }
        if (!buffer)
            reject(global_table, "path");
        else {
            global_path = buffer;
            if (!WideCharToMultiByte(CP_UTF8, 0, buffer, -1, global_path_utf8, int(sizeof(global_path_utf8)), nullptr,
                                     nullptr))
                std::memcpy(global_path_utf8, "(long)", 7);
            load_family_file(global_path, global_table);
        }
    }
    SetLastError(saved_error);
#else
    const char* value = std::getenv("X3M_FOG_FAMILIES");
    if (value && (!std::strcmp(value, "0") || !std::strcmp(value, "none") || !std::strcmp(value, "NONE"))) {
        global_table.status = FamilyFileStatus::Disabled;
        global_table.reason = "env_disabled";
    } else if (value && *value) {
        const auto length = std::strlen(value);
        global_path = new (std::nothrow) char[length + 1];
        if (global_path) {
            std::memcpy(global_path, value, length + 1);
            std::snprintf(global_path_utf8, sizeof(global_path_utf8), "%s", value);
        }
        load_family_file(global_path, global_table);
    } else
        load_family_file(nullptr, global_table); // host builds have no module directory: absent
#endif
    global_state.store(2, std::memory_order_release);
    return true;
}

const FamilyTable* family_table() noexcept {
    return global_state.load(std::memory_order_acquire) == 2 ? &global_table : nullptr;
}
const char* family_table_path() noexcept {
    return global_path_utf8;
}

bool family_info(Profile profile, ProfileInfo& info) noexcept {
    return global_state.load(std::memory_order_acquire) == 2 && family_profile_info(global_table, profile, info);
}
Result decode_family(Profile profile, std::vector<std::uint16_t>& output) noexcept {
    if (global_state.load(std::memory_order_acquire) != 2) return fail(Status::InvalidProfile, output);
#ifdef _WIN32
    const DWORD saved_error = GetLastError();
    const auto result = decode_family_packet(global_table, global_path, profile, output);
    SetLastError(saved_error);
    return result;
#else
    return decode_family_packet(global_table, global_path, profile, output);
#endif
}

} // namespace x3m::renderer::fog_field
