// FL edition of x3m::music_keep (src/proxy/music_keep.h; the ap target keeps src/proxy/music_keep.cpp): sector-music
// retention through focus loss, pause and save, the save pump, the combat resume, the track queue and the docking keep
// on X3FL.exe (docs/reverse-engineering/fl/music-keep.md, "Port (2026-11-03)"; sites, register contracts and the
// FL -> AP caller-role map: music_keep_sites_fl.h). Every decision is AP's music_keep_core.h (decide_stop, decide_seek,
// decide_status, stale_hold_to_pause, drop_holds_by_id, SavePump, the resume memory, TrackQueue, KcPatch) and
// story_detect.h, unchanged; this file holds the FL stubs, the FL register/stack indices, the FL engine calls (pause
// ECX = record, pump ECX = media, the free wrapper with slot 32) and the gzwrite / gzclose import slots.
//
// X3M_MUSIC_KEEP=1 (the claims, inside the engine_patch install window; all or none, read back, rolled back):
//   A    base+0x0b423e  stop-all classifier (7 bytes; plain write: crosses a qword)
//   C    base+0x0b4d26  play's seek call -> thunk (EDX = start ms replaced for a combat resume)
//   D    base+0x0b434a  manager's status call -> thunk (optional: a refusal leaves alt-tab in the paused mode)
//   E    base+0x0b4d42  play's Run call -> thunk (optional: a refusal keeps the save's paused mode)
//   T1   base+0x0b4210  stop-all entry (7 bytes): pauses an orphaned keep-running hold, ends the queue, the pump episode,
//                       the resume memory on vanilla callers, closes the KC load gate on the load / session callers
//   T3   base+0x0b47a0  MOV_StopMovie entry (5 bytes): drops the id's holds, the queue's defer/collapse, the resume
//   save pump: the gzwrite and gzclose import slots of X3FL.exe (base+0x1503f0, base+0x150408) exchanged for two
//        forwarding stubs (one load and a branch while no save pumps), only with E live; else the save stays paused.
// X3M_MUSIC_QUEUE=1 (with the keep): T2 base+0x0b4c50 (play entry, 5 bytes) and the KC patch from Present.
// X3M_MUSIC_DOCK_KEEP=1 (with the queue): docking requests deferred as requests for the playing id.
// X3M_MUSIC_TRACE is AP only on FL (one music_trace row says so).
//
// Threads: stop-all runs on the WndProc/main-loop thread, play and MOV_StopMovie inside the script step of the main
// loop, the save's gzwrite on the save thread (the main loop), Present on the main thread. The FL loader has no unload
// path for this module (cull_small_parts_fl.cpp): no shutdown wiring; the stubs stay in the pinned DLL.
#include "../../proxy/music_keep.h"
#include "../../proxy/capture.h"
#include "../../proxy/config.h"
#include "../../proxy/cpu_state.h"
#include "../../proxy/engine_memory.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/log_tiers.h"
#include "../../proxy/session_log.h"
#include "executable_identity_fl.h"
#include "music_keep_sites_fl.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::music_keep::core;
namespace core = x3m::music_keep::core;
namespace engine_patch = x3m::engine_patch;
namespace F = x3m::fl::music_keep_fl;
namespace W = x3m::fl::music_windows;

std::uintptr_t base_ = 0;
std::uint32_t owner_slot_ = 0, vm_slot_ = 0, alert_slot_ = 0, active_flag_ = 0, input_slot_ = 0;
bool keep_patched_ = false, keep_armed_ = false;
const char* keep_state_ = "disabled";
const char* window_ = "-";
engine_patch::Site a_site_{}, entry_site_{}, stop_movie_site_{}, play_site_{};
engine_patch::CallSite c_site_{}, d_site_{}, e_site_{};
bool status_gate_ = false;
const char* status_gate_state_ = "disabled";
unsigned long gated_ = 0;
unsigned last_active_ = 2;
Holds holds_;
bool queue_requested_ = false, queue_armed_ = false, queue_variable_ = false, dock_keep_ = false;
const char* queue_state_ = "disabled";
TrackQueue queue_;
KcPatch kc_;
unsigned char kc_window_seen_[warp_window_length]{};
bool e_live_ = false;
const char* e_state_ = "disabled";
bool save_pump_ = false;
const char* save_pump_state_ = "unbound";
bool save_pump_reported_ = false, keep_present_ = false, trace_present_ = false;
bool write_slot_live_ = false, close_slot_live_ = false;
SavePump pump_;
std::uint32_t gzclose_caller_ = 0;
std::uint64_t frequency_ = 0;
bool resume_ok_ = false;
const char* resume_state_ = "disabled";
ResumeMemory resume_;
volatile unsigned long long current_frame_ = 0;
unsigned long lines_ = 0, seq_ = 0, suppressed_ = 0, walks_cut_ = 0;
bool cap_noted_ = false;
unsigned long queue_lines_ = 0, queue_suppressed_ = 0;
bool queue_cap_noted_ = false;
std::uint32_t queue_free_pending_ = 0;
std::uint32_t combat_track_ = 0;
#ifdef X3M_MUSIC_KEEP_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
FARPROC fixture_gzwrite_ = nullptr, fixture_gzclose_ = nullptr; // the fixture's zlib exports (no zlib1.dll there)
#endif

