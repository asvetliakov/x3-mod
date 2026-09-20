#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace x3m::renderer::fog_field {

enum class Profile : std::uint32_t { None = 0, Bluewell = 1, Foggreenoutlands = 2 };
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
