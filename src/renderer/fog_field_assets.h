#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace x3m::renderer::fog_field {

// IDs are persistent packet identities; keep the original two unchanged.
enum class Profile : std::uint32_t {
    None = 0,
    Bluewell = 1,
    Foggreenoutlands = 2,
    Fogbluedistance = 3,
    Fogcyancorner = 4,
    Fogdeepred = 5,
    Foggreeneye = 6,
    Fogparanid = 7,
    Fogred = 8,
    Uranus = 9,
    Uranus3 = 10,
    Whitenexus = 11,
    Fogblue = 12,
    Fogkhaak = 13,
    Khaakhive = 14
};
struct FamilyProfile {
    const char* family;
    Profile profile;
};
// All asset-backed positive-card families in the stock AP census: 11 mapped
// plus 3 unused. Missing-asset families (xtmgreenring, earth) remain native.
// Neither card count nor fade chooses a density.
// clang-format off
inline constexpr FamilyProfile family_profiles[] = {
    {"bluewell", Profile::Bluewell}, {"foggreenoutlands", Profile::Foggreenoutlands},
    {"fogbluedistance", Profile::Fogbluedistance}, {"fogcyancorner", Profile::Fogcyancorner},
    {"fogdeepred", Profile::Fogdeepred}, {"foggreeneye", Profile::Foggreeneye},
    {"fogparanid", Profile::Fogparanid}, {"fogred", Profile::Fogred},
    {"uranus", Profile::Uranus}, {"uranus3", Profile::Uranus3}, {"whitenexus", Profile::Whitenexus},
    {"fogblue", Profile::Fogblue}, {"fogkhaak", Profile::Fogkhaak}, {"khaakhive", Profile::Khaakhive}
};
// clang-format on
inline constexpr std::uint32_t qualified_recipe_id = 1;

enum class Status : std::uint32_t {
    Ok = 0,
    InvalidArgument,
    InvalidProfile,
    ResourceNotFound,
    ResourceLoadFailed,
    ResourceLockFailed,
    BadMagic,
    WrongVersion,
    MetadataMismatch,
    Truncated,
    InvalidRun,
    TrailingBytes,
    InvalidHalf,
    ChecksumMismatch,
    AllocationFailed
};

struct ProfileInfo {
    Profile profile;
    std::uint32_t recipe_id, width, height, texel_bytes, decoded_bytes;
    float base_sigma;
    std::uint64_t decoded_fnv1a;
    std::uint16_t resource_id;
};

struct Result {
    Status status = Status::InvalidArgument;
    std::int32_t hresult = 0;
    std::uint32_t decoded_bytes = 0;
    explicit operator bool() const noexcept { return status == Status::Ok; }
};

const ProfileInfo* profile_info(Profile profile) noexcept;
Result decode_packet(const std::uint8_t* packet, std::size_t packet_size, const ProfileInfo& expected,
                     std::vector<std::uint16_t>& output) noexcept;
Result decode_from_resource(void* module, Profile profile, std::vector<std::uint16_t>& output) noexcept;
const char* status_name(Status status) noexcept;

// Data-driven families (docs/architecture/fog-family-data.md, "Implementation"): the generated
// <game>/x3m/fog-families.bin of tools/analysis/fog_families.py. A 64-byte header, one 112-byte
// row per family, one 80-byte row per distinct packet, then X3FOGPK v1 packets verbatim. Header
// or table failures reject the whole file; a row or packet failure disables the rows it touches.
// The 14 compiled names always win: a file row with one of their names is disabled.
inline constexpr std::uint32_t family_file_version = 1, family_file_header_bytes = 64;
inline constexpr std::uint32_t family_row_bytes = 112, family_packet_row_bytes = 80;
inline constexpr std::uint32_t family_file_max_rows = 256, family_dynamic_id_bit = 0x10000u;
inline constexpr std::uint32_t family_flag_background_palette = 1u, family_flag_override = 2u;
inline constexpr std::uint32_t family_table_max_bytes = family_file_max_rows *
                                                        (family_row_bytes + family_packet_row_bytes);