std::uint32_t load32(std::uint32_t at) {
    std::uint32_t v = 0;
    std::memcpy(&v, reinterpret_cast<const void*>(std::uintptr_t(at)), 4);
    return v;
}
void store32(std::uint32_t at, std::uint32_t value) {
    std::memcpy(reinterpret_cast<void*>(std::uintptr_t(at)), &value, 4);
}
std::uint64_t qpc() {
    LARGE_INTEGER v{};
    return QueryPerformanceCounter(&v) && v.QuadPart > 0 ? std::uint64_t(v.QuadPart) : 0;
}
// The AP role of an FL caller return (0: none, the vanilla/unknown caller of every AP rule) and its FL name.
std::uint32_t stop_role(std::uint32_t ret) {
    return F::ap_role(F::stop_callers, ret, base_);
}
const char* stop_name(std::uint32_t ret) {
    return F::role_name(F::stop_callers, ret, base_);
}
std::uint32_t stop_movie_role(std::uint32_t ret) {
    return F::ap_role(F::stop_movie_callers, ret, base_);
}
std::uint32_t play_role(std::uint32_t ret) {
    return F::ap_role(F::play_callers, ret, base_);
}
x3m::story::QueueSites kc_sites_{}, kc_candidate_{};
x3m::story::Retries kc_retries_{};
unsigned char kc_scratch_[x3m::story::scratch_size];
bool engine_read(std::uint32_t at, void* out, unsigned n) {
    return x3m::engine_memory::read(at, out, n);
}
const x3m::story::QueueSites* sites_now() {
    const std::uint32_t vm = load32(vm_slot_);
    x3m::story::VmKey key;
    if (!vm || !x3m::story::read_key(engine_read, vm, key)) return nullptr;
    return kc_sites_.detected && kc_sites_.key == key && kc_sites_.outcome.ok() ? &kc_sites_ : nullptr;
}
// The cockpit alert record R = *(base+0x1a82e4): [R+4] committed level, [R+8] this frame's level.
Alert read_alert() {
    Alert a;
    std::uint32_t record = 0, levels[3]{};
    a.ok = x3m::engine_memory::read(alert_slot_, &record, 4) && record &&
           x3m::engine_memory::read(record, levels, sizeof levels);
    a.committed = levels[1];
    a.current = levels[2];
    return a;
}
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&keep_patched_), &module) != FALSE &&
           module != nullptr;
}
const char* write_kind(bool in, bool atomic) {
    return in ? (atomic ? "atomic" : "plain") : "none";
}
void write_line(const char* format, ...);
// Bounded walk of the record list (owner *(base+0x1a84b4), first node [owner], the sentinel's next is 0).
template <class Visit> void walk_records(const char* what, Visit&& visit) {
    const std::uint32_t head = load32(owner_slot_);
    if (!head) return;
    std::uint32_t node = load32(head);
    unsigned i = 0;
    for (; i < list_walk_limit && node; ++i) {
        if (load32(node) == 0) return;
        if (visit(node)) return;
        node = load32(node);
    }
    if (i == list_walk_limit) {
        ++walks_cut_;
        write_line("music_walk_cut frame=%llu seq=%lu qpc=%llu walk=%s limit=%u cuts=%lu edition=fl\n", current_frame_,
                   ++seq_, qpc(), what, list_walk_limit, walks_cut_);
    }
}
std::uint32_t find_record(std::uint32_t id) {
    std::uint32_t found = 0;
    walk_records("find_record", [&](std::uint32_t node) {
        if (load32(node + record_id) == id) {
            found = node;
            return true;
        }
        return false;
    });
    return found;
}
// The first linked record with `id` and media != 0: the record the free wrapper retires (its scan at 0x0b4658).
std::uint32_t find_freeable(std::uint32_t id) {
    std::uint32_t found = 0;
    walk_records("find_freeable", [&](std::uint32_t node) {
        if (load32(node + record_media) && load32(node + record_id) == id) {
            found = node;
            return true;
        }
        return false;
    });
    return found;
}
bool live_record(std::uint32_t record, std::uint32_t id, std::uint32_t media, LiveRecord& out) {
    bool live = false;
    walk_records("live_record", [&](std::uint32_t node) {
        if (node != record) return false;
        live = load32(node + record_id) == id && load32(node + record_media) == media;
        if (live) {
            out.flags = load32(node + record_flags);
            out.context = load32(node + record_context);
        }
        return true;
    });
    return live;
}
void write_capped(unsigned long& count, unsigned long& suppressed, bool& noted, unsigned long cap, const char* notice_name,
                  const char* note, const char* format, va_list args) {
    bool notice = false;
    if (count >= cap) {
        ++suppressed;
        if (noted) return;
        noted = true;
        notice = true;
    } else
        ++count;
    const HANDLE handle = x3m::log_handle();
    if (handle == INVALID_HANDLE_VALUE || !handle) return;
    if (notice) {
        x3m::log("%s frame=%llu lines=%lu note=%s edition=fl", notice_name, current_frame_, count, note);
        return;
    }
    x3m::session_log::vlog(format, args);
}
void write_line(const char* format, ...) {
    va_list args;
    va_start(args, format);
    write_capped(lines_, suppressed_, cap_noted_, trace_line_cap, "music_trace_cap", "further_music_lines_suppressed",
                 format, args);
    va_end(args);
}
void write_queue_line(const char* format, ...) {
    va_list args;
    va_start(args, format);
    write_capped(queue_lines_, queue_suppressed_, queue_cap_noted_, queue_line_cap, "music_queue_cap",
                 "further_queue_lines_suppressed", format, args);
    va_end(args);
}
const char* action_name(SeekAction a) {
    return a == SeekAction::skip ? "skip" : a == SeekAction::pause_then_vanilla ? "pause_then_vanilla" : "vanilla";
}
}
extern "C" {
volatile std::uint32_t x3m_music_keep_fl_save_pumping = 0;
std::uint32_t __cdecl x3m_music_keep_fl_call_pause(std::uint32_t record);
std::uint32_t __cdecl x3m_music_keep_fl_call_pump(std::uint32_t media);
void __cdecl x3m_music_keep_fl_call_completion(std::uint32_t function, std::uint32_t context, std::uint32_t status);
void __cdecl x3m_music_keep_fl_call_free(std::uint32_t key);
}
namespace {
void sync_pumping() {
    x3m_music_keep_fl_save_pumping =
        keep_armed_ && save_pump_ && (pump_.pumping || (pump_.queue && pump_.open)) ? 1u : 0u;
}
std::uint64_t span_ms(std::uint64_t from, std::uint64_t to) {
    return to >= from ? ticks_to_ms(to - from, pump_.frequency) : 0;
}
void pump_line(const char* run, std::uint32_t buffer_state) {
    const SavePump& p = pump_;
    const std::uint64_t pump_end = p.pump_end != PumpEnd::none ? p.pump_end_at : p.end_at;
    write_line(
        "music_keep_save_pump frame=%llu seq=%lu qpc=%llu record=0x%08lx id=%lu calls=%lu ret0=%lu ret1=%lu ret2=%lu ret_other=%lu writes=%lu samples=%lu "
        "span_ms=%llu first_call_ms=%llu max_gap_ms=%llu end_ms=%llu pump_end=%s close_caller=0x%08lx end=%s run=%s buffer_state=%lu "
        "fallback=%s fallback_reason=%s fallback_ms=%llu interval_ms=%u edition=fl\n",
        current_frame_, ++seq_, qpc(), static_cast<unsigned long>(p.record), static_cast<unsigned long>(p.id),
        static_cast<unsigned long>(p.calls), static_cast<unsigned long>(p.returns[0]),
        static_cast<unsigned long>(p.returns[1]), static_cast<unsigned long>(p.returns[2]),
        static_cast<unsigned long>(p.returns[3]), static_cast<unsigned long>(p.writes),
        static_cast<unsigned long>(p.samples), static_cast<unsigned long long>(span_ms(p.armed_at, pump_end)),
        static_cast<unsigned long long>(p.calls ? span_ms(p.armed_at, p.first_call_at) : 0),
        static_cast<unsigned long long>(span_ms(0, p.max_gap)),
        static_cast<unsigned long long>(span_ms(p.armed_at, p.end_at ? p.end_at : pump_end)), pump_end_name(p.pump_end),
        static_cast<unsigned long>(gzclose_caller_), pump_end_name(p.end), run, static_cast<unsigned long>(buffer_state),
        fallback_name(p.fallback), pump_end_name(p.fallback_reason),
        static_cast<unsigned long long>(p.fallback != Fallback::none ? span_ms(p.armed_at, p.fallback_at) : 0),
        pump_interval_ms);
    pump_.reset();
    gzclose_caller_ = 0;
    sync_pumping();
}
void run_fallback(const x3m::LightCallBoundary& cpu) {
    if (!pump_.fallback_due()) return;
    LiveRecord r{};
    const bool linked = live_record(pump_.record, pump_.id, pump_.media, r) && !(r.flags & flag_playing) &&
                        r.context == 0 && (r.flags & flag_music);
    if (linked) {
        cpu.before_original();
        x3m_music_keep_fl_call_pause(pump_.record);
        holds_.set_mode(pump_.record, pump_.id, pump_.media, StopMode::paused);
    }
    pump_.note_fallback(linked);
}
struct TaskView {
    std::uint32_t id = 0, capacity = 0, cells = 0, index = 0, ip = 0;
};
bool read_task(std::uint32_t task, TaskView& v) {
    std::uint32_t header[(task_ip + 4) / 4]{};
    if (!task || !x3m::engine_memory::read(task, header, sizeof header)) return false;
    v.id = header[task_id / 4];
    v.capacity = header[task_capacity / 4];
    v.cells = header[task_cells / 4];
    v.index = header[task_index / 4];
    v.ip = header[task_ip / 4];
    return true;
}
bool read_cell_abs(const TaskView& v, std::int32_t index, unsigned& tag, std::uint32_t& value) {
    if (!v.cells || !cell_index_ok(index, v.capacity)) return false;
    unsigned char cell[cell_size]{};
    if (!x3m::engine_memory::read(v.cells + std::uint32_t(cell_offset(index)), cell, cell_size)) return false;
    tag = cell[0];
    std::memcpy(&value, cell + 1, 4);
    return true;
}
void resume_line(ResumeAction action, const ResumeMemory& memory, std::uint32_t other_id, const char* at) {
    write_line("music_keep_resume frame=%llu seq=%lu qpc=%llu id=%lu saved_ms=%lu action=%s at=%s other_id=%lu edition=fl\n",
               current_frame_, ++seq_, qpc(), static_cast<unsigned long>(memory.id),
               static_cast<unsigned long>(memory.ms), resume_action_name(action), at,
               static_cast<unsigned long>(other_id));
}
// The manager's own position read (0x0b4358..0x0b43a9), mirrored without x87: [m+0x8c] & 0x20 ->
// IMultiMediaStream::GetTime on [m+4] (vtbl +0x20, 100 ns); else IMediaPosition::get_CurrentPosition on [m+0x70]
// (vtbl +0x24, seconds). False on a missing interface, a failed call or a reading outside [0, 24 h).
bool read_position_ms(std::uint32_t media, std::uint32_t& ms) {
    if (!media) return false;
    if (load32(media + media_flags) & media_stream_clock) {
        const std::uint32_t stream = load32(media + media_stream);
        if (!stream || !load32(stream)) return false;
        using GetTime = HRESULT(STDMETHODCALLTYPE*)(void*, LONGLONG*);
        const auto get = reinterpret_cast<GetTime>(std::uintptr_t(load32(load32(stream) + stream_vtbl_get_time)));
        LONGLONG t = 0;
        if (!get || FAILED(get(reinterpret_cast<void*>(std::uintptr_t(stream)), &t)) || t < 0 ||
            t / 10000 >= LONGLONG(position_limit_ms))
            return false;
        ms = std::uint32_t(t / 10000);
        return true;
    }
    const std::uint32_t position = load32(media + media_position);
    if (!position || !load32(position)) return false;
    using GetPosition = HRESULT(STDMETHODCALLTYPE*)(void*, double*);
    const auto get =
        reinterpret_cast<GetPosition>(std::uintptr_t(load32(load32(position) + position_vtbl_get_current)));
    double seconds = 0;
    if (!get || FAILED(get(reinterpret_cast<void*>(std::uintptr_t(position)), &seconds))) return false;
    const double milliseconds = seconds * 1000.0;
    if (!(milliseconds >= 0.0 && milliseconds < double(position_limit_ms))) return false;
    ms = std::uint32_t(std::int32_t(milliseconds));
    return true;
}
OrphanView orphan_view() {
    OrphanView o;
    if (!queue_.active()) return o;
    const std::uint32_t record = find_record(queue_.key);
    if (!record || record != queue_.record || load32(record + record_media) != queue_.media) return o;
    o.live = true;
    o.flags = load32(record + record_flags);
    o.context = load32(record + record_context);
    return o;
}
// A completion (ctx, 1) through the VM's slot table, as the engine's own bookkeeping does (VM != 0, [VM+4] != 0,
// slot < 32, function != 0; cdecl): the engine's completion publishes a result and relinks the task, no bytecode runs.
void deliver(const x3m::LightCallBoundary& cpu, std::uint32_t slot, std::uint32_t context) {
    const std::uint32_t owner = load32(vm_slot_);
    if (!owner || !load32(owner + vm_slot_guard) || slot >= slot_count) return;
    const std::uint32_t function = load32(owner + slot_completion + slot * slot_stride);
    if (!function) return;
    cpu.before_original();
    x3m_music_keep_fl_call_completion(function, context, 1);
}
// The FL free (music-keep.md section 6.4): the wrapper with slot 32 / context 0 and the key; its EAX is not a result.
// Freed = a record with the key and media != 0 was linked before and that record is gone after.
std::uint32_t free_key(const x3m::LightCallBoundary& cpu, std::uint32_t key) {
    const std::uint32_t before = find_freeable(key);
    cpu.before_original();
    x3m_music_keep_fl_call_free(key);
    return before && find_freeable(key) != before ? 1u : 0u;
}
void queue_line(const QueueOutcome& o, const QueueStopView* stop, std::uint32_t freed) {
    if (o.event == QueueEvent::none) return;
    if (o.event == QueueEvent::refuse && stop) {
        write_queue_line(
            "music_queue frame=%llu seq=%lu qpc=%llu event=refuse reason=%s state=%s old=%lu new=%lu task=%lu ip=0x%lx k=%ld capacity=%lu cells=%u "
            "c-18=%u:0x%08lx c-13=%u:0x%08lx c-11=%u:0x%08lx c-10=%u:0x%08lx c-9=%u:0x%08lx c-7=%u:0x%08lx alert=%u/%lu/%lu record=0x%08lx flags=0x%lx ctx=%lu edition=fl\n",
            current_frame_, ++seq_, qpc(), queue_reason_name(o.reason), queue_state_name(queue_.state),
            static_cast<unsigned long>(o.old_id), static_cast<unsigned long>(o.new_id),
            static_cast<unsigned long>(o.stop_task), static_cast<unsigned long>(stop->task_ip),
            static_cast<long>(stop->index), static_cast<unsigned long>(stop->capacity), stop->cells_ok ? 1u : 0u,
            stop->cells[0].tag, static_cast<unsigned long>(stop->cells[0].value), stop->cells[1].tag,
            static_cast<unsigned long>(stop->cells[1].value), stop->cells[2].tag,
            static_cast<unsigned long>(stop->cells[2].value), stop->cells[3].tag,
            static_cast<unsigned long>(stop->cells[3].value), stop->cells[4].tag,
            static_cast<unsigned long>(stop->cells[4].value), stop->cells[5].tag,
            static_cast<unsigned long>(stop->cells[5].value), stop->alert_ok ? 1u : 0u,
            static_cast<unsigned long>(stop->alert_old), static_cast<unsigned long>(stop->alert_new),
            static_cast<unsigned long>(stop->record), static_cast<unsigned long>(stop->flags),
            static_cast<unsigned long>(stop->context));
        return;
    }
    write_queue_line(
        "music_queue frame=%llu seq=%lu qpc=%llu event=%s reason=%s state=%s old=%lu new=%lu key=0x%08lx record=0x%08lx task=%lu stop_task=%lu "
        "wake=%u clear=%u complete=%u park=%u freed=%lu free_later=%u frames=%llu episode=%lu caller=%s dock=%lu edition=fl\n",
        current_frame_, ++seq_, qpc(), queue_event_name(o.event), queue_reason_name(o.reason),
        queue_state_name(queue_.state), static_cast<unsigned long>(o.old_id), static_cast<unsigned long>(o.new_id),
        static_cast<unsigned long>(o.key), static_cast<unsigned long>(o.record), static_cast<unsigned long>(o.task),
        static_cast<unsigned long>(o.stop_task), o.wake ? 1u : 0u, o.clear ? 1u : 0u, o.complete ? 1u : 0u,
        o.park ? 1u : 0u, static_cast<unsigned long>(freed), o.free_later ? 1u : 0u, o.frames,
        static_cast<unsigned long>(queue_.episode), o.caller ? stop_caller_name(o.caller) : "-",
        static_cast<unsigned long>(o.dock));
}
// The core's outcome against the engine, in AP's order (music_keep.cpp queue_apply), with the FL free.
std::uint32_t queue_apply(const x3m::LightCallBoundary& cpu, const QueueOutcome& o) {
    std::uint32_t freed = 0;
    if (o.free_id) freed = free_key(cpu, o.free_id);
    if (o.restore) {
        if (!find_record(o.old_id))
            store32(o.record + record_id, o.old_id);
        else
            freed |= free_key(cpu, o.key) << 1;
    }
    if (o.rekey) store32(o.record + record_id, o.key);
    if (o.wake || o.clear) {
        if (o.wake) deliver(cpu, o.wake_slot, o.wake_ctx);
        store32(o.record + record_slot, 0);
        store32(o.record + record_context, 0);
    }
    if (o.complete) deliver(cpu, o.complete_slot, o.complete_ctx);
    if (o.park) {
        store32(o.record + record_context, o.park_ctx);
        store32(o.record + record_slot, o.park_slot);
    }
    if (o.free_key) freed = free_key(cpu, o.key);
    if (o.free_later) queue_free_pending_ = o.key;
    return freed;
}
}

