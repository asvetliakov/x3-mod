// Native fixture of the FL music keep (src/engine/fl/music_keep_fl.cpp, music_keep_sites_fl.h;
// docs/reverse-engineering/fl/music-keep.md, "Port (2026-11-03)"). Built with -DX3M_EDITION_FL=1
// -DX3M_MUSIC_KEEP_FL_FIXTURE by verification/analysis/test_fl_music_keep.py (the module force-includes
// fl_music_shim.h) and run natively (WSL interop): ./fl_music_keep.exe <X3FL.exe>.
//
// The installed X3FL.exe is mapped and relocated to a private base (fl_music_fixture.h). The real stop-all, play,
// MOV_StopMovie, pause, free wrapper and retirement bytes run against a synthetic arena: the media owner and its record
// list, media objects with recording COM fakes (Pause, DirectSound Stop, position), the VM with a completion slot
// table, the alert record, the active flag and the input flags. The seek, Run, status query, pump, media teardown and
// CRT free are replaced by recording fakes after the install (their heads are checked at install); the import slots
// hold fake gzwrite / gzclose exports. Caller returns are planted exits back into the harness.
#include "fl_music_fixture.h"
#include "../../src/engine/fl/music_keep_sites_fl.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/music_keep.h"
namespace F = x3m::fl::music_keep_fl;
namespace W = x3m::fl::music_windows;
namespace core = x3m::music_keep::core;
namespace engine_patch = x3m::engine_patch;
using std::uint32_t;
extern std::uintptr_t fl_music_fault_read_at, fl_music_fault_push_at;
extern unsigned fl_music_fault_read_count, fl_music_fault_read_skip, fl_music_faults_taken;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    fx::row(format, args);
    va_end(args);
}
HANDLE log_handle() noexcept {
    return reinterpret_cast<HANDLE>(std::uintptr_t(4));
}
namespace session_log {
void vlog(const char* format, va_list args) noexcept {
    fx::row(format, args);
}
}
namespace music_keep {
void fixture_base(std::uintptr_t base);
const char* fixture_window();
bool fixture_armed(bool* gate, bool* e, bool* queue, bool* pump);
const char* fixture_states(const char** gate, const char** e, const char** queue, const char** pump);
unsigned fixture_holds();
const core::TrackQueue* fixture_queue();
core::SavePump* fixture_pump();
const core::ResumeMemory* fixture_resume();
void fixture_zlib(FARPROC write, FARPROC close);
void fixture_reset_holds();
void fixture_set_story(const x3m::story::QueueSites& sites);
const core::KcPatch* fixture_kc();
}
}
namespace mk = x3m::music_keep;

