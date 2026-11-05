// X3M_COLLISION_DAMAGE fixture (src/proxy/collision_damage_core.h, collision_damage_stubs.h;
// docs/reverse-engineering/collision-damage.md, "Implementation"). One source, two builds
// (verification/analysis/test_collision_damage.py):
//   host (any C++17 compiler): `--tables` prints the core's windows, sites and hashes for the comparison with
//     verify_collision_damage.py; the checks run the window comparator over an image built from the verifier's bytes
//     (`--windows FILE`, else the core's), flip every byte of every window, and drive the two-site install and restore
//     over a byte image with a fault injected at every Ops call;
//   native (i686 MinGW with the DLL's flags, run on Windows through WSL interop): additionally, per edition, an executable
//     image at a VirtualAlloc base with the windows at their RVAs, installed and restored with VirtualProtect, and the
//     engine path executed from the site against a fabricated register, EFLAGS, stack and object image: vanilla bytes,
//     then through the jump, the stub and its replay of the displaced instructions, then the restored bytes.
// Prints one line per failed check and "checks=N failures=M"; exit 0 only when every check holds. No game, no Wine.
#include "collision_damage_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#if defined(_WIN32) && defined(__i386__)
#define X3M_CD_NATIVE 1
#include "collision_damage_stubs.h"
#include <windows.h>
#endif

namespace core = x3m::collision_damage::core;
namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char* what, const char* edition, long detail = 0) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s %s %ld\n", edition, what, detail);
    }
}
const core::Edition* editions[] = {&core::ap, &core::fl};
constexpr std::uint32_t image_size = 0x70000;

// ---- the verifier's windows (`ed name va hex` per line), else the core's ----
struct Loaded {
    std::string edition, name;
    std::uint32_t va;
    std::vector<unsigned char> bytes;
};
std::vector<Loaded> loaded;
bool load_windows(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    char ed[8], name[64], hex[256];
    unsigned va = 0;
    while (std::fscanf(f, "%7s %63s %x %255s", ed, name, &va, hex) == 4) {
        Loaded w{ed, name, va, {}};
        for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
            unsigned b = 0;
            std::sscanf(hex + i, "%2x", &b);
            w.bytes.push_back(static_cast<unsigned char>(b));
        }
        loaded.push_back(w);
    }
    std::fclose(f);
    return !loaded.empty();
}
// Writes the edition's windows into image (offset = rva); the verifier's when loaded. Returns how many were written.
unsigned fill_windows(const core::Edition& e, unsigned char* image) {
    unsigned n = 0;
    if (!loaded.empty()) {
        for (const Loaded& w : loaded)
            if (w.edition == e.name) {
                std::memcpy(image + core::rva(w.va), w.bytes.data(), w.bytes.size());
                ++n;
            }
        return n;
    }
    for (unsigned i = 0; i < e.window_count; ++i) {
        std::memcpy(image + core::rva(e.windows[i].va), e.windows[i].bytes, e.windows[i].length);
        ++n;
    }
    return n;
}
void print_hex(const unsigned char* b, unsigned n) {
    for (unsigned i = 0; i < n; ++i) std::printf("%02x", b[i]);
}
void print_tables() {
    for (const core::Edition* e : editions) {
        for (unsigned i = 0; i < e->window_count; ++i) {
            std::printf("window %s %s 0x%08x ", e->name, e->windows[i].name, unsigned(e->windows[i].va));
            print_hex(e->windows[i].bytes, e->windows[i].length);
            std::printf("\n");
        }
        std::printf("span %s contact_join_site 0x%08x %u ", e->name, unsigned(e->contact_va), e->contact_length);
        print_hex(e->contact_original, e->contact_length);
        std::printf("\nspan %s planet_gate_site 0x%08x 2 %02x->%02x continue=0x%08x\n", e->name, unsigned(e->planet_va),
                    unsigned(e->planet_original), unsigned(e->planet_patched), unsigned(e->planet_continue_va));
        std::printf("leaf %s 0x%08x\n", e->name, unsigned(e->leaf_va));
        std::printf("hash %s response 0x%08x %u %s\n", e->name, unsigned(e->response_va), e->response_size,
                    e->response_sha256);
    }
}

