// Original synthetic fixture of the FL sector-background reader (src/engine/fl/sector_background_fl.cpp,
// docs/architecture/farnhams-legacy-sector.md): no game code, no game bytes, no launch. Built with
// -DX3M_EDITION_FL=1 -DX3M_SECTOR_BACKGROUND_FIXTURE (verification/analysis/test_fl_sector_background.py).
//
// A synthetic image at a VirtualAlloc base carries the generated anchors with their relocated operands
// recomputed for that base and the four slots; a separate region holds a fake cockpit registry (map, bucket
// array, links), cockpit, sector, sector camera, ref ship, docking station, config object and a 110-row
// TBackgrounds table. The production provider reads all of it through engine_memory::read.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../../src/engine/fl/sector_background_fl.h"
#include "../../src/engine/fl/sector_background_fl_inc.h"

namespace sb = x3m::sector_background;
namespace fsb = x3m::fl::sector_background_fl;
unsigned checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
char last_row[4096];
unsigned rows = 0;
bool quiet = false;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(last_row, sizeof last_row, format, args);
    va_end(args);
    ++rows;
    if (!quiet) std::printf("LOG %s\n", last_row);
}
}
bool row_has(const char* text) {
    return std::strstr(last_row, text) != nullptr;
}

constexpr std::uint32_t image_size = 0x1b0000;
unsigned char* image = nullptr;
std::uint32_t b = 0;
void write_image(unsigned char* to, std::uint32_t base) {
    for (unsigned i = 0; i < fsb::anchor_count; ++i) {
        const fsb::Anchor& a = fsb::anchors[i];
        std::memcpy(to + a.rva, a.bytes, a.length);
        for (unsigned k = 0; k < a.operands; ++k) {
            const std::uint32_t operand = base + a.operand_rva[k];
            std::memcpy(to + a.rva + a.operand_offset[k], &operand, 4);
        }
    }
}
template <class T> void put(std::uint32_t address, T value) {
    std::memcpy(reinterpret_cast<void*>(std::uintptr_t(address)), &value, sizeof value);
}
std::uint32_t slot(std::uint32_t rva) {
    return b + rva;
}

// The fake engine heap: a bump allocator in one committed region.
unsigned char* heap = nullptr;
std::uint32_t heap_used = 0;
std::uint32_t alloc(std::uint32_t size) {
    heap_used = (heap_used + 15) & ~15u;
    const std::uint32_t at = std::uint32_t(std::uintptr_t(heap)) + heap_used;
    heap_used += size;
    return at;
}
constexpr std::uint32_t handle = 0x2a5, capacity = 16, records = 110, index_fogged = 14;
std::uint32_t registry, map_header, buckets, link_other, link_ours, cockpit, sector, camera, ship, station, config,
    table, name_page_name, name_overrun;
const char* const family = "foggreenoutlands_dust"; // 21 characters, the longest shipped family length
static_assert(sizeof("foggreenoutlands_dust") == 22, "21 characters + NUL");

