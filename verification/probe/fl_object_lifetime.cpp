// Original synthetic fixture of the FL registry observer (src/engine/fl/object_lifetime_fl.cpp,
// docs/architecture/farnhams-legacy-lifetime.md): no game code, no game bytes, no launch.
// Built with -DX3M_EDITION_FL=1 -DX3M_OBJECT_LIFETIME_FIXTURE (verification/analysis/test_fl_object_lifetime.py).
//
// Four fake engine functions carry the FL entry bytes and ABI (object-lifetimes.md, "Four candidate spans"):
// insert `push ebx; mov ebx,edx; push edi; mov edi,ecx` (ECX map, EDX key, [esp+4] value), remove
// `push esi; push edi; mov edi,ecx; mov esi,edx`, destroy `push ebx; mov ebx,ecx; cmp dword [ebx],0` followed by
// the JE the replayed ZF decides, and a renderer-load target reached through a `call rel32` site placed four bytes
// into a qword (as X3FL.exe's, so the claim takes the plain-write path). All end in a plain RET. They operate on a
// fake map with the FL layout (16-byte header, 12-byte hash nodes, bucket = key & (count-1), growth x4). A witness
// caller loads sentinels into ESI/EDI/EBX, XMM0-7, the x87 control word, MXCSR, EFLAGS and LastError; the fake
// targets record what reached them and return their own ECX/EDX/EFLAGS/XMM0-7/x87 ST0/control word/MXCSR.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <excpt.h>
#include <csetjmp>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "../../src/proxy/object_lifetime.h"
#include "../../src/proxy/engine_memory.h"
#include "../../src/proxy/engine_patch.h"
namespace layout {
#include "../../src/engine/fl/object_lifetime_fl_inc.h"
}
namespace ol = x3m::object_lifetime;
unsigned checks = 0, failures = 0, calls = 0;
const char* context = "";
void check(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s%s%s\n", context, *context ? ": " : "", label);
    }
}
char last_row[4096];
unsigned rows = 0;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(last_row, sizeof last_row, format, args);
    va_end(args);
    ++rows;
    std::printf("LOG %s\n", last_row);
}
}
bool row_has(const char* text) {
    return std::strstr(last_row, text) != nullptr;
}

// ---- fake FL map -------------------------------------------------------------------------------------------
struct FakeNode {
    uint32_t next, key, value;
};
struct FakeMap {
    uint32_t buckets, count, counter, entries;
};
FakeMap* new_map() {
    auto* m = static_cast<FakeMap*>(std::calloc(1, sizeof(FakeMap)));
    m->count = 8;
    m->counter = 1;
    return m;
}
void rehash(FakeMap* m, uint32_t count) {
    auto* fresh = static_cast<uint32_t*>(std::calloc(count, 4));
    auto* old = reinterpret_cast<uint32_t*>(m->buckets);
    for (uint32_t b = 0; b < m->count; ++b)
        for (auto* n = reinterpret_cast<FakeNode*>(old[b]); n;) {
            auto* next = reinterpret_cast<FakeNode*>(n->next);
            n->next = fresh[n->key & (count - 1)];
            fresh[n->key & (count - 1)] = reinterpret_cast<uint32_t>(n);
            n = next;
        }
    m->count = count;
    m->buckets = reinterpret_cast<uint32_t>(fresh);
    std::free(old);
}
uint32_t fake_insert(FakeMap* m, uint32_t key, uint32_t value) {
    if (!key) return 0;
    if (!m->buckets) m->buckets = reinterpret_cast<uint32_t>(std::calloc(m->count, 4));
    auto* heads = reinterpret_cast<uint32_t*>(m->buckets);
    for (auto* n = reinterpret_cast<FakeNode*>(heads[key & (m->count - 1)]); n; n = reinterpret_cast<FakeNode*>(n->next))
        if (n->key == key) {
            n->value = value;
            return 0; // replacement returns 0, as X3FL.exe's
        }
    auto* n = static_cast<FakeNode*>(std::calloc(1, sizeof(FakeNode)));
    n->key = key;
    n->value = value;
    n->next = heads[key & (m->count - 1)];
    heads[key & (m->count - 1)] = reinterpret_cast<uint32_t>(n);
    if (++m->entries >= m->count) rehash(m, m->count * 4);
    return 1;
}
uint32_t fake_remove(FakeMap* m, uint32_t key) {
    if (!m->buckets) return 0;
    auto* link = reinterpret_cast<uint32_t*>(m->buckets) + (key & (m->count - 1));
    for (auto* n = reinterpret_cast<FakeNode*>(*link); n; link = &n->next, n = reinterpret_cast<FakeNode*>(*link))
        if (n->key == key) {
            *link = n->next;
            const uint32_t value = n->value;
            std::memset(n, 0xdd, sizeof *n);
            std::free(n);
            --m->entries;
            return value;
        }
    return 0;
}
void fake_destroy(FakeMap* m) {
    auto* heads = reinterpret_cast<uint32_t*>(m->buckets);
    for (uint32_t b = 0; heads && b < m->count; ++b)
        for (auto* n = reinterpret_cast<FakeNode*>(heads[b]); n;) {
            auto* next = reinterpret_cast<FakeNode*>(n->next);
            std::free(n);
            n = next;
        }
    std::free(heads);
    m->buckets = m->entries = 0;
    m->count = 0; // the real destroy frees the map itself; the fixture keeps the header (zeroed) readable
}

// ---- witness state (absolute symbols used by the asm) -----------------------------------------------------
extern "C" {
uint32_t esp_before = 0, esp_after = 0, ebp_before = 0, ebp_after = 0, esi_after = 0, edi_after = 0, ebx_after = 0,
         ecx_after = 0, edx_after = 0, flags_after = 0, flags_out = 0x202;
uint32_t entry_esi = 0, entry_edi = 0, entry_ebx = 0, destroy_path = 0;
uint16_t entry_cw = 0, cw_out = 0x037f, cw_after = 0;
uint32_t entry_mxcsr = 0, mxcsr_out = 0x1f80, mxcsr_after = 0;
double x87_out = 0, x87_after = 0;
void* call_target = nullptr;
alignas(16) uint32_t xmm_in[32], xmm_at_target[32], xmm_out[32], xmm_after[32];
uint32_t __cdecl fl_insert_body(FakeMap* map, uint32_t key, uint32_t value);
uint32_t __cdecl fl_remove_body(FakeMap* map, uint32_t key);
uint32_t __cdecl fl_destroy_body(FakeMap* map);
uint32_t __cdecl fl_load_body(uint32_t stream);
uint32_t __cdecl fl_call(void* fn, uint32_t ecx, uint32_t edx, uint32_t word, uint32_t words, uint32_t flags);
uint32_t __cdecl fl_raw(void* fn, uint32_t ecx, uint32_t edx, uint32_t word);
void fl_fake_insert();
void fl_fake_remove();
void fl_fake_destroy();
void fl_fake_load();
}
#define WITNESS_ENTRY                                                                                                  \
    "movups %xmm0,_xmm_at_target\n\tmovups %xmm1,_xmm_at_target+16\n\tmovups %xmm2,_xmm_at_target+32\n\t"              \
    "movups %xmm3,_xmm_at_target+48\n\tmovups %xmm4,_xmm_at_target+64\n\tmovups %xmm5,_xmm_at_target+80\n\t"           \
    "movups %xmm6,_xmm_at_target+96\n\tmovups %xmm7,_xmm_at_target+112\n\tfnstcw _entry_cw\n\tstmxcsr _entry_mxcsr\n\t"