// ---- host: a byte image and Ops with one injected fault ----
struct HostOps {
    static constexpr std::uint32_t writable = 0x40;
    std::uintptr_t base = 0;
    std::vector<unsigned char>* image = nullptr;
    int fail_at = -1, fail_from = 1 << 30, calls = 0;
    std::uint32_t protection = 0x20;
    bool tick() {
        const int n = calls++;
        return n != fail_at && n < fail_from;
    }
    bool read(std::uintptr_t at, unsigned char* out, unsigned n) {
        if (!tick()) return false;
        std::memcpy(out, image->data() + (at - base), n);
        return true;
    }
    bool protect(std::uintptr_t, unsigned, std::uint32_t p, std::uint32_t* previous) {
        if (!tick()) return false;
        *previous = protection;
        protection = p;
        return true;
    }
    // A failure here is the flush's: the bytes are in, the call reports false.
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        *atomic = false;
        std::memcpy(image->data() + (at - base), bytes, n);
        return tick();
    }
};
void host_checks(const core::Edition& e) {
    const std::uintptr_t base = e.leaf_va ? 0x00400000 : 0x01230000;
    std::vector<unsigned char> image(image_size, 0xcc);
    check(fill_windows(e, image.data()) == e.window_count, "window_count", e.name, long(e.window_count));
    const auto read = [&](std::uintptr_t at, unsigned char* out, unsigned n) {
        if (at < base || at + n > base + image.size()) return false;
        std::memcpy(out, image.data() + (at - base), n);
        return true;
    };
    core::Mismatch m;
    check(core::compare_windows(read, base, e, m) && !m.window, "accepts_verifier_bytes", e.name);
    // every byte of every window, one at a time: refused, naming that window, with the bytes it found
    unsigned flips = 0, named = 0;
    for (unsigned i = 0; i < e.window_count; ++i) {
        const core::Window& w = e.windows[i];
        for (unsigned k = 0; k < w.length; ++k) {
            unsigned char& b = image[core::rva(w.va) + k];
            b ^= 0x01;
            const bool refused = !core::compare_windows(read, base, e, m);
            ++flips;
            named += refused && m.window == &w && m.readable;
            b ^= 0x01;
        }
    }
    unsigned total = 0;
    for (unsigned i = 0; i < e.window_count; ++i) total += e.windows[i].length;
    check(named == flips && flips == total, "one_byte_change_refused", e.name, long(named));
    std::printf("%s flips=%u refused=%u\n", e.name, flips, named);
    check(!core::compare_windows(read, base + 0x10000000, e, m) && m.window == &e.windows[0] && !m.readable,
          "unreadable_refused", e.name);
    check(core::compare_windows(read, base, e, m), "image_restored_after_flips", e.name);

    const std::vector<unsigned char> original = image;
    const std::uintptr_t contact = base + core::rva(e.contact_va), planet = base + core::rva(e.planet_va);
    const std::uint32_t stub = 0x7ff01230;
    // clean install: jmp rel32 to the stub + NOPs, planet xor, protection back
    {
        HostOps ops;
        ops.base = base;
        ops.image = &image;
        core::Installed in;
        const char* r = core::install(ops, base, e, stub, in);
        const unsigned char* c = image.data() + (contact - base);
        std::uint32_t rel = 0;
        std::memcpy(&rel, c + 1, 4);
        bool nops = true;
        for (unsigned i = 5; i < e.contact_length; ++i) nops = nops && c[i] == 0x90;
        check(!std::strcmp(r, "ok") && in.contact && in.planet, "install_ok", e.name);
        check(c[0] == 0xe9 && std::uint32_t(contact + 5 + rel) == stub && nops, "contact_bytes", e.name);
        check(image[planet - base] == 0x31 && image[planet - base + 1] == original[planet - base + 1], "planet_byte",
              e.name);
        check(ops.protection == 0x20, "protection_back", e.name);
        // only the two spans changed
        unsigned changed = 0;
        for (size_t i = 0; i < image.size(); ++i) changed += image[i] != original[i];
        check(changed <= e.contact_length + 1, "only_the_spans", e.name, long(changed));
        check(!core::compare_windows(read, base, e, m), "patched_image_refused_by_comparator", e.name);
        const char* back = core::restore(ops, base, e, in);
        check(!std::strcmp(back, "restored") && !in.contact && !in.planet && image == original, "restore", e.name);
        // a foreign write over the contact span is not undone; the planet byte still goes back
        core::install(ops, base, e, stub, in);
        image[contact - base + 1] ^= 0xff;
        back = core::restore(ops, base, e, in);
        check(!std::strcmp(back, "restore_not_owned") && in.contact && !in.planet && image[planet - base] == 0x85,
              "restore_not_owned", e.name);
        image = original;
    }
    // a fault at every Ops call of the install: ok, or everything back (nothing registered), or registered and named
    unsigned faults = 0, rolled = 0;
    for (int k = 0; k < 40; ++k) {
        image = original;
        HostOps ops;
        ops.base = base;
        ops.image = &image;
        ops.fail_at = k;
        core::Installed in;
        const char* r = core::install(ops, base, e, stub, in);
        if (!std::strcmp(r, "ok") && ops.calls <= k) break; // no call left to fail
        ++faults;
        const bool registered = in.contact || in.planet;
        if (!std::strcmp(r, "ok")) {
            check(false, "fault_reported_ok", e.name, k);
        } else if (!registered) {
            ++rolled;
            check(image == original, "fault_rolled_back_bytes", e.name, k);
        } else {
            check(std::strstr(r, "rollback_failed") != nullptr, "fault_registered_named", e.name, k);
            HostOps clean;
            clean.base = base;
            clean.image = &image;
            check(!std::strcmp(core::restore(clean, base, e, in), "restored") && image == original,
                  "fault_restored_later", e.name, k);
        }
    }
    // eight calls (protect, write, read, protect per site): a fault at any of them, never a silent ok
    check(faults == 8 && rolled == 8, "fault_coverage", e.name, long(faults));
    // every call from k on failing: the rollback cannot be confirmed, so the site stays registered under a named reason
    // and a later restore (shutdown) puts the original bytes back
    unsigned persistent = 0, kept = 0;
    for (int k = 0; k < 8; ++k) {
        image = original;
        HostOps ops;
        ops.base = base;
        ops.image = &image;
        ops.fail_from = k;
        core::Installed in;
        const char* r = core::install(ops, base, e, stub, in);
        ++persistent;
        check(std::strcmp(r, "ok") != 0, "persistent_fault_not_ok", e.name, k);
        if (!in.contact && !in.planet) {
            check(image == original, "persistent_fault_bytes_back", e.name, k);
            continue;
        }
        ++kept;
        check(std::strstr(r, "rollback_failed") != nullptr, "persistent_fault_named", e.name, k);
        HostOps clean;
        clean.base = base;
        clean.image = &image;
        check(!std::strcmp(core::restore(clean, base, e, in), "restored") && image == original && !in.contact &&
                  !in.planet,
              "persistent_fault_restored_later", e.name, k);
    }
    check(kept >= 4, "persistent_fault_coverage", e.name, long(kept));
    std::printf("%s faults=%u rolled_back=%u persistent=%u kept_registered=%u\n", e.name, faults, rolled, persistent,
                kept);
}
void row_checks() {
    check(core::row_due(1, 0, 5, 0), "row_first", "-");
    check(!core::row_due(7, 7, 99999, 1), "row_no_events", "-");
    check(!core::row_due(9, 7, 1999, 1000), "row_within_second", "-");
    check(core::row_due(9, 7, 2000, 1000), "row_after_second", "-");
}

