// Original synthetic fixture of the FL fog transit prefill walk (src/engine/fl/fog_prefill_fl.cpp,
// docs/architecture/farnhams-legacy-prefill.md): no game code, no game bytes, no launch. Built with
// -DX3M_EDITION_FL=1 -DX3M_FOG_PREFILL_FIXTURE (verification/analysis/test_fl_fog_prefill.py).
//
// A synthetic image at a VirtualAlloc base carries the generated anchors with their relocated operands recomputed
// for that base and the four slots (manager, background count, background table, recovery guard); a separate region
// holds a fake object manager whose parentless list (header M+8: head, tail 0, tailpred) links an old sector, the
// new sector, a ship and a cut-scene space, and a 110-row TBackgrounds table. The production provider reads all of
// it through engine_memory::read.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../src/engine/fl/fog_prefill_fl.h"
#include "../../src/engine/fl/fog_prefill_fl_inc.h"

namespace fp = x3m::fog_prefill;
namespace ffp = x3m::fl::fog_prefill_fl;
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

constexpr std::uint32_t image_size = 0x230000;
static_assert(ffp::recovery_guard_slot_rva + 4 <= image_size, "the guard slot inside the synthetic image");
unsigned char* image = nullptr;
std::uint32_t b = 0;
void write_image(unsigned char* to, std::uint32_t base) {
    for (unsigned i = 0; i < ffp::anchor_count; ++i) {
        const ffp::Anchor& a = ffp::anchors[i];
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
constexpr std::uint32_t records = 110, index_old = 3, index_new = 14, id_old = 4370, id_new = 5021;
std::uint32_t manager, old_sector, new_sector, ship, cutscene, table, name_new;
const char* const family = "foggreenoutlands_dust"; // 21 characters, the longest shipped family length

std::uint32_t head() {
    return manager + 8;
}
std::uint32_t tail() {
    return manager + 0xc;
}
// Relink the manager's list head -> ... -> tail from `nodes` (AddTail order, the newest last).
void link(std::initializer_list<std::uint32_t> nodes) {
    std::uint32_t prev = head();
    for (const std::uint32_t n : nodes) {
        put(prev + 0, n);
        put(n + 4, prev);
        prev = n;
    }
    put(prev + 0, tail());
    put(manager + 0x10, prev); // tailpred
}
// An engine object as the common initializer leaves it (class, subtype, marker) plus the sector fields.
std::uint32_t object(std::int16_t kind, std::int16_t subtype, std::uint16_t marker) {
    const std::uint32_t o = alloc(0x180);
    put(o + 0x48, kind);
    put(o + 0x4a, subtype);
    put(o + 0x9c, marker);
    return o;
}
std::uint32_t sector_object(std::int16_t subtype, std::uint32_t id, std::uint32_t scene, std::int32_t index) {
    const std::uint32_t o = object(1, subtype, 0xcafe);
    put(o + 8, id);
    put(o + 0x130, scene);
    put(o + 0x13c, index);
    return o;
}
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
    manager = alloc(0x20);
    old_sector = sector_object(0, id_old, 0x0bad0001, index_old);
    new_sector = sector_object(0, id_new, 0x0bad0002, index_new);
    ship = object(7, 0, 0xcafe);
    cutscene = sector_object(3, 7777, 0x0bad0003, 1); // 621::Create: class 1, subtype member14
    table = alloc(records * 0xdb8);
    for (std::uint32_t i = 0; i < records; ++i) {
        put(record(i) + 0, std::uint32_t(0));
        put(record(i) + 0xf0, std::int32_t(0));
    }
    const std::uint32_t r = record(index_new);
    put(r + 0xf0, std::int32_t(50));
    put(r + 0x104, std::int32_t(1500000));
    put(r + 0x108, std::int32_t(18500000));
    put(r + 0x10c, std::int32_t(7));
    name_new = at_page_end(family, 22); // the NUL ends exactly at an uncommitted page
    put(r + 0, name_new);
    link({old_sector, new_sector, ship, cutscene});
    put(slot(ffp::object_manager_slot_rva), manager);
    put(slot(ffp::background_count_slot_rva), records);
    put(slot(ffp::background_table_slot_rva), table);
    put(slot(ffp::recovery_guard_slot_rva), std::uint32_t(0));
}
fp::Result take(std::uint32_t last_ready_id = id_old) {
    fp::Result w;
    SetLastError(0x5ec7);
    const bool ok = ffp::walk(w, last_ready_id);
    check(ok, "admitted walk");
    check(GetLastError() == 0x5ec7, "LastError preserved by walk()");
    return w;
}
void expect(const fp::Result& w, fp::Walk status, const char* label) {
    if (w.status != status) std::printf("STATUS %s got=%s want=%s\n", label, fp::name(w.status), fp::name(status));
    check(w.status == status, label);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetEnvironmentVariableW(L"X3M_FL_PREFILL_LOG", L"10");
    // 1. The real process is not X3FL.exe: the identity gate refuses, LastError kept, out untouched.
    {
        fp::Result w;
        w.id = 77;
        SetLastError(0x1111);
        check(!ffp::walk(w, 0) && w.id == 77 && w.reads == 0, "foreign executable: no walk, out untouched");
        check(GetLastError() == 0x1111, "LastError preserved through the identity decision");
        check(!std::strcmp(ffp::status(), "executable_mismatch") && row_has("fl.prefill status=executable_mismatch") &&
                  row_has("manager_slot=0x00000000"),
              "executable_mismatch row without slots");
        check(!ffp::admitted() && !ffp::walk(w, 0), "the refusal is kept");
    }
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(image != nullptr, "synthetic image");
    b = std::uint32_t(std::uintptr_t(image));
    write_image(image, b);
    // 2. Foreign base: the same bytes (relocated for b) in a second region, refused at that region's base.
    {
        unsigned char* other = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        check(other != nullptr, "second image");
        std::memcpy(other, image, image_size);
        check(!ffp::fixture_admit(std::uintptr_t(other)) && !std::strcmp(ffp::status(), "anchor_mismatch") &&
                  ffp::fixture_failed_anchor() && !std::strcmp(ffp::fixture_failed_anchor(), "manager_publish"),
              "image relocated for another base refused at its first relocated anchor");
        check(row_has("status=anchor_mismatch") && row_has("failed=manager_publish") && row_has("guard_slot=0x00000000"),
              "anchor_mismatch row without slots");
        check(VirtualFree(other, 0, MEM_RELEASE) != 0, "second image released");
        check(!ffp::fixture_admit(0) && !std::strcmp(ffp::status(), "base_overflow"), "null base refused");
        check(!ffp::fixture_admit(0xfff00000u) && !std::strcmp(ffp::status(), "base_overflow"), "wrapping base refused");
        fp::Result w;
        check(!ffp::walk(w, 0), "a refused admission walks nothing");
    }
    // 3. Every single-byte flip of every anchor is refused, the operand words included.
    {
        unsigned flips = 0, refused = 0, bytes = 0;
        quiet = true;
        for (unsigned i = 0; i < ffp::anchor_count; ++i) {
            const ffp::Anchor& a = ffp::anchors[i];
            bytes += a.length;
            for (unsigned k = 0; k < a.length; ++k) {
                image[a.rva + k] ^= 0x01;
                ++flips;
                refused += !ffp::fixture_admit(b) && ffp::fixture_failed_anchor() &&
                           !std::strcmp(ffp::fixture_failed_anchor(), a.name);
                image[a.rva + k] ^= 0x01;
            }
        }
        quiet = false;
        check(flips == bytes && refused == flips, "every single-byte anchor flip refused, naming its anchor");
        std::printf("ANCHOR anchors=%u bytes=%u flips=%u refused=%u\n", ffp::anchor_count, bytes, flips, refused);
    }
    // 4. Admission at the image's own base.
    SetLastError(0x2222);
    check(ffp::fixture_admit(b) && !std::strcmp(ffp::status(), "ready") && ffp::admitted(), "relocated image admitted");
    check(GetLastError() == 0x2222, "LastError preserved by admission");
    {
        char expect_row[256];
        std::snprintf(expect_row, sizeof expect_row,
                      "manager_slot=0x%08lx count_slot=0x%08lx table_slot=0x%08lx guard_slot=0x%08lx anchors=18 "
                      "failed=none log_s=10",
                      static_cast<unsigned long>(b + 0x1a82ec), static_cast<unsigned long>(b + 0x1a8338),
                      static_cast<unsigned long>(b + 0x1a83d0), static_cast<unsigned long>(b + 0x22de10));
        check(row_has("fl.prefill status=ready") && row_has(expect_row), "ready row names the module-base slots");
    }
    build_world();
    // 5. Found: the newest class-1/subtype-0 alive node behind a cut-scene space (subtype) and a ship (class).
    {
        const fp::Result w = take();
        expect(w, fp::Walk::Found, "found");
        check(w.node == new_sector && w.id == id_new && w.steps == 3, "the new sector, three nodes from the tail");
        // guard, manager, tailpred, 3 types + 2 links, marker, block, id, count, table, row, 22 name bytes
        check(w.reads == 1 + 2 + 5 + 3 + 3 + 22, "reads: the guard first, the name to its NUL");
        const auto& s = w.sample;
        check(s.status == x3m::sector_background::Status::Ready && s.sector == new_sector && s.sector_id == id_new &&
                  s.index == int(index_new) && s.count == int(records) && s.table == table &&
                  s.record == record(index_new) && s.row_valid,
              "the record of the new sector's index");
        check(s.dust == 50 && s.fog_near == 1500000 && s.fog_far == 18500000 && s.stardust == 7, "record fields");
        check(s.name_valid && !std::strcmp(s.family, family), "21-character name ending at an uncommitted page");
        check(row_has("fl.prefill_sample row=1 polls=1 found=1 guarded=0 walk=found steps=3 reads=36") &&
                  row_has("name=\"foggreenoutlands_dust\" near=1500000 far=18500000"),
              "the first poll writes the fl.prefill_sample row");
        std::printf("FOUND node=new steps=%u reads=%u id=%u index=%d name=%s near=%d far=%d\n", w.steps, w.reads,
                    w.id, s.index, s.family, s.fog_near, s.fog_far);
    }
    // 6. The recovery guard: a callback inside allocation recovery sees a linked, typed, alive sector whose scene
    // and index are not yet initialised (garbage); the poll is refused before any list read.
    {
        const std::uint32_t partial = sector_object(0, 6000, 0xcdcdcdcd, std::int32_t(0xcdcdcdcd));
        link({old_sector, new_sector, ship, cutscene, partial});
        put(slot(ffp::recovery_guard_slot_rva), std::uint32_t(1));
        fp::Result w = take();
        expect(w, fp::Walk::Guarded, "guard set: refused");
        check(w.reads == 1 && w.steps == 0 && w.node == 0 && w.id == 0 && w.sample.index == -1,
              "guarded: one read, nothing of the node reported");
        check(!std::strcmp(fp::name(w.status), "guarded"), "the FL name of Guarded");
        put(slot(ffp::recovery_guard_slot_rva), std::uint32_t(0xffffffffu));
        expect(take(), fp::Walk::Guarded, "any nonzero guard refuses");
        // Guard clear, the constructor's zeroing done (scene 0, index 0): not ready.
        put(slot(ffp::recovery_guard_slot_rva), std::uint32_t(0));
        put(partial + 0x130, std::uint32_t(0));
        put(partial + 0x13c, std::int32_t(0));
        w = take();
        expect(w, fp::Walk::NoScene, "scene and index not yet set: not ready");
        check(w.node == partial && w.sample.index == -1, "no_scene: the index is not reported");
        // The index stored (0x6eac5) but not the scene (0x5c264): still not ready.
        put(partial + 0x13c, std::int32_t(index_new));
        expect(take(), fp::Walk::NoScene, "index set, scene not: not ready");
        // Unguarded garbage (what the guard prevents) never reaches a record: the index bound refuses it.
        put(partial + 0x130, std::uint32_t(0xcdcdcdcd));
        put(partial + 0x13c, std::int32_t(0xcdcdcdcd));
        expect(take(), fp::Walk::BadIndex, "garbage index refused by the count bound");
        link({old_sector, new_sector, ship, cutscene});
    }
    // 7. Filters: the same id as the ready sector; a dead newest sector; subtype and class skipping.
    expect(take(id_new), fp::Walk::SameId, "the newest sector is the ready one");
    {
        const std::uint32_t dead = sector_object(0, 6001, 0x0bad0004, index_new);
        put(dead + 0x9c, std::uint16_t(0xefac));
        link({old_sector, new_sector, ship, dead});
        const fp::Result w = take();
        expect(w, fp::Walk::Dead, "dead newest sector");
        check(w.node == dead && w.steps == 1, "dead: the walk stops at the first class-1/subtype-0 node");
        link({old_sector, new_sector, ship, cutscene});
    }
    put(new_sector + 0x4a, std::int16_t(2)); // the new sector as cut-scene space: the old one is next
    {
        const fp::Result w = take(0);
        expect(w, fp::Walk::Found, "subtype 2 skipped, the old sector found");
        check(w.node == old_sector && w.steps == 4 && w.sample.index == int(index_old), "old sector at step 4");
        expect(take(id_old), fp::Walk::SameId, "the old sector is the ready one");
    }
    put(new_sector + 0x4a, std::int16_t(0));
    put(new_sector + 0x48, std::int16_t(5));
    expect(take(0), fp::Walk::Found, "class 5 skipped");
    put(new_sector + 0x48, std::int16_t(1));
    // 8. The node bound: nine non-sector nodes at the tail.
    {
        std::uint32_t ships[9];
        for (auto& s : ships) s = object(7, 0, 0xcafe);
        link({old_sector, new_sector, ships[0], ships[1], ships[2], ships[3], ships[4], ships[5], ships[6], ships[7],
              ships[8]});
        fp::Result w = take();
        expect(w, fp::Walk::Bound, "nine ships at the tail");
        check(w.steps == fp::kMaxSteps && w.reads == 1 + 2 + 2 * fp::kMaxSteps - 1, "bound: 8 types, 7 links");
        link({old_sector, new_sector, ships[0], ships[1], ships[2], ships[3], ships[4], ships[5], ships[6]});
        w = take();
        expect(w, fp::Walk::Found, "seven ships: the sector is the eighth node");
        check(w.steps == fp::kMaxSteps && w.node == new_sector, "found at the bound");
        link({old_sector, new_sector, ship, cutscene});
    }
    // 9. List shape: empty, no manager, misaligned, unreadable links.
    link({});
    expect(take(), fp::Walk::Head, "empty list: tailpred is the head sentinel");
    link({old_sector, new_sector, ship, cutscene});
    put(slot(ffp::object_manager_slot_rva), std::uint32_t(0));
    expect(take(), fp::Walk::NoManager, "no manager");
    put(slot(ffp::object_manager_slot_rva), manager + 2);
    expect(take(), fp::Walk::Malformed, "misaligned manager");
    put(slot(ffp::object_manager_slot_rva), manager);
    put(cutscene + 4, ship + 1);
    expect(take(), fp::Walk::Malformed, "misaligned prev link");
    {
        void* reserved = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE, PAGE_NOACCESS);
        check(reserved != nullptr, "reserved page");
        const std::uint32_t r = std::uint32_t(std::uintptr_t(reserved));
        put(cutscene + 4, r);
        expect(take(), fp::Walk::ReadFailure, "prev link into an uncommitted page");
        put(cutscene + 4, ship);
        put(manager + 0x10, r);
        expect(take(), fp::Walk::ReadFailure, "tailpred into an uncommitted page");
        put(slot(ffp::object_manager_slot_rva), r);
        expect(take(), fp::Walk::ReadFailure, "manager on an uncommitted page");
        put(slot(ffp::object_manager_slot_rva), manager);
        link({old_sector, new_sector, ship, cutscene});
        put(slot(ffp::background_table_slot_rva), r);
        expect(take(), fp::Walk::ReadFailure, "table on an uncommitted page");
        put(slot(ffp::background_table_slot_rva), table);
        put(slot(ffp::recovery_guard_slot_rva), std::uint32_t(0));
    }
    // 10. Record refusals.
    put(slot(ffp::background_table_slot_rva), std::uint32_t(0));
    expect(take(), fp::Walk::Loading, "no table");
    put(slot(ffp::background_table_slot_rva), table);
    put(slot(ffp::background_count_slot_rva), std::int32_t(5000));
    expect(take(), fp::Walk::BadCount, "count 5000");
    put(slot(ffp::background_count_slot_rva), records);
    put(new_sector + 0x13c, std::int32_t(records));
    expect(take(), fp::Walk::BadIndex, "index == count");
    put(new_sector + 0x13c, std::int32_t(index_new));
    put(record(index_new) + 0xf0, std::int32_t(65));
    expect(take(), fp::Walk::BadRecord, "dust 65");
    put(record(index_new) + 0xf0, std::int32_t(50));
    expect(take(), fp::Walk::Found, "found again");
    // 11. Rows: one per period; counts since the previous row.
    {
        const unsigned before = rows;
        take();
        check(rows == before, "no row before the period");
        ffp::fixture_force_row();
        put(slot(ffp::recovery_guard_slot_rva), std::uint32_t(1));
        take();
        put(slot(ffp::recovery_guard_slot_rva), std::uint32_t(0));
        check(rows == before + 1 && row_has("fl.prefill_sample row=2 ") && row_has(" found=5 guarded=3 walk=guarded steps=0 reads=1 "),
              "forced row: the counts since row 1 (two guarded in 6, this one), the last walk");
    }
    // 12. Cost of one production poll (Found with the name, and the guarded refusal), rows not due.
    {
        LARGE_INTEGER f{}, t0{}, t1{};
        QueryPerformanceFrequency(&f);
        constexpr unsigned n = 100000;
        fp::Result w;
        const unsigned before = rows;
        QueryPerformanceCounter(&t0);
        for (unsigned i = 0; i < n; ++i) ffp::walk(w, id_old);
        QueryPerformanceCounter(&t1);
        check(w.status == fp::Walk::Found && rows == before, "timed walks found, no row");
        const double found_ns = double(t1.QuadPart - t0.QuadPart) * 1e9 / double(f.QuadPart) / n;
        put(slot(ffp::recovery_guard_slot_rva), std::uint32_t(1));
        QueryPerformanceCounter(&t0);
        for (unsigned i = 0; i < n; ++i) ffp::walk(w, id_old);
        QueryPerformanceCounter(&t1);
        put(slot(ffp::recovery_guard_slot_rva), std::uint32_t(0));
        check(w.status == fp::Walk::Guarded && rows == before, "timed guarded walks, no row");
        const double guarded_ns = double(t1.QuadPart - t0.QuadPart) * 1e9 / double(f.QuadPart) / n;
        std::printf("TIMING found_ns=%.1f guarded_ns=%.1f walks=%u\n", found_ns, guarded_ns, n);
    }
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