extern "C" {
volatile std::uint32_t x3m_music_keep_fl_a_continue = 0;    // Patch A claim tail (displaced + jmp base+0x0b4245)
volatile std::uint32_t x3m_music_keep_fl_a_skip = 0;        // base+0x0b4285
volatile std::uint32_t x3m_music_keep_fl_a_next = 0;        // base+0x0b42c4
volatile std::uint32_t x3m_music_keep_fl_seek = 0;          // base+0x0fe5f0
volatile std::uint32_t x3m_music_keep_fl_status_fn = 0;     // base+0x0ff580
volatile std::uint32_t x3m_music_keep_fl_run_fn = 0;        // base+0x0ff8b0
volatile std::uint32_t x3m_music_keep_fl_pause_fn = 0;      // base+0x0b41b0
volatile std::uint32_t x3m_music_keep_fl_pump_fn = 0;       // base+0x0fe7d0
volatile std::uint32_t x3m_music_keep_fl_free_fn = 0;       // base+0x0b4640
volatile std::uint32_t x3m_music_keep_fl_entry_continue = 0, x3m_music_keep_fl_stop_movie_continue = 0,
                       x3m_music_keep_fl_play_continue = 0; // the three entry claims' tails
volatile std::uint32_t x3m_music_keep_fl_gzwrite = 0, x3m_music_keep_fl_gzclose = 0; // zlib1.dll's own exports
std::uint32_t x3m_music_keep_fl_resume_ms = 0;
std::uint32_t x3m_music_keep_fl_pause_record = 0;

// Patch A: ESI = record, the stop-all's caller return. 1 = keep_running (-> base+0x0b4285), 3 = skip_all
// (-> base+0x0b42c4), else the displaced instructions through the tail.
__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_keep_fl_stop_all(std::uint32_t record,
                                                                                     std::uint32_t return_va) {
    x3m::LightCallBoundary cpu;
    if (!keep_armed_) return unsigned(StopMode::vanilla);
    const std::uint32_t role = stop_role(return_va);
    const std::uint32_t flags = load32(record + record_flags), id = load32(record + record_id),
                        media = load32(record + record_media);
    const bool save_pump_free = save_pump_ && !pump_.active();
    if (queue_keep_walk(queue_, record, role)) {
        const bool save = role == save_return_va;
        if (save && save_pump_free) {
            pump_.arm_queue(record, id, media, GetCurrentThreadId(), qpc(), frequency_);
            sync_pumping();
        }
        const StopMode mode = save && !save_pump_free ? StopMode::vanilla : StopMode::skip_all;
        write_line(
            "music_keep_stop frame=%llu seq=%lu qpc=%llu caller=0x%08lx name=%s record=0x%08lx id=%lu flags=0x%lx mode=%s held=0 holds=%u pump=%u queue=1 edition=fl\n",
            current_frame_, ++seq_, qpc(), static_cast<unsigned long>(return_va), stop_name(return_va),
            static_cast<unsigned long>(record), static_cast<unsigned long>(id), static_cast<unsigned long>(flags),
            mode_name(mode), holds_.count, save && save_pump_free ? 1u : 0u);
        return unsigned(mode);
    }
    const StopDecision d = decide_stop(holds_, record, flags, id, media, role, status_gate_, save_pump_free);
    if (d.pump) {
        pump_.arm(record, id, media, GetCurrentThreadId(), qpc(), frequency_);
        sync_pumping();
    }
    if (flags & flag_music)
        write_line(
            "music_keep_stop frame=%llu seq=%lu qpc=%llu caller=0x%08lx name=%s record=0x%08lx id=%lu flags=0x%lx mode=%s held=%u holds=%u pump=%u edition=fl\n",
            current_frame_, ++seq_, qpc(), static_cast<unsigned long>(return_va), stop_name(return_va),
            static_cast<unsigned long>(record), static_cast<unsigned long>(id), static_cast<unsigned long>(flags),
            mode_name(d.mode), d.held ? 1u : 0u, holds_.count, d.pump ? 1u : 0u);
    return unsigned(d.mode);
}
}
namespace {
__attribute__((noinline)) unsigned music_keep_status_inactive(std::uint32_t record) {
    x3m::LightCallBoundary cpu;
    const std::uint32_t pointer = load32(input_slot_);
    if (!pointer) return 0;
    const std::uint32_t input = load32(pointer), flags = load32(record + record_flags), id = load32(record + record_id),
                        media = load32(record + record_media);
    if (!decide_status(holds_, record, flags, id, media, 0, input)) return 0;
    ++gated_;
    write_line(
        "music_keep_status frame=%llu seq=%lu qpc=%llu record=0x%08lx id=%lu flags=0x%lx input_flags=0x%08lx action=playing gated=%lu edition=fl\n",
        current_frame_, ++seq_, qpc(), static_cast<unsigned long>(record), static_cast<unsigned long>(id),
        static_cast<unsigned long>(flags), static_cast<unsigned long>(input), gated_);
    return 1;
}
}
extern "C" {
// Patch D (ECX = record): 1 = answer "playing", 0 = forward. The common path is three loads and no call.
__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_keep_fl_status(std::uint32_t record) {
    if (!keep_armed_ || !status_gate_ || load32(active_flag_) != 0) return 0;
    return music_keep_status_inactive(record);
}
// Patch C (ECX = record, EDX = start ms): SeekAction plus seek_resume_bit (the thunk then replaces EDX with
// x3m_music_keep_fl_resume_ms).
__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_keep_fl_seek_decide(std::uint32_t record,
                                                                                        std::int32_t start_ms) {
    x3m::LightCallBoundary cpu;
    if (!keep_armed_) return unsigned(SeekAction::vanilla);
    const std::uint32_t flags = load32(record + record_flags);
    if (!(flags & flag_music)) return unsigned(SeekAction::vanilla);
    const std::uint32_t id = load32(record + record_id), media = load32(record + record_media);
    const unsigned holds_before = holds_.count;
    const SeekDecision d = decide_seek(holds_, record, flags, id, media, start_ms, live_record);
    x3m_music_keep_fl_pause_record = d.pause_record;
    if (pump_.active() && pump_.on_play(record, id, media, d.action == SeekAction::skip, qpc())) pump_line("none", 0);
    sync_pumping();
    unsigned answer = unsigned(d.action);
    std::uint32_t resume_ms = 0;
    const Alert alert = read_alert();
    combat_track_ = alert_on(alert) ? id : 0;
    if (resume_ok_ && resume_.valid) {
        const ResumeMemory before = resume_;
        const ResumeAction r = resume_on_play(resume_, id, start_ms, d.action, resume_ms, alert);
        if (r == ResumeAction::seek) {
            x3m_music_keep_fl_resume_ms = resume_ms;
            answer |= seek_resume_bit;
        }
        if (r != ResumeAction::none && r != ResumeAction::keep) resume_line(r, before, id, "play");
    }
    write_line(
        "music_keep_seek frame=%llu seq=%lu qpc=%llu record=0x%08lx id=%lu start_ms=%ld flags=0x%lx action=%s hold_id=%lu pause_record=0x%08lx holds_before=%u resume_ms=%lu edition=fl\n",
        current_frame_, ++seq_, qpc(), static_cast<unsigned long>(record), static_cast<unsigned long>(id),
        static_cast<long>(start_ms), static_cast<unsigned long>(flags), action_name(d.action),
        static_cast<unsigned long>(d.hold_id), static_cast<unsigned long>(d.pause_record), holds_before,
        static_cast<unsigned long>(resume_ms));
    return answer;
}
// T1 (any caller; block: [0] edx, [1] ecx, [2] eax, [3] eflags, [4] the caller return): the next orphaned keep-running
// record to pause (the stub calls the FL pause with ECX = it and asks again), 0 when none.
__attribute__((force_align_arg_pointer)) std::uint32_t __cdecl x3m_music_keep_fl_stop_all_entry(
    const std::uint32_t* block) {
    x3m::LightCallBoundary cpu;
    if (!keep_armed_) return 0;
    const std::uint32_t return_va = block[4], role = stop_role(return_va);
    if (queue_.active()) {
        SameIdView same;
        const bool same_id = role == pause_return_va && queue_.old_id == queue_.new_id;
        if (same_id) {
            same.record = find_record(queue_.new_id);
            same.found = same.record != 0;
            if (same.found) {
                same.flags = load32(same.record + record_flags);
                same.context = load32(same.record + record_context);
                same.media = load32(same.record + record_media);
            }
        }
        const QueueOutcome o = queue_on_stop_all(queue_, role, status_gate_, orphan_view(), current_frame_, save_pump_,
                                                 same_id ? &same : nullptr);
        if (o.event != QueueEvent::none) queue_line(o, nullptr, queue_apply(cpu, o));
    }
    if (queue_armed_ && story_load_caller(role)) kc_.load_begin();
    if (pump_.active() && pump_.end_now(PumpEnd::stop_all, qpc())) pump_line("none", 0);
    if (resume_ok_ && resume_.valid) {
        const ResumeMemory before = resume_;
        const ResumeAction r = resume_on_stop_all(resume_, role);
        if (r != ResumeAction::none) resume_line(r, before, 0, stop_name(return_va));
    }
    if (!holds_.count) return 0;
    const std::uint32_t record = stale_hold_to_pause(holds_, live_record);
    if (record)
        write_line(
            "music_keep_orphan frame=%llu seq=%lu qpc=%llu caller=0x%08lx name=%s record=0x%08lx id=%lu action=pause holds=%u edition=fl\n",
            current_frame_, ++seq_, qpc(), static_cast<unsigned long>(return_va), stop_name(return_va),
            static_cast<unsigned long>(record), static_cast<unsigned long>(load32(record + record_id)), holds_.count);
    return record;
}
// T3 (block: [0] EDX = the context (task id), [1] ECX = the slot, [4] the caller return, [5] the id; ESI = the task):
// 1 = the queue consumed the stop (the stub returns), 0 = the engine's body.
__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_keep_fl_stop_movie(const std::uint32_t* block,
                                                                                       std::uint32_t task) {
    x3m::LightCallBoundary cpu;
    if (!keep_armed_) return 0;
    const std::uint32_t id = block[5], return_va = block[4], role = stop_movie_role(return_va);
    const std::uint32_t slot = block[1], context = block[0];
    const unsigned dropped = holds_.count ? drop_holds_by_id(holds_, id) : 0;
    if (dropped)
        write_line(
            "music_keep_stop_movie frame=%llu seq=%lu qpc=%llu id=%lu caller=0x%08lx name=%s dropped=%u holds=%u edition=fl\n",
            current_frame_, ++seq_, qpc(), static_cast<unsigned long>(id), static_cast<unsigned long>(return_va),
            F::role_name(F::stop_movie_callers, return_va, base_), dropped, holds_.count);
    if (pump_.active() && pump_.id == id && pump_.end_now(PumpEnd::stop_movie, qpc())) pump_line("none", 0);
    if (role != stop_movie_native_return_va) return 0;
    const std::uint32_t record = find_record(id);
    const std::uint32_t flags = record ? load32(record + record_flags) : 0;
    bool resume_candidate = true;
    const Alert alert = read_alert();
    if (queue_armed_ || queue_requested_) {
        const x3m::story::QueueSites* story = sites_now();
        QueueStopView v;
        v.story = story;
        v.combat_old = combat_track_ != 0 && id == combat_track_;
        v.return_va = role;
        v.id = id;
        v.task_id = context;
        v.slot = slot;
        TaskView t{};
        v.task_ok = read_task(task, t);
        v.task_ip = t.ip;
        v.capacity = t.capacity;
        v.index = std::int32_t(t.index);
        if (story && v.task_ok && v.task_ip == story->kc_play_stop_ip) {
            v.cells_ok = true;
            for (unsigned i = 0; i < queue_cell_count && v.cells_ok; ++i)
                v.cells_ok =
                    read_cell_abs(t, -std::int32_t(story->queue_cell_depths[i]), v.cells[i].tag, v.cells[i].value);
            v.alert_ok = alert.ok;
            v.alert_old = alert.committed;
            v.alert_new = alert.current;
        }
        v.found = record != 0;
        v.record = record;
        v.flags = flags;
        v.context = record ? load32(record + record_context) : 0;
        v.rec_slot = record ? load32(record + record_slot) : 0;
        v.media = record ? load32(record + record_media) : 0;
        v.orphan = orphan_view();
        v.dock_keep = dock_keep_ && queue_armed_;
        const QueueOutcome o = queue_on_stop(queue_, v, queue_armed_, current_frame_);
        if (o.consume && o.dock && story) store32(queue_id_cell_value_at(t.cells, *story), o.new_id);
        if (o.event != QueueEvent::none) queue_line(o, &v, queue_apply(cpu, o));
        resume_candidate = o.resume_candidate;
        if (o.consume) return 1;
    }
    if (!resume_ok_ || !resume_candidate || !alert_rising(alert)) return 0;
    if ((flags & (flag_music | flag_playing)) != (flag_music | flag_playing)) return 0;
    std::uint32_t ms = 0;
    cpu.before_original();
    const bool read = read_position_ms(load32(record + record_media), ms);
    const ResumeAction r = resume_on_stop(resume_, role, id, flags, read, ms, alert);
    if (r != ResumeAction::none) resume_line(r, ResumeMemory{id, read ? ms : 0, read}, id, "stop_movie");
    return 0;
}
// T2 (block: [0] EDX = the context (task id), [1] ECX = the slot, [4] the caller return, [5] the id; ESI = the task):
// 1 = consume (the stub returns; the task waits for the orphan's natural end), 0 = the engine's body.
__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_keep_fl_queue_play(const std::uint32_t* block,
                                                                                       std::uint32_t task) {
    if (!queue_armed_ || !queue_.active()) return 0;
    x3m::LightCallBoundary cpu;
    QueuePlayView v;
    v.return_va = play_role(block[4]);
    v.slot = block[1];
    v.task_id = block[0];
    v.id = block[5];
    TaskView t{};
    v.task_ok = read_task(task, t);
    v.task_ip = t.ip;
    v.story = sites_now();
    const std::uint32_t record = find_record(v.id);
    v.found = record != 0;
    v.flags = record ? load32(record + record_flags) : 0;
    v.orphan = orphan_view();
    const QueueOutcome o = queue_on_play(queue_, v, current_frame_);
    if (o.event != QueueEvent::none) queue_line(o, nullptr, queue_apply(cpu, o));
    return o.consume ? 1u : 0u;
}
}
namespace {
void kc_row(KcResult r) {
    const bool detected = kc_.story != nullptr;
    x3m::log("music_queue_kc frame=%llu action=%s vm=0x%08lx code=0x%08lx size=%lu build=%lu object=%s detect=%s method=%s window=0x%05lx bytes=%02x%02x%02x%02x%02x%02x applied=%u refused=%u "
             "load_begins=%u markers=%u edition=fl",
             current_frame_, kc_result_name(r), static_cast<unsigned long>(kc_.vm), static_cast<unsigned long>(kc_.code),
             static_cast<unsigned long>(kc_.size), static_cast<unsigned long>(kc_.build),
             x3m::story::object_name(kc_.build), detected ? x3m::story::fail_name(kc_.story->outcome.fail) : "pending",
             detected && kc_.story->outcome.where[0] ? kc_.story->outcome.where : "-",
             static_cast<unsigned long>(detected ? kc_.story->warp_fork_offset : 0), kc_window_seen_[0],
             kc_window_seen_[1], kc_window_seen_[2], kc_window_seen_[3], kc_window_seen_[4], kc_window_seen_[5],
             kc_.applied, kc_.refused, kc_.load_begins, kc_.markers);
}
// The KC patch of 150::WarpToSector from Present, AP's procedure (music_keep.cpp music_queue_kc_present) on the FL VM
// slot: the detection and the write only through the load gate (two agreeing due checks 8 frames apart; FL has no
// save_load_complete marker), a plain 6-byte copy into the game's heap after VirtualQuery says committed and writable.
__attribute__((noinline)) void music_queue_kc_present() {
    const std::uint32_t vm = load32(vm_slot_);
    if (!kc_.due(vm)) return;
    x3m::LightCallBoundary cpu;
    std::uint32_t code = 0, size = 0, build = 0;
    const bool vm_ok = vm != 0;
    const bool fields_ok = vm_ok && x3m::engine_memory::read(vm + vm_code, &code, 4) &&
                           x3m::engine_memory::read(vm + vm_code_size, &size, 4) &&
                           x3m::engine_memory::read(vm + vm_build, &build, 4);
    kc_.code = fields_ok ? code : 0;
    kc_.size = fields_ok ? size : 0;
    kc_.build = fields_ok ? build : 0;
    const x3m::story::VmKey key{vm, kc_.code, kc_.size, kc_.build};
    kc_.story = fields_ok && kc_sites_.detected && kc_sites_.key == key ? &kc_sites_ : nullptr;
    const x3m::story::QueueSites* seen_sites = kc_.story;
    if (fields_ok && !kc_.story) {
        x3m::story::detect_queue(engine_read, vm, kc_candidate_, kc_scratch_);
        kc_candidate_.key = key;
        seen_sites = &kc_candidate_;
    }
    const bool sites = seen_sites && seen_sites->outcome.ok();
    const std::uint32_t at = sites ? code + seen_sites->warp_fork_offset : 0;
    bool window_ok = false;
    if (sites && code && size >= seen_sites->warp_fork_offset + warp_window_length && at > code)
        window_ok = x3m::engine_memory::read(at, kc_window_seen_, warp_window_length);
    if (!window_ok) std::memset(kc_window_seen_, 0, sizeof kc_window_seen_);
    KcObservation seen;
    seen.vm = vm;
    seen.code = kc_.code;
    seen.size = kc_.size;
    seen.build = kc_.build;
    seen.window_ok = window_ok;
    std::memcpy(seen.window, kc_window_seen_, warp_window_length);
    seen.detect = seen_sites ? unsigned(seen_sites->outcome.fail) : kc_no_detection;
    seen.window_at = sites ? seen_sites->warp_fork_offset : 0;
    const bool settled = kc_.settle(seen);
    KcResult r = KcResult::none;
    if (fields_ok && !kc_.story) {
        if (kc_retries_.keep(key, kc_candidate_.outcome, settled)) {
            kc_sites_ = kc_candidate_;
            kc_sites_.detected = true;
            kc_.story = &kc_sites_;
            r = kc_sites_.outcome.ok() ? KcResult::waiting : KcResult::no_sites;
        } else
            r = KcResult::waiting;
    } else if (kc_.decide(vm_ok, fields_ok, kc_window_seen_, window_ok, r) && !settled)
        r = KcResult::waiting;
    else if (r == KcResult::applied) {
        MEMORY_BASIC_INFORMATION info{};
        const bool writable =
            VirtualQuery(reinterpret_cast<const void*>(std::uintptr_t(at)), &info, sizeof info) == sizeof info &&
            info.State == MEM_COMMIT &&
            (info.Protect == PAGE_READWRITE || info.Protect == PAGE_EXECUTE_READWRITE ||
             info.Protect == PAGE_WRITECOPY || info.Protect == PAGE_EXECUTE_WRITECOPY) &&
            std::uintptr_t(info.BaseAddress) + info.RegionSize >= std::uintptr_t(at) + warp_window_length;
        if (writable)
            std::memcpy(reinterpret_cast<void*>(std::uintptr_t(at)), kc_.story->warp_patch_memory, warp_window_length);
        else
            r = KcResult::unwritable;
    }
    if (kc_.note(r)) kc_row(r);
}
__attribute__((noinline)) void music_queue_free_present() {
    x3m::LightCallBoundary cpu;
    const std::uint32_t key = queue_free_pending_;
    queue_free_pending_ = 0;
    const std::uint32_t freed = free_key(cpu, key);
    write_queue_line("music_queue frame=%llu seq=%lu qpc=%llu event=free_later key=0x%08lx freed=%lu edition=fl\n",
                     current_frame_, ++seq_, qpc(), static_cast<unsigned long>(key), static_cast<unsigned long>(freed));
}
__attribute__((noinline)) void music_queue_alert_present() {
    x3m::LightCallBoundary cpu;
    std::uint32_t alert = 0, levels[2]{};
    if (!x3m::engine_memory::read(alert_slot_, &alert, 4) || !alert ||
        !x3m::engine_memory::read(alert, levels, sizeof levels))
        return;
    const QueueOutcome o = queue_on_alert(queue_, levels[1], orphan_view(), current_frame_);
    if (o.event != QueueEvent::none) queue_line(o, nullptr, queue_apply(cpu, o));
}
__attribute__((noinline)) unsigned music_keep_run_marked(std::uint32_t record) {
    x3m::LightCallBoundary cpu;
    const std::uint32_t media = load32(record + record_media);
    const std::uint32_t state = media ? load32(media + media_buffer_state) : 0;
    const bool skip = pump_.skip_run(record, media, state, qpc());
    pump_line(skip ? "skipped" : "vanilla", state);
    return skip ? 1u : 0u;
}
__attribute__((noinline)) void music_keep_gz_write_sampled() {
    x3m::LightCallBoundary cpu;
    const std::uint64_t now = qpc();
    if (!pump_.pump_due(now, GetCurrentThreadId())) {
        run_fallback(cpu);
        sync_pumping();
        return;
    }
    LiveRecord r{};
    bool ok = live_record(pump_.record, pump_.id, pump_.media, r) &&
              (r.flags & (flag_music | flag_directsound | flag_playing)) == pump_.expected_flags();
    if (ok) {
        const std::uint32_t media_bits = load32(pump_.media + media_flags),
                            state = load32(pump_.media + media_buffer_state);
        ok = (media_bits & media_ds_mask) == media_ds_value && (state == buffer_playing || state == buffer_draining);
    }
    if (!ok) {
        pump_.refuse(now);
        run_fallback(cpu);
        sync_pumping();
        return;
    }
    cpu.before_original();
    const std::uint32_t result = x3m_music_keep_fl_call_pump(pump_.media);
    pump_.note_call(result, now, qpc());
    if (pump_.queue_ended) {
        pump_.queue_ended = false;
        LiveRecord e{};
        if (live_record(pump_.record, pump_.id, pump_.media, e) && e.context == 0 && (e.flags & flag_playing))
            store32(pump_.record + record_flags, e.flags & ~flag_playing);
    }
    run_fallback(cpu);
    sync_pumping();
}
__attribute__((noinline)) void music_keep_replay_check() {
    x3m::LightCallBoundary cpu;
    const std::uint64_t now = qpc();
    if (!pump_.replay_overdue(now, GetCurrentThreadId())) return;
    pump_.cancel_skip(PumpEnd::overdue, now);
    run_fallback(cpu);
    sync_pumping();
}
}
extern "C" {
// Patch E (ECX = record): 1 = answer 1 without the Run, 0 = forward. The common path is two loads.
__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_keep_fl_run(std::uint32_t record) {
    if (!keep_armed_ || !pump_.run_marker) return 0;
    return music_keep_run_marked(record);
}
// Every gzwrite while a save pumps (the stub tests x3m_music_keep_fl_save_pumping first).
__attribute__((force_align_arg_pointer)) void __cdecl x3m_music_keep_fl_gz_write() {
    if (!keep_armed_ || !pump_.pumping || !pump_.sample_due()) return;
    music_keep_gz_write_sampled();
}
// gzclose while a save pumps: only the save's own close ([esp] = base+0x106963 in the close wrapper, the word above the
// handle = base+0x079201 in the save) ends pumping.
__attribute__((force_align_arg_pointer)) void __cdecl x3m_music_keep_fl_gz_close(std::uint32_t return_va,
                                                                                 std::uint32_t caller_return_va) {
    if (!keep_armed_ || return_va != std::uint32_t(base_ + F::gzclose_return_rva) ||
        caller_return_va != std::uint32_t(base_ + F::save_close_return_rva))
        return;
    if (!pump_.pumping && !(pump_.queue && pump_.open)) return;
    x3m::LightCallBoundary cpu;
    gzclose_caller_ = caller_return_va;
    pump_.stop(PumpEnd::gzclose, qpc());
    if (pump_.queue && pump_.end_now(PumpEnd::gzclose, qpc())) pump_line("queue", 0);
    sync_pumping();
}
}