#if X3M_CD_NATIVE
// ---- native: the stubs executed ----
extern "C" {
struct Regs {
    std::uint32_t eax, ecx, edx, ebx, esi, edi, ebp, esp, eflags;
};
Regs fx_in{}, fx_out{};
std::uint32_t fx_saved_esp = 0, fx_target = 0, fx_tag = 0, fx_leaf_ptr = 0, fx_leaf_eax = 0;
void fx_run();
void fx_landing();
void fx_leaf_flags();
void fx_leaf_plain();
}
// fx_run: saves the callee-saved registers and ESP, switches to the fabricated stack, loads EFLAGS (DF included) and
// the seven general registers from fx_in, jumps to fx_target. fx_landing records them all, ESP and EFLAGS, clears DF and
// returns to fx_run's caller. The leaves stand for 0x0043ff80 (reads EAX only): one writes EAX and the arithmetic flags,
// the other EAX alone.
asm(R"(
    .intel_syntax noprefix
    .text
    .globl _fx_run
_fx_run:
    push ebx
    push esi
    push edi
    push ebp
    mov dword ptr [_fx_saved_esp], esp
    mov esp, dword ptr [_fx_in + 28]
    push dword ptr [_fx_in + 32]
    popfd
    mov eax, dword ptr [_fx_in + 0]
    mov ecx, dword ptr [_fx_in + 4]
    mov edx, dword ptr [_fx_in + 8]
    mov ebx, dword ptr [_fx_in + 12]
    mov esi, dword ptr [_fx_in + 16]
    mov edi, dword ptr [_fx_in + 20]
    mov ebp, dword ptr [_fx_in + 24]
    jmp dword ptr [_fx_target]

    .globl _fx_landing
_fx_landing:
    mov dword ptr [_fx_out + 0], eax
    mov dword ptr [_fx_out + 4], ecx
    mov dword ptr [_fx_out + 8], edx
    mov dword ptr [_fx_out + 12], ebx
    mov dword ptr [_fx_out + 16], esi
    mov dword ptr [_fx_out + 20], edi
    mov dword ptr [_fx_out + 24], ebp
    mov dword ptr [_fx_out + 28], esp
    pushfd
    pop dword ptr [_fx_out + 32]
    cld
    mov esp, dword ptr [_fx_saved_esp]
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

    .globl _fx_leaf_flags
_fx_leaf_flags:
    mov dword ptr [_fx_leaf_eax], eax
    xor eax, 0x5a5a5a5a
    ret
    .globl _fx_leaf_plain
_fx_leaf_plain:
    mov dword ptr [_fx_leaf_eax], eax
    lea eax, [eax + eax*2 + 7]
    ret
    .att_syntax
)");

