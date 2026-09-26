#include "../../src/proxy/chase_transition_identity_core.h"
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace x3m::chase_transition::detail;
static unsigned checks = 0;
static void check(bool ok, const char* why) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", why);
        std::exit(1);
    }
}
struct Memory {
    std::array<unsigned char, 0x20000> data{};
    unsigned reads = 0;
    std::uint32_t refused = 0;
    template <class T> void put(unsigned at, T value) { std::memcpy(data.data() + at, &value, sizeof value); }
    bool read(std::uint32_t base, unsigned off, void* out, unsigned n) {
        ++reads;
        if (base == 0x6085e4 && off == 0 && n == 4) {
            std::uint32_t p = 0x4000;
            std::memcpy(out, &p, 4);
            return true;
        }
        if (base == 0x60850c && off == 0 && n == 4) {
            std::uint32_t p = 0xa000;
            std::memcpy(out, &p, 4);
            return true;
        }
        const auto at = std::uint64_t(base) + off;
        if (at == refused || at < 0x1000 || at >= data.size() || n > data.size() - at) return false;
        std::memcpy(out, data.data() + at, n);
        return true;
    }
    void cell(unsigned at, std::uint32_t value, unsigned char tag = 1) {
        data[at] = tag;
        put(at + 1, value);
    }
    void descriptor(unsigned at, unsigned id, unsigned cells, unsigned n) {
        put(at, id);
        put(at + 8, at);
        put(at + 12, cells);
        put(at + 28, n);
    }
    void init() {
        data.fill(0);
        reads = 0;
        refused = 0;
        put(0x401c, 3u);
        put(0x4020, 0x5000u);
        put(0x52d0, 0x8000u); // VM dynamic table
        descriptor(0x5000, 0, 0x7000, 12);
        descriptor(0x5038, 0x96, 0x7100, 8);
        descriptor(0x5070, 0x25e, 0, 12);
        cell(0x7000 + 5 * 9, 0xfffffffbu);
        cell(0x7000 + 5 * 8, 0xfffffffau);
        cell(0x7100 + 5 * 3, 1);
        cell(0x7100 + 5 * 6, 0);
        put(0x8000, 0x8040u);
        put(0x8004, 4u);
        for (unsigned i = 0; i < 3; ++i) {
            unsigned id = 0xfffffffbu - i, ctx = 0x6000 + 0x100 * i, row = 0x8100 + 0x10 * i;
            put(0x8040 + 4 * i, row);
            put(row + 4, ~id);
            put(row + 8, ctx);
            put(ctx, id);
            put(ctx + 8, i == 2 ? 0x5070u : 0x5038u);
        }
        put(0x620c, 0x7200u);
        cell(0x7200, 258);
        cell(0x7200 + 55, 0xfffffffbu);
        put(0x9010, 0u);
        put(0x9008, 22u);
        put(0x9094, 0xfffffffbu);
        put(0xa014, 0xa020u);
        put(0xa020, 0xa040u);
        put(0xa024, 1u);
        put(0xa040, 0xa080u);
        put(0xa084, 22u);
        put(0xa088, 0x9000u);
    }
    Identity capture(std::uint32_t ship = 0x9000, std::uint32_t monitor = 0x6200) {
        auto reader = [&](std::uint32_t b, unsigned o, void* out, unsigned n) { return read(b, o, out, n); };
        IdentityReader<decltype(reader)> walk{reader};
        Identity result;
        walk.capture(ship, monitor, result);
        return result;
    }
};
int main() {
    using I = Identity;
    Memory m;
    m.init();
    auto good = m.capture();
    const auto ordinary_reads = m.reads;
    check(good.valid == 4095 && !good.refused, "all fresh memberships and six integer cells validate");
    check(good.native_script == good.player && good.player == good.ref && good.mode == 258 && good.warp == 1,
          "identity equality and native/script geometry baseline measurable");
    check(m.reads < 100, "ordinary admitted snapshot has bounded small read count");
    auto absent = m.capture(0, 0);
    check(!(absent.valid & (I::native_bit | I::monitor_bit | I::mode_bit | I::ref_bit)) &&
              !(absent.refused & (I::native_bit | I::monitor_bit)),
          "missing ship/context unavailable, not fake zero");
    m.init();
    m.cell(0x7100 + 15, 0);
    check((m.capture().valid & I::warp_bit) && m.capture().warp == 0, "real zero warp remains valid");
    m.init();
    m.cell(0x7000 + 45, 0);
    auto zero = m.capture();
    check((zero.valid & I::player_bit) && !(zero.valid & I::player_live_bit) && (zero.refused & I::player_live_bit),
          "zero scalar does not resolve globals as live player");
    for (unsigned tag : {0u, 2u, 3u, 10u, 255u}) {
        m.init();
        m.cell(0x7000 + 45, 0xfffffffbu, tag);
        auto bad = m.capture();
        check(!(bad.valid & I::player_bit) && (bad.refused & I::player_bit) && bad.tags[0] == tag,
              "noninteger tag logged and refused without dereference");
    }
    m.init();
    m.put(0x401c, 4097u);
    check(!(m.capture().valid & I::vm_bit), "class capacity refuses");
    m.init();
    m.put(0x4020, 0xfffffff0u);
    check(!(m.capture().valid & I::vm_bit), "class array arithmetic refuses");
    m.init();
    m.put(0x5008, 0x5038u);
    check(!(m.capture().valid & I::player_bit), "global self descriptor check");
    m.init();
    m.put(0x501c, 9u);
    auto short_global = m.capture();
    check(!(short_global.valid & I::player_bit) && (short_global.valid & I::controller_bit),
          "cell count enforces last index");
    m.init();
    m.put(0x500c, 0xfffffff0u);
    check(!(m.capture().valid & I::player_bit), "cell address overflow refuses");
    m.init();
    m.put(0x8010, 0u);
    m.put(0x8004, 3u);
    check(!(m.capture().valid & I::player_live_bit), "hash bucket count must be power of two");
    m.init();
    m.put(0x8100, 0x8100u);
    check(!(m.capture().valid & I::player_live_bit) && m.reads < 220, "cyclic matching hash refuses in fixed budget");
    m.init();
    m.put(0x8100, 0x8200u);
    m.put(0x8204, 4u);
    m.put(0x8208, 0x6000u);
    check(!(m.capture().valid & I::player_live_bit), "duplicate matching key refuses");
    m.init();
    m.put(0x8108, 0x6300u);
    check(!(m.capture().valid & I::player_live_bit), "context ID must match registry key");
    m.init();
    m.put(0x6008, 0u);
    check(!(m.capture().valid & I::player_live_bit), "dead context refuses");
    m.init();
    m.put(0x6008, 0x503cu);
    check(!(m.capture().valid & I::player_live_bit), "class descriptor must lie exactly on registered row");
    m.init();
    m.put(0x6208, 0x5038u);
    check(!(m.capture().valid & I::monitor_bit), "foreign current class cannot be monitor vars");
    m.init();
    m.put(0x6220, 0u);
    m.put(0x5070 + 28, 11u);
    check(!(m.capture().valid & I::ref_bit), "monitor cell eleven count checked");
    m.init();
    m.refused = 0x7200 + 55;
    check(!(m.capture().valid & I::ref_bit), "unreadable variable distinguished from zero");
    m.init();
    m.put(0xa088, 0x9004u);
    check(!(m.capture().valid & I::native_bit), "native body membership requires exact pointer");
    m.init();
    m.refused = 0x9094;
    check(!(m.capture().valid & I::native_bit), "native script field must be readable");
    m.init();
    m.put(0x5038, 0x97u);
    check(!(m.capture().valid & I::warp_bit), "exact warp class is mandatory");
    m.init();
    m.put(0x5038 + 28, 65537u);
    check(!(m.capture().valid & I::warp_bit), "class variable count capped");
    std::printf("chase identity host: %u checks PASS ordinary_reads=%u\n", checks, ordinary_reads);
}