// Output state: XMM0-7, ECX/EDX sentinels, control word, MXCSR, one value in ST0; EFLAGS last (pops keep them).
#define WITNESS_EXIT                                                                                                   \
    "movups _xmm_out,%xmm0\n\tmovups _xmm_out+16,%xmm1\n\tmovups _xmm_out+32,%xmm2\n\tmovups _xmm_out+48,%xmm3\n\t"     \
    "movups _xmm_out+64,%xmm4\n\tmovups _xmm_out+80,%xmm5\n\tmovups _xmm_out+96,%xmm6\n\tmovups _xmm_out+112,%xmm7\n\t" \
    "fldcw _cw_out\n\tldmxcsr _mxcsr_out\n\tfldl _x87_out\n\tmovl $0x0cc0ffee,%ecx\n\tmovl $0xd00dfeed,%edx\n\t"       \
    "pushl _flags_out\n\tpopfl\n\t"
asm(".text\n\t.p2align 3\n\t.globl _fl_fake_insert\n_fl_fake_insert:\n\t"
    ".byte 0x53,0x8b,0xda,0x57,0x8b,0xf9\n\t" // push ebx; mov ebx,edx; push edi; mov edi,ecx
    "movl %esi,_entry_esi\n\tmovl (%esp),%eax\n\tmovl %eax,_entry_edi\n\tmovl 4(%esp),%eax\n\tmovl %eax,_entry_ebx\n\t"
    WITNESS_ENTRY "pushl %esi\n\tpushl 16(%esp)\n\tpushl %ebx\n\tpushl %edi\n\tcall _fl_insert_body\n\taddl $12,%esp\n\t"
    "popl %esi\n\t" WITNESS_EXIT "popl %edi\n\tpopl %ebx\n\tret\n\t"
    ".p2align 3\n\t.globl _fl_fake_remove\n_fl_fake_remove:\n\t"
    ".byte 0x56,0x57,0x8b,0xf9,0x8b,0xf2\n\t" // push esi; push edi; mov edi,ecx; mov esi,edx
    "movl 4(%esp),%eax\n\tmovl %eax,_entry_esi\n\tmovl (%esp),%eax\n\tmovl %eax,_entry_edi\n\tmovl %ebx,_entry_ebx\n\t"
    WITNESS_ENTRY "pushl %esi\n\tpushl %edi\n\tcall _fl_remove_body\n\taddl $8,%esp\n\t" WITNESS_EXIT
    "popl %edi\n\tpopl %esi\n\tret\n\t"
    ".p2align 3\n\t.globl _fl_fake_destroy\n_fl_fake_destroy:\n\t"
    ".byte 0x53,0x8b,0xd9,0x83,0x3b,0x00\n\t" // push ebx; mov ebx,ecx; cmp dword [ebx],0
    "je 1f\n\tmovl $2,_destroy_path\n\tjmp 2f\n1:\n\tmovl $1,_destroy_path\n2:\n\t"
    "movl %esi,_entry_esi\n\tmovl %edi,_entry_edi\n\tmovl (%esp),%eax\n\tmovl %eax,_entry_ebx\n\t" WITNESS_ENTRY
    "pushl %ebx\n\tcall _fl_destroy_body\n\taddl $4,%esp\n\t" WITNESS_EXIT "popl %ebx\n\tret\n\t"
    ".p2align 3\n\t.globl _fl_fake_load\n_fl_fake_load:\n\t"
    ".byte 0x55,0x8b,0xec,0x83,0xe4,0xf0\n\t" // push ebp; mov ebp,esp; and esp,-16
    "pushl %ebx\n\tpushl %esi\n\tpushl %edi\n\tmovl %esi,_entry_esi\n\tmovl %edi,_entry_edi\n\tmovl %ebx,_entry_ebx\n\t"
    WITNESS_ENTRY "pushl %ecx\n\tcall _fl_load_body\n\taddl $4,%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\t"
    WITNESS_EXIT "movl %ebp,%esp\n\tpopl %ebp\n\tret\n\t"
    // fl_call(fn, ecx, edx, word, words, flags): sentinels in, `words` (0/1) pushed and removed by the caller,
    // EFLAGS from `flags` just before the call; everything the target returned is stored.
    ".globl _fl_call\n_fl_call:\n\tpushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
    "movl 8(%ebp),%eax\n\tmovl %eax,_call_target\n\t"
    "movups _xmm_in,%xmm0\n\tmovups _xmm_in+16,%xmm1\n\tmovups _xmm_in+32,%xmm2\n\tmovups _xmm_in+48,%xmm3\n\t"
    "movups _xmm_in+64,%xmm4\n\tmovups _xmm_in+80,%xmm5\n\tmovups _xmm_in+96,%xmm6\n\tmovups _xmm_in+112,%xmm7\n\t"
    "cmpl $0,24(%ebp)\n\tje 1f\n\tpushl 20(%ebp)\n1:\n\t"
    "movl 12(%ebp),%ecx\n\tmovl 16(%ebp),%edx\n\tmovl $0x5a5a1111,%esi\n\tmovl $0x5a5a2222,%edi\n\tmovl $0x5a5a3333,%ebx\n\t"
    "movl %esp,_esp_before\n\tmovl %ebp,_ebp_before\n\tpushl 28(%ebp)\n\tpopfl\n\tcall *_call_target\n\t"
    "pushfl\n\tpopl _flags_after\n\tmovl %esp,_esp_after\n\tmovl %ebp,_ebp_after\n\tmovl %ecx,_ecx_after\n\t"
    "movl %edx,_edx_after\n\tmovl %esi,_esi_after\n\tmovl %edi,_edi_after\n\tmovl %ebx,_ebx_after\n\t"
    "fnstcw _cw_after\n\tstmxcsr _mxcsr_after\n\tfstpl _x87_after\n\t"
    "movups %xmm0,_xmm_after\n\tmovups %xmm1,_xmm_after+16\n\tmovups %xmm2,_xmm_after+32\n\tmovups %xmm3,_xmm_after+48\n\t"
    "movups %xmm4,_xmm_after+64\n\tmovups %xmm5,_xmm_after+80\n\tmovups %xmm6,_xmm_after+96\n\tmovups %xmm7,_xmm_after+112\n\t"
    "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t"
    // fl_raw(fn, ecx, edx, word): one engine-style call (births inside the load, nested inserts), ST0 dropped.
    ".globl _fl_raw\n_fl_raw:\n\tpushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
    "pushl 20(%ebp)\n\tmovl 12(%ebp),%ecx\n\tmovl 16(%ebp),%edx\n\tcall *8(%ebp)\n\taddl $4,%esp\n\tfstp %st(0)\n\t"
    "popl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");

