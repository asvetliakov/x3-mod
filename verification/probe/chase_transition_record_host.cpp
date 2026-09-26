// Exercise actual record/snapshot/suppression code with controlled memory.
#include "chase_transition_core.h"
#include "chase_transition_identity_core.h"
#include <atomic>
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace x3m::chase_transition;
static unsigned checks = 0;
static void check(bool ok, const char* why) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", why);
        std::exit(1);
    }
}
std::atomic<bool> diagnostic{true};
detail::State state;
#include "chase_transition_record_declarations_inc.h"
static std::array<unsigned char, 0x1000> memory{};
static unsigned supplemental_reads = 0, refused = 0;
bool bytes(std::uintptr_t b, unsigned off, void* out, unsigned n) {
    if (b == 0x6085e4) {
        ++supplemental_reads;
        return false;
    }
    if (b != 0x1000 || off == refused || off >= memory.size() || n > memory.size() - off) return false;
    if (off == 0xa8 || off == 0x160 || off == 0x120) ++supplemental_reads;
    std::memcpy(out, memory.data() + off, n);
    return true;
}
template <class T> bool field(std::uintptr_t b, unsigned o, T& out) {
    return bytes(b, o, &out, sizeof out);
}
static void put(unsigned o, std::uint32_t v) {
    std::memcpy(memory.data() + o, &v, 4);
}
bool active = true;
bool active_handle(std::uintptr_t c, std::uint32_t& h) {
    h = 3;
    return active && c == 0x1000;
}
bool code_address(std::uint32_t, std::uint32_t, unsigned, std::uintptr_t&) {
    return false;
}
std::uint64_t now() {
    return 100;
}
#include "chase_transition_record_functions_inc.h"
int main() {
    state.construct(0x1000, 1);
    put(0xa8, 11);
    put(0xac, 22);
    put(0xb0, 33);
    put(0x160, 44);
    put(0x164, 55);
    put(0x168, 66);
    put(0x120, 77);
    put(0x130, 88);
    put(0x134, 99);
    put(0x138, 111);
    record(0, 0x1000, 1);
    check(!window.first[0].before.valid && !window.first[0].geometry.valid,
          "constructor entry never reads partial geometry");
    state.complete(0x1000, 1);
    record(3, 0x1000, 1);
    const auto& e = window.first[1];
    check(e.geometry.valid == 7 && e.geometry.angles[0] == 11 && e.geometry.angles[2] == 33 &&
              e.geometry.offset[0] == 44 && e.geometry.offset[2] == 66 && e.geometry.lock == 77,
          "all missing geometry offsets captured");
    check(e.before.boom[0] == 88 && e.before.boom[2] == 111 && (e.before.valid & 128),
          "existing boom remains sampled with own validity");
    const auto reads = supplemental_reads;
    for (unsigned i = 0; i < 100; ++i) {
        put(0xa8, 1000 + i);
        record(3, 0x1000, 1);
    }
    check(supplemental_reads == reads && window.first_used == 2 && suppressed == 100,
          "animated geometry does not defeat suppression or run identity walks per update");
    put(0x150, 258);
    record(3, 0x1000, 1);
    check(window.first_used == 3 && window.first[2].geometry.angles[0] == 1099 && supplemental_reads == reads + 4,
          "changed transition refreshes fresh geometry and identity once");
    refused = 0x120;
    put(0x150, 1);
    record(3, 0x1000, 1);
    check(window.first[3].geometry.valid == 3 && window.first[3].geometry.lock == 0,
          "failed lock read is explicitly invalid, not valid zero");
    check(window.first[3].identity.refused & detail::Identity::vm_bit, "unreadable VM is an explicit refused identity");
    refused = 0;
    active = false;
    record(2, 0x1000, 1, 0x41ccfb);
    check(window.first[4].geometry.valid == 7 && !window.first[4].valid && !window.first[4].handle,
          "unregistered live destructor still observes geometry");
    std::printf("chase record host: %u checks PASS\n", checks);
}