struct NativeOps {
    static constexpr std::uint32_t writable = PAGE_EXECUTE_READWRITE;
    bool read(std::uintptr_t at, unsigned char* out, unsigned n) {
        std::memcpy(out, reinterpret_cast<const void*>(at), n);
        return true;
    }
    bool protect(std::uintptr_t at, unsigned n, std::uint32_t protection, std::uint32_t* previous) {
        DWORD old = 0;
        const bool ok = VirtualProtect(reinterpret_cast<void*>(at), n, protection, &old) != FALSE;
        if (ok) *previous = old;
        return ok;
    }
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        *atomic = false;
        std::memcpy(reinterpret_cast<void*>(at), bytes, n);
        return FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), n) != FALSE;
    }
};
DWORD protection_at(std::uintptr_t at) {
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof info) ? info.Protect : 0;
}
void emit_jmp(unsigned char* at, const void* target) {
    const std::uint32_t rel = std::uint32_t(reinterpret_cast<std::uintptr_t>(target)) -
                              std::uint32_t(reinterpret_cast<std::uintptr_t>(at) + 5);
    at[0] = 0xe9;
    std::memcpy(at + 1, &rel, 4);
}
// mov dword ptr [fx_tag], tag; jmp fx_landing
void emit_tagged_landing(unsigned char* at, std::uint32_t tag) {
    const std::uint32_t slot = std::uint32_t(reinterpret_cast<std::uintptr_t>(&fx_tag));
    at[0] = 0xc7;
    at[1] = 0x05;
    std::memcpy(at + 2, &slot, 4);
    std::memcpy(at + 6, &tag, 4);
    emit_jmp(at + 10, reinterpret_cast<const void*>(&fx_landing));
}
alignas(16) std::uint32_t fx_stack[0x100];
constexpr unsigned stack_top = 0x80; // ESP = &fx_stack[0x80]; the engine's slots are above it
struct Object {
    alignas(4) unsigned char b[0x100];
};
Object obj_a, obj_b;
void set_object(Object& o, std::uint32_t flags, std::uint16_t cls) {
    std::memset(o.b, 0x3c, sizeof o.b);
    std::memcpy(o.b + 0x40, &flags, 4);
    std::memcpy(o.b + 0x48, &cls, 2);
}
std::uint32_t ptr(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
struct Run {
    Regs out;
    std::uint32_t stack[0x100 - stack_top];
    std::uint32_t leaf_eax, tag;
};
Run run_at(std::uintptr_t target) {
    fx_target = std::uint32_t(target);
    fx_tag = 0;
    fx_leaf_eax = 0;
    fx_run();
    Run r;
    r.out = fx_out;
    std::memcpy(r.stack, fx_stack + stack_top, sizeof r.stack);
    r.leaf_eax = fx_leaf_eax;
    r.tag = fx_tag;
    return r;
}
struct Case {
    std::int32_t dmg_a, dmg_b, c, v;
    std::uint32_t flags_a, flags_b, eflags;
    std::uint16_t class_a, class_b;
    bool plain_leaf;
};
const Case cases[] = {
    {1234, 5678, 40000, 250000, 0x00040000, 0, 0x202, 7, 5, false},
    {100, 2, 200, 10, 0x2, 0, 0x202 | 0x8d5, 7, 17, false},
    {0, 0, 0, 0, 0, 0x2, 0x202 | 0x400 | 0x0c1, 7, 7, true},
    {7, 2000000000, 1, 999, 0x80002, 0x40002, 0x202 | 0x400 | 0x814, 6, 7, true},
    {15000000, 15000000, 10000000, 3, 0, 0, 0x202 | 0x095, 4, 7, false},
};
// Loads the case into the fabricated stack, objects and registers (A/B and the value registers per edition).
void load_case(const core::Edition& e, const Case& k, unsigned salt) {
    for (unsigned i = 0; i < 0x100; ++i) fx_stack[i] = 0x51000000u + i * 0x01010101u + salt;
    std::uint32_t* sp = fx_stack + stack_top;
    set_object(obj_a, k.flags_a, k.class_a);
    set_object(obj_b, k.flags_b, k.class_b);
    fx_leaf_ptr = ptr(reinterpret_cast<const void*>(k.plain_leaf ? &fx_leaf_plain : &fx_leaf_flags));
    fx_in = Regs{0xa1a2a3a4u + salt, 0xc1c2c3c4u + salt, 0xd1d2d3d4u + salt, 0xb1b2b3b4u + salt,
                 0x51525354u + salt, 0xe1e2e3e4u + salt, 0x0bad0bedu + salt, ptr(sp), k.eflags};
    sp[0x10 / 4] = std::uint32_t(k.dmg_a);
    if (e.leaf_va) { // AP: A = EBX, B = ESI, dmgA [esp+0x10], dmgB EDI, c [esp+0xc], v [esp+0x1c]
        fx_in.ebx = ptr(&obj_a);
        fx_in.esi = ptr(&obj_b);
        fx_in.edi = std::uint32_t(k.dmg_b);
        sp[0x0c / 4] = std::uint32_t(k.c);
        sp[0x1c / 4] = std::uint32_t(k.v);
    } else { // FL: A = ESI, B = EDI, dmgA EDX = [esp+0x10], dmgB ECX, c [esp+0x38], v [esp+0x5c]
        fx_in.esi = ptr(&obj_a);
        fx_in.edi = ptr(&obj_b);
        fx_in.edx = std::uint32_t(k.dmg_a);
        fx_in.ecx = std::uint32_t(k.dmg_b);
        sp[0x38 / 4] = std::uint32_t(k.c);
        sp[0x5c / 4] = std::uint32_t(k.v);
    }
}
bool same_regs(const Regs& a, const Regs& b, std::uint32_t flag_mask = 0xffffffffu) {
    return a.eax == b.eax && a.ecx == b.ecx && a.edx == b.edx && a.ebx == b.ebx && a.esi == b.esi && a.edi == b.edi &&
           a.ebp == b.ebp && a.esp == b.esp && (a.eflags & flag_mask) == (b.eflags & flag_mask);
}
void native_checks(const core::Edition& e) {
    unsigned char* image = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    check(image != nullptr, "virtual_alloc", e.name);
    if (!image) return;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(image);
    std::memset(image, 0xcc, image_size);
    fill_windows(e, image);
    const auto read = [&](std::uintptr_t at, unsigned char* out, unsigned n) {
        std::memcpy(out, reinterpret_cast<const void*>(at), n);
        return true;
    };
    core::Mismatch m;
    check(core::compare_windows(read, base, e, m), "native_accepts_verifier_bytes", e.name);
    // the execution scaffolding beyond the displaced spans (after the comparison: it overwrites window tails)
    const std::uintptr_t contact = base + core::rva(e.contact_va), planet = base + core::rva(e.planet_va);
    if (e.leaf_va) { // AP: the leaf at its RVA (jmp [fx_leaf_ptr]: no flag write); land at the stub's return 0x0045eafb
        unsigned char* leaf = image + core::rva(e.leaf_va);
        leaf[0] = 0xff;
        leaf[1] = 0x25;
        const std::uint32_t slot = ptr(&fx_leaf_ptr);
        std::memcpy(leaf + 2, &slot, 4);
        emit_jmp(image + core::rva(e.contact_va) + e.contact_length, reinterpret_cast<const void*>(&fx_landing));
    } else { // FL: the engine's own `mov [esp+0x20],ecx` at base+0x68a60 runs; land after it
        emit_jmp(image + core::rva(e.contact_va) + e.contact_length + 4, reinterpret_cast<const void*>(&fx_landing));
    }
    emit_tagged_landing(image + core::rva(e.planet_va) + 8, 1);           // the jle falls through: damage applied
    emit_tagged_landing(image + core::rva(e.planet_continue_va), 2);      // the jle taken: vanilla's zero-damage branch
    DWORD unused = 0;
    VirtualProtect(image, image_size, PAGE_EXECUTE_READ, &unused);
    std::vector<unsigned char> original(image, image + image_size);

    // vanilla references
    Run ref[sizeof cases / sizeof cases[0]];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        load_case(e, cases[i], i);
        ref[i] = run_at(contact);
        if (e.leaf_va) check(ref[i].leaf_eax == fx_in.ebx, "vanilla_leaf_saw_ebx", e.name, long(i));
    }
    // planet references: from the window start (FL runs its `mov [esp+0x24],ecx` too); damage > 0 and damage = 0
    const std::uintptr_t planet_entry = e.leaf_va ? planet : planet - 4;
    load_case(e, cases[0], 77);
    (e.leaf_va ? fx_in.esi : fx_in.ecx) = 4321;
    const Run planet_hit = run_at(planet_entry);
    (e.leaf_va ? fx_in.esi : fx_in.ecx) = 0;
    const Run planet_zero = run_at(planet_entry);
    check(planet_hit.tag == 1 && planet_zero.tag == 2, "vanilla_planet_branches", e.name);

    // install with the module's sequence on real pages
    x3m::collision_damage::bind(e, base);
    NativeOps ops;
    core::Installed in;
    const char* r = core::install(ops, base, e, x3m::collision_damage::stub_address(e), in);
    check(!std::strcmp(r, "ok"), "native_install_ok", e.name);
    check(protection_at(contact) == PAGE_EXECUTE_READ && protection_at(planet) == PAGE_EXECUTE_READ,
          "native_protection_back", e.name);
    check(image[core::rva(e.contact_va)] == 0xe9 && image[core::rva(e.planet_va)] == 0x31, "native_patched_bytes",
          e.name);

    volatile X3mCollisionDamageStats& s = x3m_collision_damage_stats;
    s.events = 0;
    s.max_a = 0;
    s.max_b = 0;
    std::int32_t max_a = 0, max_b = 0;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const Case& k = cases[i];
        load_case(e, k, i);
        const Regs in_regs = fx_in;
        const Run p = run_at(contact);
        const Run& v = ref[i];
        // the two values read 0; every other register and EFLAGS as the vanilla run leaves them
        Regs expect = v.out;
        if (e.leaf_va) {
            check(v.out.edi == std::uint32_t(k.dmg_b) && p.out.edi == 0, "dmg_b_zero", e.name, long(i));
            expect.edi = 0;
            check(p.leaf_eax == in_regs.ebx, "leaf_saw_ebx", e.name, long(i));
        } else {
            check(v.out.ecx == std::uint32_t(k.dmg_b) && p.out.ecx == 0, "dmg_b_zero", e.name, long(i));
            check(v.out.edx == std::uint32_t(k.dmg_a) && p.out.edx == 0, "dmg_a_reg_zero", e.name, long(i));
            expect.ecx = 0;
            expect.edx = 0;
        }
        check(same_regs(p.out, expect), "other_registers_and_eflags", e.name, long(i));
        check(p.out.esp == in_regs.esp, "esp", e.name, long(i));
        if (k.plain_leaf && e.leaf_va)
            check((p.out.eflags & 0xcd5) == (in_regs.eflags & 0xcd5), "incoming_flags_through_flagless_leaf", e.name,
                  long(i));
        // the stack: dmgA slot 0 (FL: and the dmgB store at [esp+0x20]); every other slot as vanilla
        unsigned differ = 0;
        for (unsigned w = 0; w < 0x100 - stack_top; ++w) {
            const bool slot_a = w == 0x10 / 4, slot_b = !e.leaf_va && w == 0x20 / 4;
            if (slot_a || slot_b)
                check(p.stack[w] == 0, slot_a ? "dmg_a_slot_zero" : "dmg_b_store_zero", e.name, long(i));
            else
                differ += p.stack[w] != v.stack[w];
        }
        check(differ == 0, "stack_unchanged", e.name, long(differ));
        // the record
        if (k.dmg_a > max_a) max_a = k.dmg_a;
        if (k.dmg_b > max_b) max_b = k.dmg_b;
        check(s.events == i + 1 && s.last_a == k.dmg_a && s.last_b == k.dmg_b && s.max_a == max_a &&
                  s.max_b == max_b,
              "record_values", e.name, long(i));
        check(s.class_a == k.class_a && s.class_b == k.class_b && s.c == k.c && s.v == k.v &&
                  s.player == (((k.flags_a | k.flags_b) & 2) ? 1u : 0u),
              "record_classes_c_v_player", e.name, long(i));
    }
    // planet: a positive damage now takes the zero-damage branch, leaving what vanilla leaves at damage 0
    load_case(e, cases[0], 77);
    (e.leaf_va ? fx_in.esi : fx_in.ecx) = 4321;
    const Run planet_patched = run_at(planet_entry);
    std::uint32_t slot24 = planet_patched.stack[0x24 / 4];
    check(planet_patched.tag == 2, "planet_zero_branch", e.name);
    check(same_regs(planet_patched.out, planet_zero.out, ~0x10u), "planet_state_as_vanilla_zero", e.name); // AF undefined
    if (!e.leaf_va) check(slot24 == 4321, "fl_planet_slot_dead_value_kept", e.name);

    // rollback: the original bytes back, protection as found, vanilla behaviour again
    r = core::restore(ops, base, e, in);
    check(!std::strcmp(r, "restored") && !in.contact && !in.planet, "native_restore", e.name);
    check(!std::memcmp(image, original.data(), image_size), "native_original_bytes", e.name);
    check(protection_at(contact) == PAGE_EXECUTE_READ, "native_protection_after_restore", e.name);
    const std::uint32_t events = s.events;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        load_case(e, cases[i], i);
        const Run again = run_at(contact);
        check(same_regs(again.out, ref[i].out) && !std::memcmp(again.stack, ref[i].stack, sizeof again.stack),
              "restored_runs_vanilla", e.name, long(i));
    }
    check(s.events == events, "restored_stub_unreached", e.name);
    load_case(e, cases[0], 77);
    (e.leaf_va ? fx_in.esi : fx_in.ecx) = 4321;
    check(run_at(planet_entry).tag == 1, "restored_planet_damage_branch", e.name);
    VirtualFree(image, 0, MEM_RELEASE);
}
#endif
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--tables")) {
            print_tables();
            return 0;
        }
        if (!std::strcmp(argv[i], "--windows") && i + 1 < argc && !load_windows(argv[++i])) {
            std::printf("cannot read %s\n", argv[i]);
            return 2;
        }
    }
    std::printf("windows=%s\n", loaded.empty() ? "core" : "verifier");
    row_checks();
    for (const core::Edition* e : editions) host_checks(*e);
#if X3M_CD_NATIVE
    for (const core::Edition* e : editions) native_checks(*e);
    std::printf("native=1\n");
#else
    std::printf("native=0\n");
#endif
    std::printf("checks=%u failures=%u %s\n", checks, failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
