// Data-driven fog families: loader and switch-time decode of <game>/x3m/fog-families.bin
// (docs/architecture/fog-family-data.md, "Implementation"). Portable: the host build
// (verification/analysis/test_fog_family_file.py) uses stdio, the i686 build (CMake target
// fog_family_file_fixture) the Win32 file APIs the DLL uses.
//
//   fog_family_file_fixture --self-test DIR   header/row/packet/decode cases on files it writes in DIR
//   fog_family_file_fixture --probe           loads X3M_FOG_FAMILIES through the process-wide loader,
//                                             prints the table and decodes every enabled row
#include "fog_field_assets.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace x3m::renderer::fog_field;

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete" // the replaced global pair is malloc/free
#endif
namespace { bool fail_large_allocations = false; } // switch-time allocation failure witness
void* operator new(std::size_t size) {
    if (fail_large_allocations && size > (1u << 20)) throw std::bad_alloc();
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace {
constexpr std::uint32_t width = 1560, height = 1430, texel = 8, decoded = width * height * texel;
constexpr std::uint64_t fnv_offset = 14695981039346656037ull, fnv_prime = 1099511628211ull;

void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) { for (unsigned i = 0; i < 4; ++i) b[at + i] = std::uint8_t(v >> (8 * i)); }
void put64(std::vector<std::uint8_t>& b, std::size_t at, std::uint64_t v) { for (unsigned i = 0; i < 8; ++i) b[at + i] = std::uint8_t(v >> (8 * i)); }
void putf(std::vector<std::uint8_t>& b, std::size_t at, float v) { std::uint32_t u; std::memcpy(&u, &v, 4); put32(b, at, u); }
std::uint32_t get32(const std::vector<std::uint8_t>& b, std::size_t at) { return b[at] | b[at + 1] << 8 | b[at + 2] << 16 | std::uint32_t(b[at + 3]) << 24; }
std::uint64_t get64(const std::vector<std::uint8_t>& b, std::size_t at) { return get32(b, at) | std::uint64_t(get32(b, at + 4)) << 32; }
std::uint64_t fnv(const std::uint8_t* p, std::size_t n, std::uint64_t h = fnv_offset) { for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * fnv_prime; return h; }
std::uint64_t zeros(std::uint64_t h, std::uint64_t count) { auto f = fnv_prime; while (count) { if (count & 1) h *= f; f *= f; count >>= 1; } return h; }

// A valid X3FOGPK v1 packet: `literal` nonzero texels, then one zero run for the rest.
struct Packet { std::vector<std::uint8_t> bytes; std::uint64_t checksum; std::uint32_t profile; };
Packet make_packet(std::uint32_t profile, std::uint16_t seed, std::uint32_t literal = 4) {
    Packet p; p.profile = profile;
    std::vector<std::uint8_t> payload;
    auto word = [&](std::uint32_t v) { for (unsigned i = 0; i < 4; ++i) payload.push_back(std::uint8_t(v >> (8 * i))); };
    word(0x80000000u | literal);
    for (std::uint32_t t = 0; t < literal * 4; ++t) { const std::uint16_t half = std::uint16_t(0x3000 + ((seed + t) & 0x3ff)); payload.push_back(std::uint8_t(half)); payload.push_back(std::uint8_t(half >> 8)); }
    word(width * height - literal);
    p.checksum = zeros(fnv(payload.data() + 4, literal * texel), std::uint64_t(width * height - literal) * texel);
    p.bytes.assign(56, 0);
    std::memcpy(p.bytes.data(), "X3FOGPK", 8);
    put32(p.bytes, 8, 1); put32(p.bytes, 12, 56); put32(p.bytes, 16, profile); put32(p.bytes, 20, qualified_recipe_id);
    put32(p.bytes, 24, width); put32(p.bytes, 28, height); put32(p.bytes, 32, texel); put32(p.bytes, 36, decoded);
    put32(p.bytes, 40, 2); put64(p.bytes, 44, p.checksum); put32(p.bytes, 52, 0);
    p.bytes.insert(p.bytes.end(), payload.begin(), payload.end());
    return p;
}
struct Family { std::string name; std::uint32_t packet; float sigma = 2.5e-6f, occupancy = .12f; std::uint32_t flags = 0; };

std::vector<std::uint8_t> build(const std::vector<Family>& families, const std::vector<Packet>& packets) {
    const std::uint32_t fc = std::uint32_t(families.size()), pc = std::uint32_t(packets.size());
    const std::uint32_t table = fc * family_row_bytes + pc * family_packet_row_bytes;
    std::vector<std::uint8_t> b(64 + table, 0);
    std::uint64_t offset = b.size();
    for (std::uint32_t i = 0; i < fc; ++i) {
        const std::size_t r = 64 + i * family_row_bytes;
        std::memcpy(&b[r], families[i].name.data(), std::min<std::size_t>(families[i].name.size(), 32));
        put32(b, r + 32, family_name_id(families[i].name.c_str())); put32(b, r + 36, families[i].packet);
        putf(b, r + 40, families[i].sigma); putf(b, r + 44, families[i].occupancy);
        for (unsigned c = 0; c < 3; ++c) putf(b, r + 48 + 4 * c, .25f * float(c + 1));
        for (unsigned k = 0; k < 12; ++k) putf(b, r + 60 + 4 * k, k % 3 == 2 ? 1.f : .5f);
        put32(b, r + 108, families[i].flags);
    }
    for (std::uint32_t j = 0; j < pc; ++j) {
        const std::size_t r = 64 + fc * family_row_bytes + j * family_packet_row_bytes;
        put64(b, r, offset); put64(b, r + 8, packets[j].bytes.size());
        put32(b, r + 16, width); put32(b, r + 20, height); put32(b, r + 24, texel); put32(b, r + 28, decoded);
        put64(b, r + 32, packets[j].checksum); put32(b, r + 40, packets[j].profile);
        offset += packets[j].bytes.size();
    }
    for (const auto& p : packets) b.insert(b.end(), p.bytes.begin(), p.bytes.end());
    std::memcpy(b.data(), "X3FOGFAM", 8);
    put32(b, 8, family_file_version); put32(b, 12, 64); put32(b, 16, qualified_recipe_id);
    put32(b, 20, fc); put32(b, 24, pc); put32(b, 28, family_row_bytes); put32(b, 32, family_packet_row_bytes);
    put32(b, 36, 64); put32(b, 40, table); put32(b, 44, 0);
    put64(b, 48, fnv(&b[64], table)); put64(b, 56, b.size());
    return b;
}
void refresh(std::vector<std::uint8_t>& b) { // recompute the table checksum and size after an edit
    put64(b, 48, fnv(&b[64], get32(b, 40))); put64(b, 56, b.size());
}

std::string dir;
std::string write(const std::string& leaf, const std::vector<std::uint8_t>& bytes) {
    const std::string path = dir + "/" + leaf;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    require(bool(out), "write " + path);
    return path;
}
#ifdef _WIN32
std::wstring native(const std::string& path) {
    std::wstring out(path.size() + 1, L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &out[0], int(out.size()));
    require(n > 0, "path conversion");
    out.resize(std::size_t(n - 1));
    return out;
}
#else
std::string native(const std::string& path) { return path; }
#endif

std::unique_ptr<FamilyTable> load(const std::string& path) {
    std::unique_ptr<FamilyTable> table(new FamilyTable);
    load_family_file(native(path).c_str(), *table);
    return table;
}
const char* status(FamilyFileStatus s) {
    switch (s) { case FamilyFileStatus::Absent: return "absent"; case FamilyFileStatus::Disabled: return "disabled";
    case FamilyFileStatus::Loaded: return "loaded"; case FamilyFileStatus::Rejected: return "rejected"; default: return "not_loaded"; }
}
unsigned cases = 0;
void compiled_intact(const FamilyTable& t) {
    // The compiled 14 are untouched by any file outcome: a compiled name never matches a file row.
    require(std::size(family_profiles) == 14, "compiled table");
    for (const auto& f : family_profiles) require(!t.find(f.family), std::string("compiled name matched a file row: ") + f.family);
}
void expect_rejected(const std::string& name, const std::vector<std::uint8_t>& bytes, const char* reason) {
    const auto t = load(write(name + ".bin", bytes));
    require(t->status == FamilyFileStatus::Rejected && !std::strcmp(t->reason, reason) && t->families == 0 && !t->find("zzsynth"),
            name + ": expected rejected/" + reason + ", got " + status(t->status) + "/" + t->reason);
    compiled_intact(*t); ++cases;
    std::printf("CASE %s status=rejected reason=%s\n", name.c_str(), t->reason);
}
// Loaded, the edited row disabled with `reason`, the other row still found and decodable.
void expect_row(const std::string& name, std::vector<std::uint8_t> bytes, std::uint32_t row, const char* reason, bool fresh = true) {
    if (fresh) refresh(bytes);
    const auto path = write(name + ".bin", bytes);
    const auto t = load(path);
    require(t->status == FamilyFileStatus::Loaded, name + ": not loaded (" + t->reason + ")");
    const char* why = t->rows[row].disabled.load();
    require(why && !std::strcmp(why, reason), name + ": expected row reason " + reason + ", got " + (why ? why : "none"));
    const std::uint32_t other = row ? 0 : 1;
    require(!t->rows[other].disabled.load() && t->find(t->rows[other].name), name + ": other row disabled");
    std::vector<std::uint16_t> out;
    require(bool(decode_family_packet(*t, native(path).c_str(), Profile(t->rows[other].profile), out)) && out.size() * 2 == decoded, name + ": other row decode");
    ProfileInfo info{};
    if (t->rows[row].profile != t->rows[other].profile) // a duplicate name shares the surviving row's id
        require(!family_profile_info(*t, Profile(t->rows[row].profile), info), name + ": disabled row still has info");
    compiled_intact(*t); ++cases;
    std::printf("CASE %s status=loaded row=%u reason=%s\n", name.c_str(), row, why);
}
// Loaded and valid at load time; the switch-time decode of row 0 fails with `expected`, which
// disables row 0 (and only rows sharing its packet); nothing throws, the other row decodes.
void expect_decode_failure(const std::string& name, std::vector<std::uint8_t> bytes, Status expected,
                           const std::function<void(std::vector<std::uint8_t>&)>& swap = nullptr) {
    const auto path = write(name + ".bin", bytes);
    const auto t = load(path);
    require(t->status == FamilyFileStatus::Loaded && t->rows_disabled == 0, name + ": load");
    if (swap) { swap(bytes); write(name + ".bin", bytes); }
    std::vector<std::uint16_t> out(3, 7);
    const auto result = decode_family_packet(*t, native(path).c_str(), Profile(t->rows[0].profile), out);
    require(result.status == expected && out.empty(), name + ": expected decode status " + status_name(expected) + ", got " + status_name(result.status));
    require(t->rows[0].disabled.load() && !std::strcmp(t->rows[0].disabled.load(), status_name(expected)) && !t->find(t->rows[0].name), name + ": row not disabled");
    require(!t->rows[1].disabled.load() && t->find(t->rows[1].name), name + ": other row disabled");
    compiled_intact(*t); ++cases;
    std::printf("CASE %s status=decode_failed reason=%s\n", name.c_str(), status_name(result.status));
}

int self_test() {
    const std::string a = "zzsynth", b = "zzother";
    const auto pa = make_packet(family_name_id(a.c_str()), 11), pb = make_packet(family_name_id(b.c_str()), 97, 9);
    const auto valid = build({{a, 0}, {b, 1}}, {pa, pb});
    const std::size_t row0 = 64, row1 = 64 + family_row_bytes, prow0 = 64 + 2 * family_row_bytes;
    const std::size_t packet0 = std::size_t(get64(valid, prow0));
    {
        const auto path = write("valid.bin", valid);
        const auto t = load(path);
        require(t->status == FamilyFileStatus::Loaded && !std::strcmp(t->reason, "ok") && t->families == 2 && t->packets == 2 && t->rows_disabled == 0, "valid load");
        for (unsigned i = 0; i < 2; ++i) {
            const auto* row = t->find(i ? b.c_str() : a.c_str());
            require(row && row->profile == family_name_id(row->name) && row->profile >= family_dynamic_id_bit, "valid row");
            ProfileInfo info{};
            require(family_profile_info(*t, Profile(row->profile), info) && info.base_sigma == 2.5e-6f && info.decoded_bytes == decoded && info.resource_id == 0, "valid info");
            std::vector<std::uint16_t> out;
            const auto r = decode_family_packet(*t, native(path).c_str(), Profile(row->profile), out);
            std::uint64_t h = fnv_offset;
            for (auto w : out) { h = (h ^ std::uint8_t(w)) * fnv_prime; h = (h ^ std::uint8_t(w >> 8)) * fnv_prime; }
            require(r && r.decoded_bytes == decoded && out.size() * 2 == decoded && h == info.decoded_fnv1a && !row->disabled.load(), "valid decode");
        }
        require(!t->find("bluewell") && !t->find("zz") && !t->find(""), "unknown names");
        compiled_intact(*t); ++cases;
        std::printf("CASE valid status=loaded families=2 packets=2\n");
    }
    {   // Two rows sharing one packet (deduplicated by decoded hash): the packet carries the first row's id.
        const auto shared = build({{a, 0}, {b, 0}}, {pa});
        const auto path = write("shared.bin", shared);
        const auto t = load(path);
        require(t->status == FamilyFileStatus::Loaded && t->rows_disabled == 0 && t->packets == 1, "shared load");
        for (unsigned i = 0; i < 2; ++i) {
            std::vector<std::uint16_t> out;
            require(bool(decode_family_packet(*t, native(path).c_str(), Profile(t->rows[i].profile), out)), "shared decode");
        }
        ++cases; std::printf("CASE shared_packet status=loaded\n");
    }
    {
        const auto t = load(dir + "/does-not-exist.bin");
        require(t->status == FamilyFileStatus::Absent && !std::strcmp(t->reason, "absent"), "absent");
        compiled_intact(*t); ++cases; std::printf("CASE absent status=absent\n");
        const auto d = load(dir);
        require(d->status == FamilyFileStatus::Rejected && !std::strcmp(d->reason, "open_failed"), std::string("directory: ") + d->reason);
        ++cases; std::printf("CASE directory status=rejected reason=%s\n", d->reason);
    }
    auto edit = [&](const std::function<void(std::vector<std::uint8_t>&)>& f) { auto v = valid; f(v); return v; };
    expect_rejected("zero_length", {}, "truncated_header");
    expect_rejected("truncated_header", std::vector<std::uint8_t>(valid.begin(), valid.begin() + 40), "truncated_header");
    expect_rejected("bad_magic", edit([](auto& v) { v[7] = 'X'; }), "bad_magic");
    expect_rejected("version_2", edit([](auto& v) { put32(v, 8, 2); }), "version");
    expect_rejected("header_size_0", edit([](auto& v) { put32(v, 12, 0); }), "header_size");
    expect_rejected("recipe_2", edit([](auto& v) { put32(v, 16, 2); }), "recipe");
    expect_rejected("families_0", edit([](auto& v) { put32(v, 20, 0); }), "family_count");
    expect_rejected("families_257", edit([](auto& v) { put32(v, 20, 257); }), "family_count");
    expect_rejected("packets_0", edit([](auto& v) { put32(v, 24, 0); }), "packet_count");
    expect_rejected("packets_3", edit([](auto& v) { put32(v, 24, 3); }), "packet_count");
    expect_rejected("row_size", edit([](auto& v) { put32(v, 28, 104); }), "row_size");
    expect_rejected("table_offset", edit([](auto& v) { put32(v, 36, 72); }), "table_offset");
    expect_rejected("table_bytes", edit([](auto& v) { put32(v, 40, get32(v, 40) + 8); }), "table_bytes");
    expect_rejected("reserved", edit([](auto& v) { put32(v, 44, 1); }), "reserved");
    expect_rejected("trailing_byte", edit([](auto& v) { v.push_back(0); }), "file_size");
    expect_rejected("file_size_field", edit([](auto& v) { put64(v, 56, v.size() + 1); }), "file_size");
    expect_rejected("table_past_eof", edit([](auto& v) { v.resize(100); put64(v, 56, v.size()); }), "table_past_eof");
    expect_rejected("table_checksum", edit([&](auto& v) { v[row0 + 40] ^= 1; }), "table_checksum");

    {   // A shared packet stays usable for the second row when its owner (first) row is disabled.
        auto shared = build({{a, 0}, {b, 0}}, {pa});
        putf(shared, row0 + 40, 0.f);
        expect_row("shared_owner_disabled", shared, 0, "sigma");
    }
    {   // A disabled first row still owns its name and id: the later duplicate is disabled too, so
        // find() and row() can never resolve one family to two different rows.
        auto dup = build({{a, 0}, {a, 1}}, {pa, make_packet(family_name_id(a.c_str()), 5)});
        putf(dup, row0 + 40, 0.f); refresh(dup);
        const auto t = load(write("duplicate_after_disabled.bin", dup));
        ProfileInfo info{};
        require(t->status == FamilyFileStatus::Loaded && !std::strcmp(t->rows[0].disabled.load(), "sigma") &&
                !std::strcmp(t->rows[1].disabled.load(), "name_duplicate") && !t->find(a.c_str()) &&
                t->row(Profile(family_name_id(a.c_str()))) == &t->rows[0] &&
                !family_profile_info(*t, Profile(family_name_id(a.c_str())), info), "duplicate after a disabled row");
        compiled_intact(*t); ++cases;
        std::printf("CASE duplicate_after_disabled status=loaded rows_disabled=%u\n", t->rows_disabled);
    }
    {   // A packet header may never carry a compiled id, even when a (disabled) row names it.
        auto compiled = build({{a, 0}, {b, 0}}, {make_packet(3, 11)});
        put32(compiled, row0 + 32, 3);
        refresh(compiled);
        const auto t = load(write("packet_compiled_id.bin", compiled));
        require(t->status == FamilyFileStatus::Loaded && !std::strcmp(t->rows[0].disabled.load(), "profile_id") &&
                !std::strcmp(t->rows[1].disabled.load(), "packet_profile") && !t->find(b.c_str()), "packet with a compiled id");
        compiled_intact(*t); ++cases;
        std::printf("CASE packet_compiled_id status=loaded row=1 reason=packet_profile\n");
    }
    expect_row("name_duplicate", build({{a, 0}, {a, 1}}, {pa, make_packet(family_name_id(a.c_str()), 5)}), 1, "name_duplicate");
    expect_row("name_compiled", edit([&](auto& v) { std::memset(&v[row0], 0, 32); std::memcpy(&v[row0], "bluewell", 8); put32(v, row0 + 32, family_name_id("bluewell")); }), 0, "name_compiled");
    expect_row("name_32_bytes", edit([&](auto& v) { std::memset(&v[row0], 'z', 32); }), 0, "name");
    expect_row("name_control", edit([&](auto& v) { v[row0 + 2] = 0x07; }), 0, "name");
    expect_row("name_quote", edit([&](auto& v) { v[row0 + 2] = '"'; }), 0, "name");
    expect_row("name_backslash", edit([&](auto& v) { v[row0 + 2] = '\\'; }), 0, "name");
    expect_row("name_empty", edit([&](auto& v) { std::memset(&v[row0], 0, 32); }), 0, "name");
    expect_row("name_padding", edit([&](auto& v) { v[row0 + 20] = 'q'; }), 0, "name");
    expect_row("id_3", edit([&](auto& v) { put32(v, row0 + 32, 3); }), 0, "profile_id");
    expect_row("id_other_name", edit([&](auto& v) { put32(v, row1 + 32, family_name_id(a.c_str())); }), 1, "profile_id");
    expect_row("packet_index", edit([&](auto& v) { put32(v, row0 + 36, 2); }), 0, "packet_index");
    expect_row("sigma_0", edit([&](auto& v) { putf(v, row0 + 40, 0.f); }), 0, "sigma");
    expect_row("sigma_nan", edit([&](auto& v) { putf(v, row0 + 40, std::numeric_limits<float>::quiet_NaN()); }), 0, "sigma");
    expect_row("sigma_1e-3", edit([&](auto& v) { putf(v, row0 + 40, 1e-3f); }), 0, "sigma");
    expect_row("occupancy_0.6", edit([&](auto& v) { putf(v, row0 + 44, .6f); }), 0, "occupancy");
    expect_row("occupancy_0", edit([&](auto& v) { putf(v, row0 + 44, 0.f); }), 0, "occupancy");
    expect_row("chroma_1.5", edit([&](auto& v) { putf(v, row0 + 52, 1.5f); }), 0, "chroma");
    expect_row("colour_1.5", edit([&](auto& v) { putf(v, row0 + 64, 1.5f); }), 0, "colour");
    expect_row("colour_negative", edit([&](auto& v) { putf(v, row0 + 104, -.1f); }), 0, "colour");
    expect_row("flags", edit([&](auto& v) { put32(v, row0 + 108, 8); }), 0, "flags");
    expect_row("packet_width", edit([&](auto& v) { put32(v, prow0 + 16, 1559); }), 0, "packet_dimensions");
    expect_row("packet_decoded", edit([&](auto& v) { put32(v, prow0 + 28, decoded + 8); }), 0, "packet_dimensions");
    expect_row("packet_checksum_zero", edit([&](auto& v) { put64(v, prow0 + 32, 0); }), 0, "packet_checksum_zero");
    expect_row("packet_profile", edit([&](auto& v) { put32(v, prow0 + 40, family_name_id(b.c_str())); }), 0, "packet_profile");
    expect_row("packet_size_0", edit([&](auto& v) { put64(v, prow0 + 8, 0); }), 0, "packet_size");
    expect_row("packet_size_huge", edit([&](auto& v) { put64(v, prow0 + 8, 56ull + decoded + 1); }), 0, "packet_size");
    expect_row("packet_offset_eof", edit([&](auto& v) { put64(v, prow0, v.size()); }), 0, "packet_offset");
    expect_row("packet_offset_table", edit([&](auto& v) { put64(v, prow0, 64); }), 0, "packet_offset");
    expect_row("packet_offset_wrap", edit([&](auto& v) { put64(v, prow0, ~0ull - 8); }), 0, "packet_offset");
    // The packet's own header, checked at load (3 of the 11 decoder corruptions).
    expect_row("packet_header_version", edit([&](auto& v) { put32(v, packet0 + 8, 2); }), 0, "packet_header", false);
    expect_row("packet_header_width", edit([&](auto& v) { put32(v, packet0 + 24, 1559); }), 0, "packet_header", false);
    expect_row("packet_header_checksum", edit([&](auto& v) { v[packet0 + 44] ^= 1; }), 0, "packet_header", false);

    // Switch-time decoder statuses (the other 8 of the 11 per-profile corruptions): the row is
    // disabled, the call returns, the other family still decodes.
    const std::size_t first_run = packet0 + 56, literal = first_run + 4;
    auto shrink_packet = [&](auto& v) { put64(v, prow0 + 8, get64(v, prow0 + 8) - 1); refresh(v); };
    expect_decode_failure("decode_run_past_end", edit([&](auto& v) { put32(v, literal + 32, width * height); }), Status::InvalidRun);
    expect_decode_failure("decode_short_packet", edit(shrink_packet), Status::Truncated);
    expect_decode_failure("decode_zero_run", edit([&](auto& v) { put32(v, first_run, 0x80000000u); }), Status::InvalidRun);
    expect_decode_failure("decode_overflow_run", edit([&](auto& v) { put32(v, first_run, 0x7fffffffu); }), Status::InvalidRun);
    expect_decode_failure("decode_nonfinite_half", edit([&](auto& v) { v[literal] = 0; v[literal + 1] = 0x7c; }), Status::InvalidHalf);
    expect_decode_failure("decode_negative_half", edit([&](auto& v) { v[literal + 1] |= 0x80; }), Status::InvalidHalf);
    expect_decode_failure("decode_checksum", edit([&](auto& v) { v[literal] ^= 1; }), Status::ChecksumMismatch);
    expect_decode_failure("decode_trailing", edit([&](auto& v) {
        // One extra byte inside the packet's declared size, before the next packet.
        v.insert(v.begin() + std::ptrdiff_t(packet0 + get64(v, prow0 + 8)), 0);
        put64(v, prow0 + 8, get64(v, prow0 + 8) + 1);
        put64(v, prow0 + family_packet_row_bytes, get64(v, prow0 + family_packet_row_bytes) + 1);
        refresh(v); }), Status::TrailingBytes);
    expect_decode_failure("decode_file_swapped", valid, Status::ResourceLoadFailed, [](auto& v) { v.push_back(0); });
    {   // A first allocation failure at a switch is retried once; the second disables the rows.
        const auto path = write("allocation.bin", valid);
        const auto t = load(path);
        std::vector<std::uint16_t> out;
        fail_large_allocations = true;
        const auto first = decode_family_packet(*t, native(path).c_str(), Profile(t->rows[0].profile), out);
        const bool retained = !t->rows[0].disabled.load() && t->find(a.c_str());
        const auto second = decode_family_packet(*t, native(path).c_str(), Profile(t->rows[0].profile), out);
        fail_large_allocations = false;
        require(first.status == Status::AllocationFailed && retained && second.status == Status::AllocationFailed &&
                t->rows[0].disabled.load() && !std::strcmp(t->rows[0].disabled.load(), "allocation") && !t->find(a.c_str()) &&
                bool(decode_family_packet(*t, native(path).c_str(), Profile(t->rows[1].profile), out)), "allocation retry");
        ++cases; std::printf("CASE allocation_retry status=decode_failed_twice reason=allocation\n");
    }

    std::printf("PASS fog_family_file cases=%u compiled=%zu\n", cases, std::size(family_profiles));
    return 0;
}

int probe() {
    const bool first = load_family_table(), second = load_family_table();
    require(first && !second, "process-wide load runs exactly once");
    require(family_table() != nullptr, "table after load");
    const auto& t = *family_table();
    std::printf("TABLE status=%s reason=%s families=%u packets=%u rows_disabled=%u bytes=%llu path=%s\n", status(t.status), t.reason,
                t.families, t.packets, t.rows_disabled, static_cast<unsigned long long>(t.file_bytes), family_table_path());
    for (std::uint32_t i = 0; i < t.families; ++i) {
        const char* why = t.rows[i].disabled.load();
        std::printf("ROW %u name=%s profile=%u packet=%u disabled=%s\n", i, t.rows[i].name, t.rows[i].profile, t.rows[i].packet, why ? why : "-");
    }
    for (std::uint32_t i = 0; i < t.families; ++i) {
        if (t.rows[i].disabled.load()) continue;
        ProfileInfo info{};
        require(family_info(Profile(t.rows[i].profile), info), "family_info");
        std::vector<std::uint16_t> out;
        const auto r = decode_family(Profile(t.rows[i].profile), out);
        std::uint64_t h = fnv_offset;
        for (auto w : out) { h = (h ^ std::uint8_t(w)) * fnv_prime; h = (h ^ std::uint8_t(w >> 8)) * fnv_prime; }
        std::printf("DECODE %s status=%s bytes=%zu fnv=%016llx match=%u found_after=%u\n", t.rows[i].name, status_name(r.status), out.size() * 2,
                    static_cast<unsigned long long>(h), unsigned(bool(r) && h == info.decoded_fnv1a), unsigned(t.find(t.rows[i].name) != nullptr));
    }
    for (const auto& f : family_profiles) require(!t.find(f.family), "compiled name in file");
    std::printf("PASS fog_family_file probe\n");
    return 0;
}
} // namespace

int main(int argc, char** argv) try {
    if (argc == 3 && !std::strcmp(argv[1], "--self-test")) { dir = argv[2]; return self_test(); }
    if (argc == 2 && !std::strcmp(argv[1], "--probe")) return probe();
    std::fprintf(stderr, "usage: fog_family_file_fixture --self-test DIR | --probe\n");
    return 2;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL %s\n", e.what());
    return 1;
}