// `n` bytes ending exactly at the end of a committed page followed by a reserved (uncommitted) page.
std::uint32_t at_page_end(const char* bytes, std::uint32_t n) {
    unsigned char* pages = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x2000, MEM_RESERVE, PAGE_NOACCESS));
    check(pages && VirtualAlloc(pages, 0x1000, MEM_COMMIT, PAGE_READWRITE), "page-end allocation");
    std::memcpy(pages + 0x1000 - n, bytes, n);
    return std::uint32_t(std::uintptr_t(pages)) + 0x1000 - n;
}
std::uint32_t record(std::uint32_t index) {
    return table + index * 0xdb8 + 0x44;
}
void build_world() {
    heap = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x100000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(heap != nullptr, "fake heap");
    registry = alloc(0x20);
    map_header = alloc(8);
    buckets = alloc(4 * capacity);
    link_other = alloc(12);
    link_ours = alloc(12);
    cockpit = alloc(0x60);
    sector = alloc(0x150);
    camera = alloc(0x380);
    ship = alloc(0x60);
    station = alloc(0x60);
    config = alloc(0x780);
    table = alloc(records * 0xdb8);
    put(registry + 0, map_header);
    put(registry + 0x10, handle);
    put(map_header + 0, buckets);
    put(map_header + 4, capacity);
    // A colliding link first: the walk follows next to the matching key.
    put(buckets + 4 * ((capacity - 1) & handle), link_other);
    put(link_other + 0, link_ours);
    put(link_other + 4, handle + capacity);
    put(link_other + 8, std::uint32_t(0xdead0000));
    put(link_ours + 0, std::uint32_t(0));
    put(link_ours + 4, handle);
    put(link_ours + 8, cockpit);
    put(cockpit + 0xc, ship);
    put(cockpit + 0x54, sector);
    put(cockpit + 0x58, camera);
    put(sector + 8, std::uint32_t(4370));
    put(sector + 0x48, std::int16_t(1));
    put(sector + 0x13c, index_fogged);
    put(sector + 0x140, std::uint32_t(3));
    put(sector + 0x144, std::uint32_t(5));
    put(ship + 0x48, std::int16_t(7));
    put(ship + 0x54, sector);
    put(station + 0x48, std::int16_t(5));
    put(station + 0x54, sector);
    put(config + 0x758, std::int32_t(3));
    put(config + 0x768, std::int32_t(2)); // AP's offset: a decoy the FL reader must not read
    for (std::uint32_t i = 0; i < records; ++i) {
        const std::uint32_t r = record(i);
        put(r + 0, std::uint32_t(0)); // no name: name_valid=0
        put(r + 0xf0, std::int32_t(0));
    }
    // The fogged row: the name pointer is filled per case below.
    const std::uint32_t r = record(index_fogged);
    for (unsigned i = 0; i < 8; ++i) put(r + 0xd0 + 4 * i, std::int32_t(100 + i));
    put(r + 0xf0, std::int32_t(50));
    put(r + 0x104, std::int32_t(1500000));
    put(r + 0x108, std::int32_t(18500000));
    put(r + 0x10c, std::int32_t(7));
    put(camera + 0x270, std::uint32_t(0x10000 | 0x4));
    put(camera + 0x36c, std::int32_t(1500000));
    put(camera + 0x370, std::int32_t(18500000));
    // The name at the very end of a committed page whose successor is reserved, not committed: 21 characters and
    // the NUL end exactly at the boundary, so any read past the allocation fails.
    name_page_name = at_page_end(family, 22);
    // 26 printable bytes up to the boundary, no NUL: must be refused without a fault.
    name_overrun = at_page_end("abcdefghijklmnopqrstuvwxyz", 26);
    put(slot(fsb::cockpit_registry_slot_rva), registry);
    put(slot(fsb::background_table_slot_rva), table);
    put(slot(fsb::background_count_slot_rva), records);
    put(slot(fsb::config_slot_rva), config);
}
sb::Sample take(unsigned walk = 0) {
    sb::Sample s;
    SetLastError(0x5ec7);
    const bool ok = fsb::sample(s, walk);
    check(ok, "admitted sample");
    check(GetLastError() == 0x5ec7, "LastError preserved by sample()");
    return s;
}
void expect(const sb::Sample& s, sb::Status status, const char* label) {
    if (s.status != status) std::printf("STATUS %s got=%s want=%s\n", label, sb::name(s.status), sb::name(status));
    check(s.status == status, label);
}