// ---- recording fakes ----
struct Calls {
    unsigned seek = 0, run = 0, status = 0, pump = 0, teardown = 0, free = 0, pause_com = 0, stop_com = 0, write = 0,
             close = 0, completions = 0;
    uint32_t seek_record = 0, seek_ms = 0, run_record = 0, pump_media = 0, free_record = 0;
    uint32_t completion_ctx[16]{}, completion_status[16]{};
} calls;
uint32_t status_answer = 1, pump_answer = 1;
double position_seconds = 61.5;
extern "C" uint32_t __cdecl fake_seek_c(uint32_t record, uint32_t ms) {
    ++calls.seek;
    calls.seek_record = record;
    calls.seek_ms = ms;
    return 1;
}
extern "C" uint32_t __cdecl fake_run_c(uint32_t record, uint32_t) {
    ++calls.run;
    calls.run_record = record;
    return 1;
}
extern "C" uint32_t __cdecl fake_status_c(uint32_t, uint32_t) {
    ++calls.status;
    return status_answer;
}
extern "C" uint32_t __cdecl fake_pump_c(uint32_t media, uint32_t) {
    ++calls.pump;
    calls.pump_media = media;
    return pump_answer;
}
extern "C" uint32_t __cdecl fake_teardown_c(uint32_t, uint32_t) {
    ++calls.teardown;
    return 0;
}
extern "C" void __cdecl fake_free_c(uint32_t record) {
    ++calls.free;
    calls.free_record = record;
}
extern "C" void __cdecl fake_completion(uint32_t ctx, uint32_t status) {
    if (calls.completions < 16) {
        calls.completion_ctx[calls.completions] = ctx;
        calls.completion_status[calls.completions] = status;
    }
    ++calls.completions;
}
extern "C" int __cdecl fake_gzwrite(void*, const void*, unsigned size) {
    ++calls.write;
    return int(size);
}
extern "C" int __cdecl fake_gzclose(void*) {
    ++calls.close;
    return 0;
}
HRESULT STDMETHODCALLTYPE com_pause(void*) {
    ++calls.pause_com;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE com_stop(void*) {
    ++calls.stop_com;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE com_position(void*, double* seconds) {
    *seconds = position_seconds;
    return S_OK;
}
extern "C" void fake_seek();
extern "C" void fake_run();
extern "C" void fake_status();
extern "C" void fake_pump();
extern "C" void fake_teardown();
extern "C" void fake_free();
#define FASTCALL2(name, c)                                                                                             \
    ".globl _" name "\n_" name ":\n\tpushl %edx\n\tpushl %ecx\n\tcall _" c "\n\taddl $8,%esp\n\tret\n\t"
asm(".text\n\t" FASTCALL2("fake_seek", "fake_seek_c") FASTCALL2("fake_run", "fake_run_c")
        FASTCALL2("fake_status", "fake_status_c") FASTCALL2("fake_pump", "fake_pump_c")
            FASTCALL2("fake_teardown", "fake_teardown_c")
    ".globl _fake_free\n_fake_free:\n\tpushl 4(%esp)\n\tcall _fake_free_c\n\taddl $4,%esp\n\tret\n\t");
constexpr uint32_t teardown_rva = 0x0ffd70, crt_free_rva = 0x12f4e5;
void fakes(bool on) {
    const struct {
        uint32_t rva;
        void (*fn)();
    } list[] = {{F::seek_rva, &fake_seek}, {F::run_rva, &fake_run}, {F::status_rva, &fake_status},
                {F::pump_rva, &fake_pump}, {teardown_rva, &fake_teardown}, {crt_free_rva, &fake_free}};
    for (const auto& f : list) {
        if (on)
            fx::jump_at(f.rva, reinterpret_cast<void*>(f.fn));
        else
            fx::restore_bytes(f.rva, 6);
    }
}

// ---- the arena ----
alignas(16) uint32_t owner[0xfb0 / 4];
struct alignas(16) Record {
    uint32_t w[16];
};
struct alignas(16) Media {
    uint32_t w[0xb4 / 4];
};
Record records[16];
Media medias[16];
void* vt_control[16] = {};
void* vt_buffer[32] = {};
void* vt_position[16] = {};
struct Com {
    void** vtbl;
} control{vt_control}, buffer{vt_buffer}, position{vt_position}, stream{vt_control};
alignas(16) uint32_t vm[0x1500 / 4], registry[8], active_flag_word = 1, input_word = 0;
alignas(16) unsigned char code_buffer[64];
void reset_owner() {
    std::memset(owner, 0, sizeof owner);
    owner[0] = fx::u32(&owner[1]);
    owner[2] = fx::u32(owner);
}
// Links record i at the head (as the allocator does) with id, flags, context, slot and media i.
Record* make(unsigned i, uint32_t id, uint32_t flags, uint32_t ctx, uint32_t slot, bool ds = false) {
    Record* r = &records[i];
    Media* m = &medias[i];
    std::memset(r, 0, sizeof *r);
    std::memset(m, 0, sizeof *m);
    m->w[0x04 / 4] = fx::u32(&stream);
    m->w[0x44 / 4] = fx::u32(&buffer);
    m->w[0x48 / 4] = 2;
    m->w[0x70 / 4] = fx::u32(&position);
    m->w[0x74 / 4] = fx::u32(&control);
    m->w[0x8c / 4] = ds ? 0x40 : 0;
    r->w[0x10 / 4] = id;
    r->w[0x14 / 4] = ctx;
    r->w[0x18 / 4] = slot;
    r->w[0x24 / 4] = fx::u32(m);
    r->w[0x2c / 4] = flags;
    const uint32_t first = owner[0];
    r->w[0] = first;
    r->w[1] = fx::u32(owner);
    *reinterpret_cast<uint32_t*>(first + 4) = fx::u32(r);
    owner[0] = fx::u32(r);
    return r;
}
bool linked(const Record* r) {
    for (uint32_t node = owner[0]; node && *reinterpret_cast<uint32_t*>(node); node = *reinterpret_cast<uint32_t*>(node))
        if (node == fx::u32(r)) return true;
    return false;
}
void lay_out() {
    reset_owner();
    vt_control[0x20 / 4] = reinterpret_cast<void*>(&com_pause);
    vt_buffer[0x48 / 4] = reinterpret_cast<void*>(&com_stop);
    vt_position[0x24 / 4] = reinterpret_cast<void*>(&com_position);
    std::memset(vm, 0, sizeof vm);
    vm[1] = 1; // the slot-table guard
    for (unsigned s = 0; s < 32; ++s) vm[(0x34 + 0x18 * s) / 4] = fx::u32(reinterpret_cast<void*>(&fake_completion));
    vm[0x08 / 4] = fx::u32(code_buffer);
    vm[0x10 / 4] = sizeof code_buffer;
    vm[0x143c / 4] = 4242;
    std::memset(registry, 0, sizeof registry);
    fx::put32(F::owner_slot_rva, fx::u32(owner));
    fx::put32(F::vm_slot_rva, fx::u32(vm));
    fx::put32(F::alert_registry_slot_rva, fx::u32(registry));
    fx::put32(F::active_flag_rva, active_flag_word);
    fx::put32(F::input_flags_slot_rva, fx::u32(&input_word));
    fx::put32(F::gzwrite_iat_rva, fx::u32(reinterpret_cast<void*>(&fake_gzwrite)));
    fx::put32(F::gzclose_iat_rva, fx::u32(reinterpret_cast<void*>(&fake_gzclose)));
}
// ---- a KC task: header +8 id, +0x10 capacity, +0x14 cell end, +0x18 index, +0x1c IP; 5-byte cells ----
struct Task {
    uint32_t w[16];
    unsigned char cells[32 * 5];
} task_c;
void cell(Task& t, int index, unsigned char tag, uint32_t value) {
    unsigned char* at = t.cells + sizeof t.cells + 5 * index;
    at[0] = tag;
    std::memcpy(at + 1, &value, 4);
}
uint32_t cell_value(Task& t, int index) {
    uint32_t v;
    std::memcpy(&v, t.cells + sizeof t.cells + 5 * index + 1, 4);
    return v;
}
x3m::story::QueueSites story{};
constexpr uint32_t play_stop_ip = 0x0db54, play_ip = 0x0db88, loop_return = 0x0f36c, change_return = 0x0f3c3;
void make_task(uint32_t id, uint32_t ip, uint32_t old_id, uint32_t new_id) {
    std::memset(&task_c, 0, sizeof task_c);
    task_c.w[0x08 / 4] = id;
    task_c.w[0x10 / 4] = 32;
    task_c.w[0x14 / 4] = fx::u32(task_c.cells + sizeof task_c.cells);
    task_c.w[0x18 / 4] = uint32_t(-19);
    task_c.w[0x1c / 4] = ip;
    cell(task_c, -18, 1, old_id);
    cell(task_c, -13, 3, loop_return);
    cell(task_c, -11, 1, new_id);
    cell(task_c, -10, 1, 0);
    cell(task_c, -9, 1, 1);
    cell(task_c, -7, 3, change_return);
}

// ---- entries ----
const uint32_t exits[] = {0x0ec827, 0x078a3d, 0x07b7cf, 0x079466, 0x0b593f, 0x113911, 0x0b5993, 0x077be7};
void plant_exits(bool on) {
    for (uint32_t rva : exits) {
        if (on)
            fx::jump_at(rva, reinterpret_cast<void*>(&fx_exit));
        else
            fx::restore_bytes(rva, 6);
    }
}
fx::Call stop_all(uint32_t ret_rva) {
    fx_df = 0;
    return fx::enter(fx::va(F::stop_all_rva), {0xeaeaeaea, 0x11111111, 0x22222222, 0xb0b0b0b0, 0x5e5e5e5e, 0xd1d1d1d1,
                                               0xebebebeb},
                     {}, fx::va(ret_rva));
}
fx::Call play(uint32_t ret_rva, uint32_t slot, uint32_t ctx, uint32_t id, uint32_t task = 0x5e5e5e5e) {
    fx_df = 0;
    return fx::enter(fx::va(F::play_rva), {0xeaeaeaea, slot, ctx, 0xb0b0b0b0, task, 0xd1d1d1d1, 0xebebebeb},
                     {id, 0, 0, 0, uint32_t(-1), uint32_t(-1), uint32_t(-1), 0}, fx::va(ret_rva));
}
fx::Call stop_movie(uint32_t ret_rva, uint32_t slot, uint32_t ctx, uint32_t id, uint32_t task = 0x5e5e5e5e) {
    fx_df = 0;
    return fx::enter(fx::va(F::stop_movie_rva), {0xeaeaeaea, slot, ctx, 0xb0b0b0b0, task, 0xd1d1d1d1, 0xebebebeb}, {id},
                     fx::va(ret_rva));
}
// The callee-saved registers, ESP (plain RET: the return address popped), LastError, DF clear, MXCSR and the x87
// control word after a whole native returned. XMM registers are caller-saved at these entries and calls (the i386
// ABI); the one mid-function site, Patch A, sits in a stop-all body with no SSE/x87 instruction
// (verify_music_keep_sites_fl.py stop_all_no_simd), so the stubs save the integer state only, as AP's do.
bool abi_kept(const fx::Call& c, bool ret_popped = true) {
    return fx::kept({fx::ebx, fx::esi, fx::edi, fx::ebp}) && fx_out_esp == fx_esp_before + (ret_popped ? 4 : 0) &&
           c.error == fx::sentinel_error && !(fx_out_flags & 0x400) && fx_out_mxcsr == fx_in_mxcsr &&
           fx_out_fcw == fx_in_fcw;
}
// A call site entered directly (the thunk returns to the planted exit after the call): every register as entered
// except EAX when `answered`.
fx::Call call_site(uint32_t site_rva, uint32_t ecx, uint32_t edx) {
    fx::jump_at(site_rva + 5, reinterpret_cast<void*>(&fx_exit));
    fx_df = 0;
    const fx::Call c = fx::enter(fx::va(site_rva), {0xeaeaeaea, ecx, edx, 0xb0b0b0b0, ecx, 0xd1d1d1d1, 0xebebebeb}, {});
    fx::restore_bytes(site_rva + 5, 6);
    return c;
}
bool init(const char* expect) {
    SetLastError(fx::sentinel_error);
    mk::initialize();
    char d[300];
    std::snprintf(d, sizeof d, "state=%s window=%s", mk::state(), mk::fixture_window());
    const bool ok = GetLastError() == fx::sentinel_error && !std::strcmp(mk::state(), expect);
    fx::check(ok, expect, d);
    return ok;
}
bool sites_pristine() {
    return fx::same_as_pristine(F::stop_all_rva, 7) && fx::same_as_pristine(F::a_site_rva, 7) &&
           fx::same_as_pristine(F::c_site_rva, 5) && fx::same_as_pristine(F::d_site_rva, 5) &&
           fx::same_as_pristine(F::e_site_rva, 5) && fx::same_as_pristine(F::stop_movie_rva, 5) &&
           fx::same_as_pristine(F::play_rva, 5) && fx::get32(F::gzwrite_iat_rva) == fx::u32(reinterpret_cast<void*>(&fake_gzwrite)) &&
           fx::get32(F::gzclose_iat_rva) == fx::u32(reinterpret_cast<void*>(&fake_gzclose));
}

int main(int argc, char** argv) {
    if (argc < 2 || !fx::map_exe(argv[1])) {
        std::printf("FAIL map_exe\n");
        return 2;
    }
    std::printf("IMAGE base=%08x size=%08x\n", fx::u32(fx::image), fx::image_size);
    lay_out();
    mk::fixture_zlib(reinterpret_cast<FARPROC>(&fake_gzwrite), reinterpret_cast<FARPROC>(&fake_gzclose));
    SetEnvironmentVariableW(L"X3M_MUSIC_KEEP", L"1");
    SetEnvironmentVariableW(L"X3M_MUSIC_QUEUE", L"1");
    SetEnvironmentVariableW(L"X3M_MUSIC_DOCK_KEEP", L"1");
    // ---- refusals: executable, every window's change, a caller, the rollbacks ----
    init("executable_mismatch");
    mk::fixture_base(fx::base);
    unsigned flips = 0, refused = 0;
    for (const W::CodeWindow& w : W::keep_windows) {
        for (uint32_t at : {w.rva, w.rva + w.length / 2, w.rva + w.length - 1}) {
            fx::image[at] ^= 0x01;
            mk::initialize();
            ++flips;
            refused += !std::strcmp(mk::state(), "bytes_mismatch") && !std::strcmp(mk::fixture_window(), w.name);
            fx::image[at] ^= 0x01;
        }
        for (unsigned k = 0; k < w.count; ++k) {
            const uint32_t at = w.rva + W::keep_fixups[w.first + k].offset;
            fx::put32(at, fx::get32(at) + 0x10000);
            mk::initialize();
            ++flips;
            refused += !std::strcmp(mk::state(), "bytes_mismatch");
            fx::put32(at, fx::get32(at) - 0x10000);
        }
    }
    std::printf("FLIPS windows=%u cases=%u refused=%u\n", unsigned(sizeof W::keep_windows / sizeof W::keep_windows[0]),
                flips, refused);
    fx::check(flips == refused && sites_pristine(), "every_window_change_refused");
    for (const F::Caller& c : F::stop_callers) { // each stop-all caller's rel32 off by one
        fx::put32(c.call_rva + 1, fx::get32(c.call_rva + 1) + 1);
        init("callers_mismatch");
        fx::put32(c.call_rva + 1, fx::get32(c.call_rva + 1) - 1);
    }
    // A later claim's read-back flipped / chain refused: every earlier site and the import slots back.
    for (uint32_t rva : {F::a_site_rva, F::stop_all_rva, F::stop_movie_rva}) {
        fl_music_fault_read_at = fx::base + rva;
        fl_music_fault_read_count = 5;
        fl_music_fault_read_skip = rva == F::stop_movie_rva ? 1 : 0; // its expected-bytes read is five bytes too
        init("readback_failed");
        fx::check(sites_pristine(), "readback_rolled_back");
        fl_music_fault_push_at = fx::base + rva;
        init("chain_failed");
        fx::check(sites_pristine(), "chain_rolled_back");
    }
    fl_music_fault_read_at = fx::base + F::play_rva; // the optional queue site: the keep installs without it
    fl_music_fault_read_count = 5;
    fl_music_fault_read_skip = 1;
    init("ok");
    {
        bool gate, e, queue, pump;
        const char *gs, *es, *qs, *ps;
        mk::fixture_armed(&gate, &e, &queue, &pump);
        mk::fixture_states(&gs, &es, &qs, &ps);
        fx::check(gate && e && !queue && pump && !std::strcmp(qs, "readback_failed") && fx::same_as_pristine(F::play_rva, 5),
                  "queue_optional_rolled_back", qs);
        fx::check(mk::shutdown() && sites_pristine(), "shutdown_restores_everything");
    }
    // ---- the install ----
    init("ok");
    bool gate, e, queue, pump;
    const char *gs, *es, *qs, *ps;
    const bool armed = mk::fixture_armed(&gate, &e, &queue, &pump);
    mk::fixture_states(&gs, &es, &qs, &ps);
    std::printf("ARMED keep=%u gate=%s e=%s queue=%s pump=%s\n", armed, gs, es, qs, ps);
    fx::check(armed && gate && e && queue && pump, "installed_all");
    fx::check(fx::image[F::a_site_rva] == 0xe9 && fx::image[F::stop_all_rva] == 0xe9 && fx::image[F::play_rva] == 0xe9 &&
                  fx::image[F::stop_movie_rva] == 0xe9 && fx::image[F::c_site_rva] == 0xe8 &&
                  fx::get32(F::gzwrite_iat_rva) != fx::u32(reinterpret_cast<void*>(&fake_gzwrite)),
              "sites_written");
    fx::check(std::strstr(fx::last_row, "music_keep_save_pump_hook reason=ok save_mode=keep_running") != nullptr,
              "save_pump_row", fx::last_row);
    fakes(true);
    plant_exits(true);

    // ---- D: the status thunk at its call site ----
    {
        Record* r = make(0, 1101, 0x82, 0, 0);
        stop_all(0x0ec827); // alt-tab: skip_all (the hold the status gate reads)
        const bool skip = (r->w[0x2c / 4] & 2) && calls.pause_com == 0 && calls.completions == 0 && mk::fixture_holds() == 1;
        fx::check(skip && abi_kept(fx::Call{fx::sentinel_error}), "alt_tab_skip_all");
        fx::put32(F::active_flag_rva, 0);
        const unsigned s0 = calls.status;
        fx::Call c = call_site(F::d_site_rva, fx::u32(r), 0x28282828);
        fx::check(fx_out[fx::eax] == 1 && calls.status == s0 && fx::kept({fx::ecx, fx::edx, fx::ebx, fx::esi, fx::edi, fx::ebp}) &&
                      fx_out_esp == fx_esp_before && c.error == fx::sentinel_error,
                  "status_answered_while_inactive");
        input_word = 0x4000; // RunInBackground: the engine's own answer
        status_answer = 2;
        call_site(F::d_site_rva, fx::u32(r), 0x28282828);
        fx::check(fx_out[fx::eax] == 2 && calls.status == s0 + 1, "status_forwarded_run_in_background");
        input_word = 0;
        fx::put32(F::active_flag_rva, 1);
        call_site(F::d_site_rva, fx::u32(r), 0x28282828);
        fx::check(fx_out[fx::eax] == 2 && calls.status == s0 + 2 && fx::kept({fx::ecx, fx::edx}), "status_forwarded_active");
        status_answer = 1;
    }
    // ---- pause: keep_running for a non-DirectSound music record; the replay skips the seek and Runs ----
    {
        reset_owner();
        mk::fixture_reset_holds();
        Record* r = make(1, 1102, 0x82, 5, 3);
        calls = Calls{};
        const fx::Call c = stop_all(0x07b7cf);
        const bool kept = !(r->w[0x2c / 4] & 2) && calls.pause_com == 0 && calls.stop_com == 0 && calls.completions == 1 &&
                          calls.completion_ctx[0] == 5 && calls.completion_status[0] == 1 && r->w[0x14 / 4] == 0 &&
                          r->w[0x18 / 4] == 0 && mk::fixture_holds() == 1;
        fx::check(kept && abi_kept(c), "pause_keep_running");
        const fx::Call p = play(0x0b593f, 3, 6, 1102);
        fx::check(calls.seek == 0 && calls.run == 1 && (r->w[0x2c / 4] & 2) && r->w[0x14 / 4] == 6 && r->w[0x18 / 4] == 3 &&
                      mk::fixture_holds() == 0 && abi_kept(p),
                  "pause_replay_resumes");
        // a DirectSound record under the pause: paused (the engine's Pause and Stop run)
        Record* d = make(2, 1103, 0xc2, 0, 0, true);
        calls = Calls{};
        stop_all(0x07b7cf);
        fx::check(calls.pause_com >= 1 && calls.stop_com >= 1 && !(d->w[0x2c / 4] & 2), "pause_directsound_paused");
        // a load: the never-replayed keep-running hold of another record is paused at the entry, every hold dropped
        reset_owner();
        mk::fixture_reset_holds();
        Record* o = make(3, 1104, 0x82, 0, 0);
        calls = Calls{};
        stop_all(0x07b7cf); // keep_running, hold
        o->w[0x2c / 4] &= ~2u;
        const unsigned p0 = calls.pause_com;
        const fx::Call l = stop_all(0x079466);
        fx::check(calls.pause_com > p0 && mk::fixture_holds() == 0 && abi_kept(l), "load_pauses_orphaned_hold");
    }
    // ---- C and E at their call sites: a skipped seek answers EAX = 1 with every register intact ----
    {
        reset_owner();
        Record* r = make(4, 1105, 0x82, 0, 0); // playing, start 0: skip
        calls = Calls{};
        fx::Call c = call_site(F::c_site_rva, fx::u32(r), 0);
        fx::check(fx_out[fx::eax] == 1 && calls.seek == 0 && fx::kept({fx::ecx, fx::edx, fx::ebx, fx::esi, fx::edi, fx::ebp}) &&
                      fx_out_esp == fx_esp_before && c.error == fx::sentinel_error,
                  "seek_skipped");
        r->w[0x2c / 4] = 0x80; // not playing, no hold: vanilla seek with the registers restored
        call_site(F::c_site_rva, fx::u32(r), 1234);
        fx::check(calls.seek == 1 && calls.seek_record == fx::u32(r) && calls.seek_ms == 1234 && fx::kept({fx::ebx, fx::esi}),
                  "seek_forwarded");
        r->w[0x2c / 4] = 0x01; // not music: vanilla
        call_site(F::c_site_rva, fx::u32(r), 77);
        fx::check(calls.seek == 2 && calls.seek_ms == 77, "seek_non_music_forwarded");
        call_site(F::e_site_rva, fx::u32(r), 0);
        fx::check(calls.run == 1 && calls.run_record == fx::u32(r) && fx::kept({fx::ecx, fx::edx, fx::ebx, fx::esi}),
                  "run_forwarded");
    }
    // ---- the save pump: keep_running + armed, gzwrite pumps, the save's gzclose, the replay skips the Run ----
    {
        reset_owner();
        Record* r = make(5, 1106, 0xc2, 7, 2, true);
        calls = Calls{};
        stop_all(0x078a3d);
        const core::SavePump* pmp = mk::fixture_pump();
        fx::check(pmp->pumping && pmp->record == fx::u32(r) && calls.pause_com == 0 && calls.stop_com == 0 &&
                      !(r->w[0x2c / 4] & 2) && calls.completions == 1,
                  "save_keep_running_armed");
        // gzwrite through the import slot (cdecl, three arguments): the first write samples and pumps
        using GzWrite = int(__cdecl*)(void*, const void*, unsigned);
        const auto write = reinterpret_cast<GzWrite>(std::uintptr_t(fx::get32(F::gzwrite_iat_rva)));
        SetLastError(fx::sentinel_error);
        const int written = write(nullptr, nullptr, 512);
        fx::check(written == 512 && calls.write == 1 && calls.pump == 1 && calls.pump_media == r->w[0x24 / 4] &&
                      GetLastError() == fx::sentinel_error,
                  "gzwrite_pumps");
        for (unsigned i = 0; i < 3000; ++i) write(nullptr, nullptr, 1);
        fx::check(calls.write == 3001 && calls.pump == 1, "gzwrite_cadence"); // 250 ms between pumps
        // the save's own close: [esp] = base+0x106963, [esp+8] = base+0x079201
        fx::jump_at(F::gzclose_return_rva, reinterpret_cast<void*>(&fx_exit));
        fx_df = 0;
        const fx::Call cl = fx::enter(fx::get32(F::gzclose_iat_rva), {0, 0, 0, 0xb0b0b0b0, 0x5e5e5e5e, 0xd1d1d1d1, 0xebebebeb},
                                      {0x51515151, fx::va(F::save_close_return_rva)}, fx::va(F::gzclose_return_rva));
        fx::restore_bytes(F::gzclose_return_rva, 6);
        fx::check(calls.close == 1 && !pmp->pumping && pmp->kept && abi_kept(cl), "save_gzclose_ends_pumping");
        const unsigned runs = calls.run;
        play(0x0b593f, 2, 8, 1106); // the replay: seek skipped, Run skipped (the buffer is still playing)
        fx::check(calls.seek == 0 && calls.run == runs && (r->w[0x2c / 4] & 2) && !pmp->active() &&
                      std::strstr(fx::last_row, "run=skipped") != nullptr,
                  "save_replay_skips_run", fx::last_row);
    }
    // ---- the combat resume: a stop on the alert's rising edge remembers the position; the replay seeks to it ----
    {
        reset_owner();
        Record* r = make(6, 1107, 0x82, 4, 1);
        registry[1] = 0;
        registry[2] = 1; // rising edge
        calls = Calls{};
        const fx::Call s = stop_movie(0x0b5993, 6, 9, 1107);
        const core::ResumeMemory* m = mk::fixture_resume();
        fx::check(m->valid && m->id == 1107 && m->ms == 61500 && calls.pause_com == 1 && abi_kept(s), "resume_remembered");
        registry[2] = 0;
        play(0x0b593f, 6, 9, 1107);
        fx::check(calls.seek == 1 && calls.seek_ms == 61500 && !m->valid && r->w[0x1c / 4] == 0, "resume_seeks");
    }
    // ---- the track queue and the docking keep on a detected story ----
    {
        reset_owner();
        story = x3m::story::QueueSites{};
        story.key = x3m::story::VmKey{fx::u32(vm), fx::u32(code_buffer), sizeof code_buffer, 4242};
        story.kc_play_stop_ip = play_stop_ip;
        story.kc_play_ip = play_ip;
        story.kc_loop_music_return = loop_return;
        story.kc_change_track_return = change_return;
        story.warp_fork_offset = 16;
        const unsigned char window[6] = {0x6f, 0x33, 0x2a, 0x00, 0x00, 0x00}, patch[6] = {0x32, 0x2a, 0x00, 0x00, 0x00, 0x01};
        std::memcpy(story.warp_window_memory, window, 6);
        std::memcpy(story.warp_patch_memory, patch, 6);
        std::memcpy(code_buffer + 16, window, 6);
        mk::fixture_set_story(story);
        registry[1] = registry[2] = 0;
        Record* o = make(7, 1201, 0x82, 21, 2); // the playing sector track, owned by T_old (21) through slot 2
        make_task(30, play_stop_ip, 1201, 1301);
        calls = Calls{};
        const fx::Call d = stop_movie(0x0b5993, 5, 30, 1201, fx::u32(&task_c));
        const core::TrackQueue* q = mk::fixture_queue();
        fx::check(q->state == core::QueueState::orphaned && o->w[0x10 / 4] == q->key && (o->w[0x2c / 4] & 2) &&
                      calls.pause_com == 0 && calls.completions == 2 && calls.completion_ctx[0] == 21 &&
                      calls.completion_ctx[1] == 30 && abi_kept(d),
                  "queue_defer_consumed");
        Record* n = make(8, 1301, 0x80, 0, 0); // C's MOV_LoadMovie record
        make_task(30, play_ip, 0, 0);
        const unsigned seeks = calls.seek;
        const fx::Call p = play(0x0b593f, 5, 30, 1301, fx::u32(&task_c));
        fx::check(q->state == core::QueueState::parked && o->w[0x14 / 4] == 30 && o->w[0x18 / 4] == 5 && calls.seek == seeks &&
                      !(n->w[0x2c / 4] & 2) && abi_kept(p),
                  "queue_park_consumed");
        // another music play (the helper) ends the queue: C woken through O's slot, P freed by the wrapper (slot 32:
        // no requester completion), the play itself runs
        Record* other = make(9, 1401, 0x80, 0, 0);
        calls = Calls{};
        play(0x113911, 1, 2, 1401);
        fx::check(q->state == core::QueueState::idle && !linked(o) && calls.free == 1 && calls.free_record == fx::u32(o) &&
                      calls.teardown == 1 && calls.completions >= 1 && calls.completion_ctx[0] == 30 &&
                      (other->w[0x2c / 4] & 2) && calls.seek == 1,
                  "queue_end_other_play_frees");
        // the docking keep: a request for 2003 (a dock track) is deferred as a request for the playing id, written
        // into the requesting task's id cell
        reset_owner();
        Record* o2 = make(10, 1202, 0x82, 22, 2);
        make_task(31, play_stop_ip, 1202, 2003);
        calls = Calls{};
        stop_movie(0x0b5993, 5, 31, 1202, fx::u32(&task_c));
        fx::check(q->state == core::QueueState::orphaned && q->new_id == 1202 && cell_value(task_c, -11) == 1202 &&
                      o2->w[0x10 / 4] == q->key,
                  "dock_keep_defers_same_id");
        // a load stop-all ends it (the walk then sees neither O nor the private key) and closes the KC gate
        const unsigned begins = mk::fixture_kc()->load_begins;
        stop_all(0x079466);
        fx::check(q->state == core::QueueState::idle && !linked(o2) && mk::fixture_kc()->load_begins == begins + 1,
                  "queue_end_load");
        // the KC patch from Present: two agreeing due checks 8 frames apart, then the write
        for (unsigned f = 1; f <= 40; ++f) mk::present(1, f, false);
        fx::check(!std::memcmp(code_buffer + 16, patch, 6) && mk::fixture_kc()->applied == 1, "kc_patch_applied");
    }
    // ---- the late window: everything restored, then a fresh initialize after the close writes nothing ----
    plant_exits(false);
    fakes(false);
    fx::check(mk::shutdown() && sites_pristine(), "shutdown_restores");
    engine_patch::close_install_window("fixture");
    init("late_claim");
    fx::check(sites_pristine(), "late_claim_wrote_nothing");
    std::printf("FAULTS taken=%u\n", fl_music_faults_taken);
    return fx::result();
}
