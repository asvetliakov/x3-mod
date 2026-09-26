#pragma once
#include <cstddef>
#include <cstdint>

// Central "is this program hash one the proxy keys on" answer, and the
// session population of created programs behind it
// (docs/architecture/mod-compatibility.md, "Making unknown programs visible").
//
// Every table that selects a variant, a route, an admission or a profile by
// the proxy's FNV-1a 64 fingerprint of the original bytecode feeds this file
// through a provider function defined in the table's own translation unit;
// nothing here duplicates a hash literal. There is no D3D, no Win32, no
// allocation and no per-draw work: the proxy classifies once per distinct
// program at CreateVertexShader/CreatePixelShader and only with telemetry on.
namespace x3m::renderer {

// One consulted table. Pair tables enumerate the vertex hash of a row at an
// even index and the pixel hash at the following odd index.
struct ShaderTable {
    const char* name;
    std::size_t count;
    std::uint64_t (*at)(std::size_t) noexcept;
};

// Providers, one per owning translation unit. Each returns its own static
// descriptor array; `count` receives the number of descriptors.
const ShaderTable* linear_material_shader_tables(std::size_t& count) noexcept;
const ShaderTable* linear_emission_shader_tables(std::size_t& count) noexcept;
const ShaderTable* linear_emission_sm1_shader_tables(std::size_t& count) noexcept;
const ShaderTable* rigid_position_shader_tables(std::size_t& count) noexcept;
const ShaderTable* material_radiance_shader_tables(std::size_t& count) noexcept;

// The complete consulted set, in a stable order.
std::size_t shader_table_count() noexcept;
const ShaderTable& shader_table(std::size_t index) noexcept;
// Total entries over every table; diagnostics and tests only.
std::size_t shader_table_entry_count() noexcept;
// A zero hash is never known (the proxy uses 0 for "no program").
bool shader_hash_known(std::uint64_t hash) noexcept;

// Session population of distinct created programs. Fixed storage, no
// allocation, no COM ownership. The proxy calls observe() once per distinct
// program hash and drains the pending unknowns at the next Present.
class ShaderPopulation {
public:
    static constexpr std::size_t capacity = 256;
    struct Entry {
        std::uint64_t hash = 0;
        std::uint32_t version = 0; // Bytecode header token, e.g. 0xffff0300 = ps_3_0.
        std::uint32_t bytes = 0;
        bool vertex = false;
    };
    // Classifies one distinct created program. Returns true when a new
    // unknown was recorded and is now pending a log line; false for a known
    // program, a repeat, or an unknown the fixed table could not hold (then
    // the overflow counter moves instead). Overflowed hashes are not
    // deduplicated: the counter counts refused calls, which equals distinct
    // programs because the caller classifies each hash exactly once.
    bool observe(std::uint64_t hash, bool vertex, std::uint32_t version, std::uint32_t bytes) noexcept;
    // Removes the oldest pending unknown into `out`; false when none is left.
    bool take(Entry& out) noexcept;
    std::uint32_t known() const noexcept { return known_; }
    std::uint32_t unknown() const noexcept { return unknown_; }
    std::uint32_t overflow() const noexcept { return overflow_; }
    // True exactly once after any counter moved, so the periodic line is
    // emitted only when the population changed.
    bool counts_changed() noexcept {
        const bool changed = changed_;
        changed_ = false;
        return changed;
    }

private:
    Entry entries_[capacity]{};
    std::size_t used_ = 0;    // Recorded unknown hashes; the dedupe table.
    std::size_t pending_ = 0; // Index of the next entry to drain; <= used_.
    std::uint32_t known_ = 0, unknown_ = 0, overflow_ = 0;
    bool changed_ = false;
};

} // namespace x3m::renderer