// Persistent file-family id: FNV-1a 32 of the name with bit 16 set (never 1..14, never 0).
constexpr std::uint32_t family_name_id(const char* name) noexcept {
    std::uint32_t hash = 2166136261u;
    for (; *name; ++name) hash = (hash ^ static_cast<unsigned char>(*name)) * 16777619u;
    return hash | family_dynamic_id_bit;
}
constexpr bool is_file_profile(Profile profile) noexcept {
    return (static_cast<std::uint32_t>(profile) & family_dynamic_id_bit) != 0;
}

enum class FamilyFileStatus : std::uint32_t { NotLoaded = 0, Absent, Disabled, Loaded, Rejected };

struct FamilyRow {
    char name[32]{};
    std::uint32_t profile = 0, packet = 0, flags = 0;
    float base_sigma = 0.f, occupancy = 0.f, chroma[3]{}, colours[4][3]{};
    // Reason the row is unusable; set at most once (load or a failed switch), never cleared.
    std::atomic<const char*> disabled{nullptr};
    // The proxy logs a row's disable (and a switch retry) at most once.
    mutable std::atomic<bool> reported{false}, retry_reported{false};
};
struct FamilyPacketRow {
    std::uint64_t offset = 0, size = 0, decoded_fnv1a = 0;
    std::uint32_t width = 0, height = 0, texel_bytes = 0, decoded_bytes = 0, profile = 0;
    // A switch that cannot allocate is retried once before the packet's rows are disabled.
    std::atomic<std::uint32_t> allocation_failures{0};
};
// Process lifetime once loaded: FogSectorFrame::reason points at a row name.
struct FamilyTable {
    FamilyFileStatus status = FamilyFileStatus::NotLoaded;
    const char* reason = "not_loaded";
    std::uint32_t families = 0, packets = 0, rows_disabled = 0;
    std::uint64_t file_bytes = 0;
    FamilyRow rows[family_file_max_rows];
    FamilyPacketRow packet_rows[family_file_max_rows];

    // Enabled row by exact name (the sampler's escaped family string); nullptr otherwise.
    const FamilyRow* find(const char* name) const noexcept {
        if (!name) return nullptr;
        for (std::uint32_t i = 0; i < families; ++i)
            if (!rows[i].disabled.load(std::memory_order_relaxed) && !std::strcmp(name, rows[i].name)) return &rows[i];
        return nullptr;
    }
    // Row by profile id: the enabled one if any, else the first disabled one (its constants stay
    // valid for a field decoded before it was disabled).
    const FamilyRow* row(Profile profile) const noexcept {
        const FamilyRow* disabled_row = nullptr;
        for (std::uint32_t i = 0; i < families; ++i) {
            if (rows[i].profile != static_cast<std::uint32_t>(profile)) continue;
            if (!rows[i].disabled.load(std::memory_order_relaxed)) return &rows[i];
            if (!disabled_row) disabled_row = &rows[i];
        }
        return disabled_row;
    }
};

#ifdef _WIN32
using family_path_char = wchar_t;
#else
using family_path_char = char;
#endif
// Loads `path` into `table` (which must be NotLoaded). Reads the header, the table and every
// packet header; never sizes an allocation from the file.
void load_family_file(const family_path_char* path, FamilyTable& table) noexcept;
// Reads and decodes one file family's packet (bounded by the validated row). Any failure
// disables every row that uses the packet (a first allocation failure is left for one retry);
// output is cleared.
Result decode_family_packet(FamilyTable& table, const family_path_char* path, Profile profile,
                            std::vector<std::uint16_t>& output) noexcept;
// ProfileInfo for an enabled file row, from its packet row (profile = the packet's own id).
bool family_profile_info(const FamilyTable& table, Profile profile, ProfileInfo& info) noexcept;

// Process-wide table: X3M_FOG_FAMILIES=PATH overrides <exe dir>\x3m\fog-families.bin, "0" or
// "none" disables. load_family_table() loads once and returns true only for the loading call.
bool load_family_table() noexcept;
const FamilyTable* family_table() noexcept; // nullptr until the load has completed
const char* family_table_path() noexcept;   // UTF-8, possibly truncated, for the log line
bool family_info(Profile profile, ProfileInfo& info) noexcept;
Result decode_family(Profile profile, std::vector<std::uint16_t>& output) noexcept;

} // namespace x3m::renderer::fog_field