// Integer-only stubs. Every stub saves EFLAGS and EAX/ECX/EDX, calls its handler (cdecl, four-byte stack), restores
// them and leaves with ESP exactly as at entry (or returns for a consumed entry / an answered call). Patch A: ESI =
// record, the caller return at [esp+0xc] at the site ([esp+0x1c] after the four saves); keep_running loads EDI as the
// displaced `mov edi,[esi+0x24]` would and leaves for base+0x0b4285; skip_all leaves for base+0x0b42c4 (EBX = the
// next node). Patch C: ECX = record, EDX = start ms; skip answers EAX = 1; the resume bit rewrites the saved EDX; the
// pause of another held record calls the FL pause with ECX = it (clobbers EAX/ECX/EDX, all saved); the vanilla path
// tail-jumps to the seek with ECX/EDX restored. Patch D / E: ECX = record; "answered" returns EAX = 1, else a tail-jump
// with everything restored. T1: loops on its handler, pausing each returned record (ECX = record). T3 / T2: the saved
// block and ESI (the task) are the handler's arguments; a consumed entry returns with a plain RET (both natives are
// plain RET, the caller pops its words). The two import stubs: one load and a branch, then `jmp [zlib export]`.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_music_keep_fl_a_stub
_x3m_music_keep_fl_a_stub:
    pushfd
    push eax
    push ecx
    push edx
    push dword ptr [esp+0x1c]
    push esi
    call _x3m_music_keep_fl_stop_all
    add esp, 8
    cmp eax, 1
    je 1f
    cmp eax, 3
    je 3f
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_keep_fl_a_continue]
1:  pop edx
    pop ecx
    pop eax
    popfd
    mov edi, dword ptr [esi+0x24]
    jmp dword ptr [_x3m_music_keep_fl_a_skip]
