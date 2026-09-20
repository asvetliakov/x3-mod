#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace x3m::renderer::fog_field {

// IDs are persistent packet identities; keep the original two unchanged.
enum class Profile : std::uint32_t {
    None = 0, Bluewell = 1, Foggreenoutlands = 2,
    Fogbluedistance = 3, Fogcyancorner = 4, Fogdeepred = 5, Foggreeneye = 6,
    Fogparanid = 7, Fogred = 8, Uranus = 9, Uranus3 = 10, Whitenexus = 11,
    Fogblue = 12, Fogkhaak = 13, Khaakhive = 14
};
struct FamilyProfile { const char* family; Profile profile; };
// All asset-backed positive-card families in the stock AP census: 11 mapped
// plus 3 unused. Missing-asset families (xtmgreenring, earth) remain native.
// Neither card count nor fade chooses a density.
inline constexpr FamilyProfile family_profiles[] = {
    {"bluewell", Profile::Bluewell}, {"foggreenoutlands", Profile::Foggreenoutlands},
    {"fogbluedistance", Profile::Fogbluedistance}, {"fogcyancorner", Profile::Fogcyancorner},
    {"fogdeepred", Profile::Fogdeepred}, {"foggreeneye", Profile::Foggreeneye},
    {"fogparanid", Profile::Fogparanid}, {"fogred", Profile::Fogred},
    {"uranus", Profile::Uranus}, {"uranus3", Profile::Uranus3}, {"whitenexus", Profile::Whitenexus},
    {"fogblue", Profile::Fogblue}, {"fogkhaak", Profile::Fogkhaak}, {"khaakhive", Profile::Khaakhive}
};
inline constexpr std::uint32_t qualified_recipe_id = 1;

enum class Status : std::uint32_t {
    Ok = 0, InvalidArgument, InvalidProfile, ResourceNotFound, ResourceLoadFailed,
    ResourceLockFailed, BadMagic, WrongVersion, MetadataMismatch, Truncated,
    InvalidRun, TrailingBytes, InvalidHalf, ChecksumMismatch, AllocationFailed
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
Result decode_packet(const std::uint8_t* packet, std::size_t packet_size,
                     const ProfileInfo& expected, std::vector<std::uint16_t>& output) noexcept;
Result decode_from_resource(void* module, Profile profile,
                            std::vector<std::uint16_t>& output) noexcept;

} // namespace x3m::renderer::fog_field
