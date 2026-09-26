// Host fixture: synthetic x86 address space, no game bytes or Windows runtime.
#include "../../src/proxy/compositor_owner.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
using namespace x3m::compositor_owner;
struct Memory {
    std::map<std::uint32_t, std::vector<unsigned char>> blocks;
    unsigned reads = 0, fail_at = 0;
    void words(std::uint32_t at, std::initializer_list<std::uint32_t> values) {
        std::vector<unsigned char> bytes(values.size() * 4);
        std::memcpy(bytes.data(), values.begin(), bytes.size());
        blocks[at] = bytes;
    }
    bool operator()(std::uint32_t at, void* out, std::size_t size) {
        if (++reads == fail_at) {
            if (size) *static_cast<unsigned char*>(out) = 0xff;
            return false;
        }
        const auto i = blocks.find(at);
        if (i == blocks.end() || i->second.size() != size) return false;
        std::memcpy(out, i->second.data(), size);
        return true;
    }
};
static Memory normal() {
    Memory m;
    m.words(0x608b3c, {0x10000000});
    m.words(0x10000018, {0x20000000, 0x30000000});
    m.words(0x20000000, {0x40000000});
    m.words(0x30000004, {0x40000000});
    return m;
}
static bool empty(const Snapshot& s) {
    return !s.renderer && !s.record && !s.device && !s.manager && !s.manager_device;
}
int main() {
    unsigned checks = 0, failures = 0;
    auto check = [&](bool ok, const char* name) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("FAIL %s\n", name);
        }
    };
    auto memory = normal();
    Snapshot s{};
    check(detail::collect(0x400000, s, memory) == Result::Ok && memory.reads == 4, "exact owner / bounded reads");
    check(s.device == 0x40000000 && s.renderer == 0x10000000 && s.record == 0x20000000 && s.manager == 0x30000000 &&
              same(s, s),
          "fields and self comparison");
    const Snapshot valid = s;
    for (unsigned i = 1; i <= 4; ++i) {
        memory = normal();
        memory.fail_at = i;
        s = valid;
        check(detail::collect(0x400000, s, memory) == Result::Unreadable && empty(s) && memory.reads == i,
              "failed or partial read clears output / stops");
    }
    for (std::uintptr_t base : {std::uintptr_t(0), std::uintptr_t(UINT32_MAX)}) {
        memory = normal();
        s = valid;
        check(detail::collect(base, s, memory) == Result::InvalidAddress && empty(s) && memory.reads == 0,
              "invalid image span");
    }
    for (std::uint32_t renderer : {0u, UINT32_MAX - 0x1eu}) {
        memory = normal();
        memory.words(0x608b3c, {renderer});
        s = valid;
        check(detail::collect(0x400000, s, memory) == Result::InvalidAddress && empty(s) && memory.reads == 1,
              "invalid renderer span");
    }
    for (unsigned index = 0; index < 2; ++index)
        for (std::uint32_t pointer : {0u, UINT32_MAX}) {
            memory = normal();
            memory.words(0x10000018, {index == 0 ? pointer : 0x20000000, index == 1 ? pointer : 0x30000000});
            s = valid;
            check(detail::collect(0x400000, s, memory) == Result::InvalidAddress && empty(s) && memory.reads == 2,
                  "invalid record/manager span");
        }
    for (std::uint32_t device : {0u, 0x40000004u}) {
        memory = normal();
        memory.words(0x20000000, {device});
        s = valid;
        check(detail::collect(0x400000, s, memory) == Result::OwnerMismatch && empty(s), "inconsistent device");
    }
    for (auto member :
         {&Snapshot::renderer, &Snapshot::record, &Snapshot::device, &Snapshot::manager, &Snapshot::manager_device}) {
        s = valid;
        s.*member += 4;
        check(!same(valid, s) && !same(s, valid), "owner identity change");
    }
    s = valid;
    s.device += 4;
    s.manager_device += 4;
    check(!same(valid, s), "consistent but replaced device");
    check(!same({}, {}), "empty observations are not owners");
    // Rebase only the image global; heap links retain their actual addresses.
    memory = normal();
    memory.blocks[0x708b3c] = memory.blocks[0x608b3c];
    memory.blocks.erase(0x608b3c);
    check(detail::collect(0x500000, s, memory) == Result::Ok && same(valid, s), "admitted image base");
    std::printf("compositor_owner checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