3:  pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_keep_fl_a_next]

    .p2align 4
    .globl _x3m_music_keep_fl_c_thunk
_x3m_music_keep_fl_c_thunk:
    pushfd
    push eax
    push ecx
    push edx
    push edx
    push ecx
    call _x3m_music_keep_fl_seek_decide
    add esp, 8
    cmp eax, 1
    je 1f
    test eax, 4
    jz 4f
    mov ecx, dword ptr [_x3m_music_keep_fl_resume_ms]
    mov dword ptr [esp], ecx
4:  test eax, 2
    jz 2f
    mov ecx, dword ptr [_x3m_music_keep_fl_pause_record]
    call dword ptr [_x3m_music_keep_fl_pause_fn]
2:  pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_keep_fl_seek]
1:  pop edx
    pop ecx
    pop eax
    popfd
    mov eax, 1
    ret

    .p2align 4
    .globl _x3m_music_keep_fl_status_thunk
_x3m_music_keep_fl_status_thunk:
    pushfd
    push eax
    push ecx
    push edx
    push ecx
    call _x3m_music_keep_fl_status
    add esp, 4
    cmp eax, 1
    je 1f
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_keep_fl_status_fn]
1:  pop edx
    pop ecx
    pop eax
    popfd
    mov eax, 1
    ret

    .p2align 4
    .globl _x3m_music_keep_fl_run_thunk
