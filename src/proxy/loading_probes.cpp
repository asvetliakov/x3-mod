#include "loading_probes.h"
#include "loading_trace_light.h"
#include "capture.h"
#include "object_trace.h"
#include "log_tiers.h"
#include <cstring>

// Sites (X3AP.exe as identified by executable_identity.h, preferred base
// 0x00400000; the shipped file is SHA-256 fdbf3418…), bytes verified
// against the installed executable and Ghidra listings on 2026-09-12
// (docs/reverse-engineering/loading-probes.md). `length` covers whole
// instructions with no relative branch; `ret_pop` is the callee's `ret n`.
namespace x3m::loading_probes {
namespace {
using namespace loading_trace::light;
struct SiteDefinition {
    engine_patch::SiteSpec spec;
    ProbeKind kind;
    const char* extra[4];
};
// clang-format off
const SiteDefinition definitions[site_count]={
    {{"resource_load",  0x004e8e10,{0x55,0x8b,0xec,0x83,0xe4,0xf8},6,0,0},ProbeKind::Plain,{"-","-","-","-"}},
    {{"resource_open",  0x004e8780,{0x53,0x8b,0x5c,0x24,0x08},5,4,0},ProbeKind::ResourceOpen,{"loose","catalogue","failed","-"}},
    {{"name_resolve",   0x004e7590,{0x6a,0xff,0x68,0x1d,0xfa,0x52,0x00},7,12,0},ProbeKind::Resolve,{"hit","miss","-","-"}},
    {{"resource_read",  0x004e8880,{0x81,0xec,0x54,0x04,0x00,0x00},6,0,0},ProbeKind::ResourceRead,{"plain","catalogue","gzhandle_or_unopened","null_result"}},
    {{"read_dispatch",  0x004e9210,{0xf6,0x46,0x04,0x01,0x53},5,4,0},ProbeKind::ReadDispatch,{"plain","catalogue","gzhandle","unopened"}},
    {{"crt_fgetc",      0x0050fff5,{0x6a,0x0c,0x68,0x40,0xd0,0x56,0x00},7,0,0},ProbeKind::CountOnly,{"-","-","-","-"}},
    {{"sopen_helper",   0x00527869,{0x55,0x8b,0xec,0x83,0xec,0x34},6,0,0},ProbeKind::Plain,{"-","-","-","-"}},
    {{"find_wrapper",   0x004d2950,{0x53,0x55,0x56,0x57,0xbd,0x38,0x01,0x00,0x00},9,0,0},ProbeKind::FindWrapper,{"-","-","-","-"}},
    {{"signature_check",0x004cabc0,{0x83,0xec,0x1c,0x53,0x8b,0x5c,0x24,0x34},8,0,0},ProbeKind::Plain,{"-","-","-","-"}},
    {{"texture_body",   0x004dd2c0,{0x6a,0xff,0x68,0x50,0xfd,0x52,0x00},7,0,0},ProbeKind::Plain,{"-","-","-","-"}},
    {{"texture_loader", 0x004dc540,{0x6a,0xff,0x68,0x70,0xfd,0x52,0x00},7,0,0},ProbeKind::Plain,{"-","-","-","-"}},
    {{"mesh_body",      0x004bc680,{0x83,0xec,0x10,0x53,0x55},5,0,0},ProbeKind::Plain,{"-","-","-","-"}},
};
// clang-format on
constexpr unsigned resource_read_index = 3;
constexpr uintptr_t size_global_va = 0x00596988; // DAT_00596988: the reader's result size
engine_patch::Site sites[site_count];
SiteReport reports[site_count];
bool requested_ = false, initialized_ = false;
unsigned installed_ = 0;
uint64_t frequency = 0;
void* exit_stub = nullptr;
const char* kind_name(ProbeKind kind) {
    switch (kind) {
    case ProbeKind::ResourceOpen: return "resource_open";
    case ProbeKind::Resolve: return "resolve";
    case ProbeKind::ResourceRead: return "resource_read";
    case ProbeKind::ReadDispatch: return "read_dispatch";
    case ProbeKind::FindWrapper: return "find_wrapper";
    case ProbeKind::CountOnly: return "count_only";
    default: return "plain";
    }
}
// Shared exit stub: the callee returned here instead of to its caller.
//   sub esp,4 ; pushfd ; pushad ; mov eax,esp ; push eax ; call x3m_probe_exit
//   add esp,4 ; mov [esp+0x24],eax ; popad ; popfd ; ret
void* emit_exit_stub() {
    engine_patch::Emitter e(32);
    if (!e.ok()) return nullptr;
    e.byte(0x83);
    e.byte(0xec);
    e.byte(0x04);
    e.byte(0x9c);
    e.byte(0x60);
    e.byte(0x89);
    e.byte(0xe0);
    e.byte(0x50);
    e.byte(0xe8);
    e.rel32(reinterpret_cast<const void*>(&x3m_probe_exit));
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(0x04);
    e.byte(0x89);
    e.byte(0x44);
    e.byte(0x24);
    e.byte(0x24);
    e.byte(0x61);
    e.byte(0x9d);
    e.byte(0xc3);
    return e.finish();
}
// Entry stub for site i:
//   pushfd ; pushad ; mov eax,esp ; push eax ; push i ; call x3m_probe_enter ; add esp,8
//   popad ; popfd ; jmp [next]        (next = the previous chain head, written by push_front)
void* emit_entry_stub(unsigned index, void*** next_out) {
    engine_patch::Emitter e(40);
    if (!e.ok()) return nullptr;
    void* start = e.here();
    e.byte(0x9c);
    e.byte(0x60);
    e.byte(0x89);
    e.byte(0xe0);
    e.byte(0x50);
    e.byte(0x6a);
    e.byte(static_cast<unsigned char>(index));
    e.byte(0xe8);
    e.rel32(reinterpret_cast<const void*>(&x3m_probe_enter));
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(0x08);
    e.byte(0x61);
    e.byte(0x9d);
    // next pointer right after the jmp, 4-aligned
    unsigned char* jmp = static_cast<unsigned char*>(e.here());
    uintptr_t next = reinterpret_cast<uintptr_t>(jmp) + 6;
    next = (next + 3) & ~uintptr_t(3);
    e.byte(0xff);
    e.byte(0x25);
    e.dword(uint32_t(next));
    while (reinterpret_cast<uintptr_t>(e.here()) < next) e.byte(0xcc);
    *next_out = reinterpret_cast<void**>(next);
    e.dword(0);
    return e.finish() ? start : nullptr;
}
bool install_site(unsigned i, const engine_patch::SiteSpec& spec, ProbeKind kind, const char* const extra[4]) {
    auto& site = sites[i];
    auto& report = reports[i];
    report = {spec.name,   spec.address,
              spec.length, kind != ProbeKind::CountOnly && kind != ProbeKind::ReadDispatch,
              "pending",   {extra[0], extra[1], extra[2], extra[3]}};
    probe_configure(i, ProbeConfig{kind, spec.ret_pop});
    if (!engine_patch::claim(site, spec)) {
        report.status = site.status;
        return false;
    }
    void** next = nullptr;
    void* stub = emit_entry_stub(i, &next);
    if (!stub || !next) {
        report.status = "stub_failed";
        return false;
    }
    // Continuation first, then the chain head: a thread entering the site
    // between the two stores still lands on a complete stub.
    if (!engine_patch::store_pointer(next, *site.entry)) {
        report.status = "chain_write_failed";
        return false;
    }
    if (!engine_patch::push_front(site, stub)) {
        report.status = "chain_failed";
        return false;
    }
    report.status = "active";
    ++installed_;
    return true;
}
bool install_all(const engine_patch::SiteSpec* specs, const unsigned* kinds, const char* const (*extras)[4],
                 const uint32_t* size_global) {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    frequency = f.QuadPart > 0 ? uint64_t(f.QuadPart) : 1;
    loading_trace::light::initialize();
    if (!exit_stub) exit_stub = emit_exit_stub();
    if (!exit_stub) return false;
    probe_set_exit_stub(exit_stub);
    probe_set_size_global(size_global);
    for (unsigned i = 0; i < site_count; ++i) {
        // Timed kinds hijack the return address; count-only kinds never do, so
        // a site with no exit stub is still counted.
        install_site(i, specs[i], static_cast<ProbeKind>(kinds[i]), extras[i]);
        const auto& r = reports[i];
        log("loading_probe_site index=%u name=%s va=0x%08lx length=%u timed=%u kind=%s status=%s x0=%s x1=%s x2=%s x3=%s atomic_write=%u",
            i, r.name, static_cast<unsigned long>(r.address), r.length, unsigned(r.timed),
            kind_name(static_cast<ProbeKind>(kinds[i])), r.status, r.extra[0], r.extra[1], r.extra[2], r.extra[3],
            unsigned(sites[i].atomic_write));
    }
    return installed_ != 0;
}
}
bool requested() {
    return requested_;
}
unsigned installed_sites() {
    return installed_;
}
const SiteReport& site(unsigned index) {
    return reports[index < site_count ? index : 0];
}
engine_patch::Site* resource_read_site() {
    return sites[resource_read_index].patched_in ? &sites[resource_read_index] : nullptr;
}
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized_) {
        SetLastError(error);
        return installed_ != 0;
    }
    initialized_ = true;
    requested_ = log_tier::debug_flag(L"X3M_LOADING_PROBES"); // X3M_LOADING_PROBES=1 or X3M_DEBUG=1
    if (!requested_) {
        SetLastError(error);
        return false;
    }
    if (!object_trace::executable_verified()) {
        log("loading_probes requested=1 installed=0 status=executable_mismatch");
        SetLastError(error);
        return false;
    }
    if (!engine_patch::install_window_open()) {
        log("loading_probes requested=1 installed=0 status=late_claim window_closed_by=%s",
            engine_patch::install_window_reason());
        SetLastError(error);
        return false;
    }
    engine_patch::SiteSpec specs[site_count];
    unsigned kinds[site_count];
    static const char* extra_storage[site_count][4];
    for (unsigned i = 0; i < site_count; ++i) {
        specs[i] = definitions[i].spec;
        kinds[i] = unsigned(definitions[i].kind);
        for (unsigned k = 0; k < 4; ++k) extra_storage[i][k] = definitions[i].extra[k];
    }
    const bool okay = install_all(specs, kinds, extra_storage, reinterpret_cast<const uint32_t*>(size_global_va));
    log("loading_probes requested=1 installed=%u sites=%u exit_stub=%u nesting=%u arena_used=%u scope=entry_counting_trampolines behaviour_change=0",
        installed_, site_count, exit_stub != nullptr, unsigned(loading_trace::light::nesting_available()),
        engine_patch::arena_used());
    SetLastError(error);
    return okay;
}
void report() {
    if (!installed_) return;
    const uint64_t stamp = loading_trace::light::tick();
    for (unsigned i = 0; i < site_count; ++i) {
        if (!sites[i].patched_in) continue;
        ProbeRow row{};
        probe_take(i, row);
        if (!row.calls && !row.exits && !row.overflow && !row.desync) continue;
        const auto& r = reports[i];
        log("loading_probe site=%s va=0x%08lx qpc=%llu calls=%llu exits=%llu inclusive_ticks=%llu max_ticks=%llu total_us=%.3f max_us=%.3f overflow=%llu desync=%llu bytes=%llu x0=%llu x1=%llu x2=%llu x3=%llu",
            r.name, static_cast<unsigned long>(r.address), stamp, row.calls, row.exits, row.inclusive, row.maximum,
            double(row.inclusive) * 1e6 / double(frequency), double(row.maximum) * 1e6 / double(frequency),
            row.overflow, row.desync, row.bytes, row.extra[0], row.extra[1], row.extra[2], row.extra[3]);
        for (unsigned k = 0; k < probe_caller_limit; ++k)
            if (row.caller_address[k] && row.caller_calls[k])
                log("loading_probe_caller site=%s caller=0x%08lx calls=%llu", r.name,
                    static_cast<unsigned long>(row.caller_address[k]), row.caller_calls[k]);
    }
    PathRecord paths[path_record_limit];
    const unsigned n = take_paths(paths, path_record_limit);
    static const char* const op_names[] = {"CreateDirectoryA", "DeleteFileA", "MoveFileA", "MoveFileExA"};
    for (unsigned i = 0; i < n; ++i) {
        const unsigned op = paths[i].op;
        const unsigned base = static_cast<unsigned>(loading_trace::Operation::DirectoryCreate);
        log("loading_probe_path op=%s path=%s", op >= base && op - base < 4 ? op_names[op - base] : "?", paths[i].path);
    }
}
void shutdown() {
    for (unsigned i = 0; i < site_count; ++i)
        if (sites[i].patched_in) {
            const bool okay = engine_patch::restore(sites[i]);
            log("loading_probe_site name=%s restored=%u status=%s", reports[i].name, unsigned(okay), sites[i].status);
        }
    // No new return-address hijacks from here on; a thread already inside a
    // stub still counts its entry. The per-thread shadow blocks, both TLS
    // slots and the arena stay allocated: a frame hijacked before the restore
    // returns into the exit stub later and reads its shadow (loading_probes.md,
    // "Shutdown").
    if (installed_)
        log("loading_probes shutdown retained_shadow_blocks=%u retained_bytes=%u reason=hijacked_frames_may_be_outstanding",
            loading_trace::light::shadow_blocks(),
            loading_trace::light::shadow_blocks() * loading_trace::light::shadow_block_bytes());
    probe_set_exit_stub(nullptr);
    installed_ = 0;
}
#ifdef X3M_LOADING_PROBES_FIXTURE
bool fixture_install(const engine_patch::SiteSpec* specs, unsigned count, const unsigned* kinds,
                     const uint32_t* size_global) {
    if (count != site_count) return false;
    static const char* extra_storage[site_count][4];
    for (unsigned i = 0; i < site_count; ++i)
        for (unsigned k = 0; k < 4; ++k) extra_storage[i][k] = definitions[i].extra[k];
    initialized_ = true;
    requested_ = true;
    return install_all(specs, kinds, extra_storage, size_global);
}
void fixture_shutdown() {
    shutdown();
}
#endif
}