// ---- engine model ------------------------------------------------------------------------------------------
uint32_t engine[8]{}; // +0xc: the registry
uintptr_t engine_pointer = 0;
FakeMap* registry_map = nullptr;
uint32_t nodes[24][0x270 / 4]{}, cameras[4][0x790 / 4]{};
uintptr_t node(unsigned i) {
    return reinterpret_cast<uintptr_t>(nodes[i]);
}
uintptr_t cam(unsigned i) {
    return reinterpret_cast<uintptr_t>(cameras[i]);
}
void* load_thunk = nullptr; // `call rel32 fl_fake_load; ret`, the call four bytes into a qword
enum Mode : unsigned { Plain, Nested, Raise, Timing };
Mode mode = Plain;
uint32_t seen[3]{};
DWORD seen_error = 0;
// What the observer reported from inside the engine call (before the engine's own mutation).
struct Inside {
    unsigned live = 0;
    ol::Reason reason = ol::Reason::Disabled;
    uint64_t load_epoch = 0;
} inside;
uintptr_t probe_node = 0, probe_camera = 0;
uint32_t probe_handle = 0, probe_camera_handle = 0;
void take_inside() {
    const auto s = ol::stats();
    inside.live = s.live;
    inside.load_epoch = s.load_epoch;
    ol::Snapshot snap{};
    ol::current(reinterpret_cast<uintptr_t>(registry_map), probe_node, probe_handle, probe_camera, probe_camera_handle,
                &snap);
    inside.reason = snap.reason;
}
// A nested engine call overwrites the at-target witness words: kept around it.
struct Saved {
    uint32_t esi, edi, ebx, s[3];
    uint16_t cw;
    uint32_t mxcsr, xmm[32];
    DWORD error;
};
Saved save() {
    Saved v{entry_esi, entry_edi, entry_ebx, {seen[0], seen[1], seen[2]}, entry_cw, entry_mxcsr, {}, seen_error};
    std::memcpy(v.xmm, xmm_at_target, sizeof v.xmm);
    return v;
}
void restore(const Saved& v) {
    entry_esi = v.esi, entry_edi = v.edi, entry_ebx = v.ebx, entry_cw = v.cw, entry_mxcsr = v.mxcsr, seen_error = v.error;
    std::memcpy(seen, v.s, sizeof seen);
    std::memcpy(xmm_at_target, v.xmm, sizeof v.xmm);
}
uint32_t birth(FakeMap* map, uint32_t key, uintptr_t object) {
    const uint32_t r = fl_raw(reinterpret_cast<void*>(&fl_fake_insert), reinterpret_cast<uint32_t>(map), key, object);
    reinterpret_cast<uint32_t*>(object)[layout::node_handle / 4] = key; // the handle is written after insert returns
    return r;
}
extern "C" uint32_t __cdecl fl_insert_body(FakeMap* map, uint32_t key, uint32_t value) {
    ++calls;
    seen_error = GetLastError();
    seen[0] = reinterpret_cast<uint32_t>(map), seen[1] = key, seen[2] = value;
    if (mode == Timing) return fake_insert(map, key, value);
    take_inside();
    if (mode == Nested) {
        const Saved outer = save();
        const Inside outer_inside = inside;
        mode = Plain;
        birth(map, key + 1000, node(20));
        mode = Nested;
        restore(outer);
        inside = outer_inside;
    }
    if (mode == Raise) RaiseException(0xe3450001, 0, 0, nullptr);
    const uint32_t r = fake_insert(map, key, value);
    SetLastError(0x246);
    return r;
}
extern "C" uint32_t __cdecl fl_remove_body(FakeMap* map, uint32_t key) {
    ++calls;
    seen_error = GetLastError();
    seen[0] = reinterpret_cast<uint32_t>(map), seen[1] = key, seen[2] = 0;
    if (mode == Timing) return fake_remove(map, key);
    take_inside();
    const uint32_t r = fake_remove(map, key); // the node itself is freed by its destructor after this returns
    SetLastError(0x246);
    return r;
}
extern "C" uint32_t __cdecl fl_destroy_body(FakeMap* map) {
    ++calls;
    seen_error = GetLastError();
    seen[0] = reinterpret_cast<uint32_t>(map), seen[1] = seen[2] = 0;
    take_inside();
    fake_destroy(map);
    SetLastError(0x246);
    return 0x5eed;
}
// The renderer load: inside it the observer is mid-load; it reconstructs node 6 (key 60) and camera 2 (key 61) and
// releases key 8 (node 8). Every other registry key survives it.
extern "C" uint32_t __cdecl fl_load_body(uint32_t stream) {
    ++calls;
    seen_error = GetLastError();
    seen[0] = stream, seen[1] = seen[2] = 0;
    take_inside();
    const Saved outer = save();
    const Inside load_inside = inside;
    birth(registry_map, 60, node(6));
    birth(registry_map, 61, cam(2));
    fl_raw(reinterpret_cast<void*>(&fl_fake_remove), reinterpret_cast<uint32_t>(registry_map), 8, 0);
    restore(outer);
    inside = load_inside;
    SetLastError(0x246);
    return 1;
}
// One call through fl_call with the full witness. flags_in reaches the target's entry, target_flags is what it
// returns in EFLAGS.
uint32_t witnessed(void* fn, uint32_t ecx, uint32_t edx, uint32_t word, unsigned words, uint32_t flags_in,
                   uint32_t target_flags, const char* what) {
    context = what;
    static unsigned salt = 0;
    ++salt;
    for (unsigned i = 0; i < 32; ++i) {
        xmm_in[i] = 0xa5000000u + i + salt * 64;
        xmm_out[i] = 0x5a000000u + i + salt * 64;
    }
    flags_out = target_flags;
    cw_out = 0x007f; // 24-bit precision on return
    x87_out = 1.25 + salt;
    uint16_t cw = 0, test_cw = 0x027f; // 53-bit precision on entry
    uint32_t mxcsr = 0;
    asm volatile("fnstcw %0\n\tstmxcsr %1" : "=m"(cw), "=m"(mxcsr));
    const uint32_t test_mxcsr = (mxcsr & ~0x6000u) | 0x6000u; // round toward zero
    mxcsr_out = (mxcsr & ~0x6000u) | 0x2000u;                 // round down
    x3m::engine_memory::reset(); // the observer's first read in enter() makes a VirtualQuery (XMM0-5 change natively)
    SetLastError(0x145);
    asm volatile("fldcw %0\n\tldmxcsr %1" ::"m"(test_cw), "m"(test_mxcsr));
    const uint32_t result = fl_call(fn, ecx, edx, word, words, flags_in);
    const DWORD error = GetLastError();
    asm volatile("fldcw %0\n\tldmxcsr %1" ::"m"(cw), "m"(mxcsr));
    check(error == 0x246, "target LastError returned");
    check(seen_error == 0x145, "caller LastError reaches the target");
    check(esp_after == esp_before && ebp_after == ebp_before, "stack balanced (plain RET), EBP kept");
    check(esi_after == 0x5a5a1111 && edi_after == 0x5a5a2222 && ebx_after == 0x5a5a3333, "ESI/EDI/EBX kept");
    check(entry_esi == 0x5a5a1111 && entry_edi == 0x5a5a2222 && entry_ebx == 0x5a5a3333, "ESI/EDI/EBX reach the target");
    check(ecx_after == 0x0cc0ffee && edx_after == 0xd00dfeed, "returned ECX/EDX kept");
    check((flags_after & 0x8d5) == (target_flags & 0x8d5), "returned EFLAGS kept");
    check(!std::memcmp(xmm_at_target, xmm_in, sizeof xmm_in), "XMM0-7 reach the target");
    check(!std::memcmp(xmm_after, xmm_out, sizeof xmm_out), "XMM0-7 of the return reach the caller");
    check(entry_cw == test_cw && entry_mxcsr == test_mxcsr, "x87 control word and MXCSR reach the target");
    check(cw_after == cw_out && mxcsr_after == mxcsr_out && x87_after == x87_out,
          "returned control word, MXCSR and ST0 reach the caller");
    check(seen[0] == ecx, "ECX (map or stream) forwarded");
    if (fn == reinterpret_cast<void*>(&fl_fake_insert) || fn == reinterpret_cast<void*>(&fl_fake_remove))
        check(seen[1] == edx, "EDX (the key) forwarded");
    if (fn == reinterpret_cast<void*>(&fl_fake_insert)) check(seen[2] == word, "[esp+4] (the value) forwarded");
    context = "";
    return result;
}
ol::Snapshot query(uintptr_t map, uintptr_t n, uint32_t handle, uintptr_t c, uint32_t camera_handle) {
    ol::Snapshot s{};
    SetLastError(0x77);
    ol::current(map, n, handle, c, camera_handle, &s);
    check(GetLastError() == 0x77, "current() preserves LastError");
    return s;
}
uintptr_t reg() {
    return reinterpret_cast<uintptr_t>(registry_map);
}
// Foreign SEH unwind: an outer original frame catches an exception raised inside the target.
std::jmp_buf outer_jump;
extern "C" __attribute__((force_align_arg_pointer)) EXCEPTION_DISPOSITION __cdecl fl_fixture_handler(
    EXCEPTION_RECORD* record, void* frame, CONTEXT*, void*) {
    if (!(record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) &&
        record->ExceptionCode == 0xe3450001) {
        RtlUnwind(frame, nullptr, nullptr, nullptr);
        void* previous = *static_cast<void**>(frame);
        __asm__ __volatile__("movl %0,%%fs:0" ::"r"(previous));
        std::longjmp(outer_jump, 1);
    }
    return ExceptionContinueSearch;
}
extern "C" __attribute__((naked)) uint32_t __cdecl catch_outer(void*, uint32_t, uint32_t, uint32_t) {
    __asm__ __volatile__(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $8,%esp\n\t"
        "movl %fs:0,%eax\n\tmovl %eax,-8(%ebp)\n\tmovl $_fl_fixture_handler,-4(%ebp)\n\t"
        "leal -8(%ebp),%eax\n\tmovl %eax,%fs:0\n\t"
        "pushl 20(%ebp)\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\tcall _fl_raw\n\taddl $16,%esp\n\t"
        "movl -8(%ebp),%edx\n\tmovl %edx,%fs:0\n\tleave\n\tret\n\t");
}
// The production gate (anchors, entry spans, load site and target) against a synthetic image at a VirtualAlloc
// base: the generated bytes with each engine operand relocated to base + slot. Accepted there, refused at another
// base, with a foreign operand, and for every single-byte flip outside the operands.
void anchor_cases() {
    const SIZE_T span = (layout::engine_slot_rva + 0x1000) & ~SIZE_T(0xfff);
    auto* base = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE, PAGE_NOACCESS));
    check(base != nullptr, "synthetic image reserved");
    if (!base) return;
    const uintptr_t b = reinterpret_cast<uintptr_t>(base);
    struct Span {
        uint32_t rva;
        const uint8_t* bytes;
        unsigned length, operand;
    } spans[] = {{layout::insert_rva, layout::insert_entry, 6, 0xff},
                 {layout::remove_rva, layout::remove_entry, 6, 0xff},
                 {layout::destroy_rva, layout::destroy_entry, 6, 0xff},
                 {layout::load_anchor_rva, layout::load_anchor, sizeof layout::load_anchor, 0xff},
                 {layout::load_target_rva, layout::load_target_entry, 6, 0xff},
                 {layout::remove_anchor_rva, layout::remove_anchor, sizeof layout::remove_anchor, layout::remove_anchor_operand},
                 {layout::destroy_anchor_rva, layout::destroy_anchor, sizeof layout::destroy_anchor, layout::destroy_anchor_operand},
                 {layout::birth_anchor_rva, layout::birth_anchor, sizeof layout::birth_anchor, layout::birth_anchor_operand},
                 {layout::allocate_insert_anchor_rva, layout::allocate_insert_anchor, sizeof layout::allocate_insert_anchor,
                  layout::allocate_insert_anchor_operand}};
    bool committed = true;
    for (const Span& s : spans) committed = committed && VirtualAlloc(base + (s.rva & ~0xfffu), 0x2000, MEM_COMMIT, PAGE_READWRITE);
    check(committed, "anchor pages committed");
    auto write = [&](uint32_t operand_delta) {
        for (const Span& s : spans) {
            std::memcpy(base + s.rva, s.bytes, s.length);
            if (s.operand != 0xff) {
                const uint32_t operand = uint32_t(b + layout::engine_slot_rva + operand_delta);
                std::memcpy(base + s.rva + s.operand, &operand, 4);
            }
        }
    };
    write(0);
    check(ol::fixture_gate(b) == nullptr, "relocated synthetic image accepted at its base");
    check(ol::fixture_gate(b + 0x10000) != nullptr, "the same bytes checked at another base refused");
    write(0x10000);
    check(ol::fixture_gate(b) && !std::strcmp(ol::fixture_gate(b), "engine_anchor_mismatch"),
          "engine operand naming another slot refused");
    unsigned flips = 0, refused = 0;
    for (const Span& s : spans)
        for (unsigned i = 0; i < s.length; ++i) {
            if (s.operand != 0xff && i >= s.operand && i < s.operand + 4) continue;
            write(0);
            base[s.rva + i] ^= 0x01;
            ++flips;
            refused += ol::fixture_gate(b) != nullptr;
        }
    check(flips == 84 && refused == flips, "every single-byte flip of the spans and anchors refused");
    std::printf("ANCHOR flips=%u refused=%u\n", flips, refused);
    check(VirtualFree(base, 0, MEM_RELEASE) != 0, "synthetic image released");
}
// Retirement journal (object_lifetime.h; the AP fixture's journal cases over the FL dispatcher): written only for a
// registered consumer; Retired(serial) at every retirement point, FlushAll at every table clear; an unpublished
// insert (nested) has nothing to retire; the load appends one FlushAll and re-adopts its survivors with fresh serials.
unsigned journal_case_count = 0;
// A registry insert whose body raises, caught by an outer original frame (setjmp kept out of journal_cases).
__attribute__((noinline)) int raise_through(void* insert_fn, uint32_t key, uintptr_t value) {
    mode = Raise;
    const int caught = setjmp(outer_jump);
    if (!caught) catch_outer(insert_fn, reg(), key, value);
    mode = Plain;
    return caught;
}
void journal_cases(const ol::FixtureSites& sites) {
    using K = ol::JournalKind;
    void* const insert_fn = sites.insert_entry;
    void* const remove_fn = sites.remove_entry;
    void* const destroy_fn = sites.destroy_entry;
    static ol::JournalEntry out[ol::JournalCapacity];
    unsigned case_failures = failures;
    auto end_case = [&](const char* name) {
        std::printf("JOURNAL_CASE name=%s result=%s\n", name, failures == case_failures ? "PASS" : "FAIL");
        case_failures = failures;
        ++journal_case_count;
    };
    auto remove_key = [&](uint32_t key) { fl_raw(remove_fn, reg(), key, 0); };
    auto serial_of = [&](unsigned i, uint32_t key) {
        const auto s = query(reg(), node(i), key, cam(0), 7);
        return s.known ? s.node_serial : 0;
    };
    auto drain = [&](ol::JournalCursor& cursor, uint32_t limit = ol::JournalCapacity) {
        return ol::journal_drain(cursor, out, limit);
    };
    mode = Plain;
    registry_map = new_map();
    engine[3] = reg();
    engine_pointer = reinterpret_cast<uintptr_t>(engine);
    x3m::engine_memory::next_frame();
    check(ol::fixture_install(sites) && ol::active(), "journal install");
    birth(registry_map, 7, cam(0));

    // No consumer: retirements, an overwrite and a load write nothing; a drain demands revalidation.
    const auto idle = ol::journal_stats();
    birth(registry_map, 500, node(10));
    birth(registry_map, 500, node(10));
    remove_key(500);
    fl_raw(load_thunk, 0x1234, 0, 0);
    check(ol::journal_stats().head == idle.head && ol::journal_stats().consumers == 0,
          "no consumer: retirements, overwrite and load write nothing");
    ol::JournalCursor stale{};
    auto d = drain(stale);
    check(d.overflow && !d.available && !d.count && !stale.valid(), "drain without a consumer: overflow, unavailable");
    end_case("no_consumer");

    // Retired entries: removal, overwrite, failed membership (node then camera); fresh births and absent keys none.
    auto cursor = ol::journal_register();
    check(cursor.valid() && ol::journal_stats().consumers == 1, "consumer registered");
    d = drain(cursor);
    check(d.available && !d.overflow && !d.count && !d.more, "fresh cursor starts at the head");
    uint64_t serials[4]{};
    for (unsigned i = 0; i < 4; ++i) {
        birth(registry_map, 510 + i, node(10 + i));
        serials[i] = serial_of(10 + i, 510 + i);
        check(serials[i] != 0, "journal node born");
    }
    check(!drain(cursor).count, "births of fresh keys append nothing");
    remove_key(510);
    remove_key(511);
    remove_key(54321);                  // absent key: nothing retired
    birth(registry_map, 512, node(12)); // overwrite retires the old lifetime
    const uint64_t camera_serial = query(reg(), node(13), 513, cam(0), 7).camera_serial;
    fake_remove(registry_map, 513); // behind the observer's back
    check(query(reg(), node(13), 513, cam(0), 7).reason == ol::Reason::LookupUnavailable, "failed membership retires");
    SetLastError(0x321);
    d = drain(cursor);
    check(GetLastError() == 0x321, "drain preserves LastError");
    check(d.available && !d.overflow && !d.more && d.count == 5, "five retirements drained");
    check(d.count == 5 && out[0].kind == K::Retired && out[0].node_serial == serials[0] &&
              out[1].node_serial == serials[1] && out[2].node_serial == serials[2] && out[3].node_serial == serials[3] &&
              out[4].kind == K::Retired && camera_serial && out[4].node_serial == camera_serial &&
              out[0].load_epoch == d.load_epoch && out[0].registry_epoch == d.registry_epoch,
          "order and serials: two removals, overwrite, failed membership node then camera");
    birth(registry_map, 7, cam(0));
    check(serial_of(12, 512) != 0 && serial_of(12, 512) != serials[2], "overwritten key known with a new serial");
    end_case("retire_in_order");

    // Unpublished inserts (the FL rule): a nested insert is never published, so its removal appends nothing; the
    // published outer key's removal appends its serial.
    drain(cursor);
    mode = Nested;
    birth(registry_map, 520, node(4)); // nests key 1520 (node 20)
    mode = Plain;
    check(query(reg(), node(20), 1520, cam(0), 7).reason == ol::Reason::UnknownNodeBirth, "nested insert unpublished");
    const uint64_t outer = serial_of(4, 520);
    check(outer != 0, "outer insert published");
    check(!drain(cursor).count, "nested and outer births append nothing");
    remove_key(1520);
    remove_key(520);
    d = drain(cursor);
    check(d.count == 1 && out[0].kind == K::Retired && out[0].node_serial == outer,
          "unpublished removal appends nothing, the published one its serial");
    end_case("unpublished_insert");

    // Small buffer: oldest first, more, then the remainder.
    birth(registry_map, 530, node(14));
    birth(registry_map, 531, node(15));
    birth(registry_map, 532, node(16));
    const uint64_t a = serial_of(14, 530), b = serial_of(15, 531), c = serial_of(16, 532);
    remove_key(530);
    remove_key(531);
    remove_key(532);
    d = drain(cursor, 2);
    check(d.count == 2 && d.more && out[0].node_serial == a && out[1].node_serial == b, "small buffer: oldest first, more");
    d = drain(cursor, 2);
    check(d.count == 1 && !d.more && out[0].node_serial == c, "remainder drained");
    end_case("partial_drain");

    // Null buffer / zero capacity: invalid, cursor unmoved; an invalid cursor drains as overflow, unavailable.
    birth(registry_map, 540, node(17));
    remove_key(540);
    const auto held = cursor;
    d = ol::journal_drain(cursor, nullptr, ol::JournalCapacity);
    check(d.invalid && !d.count && !d.more && !d.overflow && cursor.sequence == held.sequence, "null buffer: invalid");
    d = drain(cursor, 0);
    check(d.invalid && !d.count && !d.more && !d.overflow && cursor.sequence == held.sequence, "zero capacity: invalid");
    ol::JournalCursor foreign{};
    d = drain(foreign);
    check(d.overflow && !d.available && !d.count && !foreign.valid(), "invalid cursor: overflow, unavailable");
    d = drain(cursor);
    check(!d.invalid && d.count == 1, "the entry still reaches a valid drain");
    end_case("invalid_drain");

    // Whole-table invalidations: foreign unwind, registry rebind, destroy; each one FlushAll with its epoch moved.
    auto pre = drain(cursor);
    const int caught = raise_through(insert_fn, 550, node(18));
    d = drain(cursor);
    check(caught == 1 && d.count == 1 && out[0].kind == K::FlushAll && d.load_epoch == pre.load_epoch + 1,
          "foreign unwind: one FlushAll, load epoch +1");
    end_case("flush_unwind");
    birth(registry_map, 7, cam(0));
    pre = drain(cursor);
    registry_map = new_map();
    engine[3] = reg();
    check(query(reg(), node(1), 42, cam(0), 7).reason == ol::Reason::RegistryChanged, "rebind seen");
    d = drain(cursor);
    check(d.count == 1 && out[0].kind == K::FlushAll && d.registry_epoch == pre.registry_epoch + 1 && d.available,
          "registry rebind: one FlushAll, registry epoch +1");
    end_case("flush_registry_rebind");
    birth(registry_map, 7, cam(0));
    birth(registry_map, 560, node(19));
    pre = drain(cursor);
    witnessed(destroy_fn, reg(), 0, 0, 0, 0x202, 0x202, "journal destroy");
    d = drain(cursor);
    check(d.count == 1 && out[0].kind == K::FlushAll && d.registry_epoch == pre.registry_epoch + 1,
          "registry destroy: one FlushAll, registry epoch +1");
    end_case("flush_registry_destroy");
    x3m::engine_memory::next_frame();

    // The load: one FlushAll at its entry (before forwarding); the key it removes appends nothing; after the normal
    // return the survivors are known again with serials newer than every serial issued before the load, and the
    // load's own births keep theirs (no further entry).
    registry_map = new_map();
    engine[3] = reg();
    birth(registry_map, 7, cam(0));
    birth(registry_map, 9, node(9));
    birth(registry_map, 8, node(8));
    const auto before = query(reg(), node(9), 9, cam(0), 7);
    check(before.known && serial_of(8, 8), "keys 9 and 8 known before the load");
    pre = drain(cursor);
    fl_raw(load_thunk, 0x1234, 0, 0);
    d = drain(cursor);
    check(d.count == 1 && out[0].kind == K::FlushAll && d.load_epoch == pre.load_epoch + 1,
          "load: exactly one FlushAll (the removal of key 8 inside it appends nothing)");
    const auto survivor = query(reg(), node(9), 9, cam(0), 7);
    const auto born = query(reg(), node(6), 60, cam(2), 61);
    check(survivor.known && born.known && survivor.node_serial > before.node_serial &&
              survivor.node_serial > before.camera_serial && survivor.camera_serial > before.camera_serial &&
              survivor.node_serial > born.node_serial && survivor.node_serial > born.camera_serial,
          "re-adopted survivors carry fresh serials");
    check(query(reg(), node(8), 8, cam(0), 7).reason == ol::Reason::UnknownNodeBirth, "key removed by the load unknown");
    check(!drain(cursor).count, "re-adoption appends nothing");
    remove_key(9);
    d = drain(cursor);
    check(d.count == 1 && out[0].kind == K::Retired && out[0].node_serial == survivor.node_serial,
          "a re-adopted survivor retires with its fresh serial");
    end_case("load_flush_and_readoption");

    // Overflow: more retirements than the ring holds between two drains moves the cursor to the head.
    const uint64_t head_before = ol::journal_stats().head;
    for (unsigned i = 0; i < ol::JournalCapacity + 88; ++i) {
        birth(registry_map, 570, node(10));
        remove_key(570);
    }
    check(ol::journal_stats().head - head_before == ol::JournalCapacity + 88, "one entry per retirement");
    d = drain(cursor);
    check(d.overflow && d.available && !d.count && !d.more && cursor.sequence == ol::journal_stats().head,
          "overflow reported, cursor at the head, nothing partial");
    birth(registry_map, 570, node(10));
    const uint64_t last = serial_of(10, 570);
    remove_key(570);
    d = drain(cursor);
    check(!d.overflow && d.count == 1 && last && out[0].node_serial == last, "cursor recovers after overflow");
    for (unsigned i = 0; i < ol::JournalCapacity; ++i) {
        birth(registry_map, 570, node(10));
        remove_key(570);
    }
    d = drain(cursor);
    check(!d.overflow && d.count == ol::JournalCapacity && !d.more, "exactly one full ring drains without overflow");
    end_case("overflow_and_recovery");

    // Unregister: nothing is written, the old cursor drains as overflow; a new registration starts past the ring.
    birth(registry_map, 580, node(11));
    remove_key(580);
    ol::journal_unregister();
    check(ol::journal_stats().consumers == 0, "unregistered");
    const uint64_t frozen = ol::journal_stats().head;
    birth(registry_map, 581, node(11));
    remove_key(581);
    check(ol::journal_stats().head == frozen, "nothing written after the last consumer left");
    auto old = cursor;
    d = drain(cursor);
    check(d.overflow && !d.available && !d.count, "unregister then drain: overflow, unavailable");
    auto fresh = ol::journal_register();
    d = drain(old);
    check(d.overflow && !d.count && fresh.valid() && fresh.sequence > frozen + ol::JournalCapacity,
          "re-registration: earlier cursors overflow, the new one starts past the ring");
    check(!drain(fresh).count, "fresh cursor empty");
    end_case("unregister");

    // Shutdown with a consumer still registered: one FlushAll; the drain then reports the observer unavailable.
    check(ol::fixture_shutdown() && !ol::active(), "journal shutdown");
    d = drain(fresh);
    check(d.count == 1 && out[0].kind == K::FlushAll && !d.available, "shutdown: one FlushAll, unavailable");
    ol::journal_unregister();
    check(ol::journal_stats().consumers == 0, "consumer count balanced");
    end_case("flush_shutdown");
    std::printf("JOURNAL cases=%u\n", journal_case_count);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    auto* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!page) return 2;
    // The load caller: `call rel32 fl_fake_load; ret` at page+4 (the five bytes cross a qword, as at X3FL.exe RVA
    // 0x7984c), and a second one calling another function (target refusal).
    unsigned char* site = page + 4;
    unsigned char* other_site = page + 68;
    auto emit_call = [](unsigned char* at, const void* target) {
        at[0] = 0xe8;
        const uint32_t rel = uint32_t(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(at) + 5));
        std::memcpy(at + 1, &rel, 4);
        at[5] = 0xc3;
    };
    emit_call(site, reinterpret_cast<void*>(&fl_fake_load));
    emit_call(other_site, reinterpret_cast<void*>(&fl_fake_remove));
    DWORD old = 0;
    VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), page, 4096);
    load_thunk = site;
    void* const insert_fn = reinterpret_cast<void*>(&fl_fake_insert);
    void* const remove_fn = reinterpret_cast<void*>(&fl_fake_remove);
    void* const destroy_fn = reinterpret_cast<void*>(&fl_fake_destroy);
    unsigned char originals[4][6];
    std::memcpy(originals[0], insert_fn, 6);
    std::memcpy(originals[1], remove_fn, 6);
    std::memcpy(originals[2], destroy_fn, 6);
    std::memcpy(originals[3], site, 6);
    const auto pristine = [&]() {
        return !std::memcmp(originals[0], insert_fn, 6) && !std::memcmp(originals[1], remove_fn, 6) &&
               !std::memcmp(originals[2], destroy_fn, 6) && !std::memcmp(originals[3], site, 6);
    };
    const uintptr_t engine_slot = reinterpret_cast<uintptr_t>(&engine_pointer);
    ol::FixtureSites sites{insert_fn, remove_fn, destroy_fn, site, reinterpret_cast<void*>(&fl_fake_load), engine_slot};
    registry_map = new_map();
    engine[3] = reg();

    // Unhooked baseline through the same callers: the witness holds without the hook.
    witnessed(insert_fn, reg(), 5, node(5), 1, 0x202, 0x8d7, "unhooked insert");
    witnessed(remove_fn, reg(), 5, 0, 0, 0x202, 0x202, "unhooked remove");
    witnessed(load_thunk, 0x1234, 0, 0, 0, 0x202, 0x8d7, "unhooked load");
    // Production entry in a non-X3FL process.
    SetEnvironmentVariableW(L"X3M_OBJECT_LIFETIME", nullptr);
    check(!ol::initialize() && !ol::active() && row_has("fl.lifetime status=disabled"), "unset: disabled");
    SetEnvironmentVariableW(L"X3M_OBJECT_LIFETIME", L"1");
    check(!ol::initialize() && !ol::active() && row_has("fl.lifetime status=executable_mismatch"),
          "not X3FL.exe: executable_mismatch");
    // Partial installs roll back: a destroy span with other bytes (insert and remove already claimed), then a load
    // call to another target (all three entries claimed).
    ol::FixtureSites bad = sites;
    bad.destroy_entry = remove_fn;
    check(!ol::fixture_install(bad) && !ol::active() && !ol::recovery_required() && row_has("failed_site=destroy") &&
              row_has("status=bytes_mismatch"),
          "destroy refusal after two claims");
    check(pristine(), "destroy refusal restored insert and remove");
    bad = sites;
    bad.load_call = other_site;
    check(!ol::fixture_install(bad) && !ol::active() && !ol::recovery_required() && row_has("failed_site=load") &&
              row_has("status=target_mismatch"),
          "load refusal after three claims");
    check(pristine(), "load refusal restored all three entries");

    // Install before the engine exists (as in X3FL.exe: the DLL loads first).
    engine_pointer = 0;
    check(ol::fixture_install(sites) && ol::active() && !std::strcmp(ol::status(), "active_without_baseline") &&
              row_has("baseline=no_engine baseline_entries=0") && row_has("write=atomic,atomic,atomic,plain"),
          "installed with no engine: active, baseline no_engine, entry writes atomic, load write plain");
    check(!pristine(), "four sites patched");
    check(query(reg(), node(0), 1, cam(0), 2).reason == ol::Reason::RegistryUnavailable, "no engine: unavailable");
    birth(registry_map, 3, node(3)); // a generic-map use before the engine: dormant, nothing adopted
    engine_pointer = reinterpret_cast<uintptr_t>(engine);
    check(query(reg(), node(3), 3, cam(0), 2).reason == ol::Reason::RegistryChanged, "first sight of the registry binds");
    check(query(reg(), node(3), 3, cam(0), 2).reason == ol::Reason::UnknownNodeBirth, "dormant birth never adopted");

    // Births: published only after the engine call returned and found the handle in the map.
    probe_node = node(1), probe_handle = 42, probe_camera = cam(0), probe_camera_handle = 7;
    birth(registry_map, 7, cam(0));
    const uint32_t inserted = witnessed(insert_fn, reg(), 42, node(1), 1, 0x202, 0x8d7, "insert");
    nodes[1][layout::node_handle / 4] = 42;
    check(inserted == 1, "insert result kept");
    check(inside.reason == ol::Reason::MutationInProgress && inside.live == 1, "inside insert: in flight, unpublished");
    auto known = query(reg(), node(1), 42, cam(0), 7);
    check(known.known && known.node_serial && known.camera_serial && known.node_serial != known.camera_serial,
          "after insert: known, two serials");
    check(query(reg(), node(1), 42, cam(1), 7).reason == ol::Reason::PointerMismatch, "camera pointer mismatch");
    check(query(reg(), node(1), 4242, cam(0), 7).reason == ol::Reason::UnknownNodeBirth, "unknown node");
    check(query(reg(), node(1), 42, cam(0), 77).reason == ol::Reason::UnknownCameraBirth, "unknown camera");
    check(query(reg() + 16, node(1), 42, cam(0), 7).reason == ol::Reason::RegistryMismatch, "other map");
    // Replacement (X3FL.exe returns 0): the key is retired before, a new serial published after.
    check(witnessed(insert_fn, reg(), 42, node(2), 1, 0x8d7, 0x202, "replace") == 0, "replacement result 0 kept");
    nodes[2][layout::node_handle / 4] = 42;
    check(query(reg(), node(1), 42, cam(0), 7).reason == ol::Reason::PointerMismatch, "old pointer refused");
    const auto replaced = query(reg(), node(2), 42, cam(0), 7);
    check(replaced.known && replaced.node_serial > known.node_serial, "replacement: new serial");
    // Removal retires before the engine unlinks and frees.
    probe_node = node(2);
    check(witnessed(remove_fn, reg(), 42, 0, 0, 0x202, 0x8d7, "remove") == node(2), "removed value kept");
    check(inside.reason == ol::Reason::MutationInProgress && inside.live == 1, "inside remove: already retired");
    check(query(reg(), node(2), 42, cam(0), 7).reason == ol::Reason::UnknownNodeBirth, "after remove: unknown");
    // Nesting: an insert reentering insert (allocator recovery) stays unpublished; the outer one is published.
    mode = Nested;
    birth(registry_map, 50, node(4));
    mode = Plain;
    check(query(reg(), node(4), 50, cam(0), 7).known, "outer insert published");
    check(query(reg(), node(20), 1050, cam(0), 7).reason == ol::Reason::UnknownNodeBirth, "nested insert unpublished");
    // Another map: forwarded, not tracked.
    FakeMap* other = new_map();
    birth(other, 42, node(7));
    check(query(reg(), node(7), 42, cam(0), 7).reason == ol::Reason::UnknownNodeBirth, "other map not tracked");
    // Foreign SEH unwind through the dispatcher's registration: table dropped, load epoch advanced, observer kept.
    const uint64_t epoch_before_unwind = ol::stats().load_epoch;
    mode = Raise;
    const int caught = setjmp(outer_jump);
    if (!caught) catch_outer(insert_fn, reg(), 51, node(8));
    mode = Plain;
    check(caught == 1, "SEH reached the outer handler");
    check(ol::active() && ol::stats().live == 0 && ol::stats().load_epoch == epoch_before_unwind + 1,
          "unwind: active, table cleared, load epoch +1");
    check(query(reg(), node(4), 50, cam(0), 7).reason == ol::Reason::UnknownNodeBirth, "unwind: not in flight, dropped");
    // Renderer load: epoch advanced before forwarding, history unavailable inside, births inside published by the
    // insert hook; after the normal return the survivors are adopted again with fresh serials, once.
    birth(registry_map, 7, cam(0)); // the view camera again (a replacement with the same value)
    birth(registry_map, 9, node(9));
    birth(registry_map, 8, node(8));
    const auto survivor_before = query(reg(), node(9), 9, cam(0), 7);
    check(survivor_before.known && query(reg(), node(8), 8, cam(0), 7).known, "keys 9 and 8 known before the load");
    probe_node = node(9), probe_handle = 9;
    const uint64_t epoch_before_load = ol::stats().load_epoch;
    check(witnessed(load_thunk, 0x1234, 0, 0, 0, 0x202, 0x202, "load") == 1, "load result kept");
    check(inside.reason == ol::Reason::MutationInProgress && inside.load_epoch == epoch_before_load + 1 &&
              inside.live == 0,
          "inside load: cleared, epoch already advanced, in flight");
    const auto loaded = query(reg(), node(6), 60, cam(2), 61);
    check(loaded.known && loaded.load_epoch == epoch_before_load + 1, "births inside the load published");
    const auto survivor = query(reg(), node(9), 9, cam(0), 7);
    check(survivor.known && survivor.load_epoch == epoch_before_load + 1, "survivor and view camera known after load");
    check(survivor.node_serial != survivor_before.node_serial && survivor.camera_serial != survivor_before.camera_serial,
          "survivors carry fresh serials");
    check(loaded.node_serial < survivor.node_serial && loaded.camera_serial < survivor.node_serial,
          "keys born during the load published once (their insert-hook serials, older than the re-baseline's)");
    check(query(reg(), node(8), 8, cam(0), 7).reason == ol::Reason::UnknownNodeBirth, "key removed by the load unknown");
    check(ol::stats().live == 7, "live after the load: 3, 7, 50, 1050, 9 adopted again, 60, 61 born");
    // Destroy: the replayed CMP decides the JE whatever EFLAGS arrived. An empty other map with ZF clear on entry,
    // then the populated registry with ZF set on entry.
    FakeMap* empty = new_map();
    witnessed(destroy_fn, reinterpret_cast<uint32_t>(empty), 0, 0, 0, 0x202, 0x8d7, "destroy empty");
    check(destroy_path == 1, "empty map: JE taken (ZF from the replayed CMP)");
    probe_node = node(6), probe_handle = 60, probe_camera = cam(2), probe_camera_handle = 61;
    check(witnessed(destroy_fn, reg(), 0, 0, 0, 0x8d7, 0x202, "destroy registry") == 0x5eed, "destroy result kept");
    check(destroy_path == 2, "populated map: JE not taken");
    check(inside.live == 0 && inside.reason == ol::Reason::MutationInProgress, "inside destroy: cleared, in flight");
    check(query(reg(), node(6), 60, cam(2), 61).reason == ol::Reason::RegistryUnavailable, "after destroy: dead");
    check(x3m::engine_memory::shutting_down(), "destroy raised the engine_memory shutdown signal");
    x3m::engine_memory::next_frame();
    // A new registry after destroy rebinds; a registry changing without destroy is counted.
    registry_map = new_map();
    engine[3] = reg();
    birth(registry_map, 7, cam(0));
    birth(registry_map, 42, node(1));
    check(query(reg(), node(1), 42, cam(0), 7).known, "new registry after destroy: known");
    FakeMap* previous = registry_map;
    registry_map = new_map();
    engine[3] = reg();
    check(query(reinterpret_cast<uintptr_t>(previous), node(1), 42, cam(0), 7).reason == ol::Reason::RegistryMismatch,
          "the old registry refused");
    check(query(reg(), node(1), 42, cam(0), 7).reason == ol::Reason::RegistryChanged, "changed registry rebinds");
    // The sample row: forced, then written by the frame boundary call.
    ol::fixture_force_sample();
    const unsigned before_row = rows;
    ol::fl_sample_row();
    check(rows == before_row + 1 && row_has("fl.lifetime_sample row=1 status=active_without_baseline "), "one row");
    check(row_has(" destroys=1 ") && row_has(" loads=1 ") && row_has(" unwinds=1 ") && row_has(" unpublished_nested=1 "),
          "destroy, load, unwind and nested counts");
    check(row_has(" binds=3 rebind_without_destroy=1 ") && row_has(" registry_changed=0 "),
          "binds and the rebind without destroy");
    check(row_has(" rebaselined=5 rebaseline_rejected=0 rebaseline_skipped=0 "), "one re-baseline, five survivors");
    check(row_has(" top_reason=mutation_in_progress "), "top reason: every hooked body queried current() in flight");
    ol::fl_sample_row();
    check(rows == before_row + 1, "no second row before the period");
    check(ol::fixture_shutdown() && pristine() && !ol::active() && !ol::recovery_required(), "exact restore");
    witnessed(insert_fn, reg(), 5, node(5), 1, 0x202, 0x8d7, "after restore");

    // Baseline over a populated registry (installation is quiescent): 20 nodes adopted under the new epoch.
    registry_map = new_map();
    engine[3] = reg();
    for (unsigned i = 0; i < 20; ++i) {
        fake_insert(registry_map, 100 + i, node(i));
        nodes[i][layout::node_handle / 4] = 100 + i;
    }
    fake_insert(registry_map, 7, cam(0));
    cameras[0][layout::node_handle / 4] = 7;
    check(ol::fixture_install(sites) && !std::strcmp(ol::status(), "active") &&
              row_has("baseline=complete baseline_entries=21"),
          "populated baseline complete (21 entries, one rehash)");
    check(query(reg(), node(13), 113, cam(0), 7).known, "adopted entry known");
    check(ol::fixture_shutdown(), "restore after baseline");
    nodes[5][layout::node_handle / 4] = 999; // a node whose handle is not its key
    check(ol::fixture_install(sites) && !std::strcmp(ol::status(), "active_without_baseline") &&
              row_has("baseline=rejected baseline_entries=0"),
          "inconsistent baseline rejected, nothing adopted");
    check(query(reg(), node(13), 113, cam(0), 7).reason == ol::Reason::UnknownNodeBirth, "rejected baseline adopts none");
    check(ol::fixture_shutdown(), "restore after rejected baseline");
    // Capacity: an eight-slot table, nine births. The ninth disables the observer (never evicts); the patches stay
    // owned (recovery_required) and keep forwarding.
    registry_map = new_map();
    engine[3] = reg();
    check(ol::fixture_install(sites, 8), "eight-slot install");
    for (unsigned i = 0; i < 9; ++i) birth(registry_map, 200 + i, node(i));
    ol::DisableEvent event{};
    check(!ol::active() && ol::recovery_required() && !std::strcmp(ol::status(), "capacity_exhausted"),
          "overflow disables");
    check(ol::disable_pending() && ol::take_disable_event(&event) && event.reason == ol::Reason::CapacityExhausted &&
              event.live == 8 && event.capacity == 8 && !ol::disable_pending(),
          "one disable event, live 8 of 8");
    check(query(reg(), node(0), 200, cam(0), 7).reason == ol::Reason::CapacityExhausted, "current() reports it");
    witnessed(insert_fn, reg(), 300, node(10), 1, 0x202, 0x8d7, "disabled forwarding");
    check(ol::fixture_shutdown() && pristine(), "restore after overflow");
    // X3M_FL_LIFETIME_LOG=0: no rows.
    SetEnvironmentVariableW(L"X3M_FL_LIFETIME_LOG", L"0");
    check(ol::fixture_install(sites), "install with rows off");
    {
        const unsigned b = rows;
        ol::fixture_force_sample();
        ol::fl_sample_row();
        check(rows == b, "X3M_FL_LIFETIME_LOG=0 writes no row");
    }
    SetEnvironmentVariableW(L"X3M_FL_LIFETIME_LOG", nullptr);
    // Cost: an insert/remove pair on the registry unhooked and hooked, and one current() call.
    {
        LARGE_INTEGER frequency{}, begin{}, end{};
        QueryPerformanceFrequency(&frequency);
        const unsigned iterations = 100000;
        double ns[2]{};
        mode = Timing;
        nodes[10][layout::node_handle / 4] = 400;
        for (unsigned pass = 0; pass < 2; ++pass) {
            if (pass == 0) check(ol::fixture_shutdown(), "unhooked timing pass");
            if (pass == 1) check(ol::fixture_install(sites), "hooked timing pass");
            QueryPerformanceCounter(&begin);
            for (unsigned i = 0; i < iterations; ++i) {
                if ((i & 1023) == 0) x3m::engine_memory::next_frame();
                fl_raw(insert_fn, reg(), 400, node(10));
                fl_raw(remove_fn, reg(), 400, 0);
            }
            QueryPerformanceCounter(&end);
            ns[pass] = double(end.QuadPart - begin.QuadPart) * 1e9 / double(frequency.QuadPart) / iterations / 2;
        }
        fl_raw(insert_fn, reg(), 400, node(10));
        fl_raw(insert_fn, reg(), 7, cam(0));
        mode = Plain;
        check(query(reg(), node(10), 400, cam(0), 7).known, "timing key known");
        QueryPerformanceCounter(&begin);
        ol::Snapshot s{};
        for (unsigned i = 0; i < iterations; ++i) {
            if ((i & 1023) == 0) x3m::engine_memory::next_frame();
            ol::current(reg(), node(10), 400, cam(0), 7, &s);
        }
        QueryPerformanceCounter(&end);
        const double current_ns = double(end.QuadPart - begin.QuadPart) * 1e9 / double(frequency.QuadPart) / iterations;
        std::printf("TIMING native_ns=%.1f hooked_ns=%.1f hook_ns=%.1f current_ns=%.1f\n", ns[0], ns[1], ns[1] - ns[0],
                    current_ns);
        check(ol::fixture_shutdown(), "restore after timing");
    }
    // Re-baseline cost: a registry of 20000 nodes, one hooked load (its body adds keys 60 and 61).
    {
        registry_map = new_map();
        engine[3] = reg();
        constexpr unsigned count = 20000;
        auto* pool = static_cast<uint32_t*>(std::calloc(count, 0x30));
        for (unsigned i = 0; i < count; ++i) {
            pool[i * 12 + layout::node_handle / 4] = 5000 + i;
            fake_insert(registry_map, 5000 + i, reinterpret_cast<uint32_t>(pool + i * 12));
        }
        check(ol::fixture_install(sites) && row_has("baseline=complete baseline_entries=20000"), "20000-entry baseline");
        LARGE_INTEGER frequency{}, begin{}, end{};
        QueryPerformanceFrequency(&frequency);
        x3m::engine_memory::next_frame();
        QueryPerformanceCounter(&begin);
        fl_raw(load_thunk, 0x1234, 0, 0);
        QueryPerformanceCounter(&end);
        const double load_us = double(end.QuadPart - begin.QuadPart) * 1e6 / double(frequency.QuadPart);
        check(query(reg(), reinterpret_cast<uintptr_t>(pool + 777 * 12), 5777, cam(2), 61).known,
              "a survivor of the 20000 known after the load");
        ol::fixture_force_sample();
        ol::fl_sample_row();
        check(row_has(" rebaselined=20000 "), "20000 survivors adopted, the two load births not twice");
        const char* us = std::strstr(last_row, " rebaseline_us_max=");
        std::printf("REBASELINE entries=%u load_us=%.0f%s\n", count + 2, load_us, us ? us : "");
        check(ol::fixture_shutdown(), "restore after the re-baseline cost case");
    }
    journal_cases(sites);
    anchor_cases();
    // Late window: after the first Present closes the engine_patch window, nothing is claimed or written.
    x3m::engine_patch::close_install_window("fixture_first_present");
    check(!ol::fixture_install(sites) && !std::strcmp(ol::status(), "late_claim") && pristine() &&
              !ol::recovery_required(),
          "late claim refused");
    VirtualFree(page, 0, MEM_RELEASE);
    std::printf("RESULT %s checks=%u failures=%u target_calls=%u\n", failures ? "FAIL" : "PASS", checks, failures, calls);
    return failures ? 1 : 0;
}