_x3m_music_keep_fl_run_thunk:
    pushfd
    push eax
    push ecx
    push edx
    push ecx
    call _x3m_music_keep_fl_run
    add esp, 4
    cmp eax, 1
    je 1f
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_keep_fl_run_fn]
1:  pop edx
    pop ecx
    pop eax
    popfd
    mov eax, 1
    ret

    .p2align 4
    .globl _x3m_music_keep_fl_entry_stub
_x3m_music_keep_fl_entry_stub:
    pushfd
    push eax
    push ecx
    push edx
1:  push esp
    call _x3m_music_keep_fl_stop_all_entry
    add esp, 4
    test eax, eax
    jz 2f
    mov ecx, eax
    call dword ptr [_x3m_music_keep_fl_pause_fn]
    jmp 1b
2:  pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_keep_fl_entry_continue]

    .p2align 4
    .globl _x3m_music_keep_fl_stop_movie_stub
_x3m_music_keep_fl_stop_movie_stub:
    pushfd
    push eax
    push ecx
    push edx
    mov eax, esp
    push esi
    push eax
    call _x3m_music_keep_fl_stop_movie
    add esp, 8
    test eax, eax
    jnz 1f
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_keep_fl_stop_movie_continue]
1:  pop edx
    pop ecx
    pop eax
    popfd
    ret

    .p2align 4
    .globl _x3m_music_keep_fl_play_stub
_x3m_music_keep_fl_play_stub:
    pushfd
    push eax
    push ecx
    push edx
    mov eax, esp
    push esi
    push eax
    call _x3m_music_keep_fl_queue_play
    add esp, 8
    test eax, eax
    jnz 1f
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_keep_fl_play_continue]
1:  pop edx
    pop ecx
    pop eax
    popfd
    ret

    .p2align 4
    .globl _x3m_music_keep_fl_gzwrite_stub
_x3m_music_keep_fl_gzwrite_stub:
    cmp dword ptr [_x3m_music_keep_fl_save_pumping], 0
    je 1f
    pushfd
    push eax
    push ecx
    push edx
    call _x3m_music_keep_fl_gz_write
    pop edx
    pop ecx
    pop eax
    popfd
1:  jmp dword ptr [_x3m_music_keep_fl_gzwrite]

    .p2align 4
    .globl _x3m_music_keep_fl_gzclose_stub
_x3m_music_keep_fl_gzclose_stub:
    cmp dword ptr [_x3m_music_keep_fl_save_pumping], 0
    je 1f
    pushfd
    push eax
    push ecx
    push edx
    push dword ptr [esp+0x18]
    push dword ptr [esp+0x14]
    call _x3m_music_keep_fl_gz_close
    add esp, 8
    pop edx
    pop ecx
    pop eax
    popfd
1:  jmp dword ptr [_x3m_music_keep_fl_gzclose]

    .p2align 4
    .globl _x3m_music_keep_fl_call_pump
_x3m_music_keep_fl_call_pump:
    push ebx
    push esi
    push edi
    push ebp
    mov ecx, dword ptr [esp+0x14]
    call dword ptr [_x3m_music_keep_fl_pump_fn]
    movzx eax, ax
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

    .p2align 4
    .globl _x3m_music_keep_fl_call_pause
_x3m_music_keep_fl_call_pause:
    push ebx
    push esi
    push edi
    push ebp
    mov ecx, dword ptr [esp+0x14]
    call dword ptr [_x3m_music_keep_fl_pause_fn]
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

    .p2align 4
    .globl _x3m_music_keep_fl_call_completion
_x3m_music_keep_fl_call_completion:
    push ebx
    push esi
    push edi
    push ebp
    mov eax, dword ptr [esp+0x14]
    push dword ptr [esp+0x1c]
    push dword ptr [esp+0x1c]
    call eax
    add esp, 8
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

    .p2align 4
    .globl _x3m_music_keep_fl_call_free
_x3m_music_keep_fl_call_free:
    push ebx
    push esi
    push edi
    push ebp
    push dword ptr [esp+0x14]
    mov ecx, 32
    xor edx, edx
    call dword ptr [_x3m_music_keep_fl_free_fn]
    add esp, 4
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
    .att_syntax
)");
extern "C" void x3m_music_keep_fl_a_stub();
extern "C" void x3m_music_keep_fl_c_thunk();
extern "C" void x3m_music_keep_fl_status_thunk();
extern "C" void x3m_music_keep_fl_run_thunk();
extern "C" void x3m_music_keep_fl_entry_stub();
extern "C" void x3m_music_keep_fl_stop_movie_stub();
extern "C" void x3m_music_keep_fl_play_stub();
extern "C" void x3m_music_keep_fl_gzwrite_stub();
extern "C" void x3m_music_keep_fl_gzclose_stub();

