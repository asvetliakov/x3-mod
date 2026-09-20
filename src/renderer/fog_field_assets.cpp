#include "fog_field_assets.h"
#include "fog_field_assets_metadata_inc.h"

#include <algorithm>
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
#endif

namespace x3m::renderer::fog_field {
namespace {
constexpr std::uint8_t magic[8] = {'X','3','F','O','G','P','K'};
constexpr std::uint32_t version = 1, header_size = 56;
constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
constexpr std::uint64_t fnv_prime = 1099511628211ull;
constexpr std::int32_t hresult(std::uint32_t value) noexcept { return static_cast<std::int32_t>(value); }

std::uint32_t u32(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::uint64_t u64(const std::uint8_t* p) noexcept {
    return std::uint64_t(u32(p)) | (std::uint64_t(u32(p + 4)) << 32);
}
std::int32_t status_hresult(Status status) noexcept {
    switch (status) {
    case Status::InvalidArgument: case Status::InvalidProfile:
        return hresult(0x80070057u);
    case Status::AllocationFailed:
        return hresult(0x8007000eu);
    case Status::Truncated:
        return hresult(0x80070026u);
    case Status::ChecksumMismatch:
        return hresult(0x80070017u);
    default:
        return hresult(0x8007000bu);
    }
}
Result fail(Status status, std::vector<std::uint16_t>& output, std::int32_t hr = 0) noexcept {
    output.clear(); return {status, hr ? hr : status_hresult(status), 0};
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
    for (const auto& info : kProfiles) if (info.profile == profile) return &info;
    return nullptr;
}

Result decode_packet(const std::uint8_t* packet, std::size_t size,
                     const ProfileInfo& expected, std::vector<std::uint16_t>& output) noexcept {
    output.clear();
    const auto expected_size = std::uint64_t(expected.width) * expected.height * expected.texel_bytes;
    if (!packet || size < header_size || expected.profile == Profile::None ||
        expected.texel_bytes != 8 || expected.decoded_bytes == 0 ||
        expected.decoded_bytes % expected.texel_bytes != 0 ||
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
    if (run_count == 0 || run_count > decoded / expected.texel_bytes)
        return fail(Status::InvalidRun, output);
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
        const auto encoded = u32(packet + cursor); cursor += 4;
        const bool literal = (encoded & 0x80000000u) != 0;
        const std::size_t count = encoded & 0x7fffffffu;
        if (count == 0 || count > total_texels - texel_index) return fail(Status::InvalidRun, output);
        if (literal) {
            if (count > (size - cursor) / expected.texel_bytes) return fail(Status::Truncated, output);
            for (std::size_t i = 0; i < count * 4; ++i) {
                const auto low = packet[cursor + 2*i], high = packet[cursor + 2*i + 1];
                const auto word = std::uint16_t(low) | (std::uint16_t(high) << 8);
                if (!valid_half(word)) return fail(Status::InvalidHalf, output);
                output[texel_index*4 + i] = word;
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

Result decode_from_resource(void* module, Profile profile,
                            std::vector<std::uint16_t>& output) noexcept {
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

} // namespace x3m::renderer::fog_field