// The shared NUL-stopped copy with a recording reader: the highest byte read is the NUL.
struct Recorder {
    std::uint32_t start = 0, end = 0, highest = 0;
    unsigned reads = 0;
    bool operator()(std::uintptr_t p, void* out, std::size_t n) {
        ++reads;
        if (p < start || p + n > end) return false;
        if (p + n - 1 > highest) highest = std::uint32_t(p + n - 1);
        std::memcpy(out, reinterpret_cast<void*>(p), n);
        return true;
    }
};

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetEnvironmentVariableW(L"X3M_FL_SECTOR_LOG", L"10");
    // 1. The real process is not X3FL.exe: the identity gate refuses, LastError kept.
    {
        sb::Sample s;
        s.index = 77;
        SetLastError(0x1111);
        check(!fsb::sample(s, 0) && s.index == 77, "foreign executable: no sample, out untouched");
        check(GetLastError() == 0x1111, "LastError preserved through the identity decision");
        check(!std::strcmp(fsb::status(), "executable_mismatch") && row_has("fl.sector status=executable_mismatch"),
              "executable_mismatch row");
        check(!fsb::admitted() && !fsb::sample(s, 0), "the refusal is kept");
    }
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(image != nullptr, "synthetic image");
    b = std::uint32_t(std::uintptr_t(image));
    write_image(image, b);
    // 2. Foreign base: the same bytes (relocated for b) in a second region, admitted at that region's base.
    {
        unsigned char* other = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        check(other != nullptr, "second image");
        std::memcpy(other, image, image_size);
        check(!fsb::fixture_admit(std::uintptr_t(other)) && !std::strcmp(fsb::status(), "anchor_mismatch") &&
                  fsb::fixture_failed_anchor() && !std::strcmp(fsb::fixture_failed_anchor(), "registry_lookup"),
              "image relocated for another base refused at its first anchor");
        check(row_has("status=anchor_mismatch") && row_has("failed=registry_lookup") && row_has("registry_slot=0x00000000"),
              "anchor_mismatch row without slots");
        check(VirtualFree(other, 0, MEM_RELEASE) != 0, "second image released");
        check(!fsb::fixture_admit(0) && !std::strcmp(fsb::status(), "base_overflow"), "null base refused");
        check(!fsb::fixture_admit(0xfff00000u) && !std::strcmp(fsb::status(), "base_overflow"), "wrapping base refused");
    }
    // 3. Every single-byte flip of every anchor is refused, the operand words included.
    {
        unsigned flips = 0, refused = 0, bytes = 0;
        quiet = true;
        for (unsigned i = 0; i < fsb::anchor_count; ++i) {
            const fsb::Anchor& a = fsb::anchors[i];
            bytes += a.length;
            for (unsigned k = 0; k < a.length; ++k) {
                image[a.rva + k] ^= 0x01;
                ++flips;
                refused += !fsb::fixture_admit(b) && fsb::fixture_failed_anchor() &&
                           !std::strcmp(fsb::fixture_failed_anchor(), a.name);
                image[a.rva + k] ^= 0x01;
            }
        }
        quiet = false;
        check(flips == bytes && refused == flips, "every single-byte anchor flip refused, naming its anchor");
        std::printf("ANCHOR anchors=%u bytes=%u flips=%u refused=%u\n", fsb::anchor_count, bytes, flips, refused);
    }
    // 4. Admission at the image's own base.
    SetLastError(0x2222);
    check(fsb::fixture_admit(b) && !std::strcmp(fsb::status(), "ready") && fsb::admitted(), "relocated image admitted");
    check(GetLastError() == 0x2222, "LastError preserved by admission");
    {
        char expect_row[256];
        std::snprintf(expect_row, sizeof expect_row,
                      "registry_slot=0x%08lx table_slot=0x%08lx count_slot=0x%08lx config_slot=0x%08lx view_distance=+0x758",
                      static_cast<unsigned long>(b + 0x1a82e4), static_cast<unsigned long>(b + 0x1a83d0),
                      static_cast<unsigned long>(b + 0x1a8338), static_cast<unsigned long>(b + 0x1a83b0));
        check(row_has("fl.sector status=ready") && row_has(expect_row) && row_has("anchors=22 failed=none log_s=10"),
              "ready row names the module-base slots");
    }
    build_world();
    const std::uint32_t r = record(index_fogged);
    put(r + 0, name_page_name);
    // 5. Ready with the record's fields; the periodic row.
    fsb::fixture_force_sample();
    {
        const sb::Sample s = take();
        expect(s, sb::Status::Ready, "ready");
        check(s.registry == registry && s.handle == handle && s.cockpit == cockpit && s.sector == sector &&
                  s.class48 == 1 && s.index == int(index_fogged) && s.count == int(records) && s.table == table &&
                  s.record == r && s.row_valid,
              "ready chain");
        check(s.dust == 50 && s.fog_near == 1500000 && s.fog_far == 18500000 && s.stardust == 7, "ready record fields");
        bool rates = true;
        for (unsigned i = 0; i < 8; ++i) rates = rates && s.rates[i] == int(100 + i);
        check(rates, "ready rates");
        check(s.name_pointer == name_page_name && s.name_valid && !std::strcmp(s.family, family),
              "21-character name ending at an uncommitted page read to its NUL");
        check(s.neb == 3 && s.stars == 5, "sector +0x140/+0x144");
        check(s.camera == camera && s.camera_valid && s.camera_check == sb::Check::Match && s.flags270 == 0x10004,
              "camera fog pair matches");
        check(s.anchor_check == sb::Check::Match && s.ref_object == ship && s.ref_sector == sector, "ref anchor matches");
        check(s.config_valid && s.config == 3 && s.far_floor == 500000000 && s.effective_far == 500000000,
              "config +0x758 view distance (not +0x768) and the high floor");
        check(row_has("fl.sector_sample row=1 samples=1 ready=1 status=ready") &&
                  row_has("name_valid=1 name=\"foggreenoutlands_dust\" dust=50 near=1500000 far=18500000 stardust=7 config758=3"),
              "fl.sector_sample row");
        std::printf("READY name=%s dust=%d near=%d far=%d stardust=%d config758=%d effective_far=%d\n", s.family,
                    s.dust, s.fog_near, s.fog_far, s.stardust, s.config, s.effective_far);
    }
    put(config + 0x758, std::int32_t(2));
    {
        const sb::Sample s = take();
        check(s.config == 2 && s.far_floor == 100000000 && s.effective_far == 100000000, "view distance 2: mid floor");
    }
    put(config + 0x758, std::int32_t(3));
    // 6. Names: no NUL up to the page boundary is refused safely; an escaped name; no name.
    put(r + 0, name_overrun);
    {
        const sb::Sample s = take();
        expect(s, sb::Status::Ready, "unterminated name keeps the row");
        check(!s.name_valid && s.family[0] == 0, "26 bytes up to an uncommitted page: name refused without a fault");
    }
    {
        const std::uint32_t quoted = alloc(8);
        std::memcpy(reinterpret_cast<void*>(std::uintptr_t(quoted)), "a\"b\\c", 6);
        put(r + 0, quoted);
        const sb::Sample s = take();
        check(s.name_valid && !std::strcmp(s.family, "a\\\"b\\\\c"), "quote and backslash escaped");
    }
    put(r + 0, std::uint32_t(0));
    {
        const sb::Sample s = take();
        expect(s, sb::Status::Ready, "no name");
        check(!s.name_valid, "null name pointer: name_valid=0");
    }
    put(r + 0, name_page_name);
    // 7. The shared copy itself: the highest address read is the NUL, one byte per read.
    {
        Recorder rec;
        rec.start = name_page_name;
        rec.end = name_page_name + 22;
        char raw[32]{};
        check(sb::copy_name(rec, name_page_name, raw) && rec.highest == name_page_name + 21 && rec.reads == 22 &&
                  !std::strcmp(raw, family),
              "copy_name stops at the NUL of an exact allocation");
        Recorder cut;
        cut.start = name_page_name;
        cut.end = name_page_name + 21; // the NUL unreadable
        char raw2[32]{};
        check(!sb::copy_name(cut, name_page_name, raw2), "copy_name refuses a name whose NUL is unreadable");
    }
    // 8. Docked: the ref ship's parent is a station whose parent is the sector.
    put(ship + 0x54, station);
    {
        const sb::Sample s = take(0);
        expect(s, sb::Status::AnchorMismatch, "docked without the walk");
        check(s.anchor_check == sb::Check::Mismatch && s.anchor_walk == sb::AnchorWalk::None, "raw mismatch");
        const sb::Sample w = take(sb::anchor_walk_limit);
        expect(w, sb::Status::Ready, "docked with the walk");
        check(w.anchor_check == sb::Check::Mismatch && w.anchor_walk == sb::AnchorWalk::Found && w.anchor_depth == 1 &&
                  w.anchor_last == sector,
              "walk found the sector one hop up");
        const std::uint32_t other = alloc(0x60);
        put(other + 0x48, std::int16_t(1));
        put(station + 0x54, other);
        const sb::Sample o = take(sb::anchor_walk_limit);
        expect(o, sb::Status::AnchorMismatch, "docked in another sector");
        check(o.anchor_walk == sb::AnchorWalk::OtherSector, "walk reports another sector");
        put(station + 0x54, sector);
    }
    put(ship + 0x54, sector);
    // 9. Refusals.
    put(registry + 0x10, std::uint32_t(0));
    expect(take(), sb::Status::NoCockpit, "no active handle");
    put(registry + 0x10, handle);
    put(slot(fsb::cockpit_registry_slot_rva), std::uint32_t(0));
    expect(take(), sb::Status::NoCockpit, "no registry");
    put(slot(fsb::cockpit_registry_slot_rva), registry);
    put(link_ours + 4, handle + 2 * capacity);
    expect(take(), sb::Status::Missing, "handle not in the map");
    put(link_ours + 4, handle);
    put(cockpit + 0x54, std::uint32_t(0));
    expect(take(), sb::Status::NoSector, "no sector");
    put(cockpit + 0x54, sector);
    put(sector + 0x48, std::int16_t(2));
    expect(take(), sb::Status::BadClass, "class word 2");
    put(sector + 0x48, std::int16_t(1));
    put(sector + 0x13c, records);
    expect(take(), sb::Status::BadIndex, "index == count");
    put(sector + 0x13c, std::int32_t(-1));
    expect(take(), sb::Status::BadIndex, "index -1");
    put(sector + 0x13c, index_fogged);
    put(slot(fsb::background_count_slot_rva), std::int32_t(5000));
    expect(take(), sb::Status::BadCount, "count 5000");
    put(slot(fsb::background_count_slot_rva), std::int32_t(-3));
    expect(take(), sb::Status::BadCount, "count -3");
    put(slot(fsb::background_count_slot_rva), records);
    put(slot(fsb::background_table_slot_rva), std::uint32_t(0));
    expect(take(), sb::Status::Loading, "no table");
    put(slot(fsb::background_table_slot_rva), table);
    put(r + 0xf0, std::int32_t(65));
    expect(take(), sb::Status::BadDust, "dust 65");
    put(r + 0xf0, std::int32_t(50));
    put(camera + 0x370, std::int32_t(500000000));
    expect(take(), sb::Status::CameraMismatch, "camera far differs from the row");
    put(camera + 0x370, std::int32_t(18500000));
    put(link_ours + 0, link_other);
    put(link_ours + 4, handle + 3 * capacity);
    expect(take(), sb::Status::Cycle, "link cycle");
    put(link_ours + 0, std::uint32_t(0));
    put(link_ours + 4, handle);
    // An unreadable pointer mid-chain (a reserved, uncommitted page as the sector): no fault, ReadFailure.
    {
        void* reserved = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE, PAGE_NOACCESS);
        check(reserved != nullptr, "reserved page");
        put(cockpit + 0x54, std::uint32_t(std::uintptr_t(reserved)));
        expect(take(), sb::Status::ReadFailure, "sector on an uncommitted page");
        put(cockpit + 0x54, sector);
        put(slot(fsb::background_table_slot_rva), std::uint32_t(std::uintptr_t(reserved)));
        expect(take(), sb::Status::ReadFailure, "table on an uncommitted page");
        put(slot(fsb::background_table_slot_rva), table);
    }
    // Misaligned pointers: Malformed.
    put(slot(fsb::cockpit_registry_slot_rva), registry + 1);
    expect(take(), sb::Status::Malformed, "misaligned registry");
    put(slot(fsb::cockpit_registry_slot_rva), registry);
    put(link_ours + 8, cockpit + 2);
    expect(take(), sb::Status::Malformed, "misaligned cockpit");
    put(link_ours + 8, cockpit);
    put(map_header + 4, std::uint32_t(12));
    expect(take(), sb::Status::Malformed, "capacity not a power of two");
    put(map_header + 4, capacity);
    put(slot(fsb::background_table_slot_rva), table + 2);
    expect(take(), sb::Status::Malformed, "misaligned table");
    put(slot(fsb::background_table_slot_rva), table);
    // 33 distinct links without the key: Limit (the walk examines at most 32).
    {
        const std::uint32_t first = alloc(12 * 33);
        for (std::uint32_t i = 0; i < 33; ++i) {
            put(first + 12 * i + 0, i + 1 < 33 ? first + 12 * (i + 1) : std::uint32_t(0));
            put(first + 12 * i + 4, handle + capacity * (i + 1));
            put(first + 12 * i + 8, std::uint32_t(0));
        }
        put(buckets + 4 * ((capacity - 1) & handle), first);
        expect(take(), sb::Status::Limit, "33-link chain");
        put(buckets + 4 * ((capacity - 1) & handle), link_other);
    }
    // Fog bounds: near > far, far > 2e9, near 0 with far set.
    put(r + 0x104, std::int32_t(20000000));
    expect(take(), sb::Status::BadFog, "near > far");
    put(r + 0x104, std::int32_t(1500000));
    put(r + 0x108, std::int32_t(2000000001));
    expect(take(), sb::Status::BadFog, "far > 2e9");
    put(r + 0x104, std::int32_t(0));
    expect(take(), sb::Status::BadFog, "near 0, far set");
    put(r + 0x104, std::int32_t(1500000));
    put(r + 0x108, std::int32_t(18500000));
    expect(take(), sb::Status::Ready, "ready again");
    // 10. Cost of one production sample (Ready, docked walk on, name at the page end), rows not due.
    {
        LARGE_INTEGER f{}, t0{}, t1{};
        QueryPerformanceFrequency(&f);
        constexpr unsigned n = 100000;
        sb::Sample s;
        const unsigned before = rows;
        QueryPerformanceCounter(&t0);
        for (unsigned i = 0; i < n; ++i) fsb::sample(s, sb::anchor_walk_limit);
        QueryPerformanceCounter(&t1);
        check(s.status == sb::Status::Ready && s.name_valid && rows == before, "timed samples ready, no row");
        const double with_name = double(t1.QuadPart - t0.QuadPart) * 1e9 / double(f.QuadPart) / n;
        // The same without a name pointer: the 22 one-byte name reads' share.
        put(r + 0, std::uint32_t(0));
        QueryPerformanceCounter(&t0);
        for (unsigned i = 0; i < n; ++i) fsb::sample(s, sb::anchor_walk_limit);
        QueryPerformanceCounter(&t1);
        put(r + 0, name_page_name);
        check(s.status == sb::Status::Ready && !s.name_valid && rows == before, "timed samples without a name");
        const double without_name = double(t1.QuadPart - t0.QuadPart) * 1e9 / double(f.QuadPart) / n;
        std::printf("TIMING sample_ns=%.1f no_name_ns=%.1f samples=%u\n", with_name, without_name, n);
    }
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