namespace {
template <class T> std::uint32_t address_of(T* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
bool read_code(std::uintptr_t at, unsigned char* out, unsigned n) {
    return engine_patch::read_code(at, out, n);
}
bool call_at(std::uint32_t call_rva, std::uint32_t target_rva) {
    unsigned char code[5]{};
    return read_code(base_ + call_rva, code, 5) &&
           W::calls(code, std::uint32_t(base_ + call_rva), std::uint32_t(base_ + target_rva));
}
template <unsigned N> bool callers_at(const F::Caller (&table)[N], std::uint32_t target_rva) {
    for (const F::Caller& c : table)
        if (c.return_rva != c.call_rva + 5 || !call_at(c.call_rva, target_rva)) return false;
    return true;
}
bool callers_match() {
    return callers_at(F::stop_callers, F::stop_all_rva) && callers_at(F::play_callers, F::play_rva) &&
           callers_at(F::stop_movie_callers, F::stop_movie_rva) && callers_at(F::free_callers, F::free_wrapper_rva) &&
           call_at(F::c_site_rva, F::seek_rva) && call_at(F::d_site_rva, F::status_rva) &&
           call_at(F::e_site_rva, F::run_rva) && call_at(F::save_close_call_rva, F::close_wrapper_rva);
}
// A site the keep owns: claimed with the live bytes the windows proved (an absolute operand already rebased), the
// current head (the tail) stored as the stub's continuation, the stub chained, the jump read back.
bool hook_site(engine_patch::Site& site, const char* name, std::uint32_t rva, unsigned length, void (*stub)(),
               volatile std::uint32_t& continuation, const char** reason) {
    engine_patch::SiteSpec spec{};
    spec.name = name;
    spec.address = base_ + rva;
    spec.length = length;
    if (!read_code(spec.address, spec.expected, length)) {
        *reason = "unreadable";
        return false;
    }
    site = engine_patch::Site{};
    if (!engine_patch::claim(site, spec)) {
        *reason = site.status;
        return false;
    }
    continuation = address_of(*site.entry);
    unsigned char now[5]{};
    if (!engine_patch::push_front(site, reinterpret_cast<void*>(stub))) {
        *reason = "chain_failed";
        return false;
    }
    if (!read_code(spec.address, now, 5) || std::memcmp(now, site.patched, 5)) {
        *reason = "readback_failed";
        return false;
    }
    return true;
}
bool claim_call(engine_patch::CallSite& site, std::uint32_t rva, std::uint32_t target_rva, void (*thunk)()) {
    site = engine_patch::CallSite{};
    if (!engine_patch::claim_call(site, base_ + rva, base_ + target_rva, reinterpret_cast<void*>(thunk))) return false;
    unsigned char now[5]{};
    return read_code(base_ + rva, now, 5) && !std::memcmp(now, site.patched, 5);
}
// The two zlib1.dll import slots: each must hold zlib1.dll's own export (the windows proved the save's gzwrite call
// and the close wrapper's gzclose call go through exactly these slots); exchanged for the forwarding stubs (both or
// neither), read back, protection restored.
struct ImportSlot {
    std::uint32_t rva;
    const char* name;
    void (*stub)();
    volatile std::uint32_t* original;
    bool* live;
};
bool exchange_slot(std::uintptr_t at, std::uint32_t expected, std::uint32_t desired) {
    DWORD old = 0, unused = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(at), 4, PAGE_READWRITE, &old)) return false;
    const LONG seen = InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(at), LONG(desired), LONG(expected));
    const bool protect_back = VirtualProtect(reinterpret_cast<void*>(at), 4, old, &unused) != FALSE;
    return std::uint32_t(seen) == expected && protect_back && load32(std::uint32_t(at)) == desired;
}
const char* bind_save_pump() {
    if (!e_live_) return "patch_e_off";
    if (!engine_patch::install_window_open()) return "late_claim";
#ifndef X3M_MUSIC_KEEP_FL_FIXTURE
    const HMODULE zlib = GetModuleHandleW(L"zlib1.dll");
    const FARPROC write = zlib ? GetProcAddress(zlib, "gzwrite") : nullptr;
    const FARPROC close = zlib ? GetProcAddress(zlib, "gzclose") : nullptr;
#else
    const FARPROC write = fixture_gzwrite_, close = fixture_gzclose_;
#endif
    if (!write || !close) return "zlib_not_loaded";
    ImportSlot slots[2] = {{F::gzclose_iat_rva, "gzclose", &x3m_music_keep_fl_gzclose_stub, &x3m_music_keep_fl_gzclose,
                            &close_slot_live_},
                           {F::gzwrite_iat_rva, "gzwrite", &x3m_music_keep_fl_gzwrite_stub, &x3m_music_keep_fl_gzwrite,
                            &write_slot_live_}};
    const std::uint32_t exports[2] = {address_of(reinterpret_cast<void*>(close)),
                                      address_of(reinterpret_cast<void*>(write))};
    for (unsigned i = 0; i < 2; ++i)
        if (load32(std::uint32_t(base_ + slots[i].rva)) != exports[i]) return "original_mismatch";
    x3m_music_keep_fl_gzclose = exports[0];
    x3m_music_keep_fl_gzwrite = exports[1];
    for (unsigned i = 0; i < 2; ++i) {
        if (exchange_slot(base_ + slots[i].rva, exports[i], address_of(slots[i].stub))) {
            *slots[i].live = true;
            continue;
        }
        *slots[i].live = load32(std::uint32_t(base_ + slots[i].rva)) == address_of(slots[i].stub);
        for (unsigned j = i + 1; j-- > 0;)
            if (*slots[j].live && exchange_slot(base_ + slots[j].rva, address_of(slots[j].stub), exports[j]))
                *slots[j].live = false;
        return close_slot_live_ || write_slot_live_ ? "rollback_failed" : "slot_not_writable";
    }
    return "ok";
}
bool install_keep(const char** reason) {
    if (!engine_patch::install_window_open()) {
        *reason = "late_claim";
        return false;
    }
    if (const char* bad = W::first_mismatch(read_code, base_, W::keep_windows, W::keep_fixups)) {
        window_ = bad;
        *reason = "bytes_mismatch";
        return false;
    }
    if (!callers_match()) {
        *reason = "callers_mismatch";
        return false;
    }
    if (!pin_self()) {
        *reason = "pin_failed";
        return false;
    }
    holds_.clear();
    x3m_music_keep_fl_pause_record = 0;
    x3m_music_keep_fl_a_skip = std::uint32_t(base_ + F::a_skip_rva);
    x3m_music_keep_fl_a_next = std::uint32_t(base_ + F::a_next_rva);
    x3m_music_keep_fl_seek = std::uint32_t(base_ + F::seek_rva);
    x3m_music_keep_fl_status_fn = std::uint32_t(base_ + F::status_rva);
    x3m_music_keep_fl_run_fn = std::uint32_t(base_ + F::run_rva);
    x3m_music_keep_fl_pause_fn = std::uint32_t(base_ + F::pause_rva);
    x3m_music_keep_fl_pump_fn = std::uint32_t(base_ + F::pump_rva);
    x3m_music_keep_fl_free_fn = std::uint32_t(base_ + F::free_wrapper_rva);
    x3m_music_keep_fl_resume_ms = 0;
    x3m_music_keep_fl_save_pumping = 0;
    pump_.reset();
    resume_ = ResumeMemory{};
    queue_ = TrackQueue{};
    kc_ = KcPatch{};
    kc_sites_ = x3m::story::QueueSites{};
    kc_candidate_ = x3m::story::QueueSites{};
    kc_retries_ = x3m::story::Retries{};
    combat_track_ = 0;
    queue_armed_ = false;
    queue_state_ = queue_requested_ ? "disabled" : "not_requested";
    e_live_ = save_pump_ = false;
    e_state_ = "disabled";
    LARGE_INTEGER frequency{};
    frequency_ = QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 ? std::uint64_t(frequency.QuadPart) : 0;
    resume_ok_ = true; // the manager window (its inline position read) matched with the rest
    resume_state_ = "ok";
    status_gate_ = false;
    status_gate_state_ = "disabled";
    gated_ = 0;
    last_active_ = 2;
    bool ok = hook_site(a_site_, "music_keep_a_fl", F::a_site_rva, F::a_site_length, &x3m_music_keep_fl_a_stub,
                        x3m_music_keep_fl_a_continue, reason);
    if (ok && !claim_call(c_site_, F::c_site_rva, F::seek_rva, &x3m_music_keep_fl_c_thunk)) {
        ok = false;
        *reason = c_site_.patched_in ? "readback_failed" : c_site_.status;
    }
    if (ok) // Patch D, optional
        status_gate_state_ = claim_call(d_site_, F::d_site_rva, F::status_rva, &x3m_music_keep_fl_status_thunk)
                                 ? "ok"
                                 : (d_site_.patched_in ? "readback_failed" : d_site_.status);
    if (ok) { // Patch E, optional
        if (!frequency_)
            e_state_ = "no_clock";
        else
            e_state_ = claim_call(e_site_, F::e_site_rva, F::run_rva, &x3m_music_keep_fl_run_thunk)
                           ? "ok"
                           : (e_site_.patched_in ? "readback_failed" : e_site_.status);
    }
    if (ok)
        ok = hook_site(entry_site_, "music_stop_all_entry_fl", F::stop_all_rva, F::stop_all_length,
                       &x3m_music_keep_fl_entry_stub, x3m_music_keep_fl_entry_continue, reason);
    if (ok)
        ok = hook_site(stop_movie_site_, "music_stop_movie_entry_fl", F::stop_movie_rva, F::stop_movie_length,
                       &x3m_music_keep_fl_stop_movie_stub, x3m_music_keep_fl_stop_movie_continue, reason);
    if (ok && queue_requested_) { // the track queue, optional
        const char* why = "ok";
        queue_state_ = hook_site(play_site_, "music_play_entry_fl", F::play_rva, F::play_length,
                                 &x3m_music_keep_fl_play_stub, x3m_music_keep_fl_play_continue, &why)
                           ? "ok"
                           : why;
        if (std::strcmp(queue_state_, "ok")) engine_patch::restore(play_site_);
    }
    if (ok && std::strcmp(status_gate_state_, "ok")) engine_patch::restore_call(d_site_); // a live but unverified claim
    if (ok && std::strcmp(e_state_, "ok")) engine_patch::restore_call(e_site_);
    if (!ok) {
        bool back = true;
        back = engine_patch::restore(play_site_) && back;
        back = engine_patch::restore(stop_movie_site_) && back;
        back = engine_patch::restore(entry_site_) && back;
        back = engine_patch::restore_call(e_site_) && back;
        back = engine_patch::restore_call(d_site_) && back;
        back = engine_patch::restore_call(c_site_) && back;
        back = engine_patch::restore(a_site_) && back;
        keep_patched_ = a_site_.patched_in || c_site_.patched_in || d_site_.patched_in || e_site_.patched_in ||
                        entry_site_.patched_in || stop_movie_site_.patched_in || play_site_.patched_in;
        if (!back || keep_patched_) *reason = "rollback_failed";
        return false;
    }
    keep_patched_ = true;
    keep_armed_ = true;
    status_gate_ = d_site_.patched_in && !std::strcmp(status_gate_state_, "ok");
    e_live_ = e_site_.patched_in && !std::strcmp(e_state_, "ok");
    queue_armed_ = play_site_.patched_in && !std::strcmp(queue_state_, "ok");
    save_pump_state_ = bind_save_pump();
    save_pump_ = !std::strcmp(save_pump_state_, "ok");
    sync_pumping();
    *reason = "ok";
    return true;
}
bool requested(const wchar_t* name, bool* present) {
    wchar_t setting[4]{};
    const DWORD length = x3m::config::get(name, setting, 4);
    *present = length != 0;
    return length == 1 && setting[0] == L'1';
}
}

namespace x3m::music_keep {
bool initialize() {
    const DWORD error = GetLastError();
    if (keep_patched_) {
        SetLastError(error);
        return keep_armed_;
    }
    bool keep_present = false, trace_present = false, queue_present = false, dock_present = false;
    const bool keep_requested = requested(L"X3M_MUSIC_KEEP", &keep_present);
    const bool trace_requested = requested(L"X3M_MUSIC_TRACE", &trace_present);
    queue_variable_ = requested(L"X3M_MUSIC_QUEUE", &queue_present);
    queue_requested_ = queue_variable_ && keep_requested;
    dock_keep_ = requested(L"X3M_MUSIC_DOCK_KEEP", &dock_present) && queue_requested_;
    queue_state_ = queue_present && !keep_requested ? "keep_off" : "disabled";
    trace_present_ = trace_present;
    keep_present_ = keep_present;
    save_pump_reported_ = false;
#ifdef X3M_MUSIC_KEEP_FL_FIXTURE
    base_ = fixture_base_ ? fixture_base_ : x3m::fl::module_base();
#else
    base_ = x3m::fl::module_base();
#endif
    owner_slot_ = std::uint32_t(base_ + F::owner_slot_rva);
    vm_slot_ = std::uint32_t(base_ + F::vm_slot_rva);
    alert_slot_ = std::uint32_t(base_ + F::alert_registry_slot_rva);
    active_flag_ = std::uint32_t(base_ + F::active_flag_rva);
    input_slot_ = std::uint32_t(base_ + F::input_flags_slot_rva);
    lines_ = seq_ = suppressed_ = walks_cut_ = 0;
    queue_lines_ = queue_suppressed_ = 0;
    queue_cap_noted_ = cap_noted_ = false;
    queue_free_pending_ = 0;
    window_ = "-";
    bool applied = false;
    if (!keep_requested)
        keep_state_ = "disabled";
    else if (!base_)
        keep_state_ = "executable_mismatch";
    else
        applied = install_keep(&keep_state_);
    if (keep_present)
        log("music_keep requested=%u patched=%u armed=%u reason=%s window=%s site_a=0x%08lx site_c=0x%08lx site_entry=0x%08lx site_stop_movie=0x%08lx write_a=%s write_c=%s write_entry=%s write_stop_movie=%s "
            "status_gate=%s site_d=0x%08lx write_d=%s alt_tab_mode=%s patch_e=%s site_e=0x%08lx write_e=%s save_pump=%s resume=%s "
            "stub_a=0x%08lx thunk_c=0x%08lx stub_entry=0x%08lx stub_stop_movie=0x%08lx skip=0x%08lx next_record=0x%08lx seek=0x%08lx pause=0x%08lx holds=%u walk_limit=%u edition=fl",
            keep_requested ? 1u : 0u, keep_patched_ ? 1u : 0u, keep_armed_ ? 1u : 0u, keep_state_, window_,
            static_cast<unsigned long>(base_ + F::a_site_rva), static_cast<unsigned long>(base_ + F::c_site_rva),
            static_cast<unsigned long>(base_ + F::stop_all_rva), static_cast<unsigned long>(base_ + F::stop_movie_rva),
            write_kind(a_site_.patched_in, a_site_.atomic_write), write_kind(c_site_.patched_in, c_site_.atomic_write),
            write_kind(entry_site_.patched_in, entry_site_.atomic_write),
            write_kind(stop_movie_site_.patched_in, stop_movie_site_.atomic_write), status_gate_state_,
            static_cast<unsigned long>(base_ + F::d_site_rva), write_kind(d_site_.patched_in, d_site_.atomic_write),
            mode_name(status_gate_ ? StopMode::skip_all : StopMode::paused), e_state_,
            static_cast<unsigned long>(base_ + F::e_site_rva), write_kind(e_site_.patched_in, e_site_.atomic_write),
            save_pump_ ? "bound" : save_pump_state_, resume_state_,
            static_cast<unsigned long>(address_of(&x3m_music_keep_fl_a_stub)),
            static_cast<unsigned long>(address_of(&x3m_music_keep_fl_c_thunk)),
            static_cast<unsigned long>(address_of(&x3m_music_keep_fl_entry_stub)),
            static_cast<unsigned long>(address_of(&x3m_music_keep_fl_stop_movie_stub)),
            static_cast<unsigned long>(x3m_music_keep_fl_a_skip), static_cast<unsigned long>(x3m_music_keep_fl_a_next),
            static_cast<unsigned long>(x3m_music_keep_fl_seek), static_cast<unsigned long>(x3m_music_keep_fl_pause_fn),
            hold_capacity, list_walk_limit);
    if (trace_present)
        log("music_trace requested=%u patched=0 armed=0 reason=ap_only edition=fl", trace_requested ? 1u : 0u);
    if (queue_present)
        log("music_queue requested=%u armed=%u reason=%s site_play=0x%08lx write_play=%s stub_play=0x%08lx free=0x%08lx free_slot=%lu vm=0x%08lx "
            "detect=runtime kc_patch=%s key_base=0x%08lx stack_depth=%u cells=-%u,-%u,-%u,-%u,-%u,-%u dock_keep=%u edition=fl",
            queue_variable_ ? 1u : 0u, queue_armed_ ? 1u : 0u, queue_state_,
            static_cast<unsigned long>(base_ + F::play_rva), write_kind(play_site_.patched_in, play_site_.atomic_write),
            static_cast<unsigned long>(address_of(&x3m_music_keep_fl_play_stub)),
            static_cast<unsigned long>(base_ + F::free_wrapper_rva), static_cast<unsigned long>(F::free_slot),
            static_cast<unsigned long>(vm_slot_), queue_armed_ ? "pending" : "off",
            static_cast<unsigned long>(queue_key_base), x3m::story::fork_child_stack_depth,
            x3m::story::fork_child_cell_depths[0], x3m::story::fork_child_cell_depths[1],
            x3m::story::fork_child_cell_depths[2], x3m::story::fork_child_cell_depths[3],
            x3m::story::fork_child_cell_depths[4], x3m::story::fork_child_cell_depths[5],
            dock_keep_ && queue_armed_ ? 1u : 0u);
    save_pump_report(); // FL binds its import slots in install_keep: the row follows at once
    SetLastError(error);
    return applied;
}
// Fixture only on FL (the FL loader's unload calls are AP only): every live site back, the import slots back over our
// stubs, the KC window back when it still holds the queue's bytes.
bool shutdown() {
    if (!keep_patched_ && !write_slot_live_ && !close_slot_live_) return true;
    const DWORD error = GetLastError();
    keep_armed_ = queue_armed_ = false;
    bool ok = true;
    if (write_slot_live_ && exchange_slot(base_ + F::gzwrite_iat_rva, address_of(&x3m_music_keep_fl_gzwrite_stub),
                                          x3m_music_keep_fl_gzwrite))
        write_slot_live_ = false;
    if (close_slot_live_ && exchange_slot(base_ + F::gzclose_iat_rva, address_of(&x3m_music_keep_fl_gzclose_stub),
                                          x3m_music_keep_fl_gzclose))
        close_slot_live_ = false;
    ok = !write_slot_live_ && !close_slot_live_;
    const x3m::story::QueueSites* s = kc_.applied_story;
    if ((kc_.result == KcResult::applied || kc_.result == KcResult::already) && s && kc_.story == s) {
        unsigned char window[warp_window_length]{};
        const std::uint32_t at = kc_.code + s->warp_fork_offset;
        bool ours = kc_.code && x3m::engine_memory::read(at, window, warp_window_length);
        for (unsigned i = 0; ours && i < warp_window_length; ++i) ours = window[i] == s->warp_patch_memory[i];
        if (ours) std::memcpy(reinterpret_cast<void*>(std::uintptr_t(at)), s->warp_window_memory, warp_window_length);
    }
    ok = engine_patch::restore(play_site_) && ok;
    ok = engine_patch::restore(stop_movie_site_) && ok;
    ok = engine_patch::restore(entry_site_) && ok;
    ok = engine_patch::restore_call(e_site_) && ok;
    ok = engine_patch::restore_call(d_site_) && ok;
    ok = engine_patch::restore_call(c_site_) && ok;
    ok = engine_patch::restore(a_site_) && ok;
    keep_patched_ = a_site_.patched_in || c_site_.patched_in || d_site_.patched_in || e_site_.patched_in ||
                    entry_site_.patched_in || stop_movie_site_.patched_in || play_site_.patched_in;
    status_gate_ = save_pump_ = e_live_ = false;
    x3m_music_keep_fl_save_pumping = 0;
    pump_.reset();
    queue_ = TrackQueue{};
    queue_free_pending_ = 0;
    holds_.clear();
    keep_state_ = ok && !keep_patched_ ? "restored" : "restore_failed";
    SetLastError(error);
    return ok && !keep_patched_;
}
const char* state() {
    return keep_state_;
}
const char* trace_state() {
    return trace_present_ ? "ap_only" : "disabled";
}
// One music_keep_save_pump_hook row when the keep was asked for: the import slots bound, or the refusal.
void save_pump_report() {
    if (!keep_present_ || save_pump_reported_) return;
    save_pump_reported_ = true;
    const DWORD error = GetLastError();
    const char* reason = !keep_armed_ ? keep_state_ : !e_live_ ? e_state_ : save_pump_state_;
    log("music_keep_save_pump_hook reason=%s save_mode=%s write_slot=0x%08lx close_slot=0x%08lx write_row=%u close_row=%u site_e=0x%08lx write_e=%s pump=0x%08lx "
        "interval_ms=%u stride=%u bound_ms=%u resume=%s combat=alert_edge edition=fl",
        reason, save_pump_ ? "keep_running" : "paused", static_cast<unsigned long>(base_ + F::gzwrite_iat_rva),
        static_cast<unsigned long>(base_ + F::gzclose_iat_rva), write_slot_live_ ? 1u : 0u, close_slot_live_ ? 1u : 0u,
        static_cast<unsigned long>(base_ + F::e_site_rva), write_kind(e_site_.patched_in, e_site_.atomic_write),
        static_cast<unsigned long>(base_ + F::pump_rva), pump_interval_ms, pump_poll_stride, pump_bound_ms,
        resume_state_);
    SetLastError(error);
}
// FL has no save_load_complete marker; the KC gate opens on two agreeing observations (KcPatch::settle).
void loaded() {
    if (queue_armed_) kc_.load_complete();
}
void present(unsigned long long, unsigned long long frame, bool) {
    if (!keep_patched_) return;
    current_frame_ = frame;
    if (!keep_armed_) return;
    if (queue_armed_) {
        if (queue_free_pending_) music_queue_free_present();
        queue_.walk_caller = 0;
        music_queue_kc_present();
        if (queue_.active()) music_queue_alert_present();
    }
    if (pump_.kept && !pump_.pumping && !pump_.run_marker) music_keep_replay_check();
    const unsigned active = load32(active_flag_) != 0 ? 1u : 0u;
    if (active == last_active_) return;
    const DWORD error = GetLastError();
    const std::uint32_t pointer = load32(input_slot_);
    write_line("music_keep_active frame=%llu seq=%lu qpc=%llu from=%s to=%u run_in_background=%u gated=%lu edition=fl\n",
               frame, ++seq_, qpc(),
               last_active_ == 2 ? "unsampled"
               : last_active_    ? "1"
                                 : "0",
               active, pointer && (load32(pointer) & run_in_background_bit) ? 1u : 0u, gated_);
    last_active_ = active;
    SetLastError(error);
}
#ifdef X3M_MUSIC_KEEP_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
const char* fixture_window() {
    return window_;
}
bool fixture_armed(bool* gate, bool* e, bool* queue, bool* pump) {
    *gate = status_gate_;
    *e = e_live_;
    *queue = queue_armed_;
    *pump = save_pump_;
    return keep_armed_;
}
const char* fixture_states(const char** gate, const char** e, const char** queue, const char** pump) {
    *gate = status_gate_state_;
    *e = e_state_;
    *queue = queue_state_;
    *pump = save_pump_state_;
    return keep_state_;
}
unsigned fixture_holds() {
    return holds_.count;
}
const TrackQueue* fixture_queue() {
    return &queue_;
}
SavePump* fixture_pump() {
    return &pump_;
}
const ResumeMemory* fixture_resume() {
    return &resume_;
}
void fixture_set_queue(const TrackQueue& q) {
    queue_ = q;
}
void fixture_reset_holds() {
    holds_.clear();
}
void fixture_zlib(FARPROC write, FARPROC close) {
    fixture_gzwrite_ = write;
    fixture_gzclose_ = close;
}
// A detection for the VM's current key, as music_queue_kc_present keeps one (the fixture has no story object).
void fixture_set_story(const x3m::story::QueueSites& sites) {
    kc_sites_ = sites;
    kc_sites_.detected = true;
}
const KcPatch* fixture_kc() {
    return &kc_;
}
#endif
}
