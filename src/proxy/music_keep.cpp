#include "music_keep.h"
#include "config.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "cpu_state.h"
#include "capture.h"
#include "log_tiers.h"
#include "session_log.h"
#include "loading_trace.h"
#include "engine_memory.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::music_keep::core;
namespace core = x3m::music_keep::core;
namespace engine_patch = x3m::engine_patch;

// patched_: sites registered for shutdown() (also after a failed rollback);
// armed_: a complete install, the only state in which the handlers act. A
// stub left chained by a partial install or an unrestorable site sees
// armed_ false and behaves as vanilla.
bool keep_patched_ = false, keep_armed_ = false, trace_patched_ = false, trace_armed_ = false;
const char* keep_state_ = "disabled";
const char* trace_state_ = "disabled";
// Sites the keep owns alone (A, C) and the trace owns alone (play entry);
// the stop-all entry and the MOV_StopMovie entry are shared: claimed once by
// whichever feature installs first, each feature chains its own stub in front
// (users counts the chained features; the claim is restored when the last one
// releases it). The window checks see through a live shared claim
// (core::window_matches), so either feature may install first.
using SharedSite = core::SharedSite<engine_patch::Site>;
engine_patch::Site a_site_{};
engine_patch::CallSite c_site_{}, d_site_{};
// Patch D (the status-query gate) is optional inside the keep: when it cannot be claimed the keep still installs and
// alt-tab takes the flown paused mode (status_gate_ false, status_gate_state_ the refusal, logged).
bool status_gate_ = false;
const char* status_gate_state_ = "disabled";
unsigned long gated_ = 0;  // status queries answered "playing" by the gate
unsigned last_active_ = 2; // the engine's active flag at the last Present (2 = not sampled yet)
// Which shared sites each feature acquired (released only by that feature; an orphaned live claim with no users is
// restored by whoever rolls back or shuts down).
bool keep_entry_acquired_ = false, keep_stop_movie_acquired_ = false, trace_entry_acquired_ = false,
     trace_stop_movie_acquired_ = false, keep_play_acquired_ = false, trace_play_acquired_ = false;
SharedSite entry_site_{}, stop_movie_site_{}, play_site_{}; // the play entry 0x00498c90 is shared by the queue and the trace
Holds holds_;
// The track queue (X3M_MUSIC_QUEUE=1 under the keep; music_keep_core.h "The track queue"): optional inside the keep like
// Patches D and E. queue_armed_ only after its windows matched and the play entry was acquired; the KC patch is
// maintained from Present (kc_), the state machine lives in queue_.
bool queue_requested_ = false, queue_armed_ = false, queue_variable_ = false; // queue_variable_: X3M_MUSIC_QUEUE is exactly 1
const char* queue_state_ = "disabled";
TrackQueue queue_;
KcPatch kc_;
unsigned char kc_window_seen_[warp_window_length]{}; // the last window bytes read, for the row
// Music through the save: Patch E (optional; with the gzwrite row bound it enables the save pump) and the episode.
engine_patch::CallSite e_site_{};
bool e_live_ = false;
const char* e_state_ = "disabled";
bool save_pump_ = false;                  // Patch E live and the gzwrite / gzclose rows bound on the verified slot
const char* save_pump_state_ = "unbound"; // the bind's refusal, or "ok"
bool save_pump_bind_called_ = false, save_pump_reported_ = false, keep_present_ = false;
bool write_row_live_ = false, close_row_live_ = false;
SavePump pump_;
std::uint32_t gzclose_caller_ = 0;
std::uint64_t frequency_ = 0;
// Resume after combat: available when the engine's position reader matches (the keep reads the position the same way).
bool resume_ok_ = false;
const char* resume_state_ = "disabled";
ResumeMemory resume_;
volatile unsigned long long current_frame_ = 0; // stored at Present, read by the handlers
unsigned long lines_ = 0, seq_ = 0, suppressed_ = 0, walks_cut_ = 0;
bool cap_noted_ = false;
unsigned long queue_lines_ = 0, queue_suppressed_ = 0; // the music_queue rows' own cap (queue_line_cap)
bool queue_cap_noted_ = false;
std::uint32_t queue_free_pending_ = 0; // a private key to free at the next Present (alt-tab under paused), 0 none

std::uint32_t load32(std::uint32_t at) {
    std::uint32_t v = 0;
    std::memcpy(&v, reinterpret_cast<const void*>(std::uintptr_t(at)), 4);
    return v;
}
std::uint64_t qpc() {
    LARGE_INTEGER v{};
    return QueryPerformanceCounter(&v) && v.QuadPart > 0 ? std::uint64_t(v.QuadPart) : 0;
}
// The queue's story sites (story_detect.h): kc_sites_ the detection kept for a VM key (a success or a definitive failure,
// once the load gate opened over it; Retries::keep), kc_candidate_ the detection of the current due check while none is
// kept for the key, kc_retries_ the transient failures per key, and the scratch the detection reads a method into.
// Present and the T2/T3 handlers run on the game's main thread.
x3m::story::QueueSites kc_sites_{}, kc_candidate_{};
x3m::story::Retries kc_retries_{};
unsigned char kc_scratch_[x3m::story::scratch_size];
bool engine_read(std::uint32_t at, void* out, unsigned n) {
    return x3m::engine_memory::read(at, out, n);
}
// The detection for the VM's current object when one was made for exactly this key and found every site: the VM
// pointer from the image, its CODE buffer, size and build through the fault-free reader. Null otherwise (no VM, a
// reload not yet detected, no story loaded, a signature that did not match): nothing defers.
const x3m::story::QueueSites* sites_now() {
    const std::uint32_t vm = load32(vm_ptr_va);
    x3m::story::VmKey key;
    if (!vm || !x3m::story::read_key(engine_read, vm, key)) return nullptr;
    return kc_sites_.detected && kc_sites_.key == key && kc_sites_.outcome.ok() ? &kc_sites_ : nullptr;
}
// The cockpit alert record R = *0x00608504: [R+4] committed level, [R+8] this frame's level (the combat test).
Alert read_alert() {
    Alert a;
    std::uint32_t record = 0, levels[3]{};
    a.ok = x3m::engine_memory::read(alert_record_ptr_va, &record, 4) && record &&
           x3m::engine_memory::read(record, levels, sizeof levels);
    a.committed = levels[1];
    a.current = levels[2];
    return a;
}
// The id of the last music play and whether the alert was on at it (a combat track): a T3 stop of that id is combat_old.
std::uint32_t combat_track_ = 0;
// The known builds' expectations for the install row (story_builds.h; the run-time source is the detection):
// name:build:window:continue per entry.
char stories_[192] = "";
void format_stories() {
    unsigned at = 0;
    for (const x3m::story::Build& b : x3m::story::builds) {
        const int n = std::snprintf(stories_ + at, sizeof stories_ - at, "%s%s:%lu:0x%05lx:0x%05lx", at ? "," : "", b.name,
                                    static_cast<unsigned long>(b.build), static_cast<unsigned long>(b.warp_fork_offset),
                                    static_cast<unsigned long>(b.warp_continue_offset));
        if (n > 0 && unsigned(n) < sizeof stories_ - at) at += unsigned(n);
    }
}
// Compares a window with the unpatched image: a live shared entry claim (either feature's) is read as its
// original bytes when the site still holds exactly that claim's patch; anything else there fails.
bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    LiveClaim claims[3]{};
    unsigned count = 0;
    const SharedSite* const shared[] = {&entry_site_, &stop_movie_site_, &play_site_};
    for (const SharedSite* s : shared)
        if (s->site.patched_in)
            claims[count++] = LiveClaim{std::uint32_t(s->site.spec.address), s->site.original, s->site.patched, 5};
    return window_matches(
        [](std::uint32_t a, unsigned char* out, unsigned n) { return engine_patch::read_code(a, out, n); },
        std::uint32_t(at), expected, length, claims, count);
}
bool call_targets(std::uintptr_t at, std::uintptr_t target) {
    unsigned char code[5]{};
    if (!engine_patch::read_code(at, code, 5) || code[0] != 0xe8) return false;
    std::uint32_t rel = 0;
    std::memcpy(&rel, code + 1, 4);
    return at + 5 + rel == target;
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine jumps into the stubs.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&keep_patched_), &module) != FALSE &&
           module != nullptr;
}
const char* write_kind(bool in, bool atomic) {
    return in ? (atomic ? "atomic" : "plain") : "none";
}
engine_patch::SiteSpec spec_for(const char* name, std::uintptr_t address, const unsigned char* bytes, unsigned length) {
    engine_patch::SiteSpec spec{};
    spec.name = name;
    spec.address = address;
    spec.length = length;
    spec.ret_pop = 0;
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, bytes, length);
    return spec;
}
void write_line(const char* format, ...);
// Bounded walk of the engine's record list; a walk that reaches the limit is logged (music_walk_cut) and counts.
template <class Visit> void walk_records(const char* what, Visit&& visit) {
    const std::uint32_t head = load32(list_head_va);
    if (!head) return;
    std::uint32_t node = load32(head);
    unsigned i = 0;
    for (; i < list_walk_limit && node; ++i) {
        if (load32(node) == 0) return; // the sentinel
        if (visit(node)) return;
        node = load32(node);
    }
    if (i == list_walk_limit) {
        ++walks_cut_;
        write_line("music_walk_cut frame=%llu seq=%lu qpc=%llu walk=%s limit=%u cuts=%lu\n", current_frame_, ++seq_,
                   qpc(), what, list_walk_limit, walks_cut_);
    }
}
// The first record with `id`; 0 when absent.
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
// Whether `record` is still linked with the same id and media object; fills its flags and completion context.
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
// One event line through the session log's buffer (no file I/O on the game thread since the logging tiers; the
// callers' formats keep their trailing newline, which the log strips). The CRT formatter runs behind call_preserved
// inside the session log, keeping the handlers' audited graph x87-free. Capped per session (`cap` event lines on the
// `count` counter), then one notice.
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
        x3m::log("%s frame=%llu lines=%lu note=%s", notice_name, current_frame_, count, note);
        return;
    }
    x3m::session_log::vlog(format, args);
}
// The music_trace_* / music_keep_* lines: trace_line_cap per session, then one music_trace_cap notice.
void write_line(const char* format, ...) {
    va_list args;
    va_start(args, format);
    write_capped(lines_, suppressed_, cap_noted_, trace_line_cap, "music_trace_cap", "further_music_lines_suppressed", format,
                 args);
    va_end(args);
}
// The music_queue rows: queue_line_cap per session of their own, then one music_queue_cap notice.
void write_queue_line(const char* format, ...) {
    va_list args;
    va_start(args, format);
    write_capped(queue_lines_, queue_suppressed_, queue_cap_noted_, queue_line_cap, "music_queue_cap", "further_queue_lines_suppressed",
                 format, args);
    va_end(args);
}
const char* action_name(SeekAction a) {
    return a == SeekAction::skip ? "skip" : a == SeekAction::pause_then_vanilla ? "pause_then_vanilla" : "vanilla";
}
}
extern "C" volatile std::uint32_t x3m_music_keep_save_pumping;
extern "C" std::uint32_t __cdecl x3m_music_keep_call_pause(std::uint32_t record);
extern "C" void __cdecl x3m_music_queue_call_completion(std::uint32_t function, std::uint32_t context, std::uint32_t status);
extern "C" std::uint32_t __cdecl x3m_music_queue_call_free(std::uint32_t key);
namespace {
// The flag loading_trace's gzwrite / gzclose rows test before calling the observer.
void sync_pumping() {
    x3m_music_keep_save_pumping = keep_armed_ && save_pump_ && pump_.pumping ? 1u : 0u;
}
std::uint64_t span_ms(std::uint64_t from, std::uint64_t to) {
    return to >= from ? ticks_to_ms(to - from, pump_.frequency) : 0;
}
// One line per save pump episode (written when it ends), then the episode is reset.
void pump_line(const char* run, std::uint32_t buffer_state) {
    const SavePump& p = pump_;
    const std::uint64_t pump_end = p.pump_end != PumpEnd::none ? p.pump_end_at : p.end_at;
    write_line(
        "music_keep_save_pump frame=%llu seq=%lu qpc=%llu record=0x%08lx id=%lu calls=%lu ret0=%lu ret1=%lu ret2=%lu ret_other=%lu writes=%lu samples=%lu "
        "span_ms=%llu first_call_ms=%llu max_gap_ms=%llu end_ms=%llu pump_end=%s close_caller=0x%08lx end=%s run=%s buffer_state=%lu "
        "fallback=%s fallback_reason=%s fallback_ms=%llu interval_ms=%u\n",
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
// The fallback of an early stop of the kept-running state: pause the record through the engine's 0x004d1810 (the
// flown paused mode; the replay's seek skip and vanilla Run resume it) when it is still linked as the target, flag 2
// clear, unclaimed and music (the F2 orphan rule), and turn its hold into a paused one; else nothing to pause.
void run_fallback(const x3m::LightCallBoundary& cpu) {
    if (!pump_.fallback_due()) return;
    LiveRecord r{};
    const bool linked = live_record(pump_.record, pump_.id, pump_.media, r) && !(r.flags & flag_playing) &&
                        r.context == 0 && (r.flags & flag_music);
    if (linked) {
        cpu.before_original(); // the engine's MXCSR and LastError for the call; restored on return
        x3m_music_keep_call_pause(pump_.record);
        holds_.set_mode(pump_.record, pump_.id, pump_.media, StopMode::paused);
    }
    pump_.note_fallback(linked);
}
// Trace of the calling KC task (T2, T3): the header through the IP, read fault-free; false when unreadable.
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
// Cell `index` of the task's stack (absolute: [ESI+0x14] is the end of the cell buffer, valid cells -capacity..-1,
// music-script-driver.md section 8.4): tag byte and dword, read fault-free, only inside the buffer.
bool read_cell_abs(const TaskView& v, std::int32_t index, unsigned& tag, std::uint32_t& value) {
    if (!v.cells || !cell_index_ok(index, v.capacity)) return false;
    unsigned char cell[cell_size]{};
    if (!x3m::engine_memory::read(v.cells + std::uint32_t(cell_offset(index)), cell, cell_size)) return false;
    tag = cell[0];
    std::memcpy(&value, cell + 1, 4);
    return true;
}
// Cell k+i (the trace's relative reads; k = [ESI+0x18] is negative).
bool read_cell(const TaskView& v, unsigned i, unsigned& tag, std::uint32_t& value) {
    return read_cell_abs(v, std::int32_t(v.index) + std::int32_t(i), tag, value);
}
void resume_line(ResumeAction action, const ResumeMemory& memory, std::uint32_t other_id, const char* at) {
    write_line("music_keep_resume frame=%llu seq=%lu qpc=%llu id=%lu saved_ms=%lu action=%s at=%s other_id=%lu\n",
               current_frame_, ++seq_, qpc(), static_cast<unsigned long>(memory.id),
               static_cast<unsigned long>(memory.ms), resume_action_name(action), at,
               static_cast<unsigned long>(other_id));
}
// The engine's position reader 0x004d0600, mirrored (no x87: the REFTIME product is SSE2 under -mfpmath=sse):
// [m+0x8c] & 0x20 -> IMultiMediaStream::GetTime on [m+4] (vtbl +0x20, 100 ns units); else
// IMediaPosition::get_CurrentPosition on [m+0x70] (vtbl +0x24, seconds). False on a missing interface, a failed call
// or a reading outside [0, 24 h).
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
    if (!(milliseconds >= 0.0 && milliseconds < double(position_limit_ms))) return false; // NaN fails too
    ms = std::uint32_t(std::int32_t(milliseconds)); // < 2^31: cvttsd2si (an unsigned conversion would use x87 fistp)
    return true;
}
// ---- The track queue's engine operations ----
void store32(std::uint32_t at, std::uint32_t value) {
    std::memcpy(reinterpret_cast<void*>(std::uintptr_t(at)), &value, 4);
}
// The orphan O as it is now: the first record under the private key, with the same media object.
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
// A completion (ctx, 1) through the script owner's slot table, exactly as the MOV_StopMovie body does it (owner != 0,
// [owner+4] != 0, slot < 32, function != 0; cdecl). It only relinks the task (0x004a4910 -> 0x004a4740), no bytecode runs.
void deliver(const x3m::LightCallBoundary& cpu, std::uint32_t slot, std::uint32_t context) {
    const std::uint32_t owner = load32(vm_ptr_va);
    if (!owner || !load32(owner + vm_slot_guard) || slot >= slot_count) return;
    const std::uint32_t function = load32(owner + slot_completion + slot * slot_stride);
    if (!function) return;
    cpu.before_original(); // the engine's MXCSR and LastError for the call; restored by the boundary
    x3m_music_queue_call_completion(function, context, 1);
}
// One row per transition or refusal.
void queue_line(const QueueOutcome& o, const QueueStopView* stop, std::uint32_t freed) {
    if (o.event == QueueEvent::none) return;
    if (o.event == QueueEvent::refuse && stop) {
        write_queue_line(
            "music_queue frame=%llu seq=%lu qpc=%llu event=refuse reason=%s state=%s old=%lu new=%lu task=%lu ip=0x%lx k=%ld capacity=%lu cells=%u "
            "c-18=%u:0x%08lx c-13=%u:0x%08lx c-11=%u:0x%08lx c-10=%u:0x%08lx c-9=%u:0x%08lx c-7=%u:0x%08lx alert=%u/%lu/%lu record=0x%08lx flags=0x%lx ctx=%lu\n",
            current_frame_, ++seq_, qpc(), queue_reason_name(o.reason), queue_state_name(queue_.state),
            static_cast<unsigned long>(o.old_id), static_cast<unsigned long>(o.new_id), static_cast<unsigned long>(o.stop_task),
            static_cast<unsigned long>(stop->task_ip), static_cast<long>(stop->index), static_cast<unsigned long>(stop->capacity),
            stop->cells_ok ? 1u : 0u, stop->cells[0].tag, static_cast<unsigned long>(stop->cells[0].value), stop->cells[1].tag,
            static_cast<unsigned long>(stop->cells[1].value), stop->cells[2].tag, static_cast<unsigned long>(stop->cells[2].value),
            stop->cells[3].tag, static_cast<unsigned long>(stop->cells[3].value), stop->cells[4].tag,
            static_cast<unsigned long>(stop->cells[4].value), stop->cells[5].tag, static_cast<unsigned long>(stop->cells[5].value),
            stop->alert_ok ? 1u : 0u, static_cast<unsigned long>(stop->alert_old), static_cast<unsigned long>(stop->alert_new),
            static_cast<unsigned long>(stop->record), static_cast<unsigned long>(stop->flags), static_cast<unsigned long>(stop->context));
        return;
    }
    write_queue_line(
        "music_queue frame=%llu seq=%lu qpc=%llu event=%s reason=%s state=%s old=%lu new=%lu key=0x%08lx record=0x%08lx task=%lu stop_task=%lu "
        "wake=%u clear=%u complete=%u park=%u freed=%lu free_later=%u frames=%llu episode=%lu\n",
        current_frame_, ++seq_, qpc(), queue_event_name(o.event), queue_reason_name(o.reason), queue_state_name(queue_.state),
        static_cast<unsigned long>(o.old_id), static_cast<unsigned long>(o.new_id), static_cast<unsigned long>(o.key),
        static_cast<unsigned long>(o.record), static_cast<unsigned long>(o.task), static_cast<unsigned long>(o.stop_task),
        o.wake ? 1u : 0u, o.clear ? 1u : 0u, o.complete ? 1u : 0u, o.park ? 1u : 0u, static_cast<unsigned long>(freed),
        o.free_later ? 1u : 0u, o.frames,
        static_cast<unsigned long>(queue_.episode));
}
// Executes an outcome against the engine, in the core's order: re-key, wake (then zero O's context and slot) or only
// zero them (`clear`), complete the caller, park, free now or at the next Present. Returns the free's answer (1 found)
// for the row.
std::uint32_t queue_apply(const x3m::LightCallBoundary& cpu, const QueueOutcome& o) {
    std::uint32_t freed = 0;
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
    if (o.free_key) {
        cpu.before_original();
        freed = x3m_music_queue_call_free(o.key);
    }
    if (o.free_later) queue_free_pending_ = o.key;
    return freed;
}
}

extern "C" {
// Pointer words the stubs jump or call through: set before a site goes live.
volatile std::uint32_t x3m_music_stop_all_continue = 0; // Patch A claim tail (the displaced instructions + jmp
                                                        // 0x004982e1)
volatile std::uint32_t x3m_music_stop_all_skip = 0;     // 0x00498322
volatile std::uint32_t x3m_music_stop_all_next = 0;     // 0x00498359 (the per-record loop's continue)
volatile std::uint32_t x3m_music_seek_original = 0;     // 0x004d0430
volatile std::uint32_t x3m_music_pause_fn = 0;          // 0x004d1810
volatile std::uint32_t x3m_music_status_original = 0;   // 0x004d14e0
volatile std::uint32_t x3m_music_run_original = 0;      // 0x004d1870 (Patch E)
volatile std::uint32_t x3m_music_pump_fn = 0;           // 0x004d0700 (the save pump)
volatile std::uint32_t x3m_music_keep_save_pumping = 0;
std::uint32_t x3m_music_keep_resume_ms = 0;
volatile std::uint32_t x3m_music_keep_entry_continue = 0, x3m_music_keep_stop_movie_continue = 0;
volatile std::uint32_t x3m_music_trace_stop_continue = 0, x3m_music_trace_play_continue = 0,
                       x3m_music_trace_stop_movie_continue = 0;
volatile std::uint32_t x3m_music_queue_play_continue = 0; // the queue's stub on the shared play entry
volatile std::uint32_t x3m_music_free_fn = 0;             // 0x004986b0 (delete-by-id, ECX = id)
std::uint32_t x3m_music_keep_pause_record = 0;

__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_keep_stop_all(std::uint32_t record,
                                                                                  std::uint32_t return_va) {
    x3m::LightCallBoundary cpu; // MXCSR + LastError; no x87 below
    if (!keep_armed_) return unsigned(StopMode::vanilla);
    const std::uint32_t flags = load32(record + record_flags), id = load32(record + record_id),
                        media = load32(record + record_media);
    const bool save_pump_free = save_pump_ && !pump_.active(); // one target per save
    const StopDecision d = decide_stop(holds_, record, flags, id, media, return_va, status_gate_, save_pump_free);
    if (d.pump) {
        pump_.arm(record, id, media, GetCurrentThreadId(), qpc(), frequency_);
        sync_pumping();
    }
    if (flags & flag_music)
        write_line(
            "music_keep_stop frame=%llu seq=%lu qpc=%llu caller=0x%08lx name=%s record=0x%08lx id=%lu flags=0x%lx mode=%s held=%u holds=%u pump=%u\n",
            current_frame_, ++seq_, qpc(), static_cast<unsigned long>(return_va), stop_caller_name(return_va),
            static_cast<unsigned long>(record), static_cast<unsigned long>(id), static_cast<unsigned long>(flags),
            mode_name(d.mode), d.held ? 1u : 0u, holds_.count, d.pump ? 1u : 0u);
    return unsigned(d.mode);
}
}
namespace {
// The gated case of Patch D (engine inactive): the boundary lives here so the per-frame fast path below carries no
// unwind registration.
__attribute__((noinline)) unsigned music_keep_status_inactive(std::uint32_t record) {
    x3m::LightCallBoundary cpu;
    const std::uint32_t pointer = load32(input_flags_ptr_va);
    if (!pointer) return 0; // the engine dereferences it itself on this path; forward unchanged
    const std::uint32_t input = load32(pointer), flags = load32(record + record_flags), id = load32(record + record_id),
                        media = load32(record + record_media);
    if (!decide_status(holds_, record, flags, id, media, 0, input)) return 0;
    ++gated_;
    write_line(
        "music_keep_status frame=%llu seq=%lu qpc=%llu record=0x%08lx id=%lu flags=0x%lx input_flags=0x%08lx action=playing gated=%lu\n",
        current_frame_, ++seq_, qpc(), static_cast<unsigned long>(record), static_cast<unsigned long>(id),
        static_cast<unsigned long>(flags), static_cast<unsigned long>(input), gated_);
    return 1;
}
}
extern "C" {
// Patch D (ECX = record at the call 0x004983d9): 1 = answer "playing" without asking the engine, 0 = forward.
// Per flag-2 record per frame; the common path (gate off or the engine active) is three integer loads and no call,
// no API, no floating point, no unwind registration.
__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_keep_status(std::uint32_t record) {
    if (!keep_armed_ || !status_gate_ || load32(active_flag_va) != 0) return 0;
    return music_keep_status_inactive(record);
}
__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_keep_seek(std::uint32_t record,
                                                                              std::int32_t start_ms) {
    x3m::LightCallBoundary cpu;
    if (!keep_armed_) return unsigned(SeekAction::vanilla);
    const std::uint32_t flags = load32(record + record_flags);
    if (!(flags & flag_music))
        return unsigned(SeekAction::vanilla); // the common path: speech, video and every other record class
    const std::uint32_t id = load32(record + record_id), media = load32(record + record_media);
    const unsigned holds_before = holds_.count;
    const SeekDecision d = decide_seek(holds_, record, flags, id, media, start_ms, live_record);
    x3m_music_keep_pause_record = d.pause_record;
    // Every music play ends the save pump episode; the replay of the kept record leaves the Run marker for Patch E.
    if (pump_.active() && pump_.on_play(record, id, media, d.action == SeekAction::skip, qpc())) pump_line("none", 0);
    sync_pumping();
    unsigned answer = unsigned(d.action);
    std::uint32_t resume_ms = 0;
    const Alert alert = read_alert(); // a play with the alert on is a combat track
    combat_track_ = alert_on(alert) ? id : 0;
    if (resume_ok_ && resume_.valid) {
        const ResumeMemory before = resume_;
        const ResumeAction r = resume_on_play(resume_, id, start_ms, d.action, resume_ms, alert);
        if (r == ResumeAction::seek) {
            x3m_music_keep_resume_ms = resume_ms;
            answer |= seek_resume_bit;
        }
        if (r != ResumeAction::none && r != ResumeAction::keep) resume_line(r, before, id, "play");
    }
    write_line(
        "music_keep_seek frame=%llu seq=%lu qpc=%llu record=0x%08lx id=%lu start_ms=%ld flags=0x%lx action=%s hold_id=%lu pause_record=0x%08lx holds_before=%u resume_ms=%lu\n",
        current_frame_, ++seq_, qpc(), static_cast<unsigned long>(record), static_cast<unsigned long>(id),
        static_cast<long>(start_ms), static_cast<unsigned long>(flags), action_name(d.action),
        static_cast<unsigned long>(d.hold_id), static_cast<unsigned long>(d.pause_record), holds_before,
        static_cast<unsigned long>(resume_ms));
    return answer;
}
// Stop-all entry (any caller): the next orphaned keep-running track to pause, 0 when none (the stub loops).
__attribute__((force_align_arg_pointer)) std::uint32_t __cdecl x3m_music_keep_stop_all_entry(
    const std::uint32_t* block) {
    x3m::LightCallBoundary cpu;
    if (!keep_armed_) return 0;
    const std::uint32_t return_va = block[4];
    // The track queue ends before the walk on every caller but alt-tab under skip_all: C is woken through O's slot and P
    // is freed (outside the walk, which has not loaded its first node yet), so the walk sees neither O nor the private
    // key. Alt-tab under paused ends it too, with the free deferred to the next Present (queue_on_stop_all).
    if (queue_.active()) {
        const QueueOutcome o = queue_on_stop_all(queue_, return_va, status_gate_, orphan_view(), current_frame_);
        if (o.event != QueueEvent::none) queue_line(o, nullptr, queue_apply(cpu, o));
    }
    // A story reload begins (the session function's or the loader's stop-all): the KC patch waits for its completion.
    if (queue_armed_ && story_load_caller(return_va)) kc_.load_begin();
    // Any stop-all ends a save pump episode (the stub calls this handler until it returns 0: idempotent), and a vanilla
    // caller clears the resume memory.
    if (pump_.active() && pump_.end_now(PumpEnd::stop_all, qpc())) pump_line("none", 0);
    if (resume_ok_ && resume_.valid) {
        const ResumeMemory before = resume_;
        const ResumeAction r = resume_on_stop_all(resume_, return_va);
        if (r != ResumeAction::none) resume_line(r, before, 0, stop_caller_name(return_va));
    }
    if (!holds_.count) return 0;
    const std::uint32_t record = stale_hold_to_pause(holds_, live_record);
    if (record)
        write_line(
            "music_keep_orphan frame=%llu seq=%lu qpc=%llu caller=0x%08lx name=%s record=0x%08lx id=%lu action=pause holds=%u\n",
            current_frame_, ++seq_, qpc(), static_cast<unsigned long>(return_va), stop_caller_name(return_va),
            static_cast<unsigned long>(record), static_cast<unsigned long>(load32(record + record_id)), holds_.count);
    return record;
}
// MOV_StopMovie native entry (EAX = id, ESI = the calling task, EDI = its completion slot): the id's holds go; the
// track queue decides (defer, collapse, end or nothing); the engine pauses the record itself unless the queue consumed
// the stop (returns 1: the stub returns to the caller instead of running the body).
__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_keep_stop_movie(const std::uint32_t* block,
                                                                                    std::uint32_t task,
                                                                                    std::uint32_t slot) {
    x3m::LightCallBoundary cpu;
    if (!keep_armed_) return 0;
    const std::uint32_t id = block[2], return_va = block[4];
    const unsigned dropped = holds_.count ? drop_holds_by_id(holds_, id) : 0;
    if (dropped)
        write_line(
            "music_keep_stop_movie frame=%llu seq=%lu qpc=%llu id=%lu caller=0x%08lx name=%s dropped=%u holds=%u\n",
            current_frame_, ++seq_, qpc(), static_cast<unsigned long>(id), static_cast<unsigned long>(return_va),
            stop_movie_caller_name(return_va), dropped, holds_.count);
    if (pump_.active() && pump_.id == id && pump_.end_now(PumpEnd::stop_movie, qpc())) pump_line("none", 0);
    if (return_va != stop_movie_native_return_va) return 0; // the two other callers carry no task: vanilla
    const std::uint32_t record = find_record(id);
    const std::uint32_t flags = record ? load32(record + record_flags) : 0;
    // The track queue (queue_armed_ only with the keep; a disarmed queue still logs one refuse row per
    // PlayMusicWithFade stop of a music record, so a flight shows what it would have deferred).
    bool resume_candidate = true;
    const Alert alert = read_alert(); // the rising edge (a combat start) and the queue's 0/0 test
    if (queue_armed_ || queue_requested_) {
        const x3m::story::QueueSites* story = sites_now(); // the queue's IPs and cells
        QueueStopView v;
        v.story = story;
        v.combat_old = combat_track_ != 0 && id == combat_track_;
        v.return_va = return_va;
        v.id = id;
        v.task_id = block[5];
        v.slot = slot;
        TaskView t{};
        v.task_ok = read_task(task, t);
        v.task_ip = t.ip;
        v.capacity = t.capacity;
        v.index = std::int32_t(t.index);
        if (story && v.task_ok && v.task_ip == story->kc_play_stop_ip) {
            v.cells_ok = true;
            for (unsigned i = 0; i < queue_cell_count && v.cells_ok; ++i)
                v.cells_ok = read_cell_abs(t, -std::int32_t(story->queue_cell_depths[i]), v.cells[i].tag, v.cells[i].value);
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
        const QueueOutcome o = queue_on_stop(queue_, v, queue_armed_, current_frame_);
        if (o.event != QueueEvent::none) queue_line(o, &v, queue_apply(cpu, o)); // one row per transition or refusal
        resume_candidate = o.resume_candidate;
        if (o.consume) return 1;
    }
    // Resume memory: the script's own stop of a playing sector track on the alert's rising edge (a combat start), read
    // before the engine pauses it. A record the queue parks or keeps is never a candidate.
    if (!resume_ok_ || !resume_candidate || !alert_rising(alert)) return 0;
    if ((flags & (flag_music | flag_playing)) != (flag_music | flag_playing)) return 0;
    std::uint32_t ms = 0;
    cpu.before_original(); // the COM call sees the engine's MXCSR and LastError; both are restored on return
    const bool read = read_position_ms(load32(record + record_media), ms);
    const ResumeAction r = resume_on_stop(resume_, return_va, id, flags, read, ms, alert);
    if (r != ResumeAction::none) resume_line(r, ResumeMemory{id, read ? ms : 0, read}, id, "stop_movie");
    return 0;
}
// The play routine entry (shared with the trace; every caller: [esp+4] = slot, [esp+8] = task id, [esp+0xc] = id, ESI =
// the calling task from MOV_PlayMovie): parks C on the orphan (returns 1: the stub returns, the task suspends as vanilla
// does without a play), releases the orphan on C's replay, or ends the queue on any other music play (MOV_PlayMovieFrom
// and the helper 0x004f6640 included; the record's flag 0x80 decides); everything else is vanilla.
__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_queue_play(const std::uint32_t* block,
                                                                               std::uint32_t task) {
    if (!queue_armed_ || !queue_.active()) return 0; // the common path: two loads
    x3m::LightCallBoundary cpu;
    QueuePlayView v;
    v.return_va = block[4];
    v.slot = block[5];
    v.task_id = block[6];
    v.id = block[7];
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
// The KC patch of 150::WarpToSector, maintained from Present on the main thread (no task runs there): the VM pointer is
// read every frame (one image load); its CODE buffer, size, build id and the 6-byte window are read on a change of the
// pointer and every kc_check_stride_frames (8) otherwise. The detection is kept and the write made only through the load
// gate (KcPatch::settle): after a story reload began (the session or loader stop-all), only the first check after the
// save_load_complete marker or two consecutive checks 8 frames apart that read the same observation (key, detection,
// window) may keep a detection, and the next such check may write, so a reload is patched again within 16 to 24 frames
// of its completion and never while one may be in progress. The write is a plain 6-byte copy into the game's own heap
// buffer after VirtualQuery says committed and writable; nothing executes bytecode during Present.
void kc_row(KcResult r) {
    const bool detected = kc_.story != nullptr;
    x3m::log("music_queue_kc frame=%llu action=%s vm=0x%08lx code=0x%08lx size=%lu build=%lu object=%s detect=%s method=%s window=0x%05lx bytes=%02x%02x%02x%02x%02x%02x applied=%u refused=%u "
             "load_begins=%u markers=%u",
             current_frame_, kc_result_name(r), static_cast<unsigned long>(kc_.vm), static_cast<unsigned long>(kc_.code),
             static_cast<unsigned long>(kc_.size), static_cast<unsigned long>(kc_.build), x3m::story::object_name(kc_.build),
             detected ? x3m::story::fail_name(kc_.story->outcome.fail) : "pending",
             detected && kc_.story->outcome.where[0] ? kc_.story->outcome.where : "-",
             static_cast<unsigned long>(detected ? kc_.story->warp_fork_offset : 0),
             kc_window_seen_[0], kc_window_seen_[1], kc_window_seen_[2], kc_window_seen_[3], kc_window_seen_[4], kc_window_seen_[5],
             kc_.applied, kc_.refused, kc_.load_begins, kc_.markers);
}
__attribute__((noinline)) void music_queue_kc_present() {
    const std::uint32_t vm = load32(vm_ptr_va); // image load, no engine read: the due test runs before the boundary
    if (!kc_.due(vm)) return;                    // (7 of 8 Presents leave here; perf review F4)
    x3m::LightCallBoundary cpu;
    std::uint32_t code = 0, size = 0, build = 0;
    const bool vm_ok = vm != 0;
    const bool fields_ok = vm_ok && x3m::engine_memory::read(vm + vm_code, &code, 4) &&
                           x3m::engine_memory::read(vm + vm_code_size, &size, 4) && x3m::engine_memory::read(vm + vm_build, &build, 4);
    kc_.code = fields_ok ? code : 0;
    kc_.size = fields_ok ? size : 0;
    kc_.build = fields_ok ? build : 0;
    const x3m::story::VmKey key{vm, kc_.code, kc_.size, kc_.build};
    // The detection kept for this object (its window is read and compared). While none is kept, every due check detects
    // anew (kc_candidate_): the gate's observation then carries the candidate's outcome, its window offset and the window
    // bytes read live there, so a detection is kept only when two consecutive checks 8 frames apart agree on all of them
    // and on the key (or at the first check after the save_load_complete marker).
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
        // No detection kept for this object yet: keep the candidate once the gate is open over it (a transient failure is
        // retried at the next due check, at most detect_retry_limit times per key, then kept as retry_exhausted). The
        // write waits for the next check, which observes the kept window.
        if (kc_retries_.keep(key, kc_candidate_.outcome, settled)) {
            kc_sites_ = kc_candidate_;
            kc_sites_.detected = true;
            kc_.story = &kc_sites_;
            r = kc_sites_.outcome.ok() ? KcResult::waiting : KcResult::no_sites;
        } else
            r = KcResult::waiting;
    } else if (kc_.decide(vm_ok, fields_ok, kc_window_seen_, window_ok, r) && !settled)
        r = KcResult::waiting; // a story load may still be in progress: no write at this check
    else if (r == KcResult::applied) {
        MEMORY_BASIC_INFORMATION info{};
        const bool writable =
            VirtualQuery(reinterpret_cast<const void*>(std::uintptr_t(at)), &info, sizeof info) == sizeof info &&
            info.State == MEM_COMMIT &&
            (info.Protect == PAGE_READWRITE || info.Protect == PAGE_EXECUTE_READWRITE || info.Protect == PAGE_WRITECOPY ||
             info.Protect == PAGE_EXECUTE_WRITECOPY) &&
            std::uintptr_t(info.BaseAddress) + info.RegionSize >= std::uintptr_t(at) + warp_window_length;
        if (writable)
            std::memcpy(reinterpret_cast<void*>(std::uintptr_t(at)), kc_.story->warp_patch_memory, warp_window_length);
        else
            r = KcResult::unwritable;
    }
    if (kc_.note(r)) kc_row(r);
}
// The private key an alt-tab under paused ended the queue with (queue_on_stop_all): freed here, outside the manager's
// walk, with the same delete-by-id the T2/T3 paths use (a miss when the engine freed the record already).
__attribute__((noinline)) void music_queue_free_present() {
    x3m::LightCallBoundary cpu;
    const std::uint32_t key = queue_free_pending_;
    queue_free_pending_ = 0;
    cpu.before_original();
    const std::uint32_t freed = x3m_music_queue_call_free(key);
    write_queue_line("music_queue frame=%llu seq=%lu qpc=%llu event=free_later key=0x%08lx freed=%lu\n", current_frame_, ++seq_, qpc(),
                     static_cast<unsigned long>(key), static_cast<unsigned long>(freed));
}
// The committed alert level [R+4] while the queue is active: a nonzero level ends it (the fallback: C woken, P freed).
__attribute__((noinline)) void music_queue_alert_present() {
    x3m::LightCallBoundary cpu;
    std::uint32_t alert = 0, levels[2]{};
    if (!x3m::engine_memory::read(alert_record_ptr_va, &alert, 4) || !alert || !x3m::engine_memory::read(alert, levels, sizeof levels))
        return;
    const QueueOutcome o = queue_on_alert(queue_, levels[1], orphan_view(), current_frame_);
    if (o.event != QueueEvent::none) queue_line(o, nullptr, queue_apply(cpu, o));
}
// The marked Run (the boundary lives here so the per-play fast path below carries no unwind registration).
__attribute__((noinline)) unsigned music_keep_run_marked(std::uint32_t record) {
    x3m::LightCallBoundary cpu;
    const std::uint32_t media = load32(record + record_media);
    const std::uint32_t state = media ? load32(media + media_buffer_state) : 0;
    const bool skip = pump_.skip_run(record, media, state, qpc());
    pump_line(skip ? "skipped" : "vanilla", state);
    return skip ? 1u : 0u;
}
// A sampled gzwrite (one in pump_poll_stride while pumping): the cadence, the record's checks, the pump call, and the
// fallback pause after an early stop.
__attribute__((noinline)) void music_keep_gz_write_sampled() {
    x3m::LightCallBoundary cpu;
    const std::uint64_t now = qpc();
    if (!pump_.pump_due(now, GetCurrentThreadId())) { // the wall bound cancels the Run skip in here
        run_fallback(cpu);
        sync_pumping();
        return;
    }
    LiveRecord r{};
    bool ok = live_record(pump_.record, pump_.id, pump_.media, r) &&
              (r.flags & (flag_music | flag_directsound | flag_playing)) == (flag_music | flag_directsound);
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
    cpu.before_original(); // the engine's MXCSR and LastError for the pump; restored on return
    const std::uint32_t result = x3m_music_keep_call_pump(pump_.media);
    pump_.note_call(result, now, qpc());
    run_fallback(cpu); // after a 0/2/other return
    sync_pumping();
}
// After gzclose the replay is overdue (checked at Present on the armed thread): pause the record.
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
// Patch E (EAX = record at the Run call 0x00498d71): 1 = answer 1 without the Run, 0 = forward. Per music play; the
// common path is two loads and no call, no unwind registration.
__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_keep_run(std::uint32_t record) {
    if (!keep_armed_ || !pump_.run_marker) return 0;
    return music_keep_run_marked(record);
}
// The save-write observer: every gzwrite while the pump may run. One decrement per call; every pump_poll_stride calls
// the clock, and at most every pump_interval_ms (on the armed thread, after the record's checks) one pump call before
// the real gzwrite runs.
__attribute__((force_align_arg_pointer)) void __cdecl x3m_music_keep_gz_write() {
    if (!keep_armed_ || !pump_.pumping || !pump_.sample_due()) return;
    music_keep_gz_write_sampled();
}
// gzclose while the pump may run: only the save's own close (the hook's return into the close wrapper 0x004e9384 and
// the wrapper's return 0x00404a74) ends pumping; the episode then waits for the replay. Any other gzclose is ignored.
__attribute__((force_align_arg_pointer)) void __cdecl x3m_music_keep_gz_close(std::uint32_t return_va,
                                                                              std::uint32_t caller_return_va) {
    if (!keep_armed_ || !pump_.pumping || return_va != gzclose_wrapper_return_va ||
        caller_return_va != save_gzclose_return_va)
        return;
    x3m::LightCallBoundary cpu;
    gzclose_caller_ = caller_return_va;
    pump_.stop(PumpEnd::gzclose, qpc());
    sync_pumping();
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_music_trace_stop(const std::uint32_t* block) {
    x3m::LightCallBoundary cpu;
    if (!trace_armed_) return;
    const std::uint32_t return_va = block[4];
    write_line("music_trace_stop frame=%llu seq=%lu qpc=%llu caller=0x%08lx name=%s\n", current_frame_, ++seq_, qpc(),
               static_cast<unsigned long>(return_va), stop_caller_name(return_va));
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_music_trace_play(const std::uint32_t* block,
                                                                           std::uint32_t task) {
    x3m::LightCallBoundary cpu;
    if (!trace_armed_) return;
    const std::uint32_t return_va = block[4], id = block[4 + play_arg_id];
    const std::int32_t minutes = std::int32_t(block[4 + play_arg_minutes]),
                       seconds = std::int32_t(block[4 + play_arg_seconds]), ms = std::int32_t(block[4 + play_arg_ms]);
    const std::int32_t start_ms = std::int32_t((std::uint32_t(minutes) * 60u + std::uint32_t(seconds)) * 1000u +
                                               std::uint32_t(ms)); // the engine's own wrap-around arithmetic
    const std::uint32_t record = find_record(id);
    TaskView t{};
    const bool task_ok = read_task(task, t);
    write_line(
        "music_trace_play frame=%llu seq=%lu qpc=%llu id=%lu start_ms=%ld caller=0x%08lx name=%s record=0x%08lx flags=0x%lx ctx=0x%lx slot=%lu "
        "task=0x%08lx task_ok=%u task_id=%lu ip=0x%lx a1_slot=%lu a2_task=%lu\n",
        current_frame_, ++seq_, qpc(), static_cast<unsigned long>(id), static_cast<long>(start_ms),
        static_cast<unsigned long>(return_va), play_caller_name(return_va), static_cast<unsigned long>(record),
        static_cast<unsigned long>(record ? load32(record + record_flags) : 0),
        static_cast<unsigned long>(record ? load32(record + record_context) : 0),
        static_cast<unsigned long>(record ? load32(record + record_slot) : 0), static_cast<unsigned long>(task),
        task_ok ? 1u : 0u, static_cast<unsigned long>(t.id), static_cast<unsigned long>(t.ip),
        static_cast<unsigned long>(block[4 + 1]), static_cast<unsigned long>(block[4 + 2]));
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_music_trace_stop_movie(const std::uint32_t* block,
                                                                                 std::uint32_t task) {
    x3m::LightCallBoundary cpu;
    if (!trace_armed_) return;
    const std::uint32_t id = block[2], return_va = block[4];
    const std::uint32_t record = find_record(id);
    TaskView t{};
    const bool task_ok = read_task(task, t);
    unsigned tags[4]{};
    std::uint32_t values[4]{};
    bool cells_ok = task_ok;
    for (unsigned i = 0; i < 4 && cells_ok; ++i) cells_ok = read_cell(t, trace_cells[i], tags[i], values[i]);
    std::uint32_t alert = 0, levels[3]{};
    const bool alert_ok = x3m::engine_memory::read(alert_record_ptr_va, &alert, 4) && alert &&
                          x3m::engine_memory::read(alert, levels, sizeof levels);
    write_line(
        "music_trace_stop_movie frame=%llu seq=%lu qpc=%llu id=%lu caller=0x%08lx name=%s record=0x%08lx flags=0x%lx ctx=0x%lx slot=%lu "
        "task=0x%08lx task_ok=%u task_id=%lu arg_task=%lu ip=0x%lx k=%lu capacity=%lu cells=%s c6=%u:0x%08lx c8=%u:0x%08lx "
        "c10=%u:0x%08lx c12=%u:0x%08lx alert=%u alert_old=%lu alert_new=%lu\n",
        current_frame_, ++seq_, qpc(), static_cast<unsigned long>(id), static_cast<unsigned long>(return_va),
        stop_movie_caller_name(return_va), static_cast<unsigned long>(record),
        static_cast<unsigned long>(record ? load32(record + record_flags) : 0),
        static_cast<unsigned long>(record ? load32(record + record_context) : 0),
        static_cast<unsigned long>(record ? load32(record + record_slot) : 0), static_cast<unsigned long>(task),
        task_ok ? 1u : 0u, static_cast<unsigned long>(t.id), static_cast<unsigned long>(block[5]),
        static_cast<unsigned long>(t.ip), static_cast<unsigned long>(t.index), static_cast<unsigned long>(t.capacity),
        cells_ok ? "ok" : "invalid", tags[0], static_cast<unsigned long>(values[0]), tags[1],
        static_cast<unsigned long>(values[1]), tags[2], static_cast<unsigned long>(values[2]), tags[3],
        static_cast<unsigned long>(values[3]), alert_ok ? 1u : 0u, static_cast<unsigned long>(levels[1]),
        static_cast<unsigned long>(levels[2]));
}
}

// Integer-only stubs. Every stub saves EFLAGS and EAX/ECX/EDX, calls its
// handler (cdecl, four-byte stack), restores them and leaves with ESP exactly
// as at entry. Patch A: entered by the claim's dispatcher at 0x004982db with
// ESI = record, EBX = 0, EBP = next node and the stop-all's return address at
// [esp+0x10]; keep_running leaves for 0x00498322 with EDI loaded as the
// displaced `mov edi,[esi+0x24]` would have (the flags are dead: the
// continuation rewrites them before any reader); skip_all jumps to
// 0x00498359, the loop's continue, without the EDI load (EDI is dead there:
// the next site reloads it, the epilogue pops it; EBP already holds the next
// node, EBX 0; nothing of the record is touched); every other mode replays the
// two displaced instructions through the tail so the `je 0x4982f6` consumes
// the tail's CMP. Patch C: entered by `call` from 0x00498d54 with EAX =
// record and [esp+4] = start ms; skip returns 1 with the stack untouched
// (the caller pops the argument), the vanilla paths tail-jump to 0x004d0430
// with the same stack; the pause of a held record calls 0x004d1810 (EAX =
// record; it clobbers EAX/ECX/EDX only, all saved here). Keep entry stub
// (stop-all entry, shared site): loops on the handler, pausing each orphaned
// keep-running record it returns until 0. Keep stop-movie stub and the trace
// stubs: the saved block's address is the handler's one argument. Patch D
// (status thunk): entered by `call` from 0x004983d9 with ECX = record, EAX =
// [rec+0x28] and no stack argument; "playing" returns EAX = 1 (the caller
// reads AX only) with ESP as at entry, every other answer tail-jumps to
// 0x004d14e0 with EAX/ECX/EDX/EFLAGS restored.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_music_keep_a_stub
_x3m_music_keep_a_stub:
    pushfd
    push eax
    push ecx
    push edx
    push dword ptr [esp+0x20]
    push esi
    call _x3m_music_keep_stop_all
    add esp, 8
    cmp eax, 1
    je 1f
    cmp eax, 3
    je 3f
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_stop_all_continue]
1:  pop edx
    pop ecx
    pop eax
    popfd
    mov edi, dword ptr [esi+0x24]
    jmp dword ptr [_x3m_music_stop_all_skip]
3:  pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_stop_all_next]

    .p2align 4
    .globl _x3m_music_keep_c_thunk
_x3m_music_keep_c_thunk:
    pushfd
    push eax
    push ecx
    push edx
    push dword ptr [esp+0x14]
    push eax
    call _x3m_music_keep_seek
    add esp, 8
    cmp eax, 1
    je 1f
    test eax, 4
    jz 4f
    mov ecx, dword ptr [_x3m_music_keep_resume_ms]
    mov dword ptr [esp+0x14], ecx
4:  test eax, 2
    jz 2f
    mov eax, dword ptr [_x3m_music_keep_pause_record]
    call dword ptr [_x3m_music_pause_fn]
2:  pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_seek_original]
1:  pop edx
    pop ecx
    pop eax
    popfd
    mov eax, 1
    ret

    .p2align 4
    .globl _x3m_music_keep_status_thunk
_x3m_music_keep_status_thunk:
    pushfd
    push eax
    push ecx
    push edx
    push ecx
    call _x3m_music_keep_status
    add esp, 4
    cmp eax, 1
    je 1f
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_status_original]
1:  pop edx
    pop ecx
    pop eax
    popfd
    mov eax, 1
    ret

    .p2align 4
    .globl _x3m_music_keep_run_thunk
_x3m_music_keep_run_thunk:
    pushfd
    push eax
    push ecx
    push edx
    push eax
    call _x3m_music_keep_run
    add esp, 4
    cmp eax, 1
    je 1f
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_run_original]
1:  pop edx
    pop ecx
    pop eax
    popfd
    mov eax, 1
    ret

    .p2align 4
    .globl _x3m_music_keep_entry_stub
_x3m_music_keep_entry_stub:
    pushfd
    push eax
    push ecx
    push edx
1:  push esp
    call _x3m_music_keep_stop_all_entry
    add esp, 4
    test eax, eax
    jz 2f
    call dword ptr [_x3m_music_pause_fn]
    jmp 1b
2:  pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_keep_entry_continue]

    .p2align 4
    .globl _x3m_music_keep_stop_movie_stub
_x3m_music_keep_stop_movie_stub:
    pushfd
    push eax
    push ecx
    push edx
    mov eax, esp
    push edi
    push esi
    push eax
    call _x3m_music_keep_stop_movie
    add esp, 12
    test eax, eax
    jnz 1f
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_keep_stop_movie_continue]
1:  pop edx
    pop ecx
    pop eax
    popfd
    ret

    .p2align 4
    .globl _x3m_music_trace_stop_stub
_x3m_music_trace_stop_stub:
    pushfd
    push eax
    push ecx
    push edx
    push esp
    call _x3m_music_trace_stop
    add esp, 4
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_trace_stop_continue]

    .p2align 4
    .globl _x3m_music_trace_play_stub
_x3m_music_trace_play_stub:
    pushfd
    push eax
    push ecx
    push edx
    mov eax, esp
    push esi
    push eax
    call _x3m_music_trace_play
    add esp, 8
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_trace_play_continue]

    .p2align 4
    .globl _x3m_music_trace_stop_movie_stub
_x3m_music_trace_stop_movie_stub:
    pushfd
    push eax
    push ecx
    push edx
    mov eax, esp
    push esi
    push eax
    call _x3m_music_trace_stop_movie
    add esp, 8
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_trace_stop_movie_continue]

    .p2align 4
    .globl _x3m_music_queue_play_stub
_x3m_music_queue_play_stub:
    pushfd
    push eax
    push ecx
    push edx
    mov eax, esp
    push esi
    push eax
    call _x3m_music_queue_play
    add esp, 8
    test eax, eax
    jnz 1f
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_queue_play_continue]
1:  pop edx
    pop ecx
    pop eax
    popfd
    ret
    .att_syntax
)");
// The pump call (not an engine entry stub): cdecl from the gzwrite observer, EAX = media, the engine pump 0x004d0700
// (no stack argument, plain `ret`, preserves EBX/EBP/ESI/EDI, clobbers EAX/ECX/EDX/EFLAGS); EBX/ESI/EDI/EBP are saved
// here as well, and only AX of the answer is defined (`mov ax,2` on the drain exit). The pause call: the same shape
// around 0x004d1810 (EAX = record; clobbers EAX/ECX/EDX only), the save pump's fallback.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_music_keep_call_pump
_x3m_music_keep_call_pump:
    push ebx
    push esi
    push edi
    push ebp
    mov eax, dword ptr [esp+0x14]
    call dword ptr [_x3m_music_pump_fn]
    movzx eax, ax
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

    .p2align 4
    .globl _x3m_music_keep_call_pause
_x3m_music_keep_call_pause:
    push ebx
    push esi
    push edi
    push ebp
    mov eax, dword ptr [esp+0x14]
    call dword ptr [_x3m_music_pause_fn]
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

    .p2align 4
    .globl _x3m_music_queue_call_completion
_x3m_music_queue_call_completion:
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
    .globl _x3m_music_queue_call_free
_x3m_music_queue_call_free:
    push ebx
    push esi
    push edi
    push ebp
    mov ecx, dword ptr [esp+0x14]
    call dword ptr [_x3m_music_free_fn]
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
    .att_syntax
)");
extern "C" void x3m_music_keep_a_stub();
extern "C" void x3m_music_queue_play_stub();
extern "C" void x3m_music_keep_c_thunk();
extern "C" void x3m_music_keep_status_thunk();
extern "C" void x3m_music_keep_run_thunk();
extern "C" void x3m_music_keep_entry_stub();
extern "C" void x3m_music_keep_stop_movie_stub();
extern "C" void x3m_music_trace_stop_stub();
extern "C" void x3m_music_trace_play_stub();
extern "C" void x3m_music_trace_stop_movie_stub();

namespace {
template <class T> std::uint32_t address_of(T* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
// Claims a site the feature owns alone, stores the current chain head (the tail) into `continuation` and chains the
// stub in front.
bool hook_site(engine_patch::Site& site, const engine_patch::SiteSpec& spec, void (*stub)(),
               volatile std::uint32_t& continuation, const char** reason) {
    site = engine_patch::Site{};
    if (!engine_patch::claim(site, spec)) {
        *reason = site.status;
        return false;
    }
    continuation = address_of(*site.entry);
    if (!engine_patch::push_front(site, reinterpret_cast<void*>(stub))) {
        *reason = "chain_failed";
        return false;
    }
    return true;
}
// Restores a site the feature owns alone; false only when a live patch could not be put back.
bool unhook_site(engine_patch::Site& site) {
    return engine_patch::restore(site);
}
// Shared site protocol (core::acquire_shared / release_shared, host-tested in both install orders) over
// engine_patch. push: the stub's continuation is stored before the stub becomes the head (store_pointer is
// the only write; a failed push leaves the chain untouched).
struct PatchOps {
    bool claim(engine_patch::Site& s, const engine_patch::SiteSpec& spec) { return engine_patch::claim(s, spec); }
    const char* status(const engine_patch::Site& s) { return s.status; }
    bool push(engine_patch::Site& s, void (*stub)(), volatile std::uint32_t& continuation) {
        continuation = address_of(*s.entry);
        return engine_patch::push_front(s, reinterpret_cast<void*>(stub)) != nullptr;
    }
    bool restore(engine_patch::Site& s) { return engine_patch::restore(s); }
    bool live(const engine_patch::Site& s) { return s.patched_in; }
};
bool acquire_shared(SharedSite& s, bool& acquired, const engine_patch::SiteSpec& spec, void (*stub)(),
                    volatile std::uint32_t& continuation, const char** reason) {
    PatchOps ops;
    acquired = core::acquire_shared(ops, s, spec, stub, continuation, reason);
    return acquired;
}
bool release_shared(SharedSite& s) {
    PatchOps ops;
    return core::release_shared(ops, s);
}
// Releases a shared site for one feature: its own use when it acquired one; otherwise only an orphaned live claim
// (no users, left live by a failed rollback), whatever install stage was reached.
bool release_for(SharedSite& s, bool& acquired) {
    if (acquired) {
        acquired = false;
        return release_shared(s);
    }
    return s.users ? true : release_shared(s);
}
bool status_windows_match() {
    return bytes_match(d_caller_va, d_caller_window, d_caller_length) &&
           bytes_match(d_target_va, status_head, status_head_length) &&
           bytes_match(status_ended_va, status_ended_window, status_ended_length) &&
           call_targets(d_site_va, d_target_va);
}
// Patch E and the save pump: the pump's head, its drain exit and the status query's DirectSound tail (the pump's
// contract), the Run call's continuation and the gzwrite thunk on the IAT slot.
bool save_windows_match() {
    return bytes_match(pump_va, pump_head, pump_head_length) && bytes_match(pump_end_va, pump_end, pump_end_length) &&
           bytes_match(status_ds_tail_va, status_ds_tail, status_ds_tail_length) &&
           bytes_match(e_return_va, e_post_window, e_post_length) &&
           bytes_match(gzwrite_thunk_va, gzwrite_thunk, gzwrite_thunk_length) && call_targets(e_site_va, e_target_va) &&
           bytes_match(gzclose_wrapper_va, gzclose_wrapper, gzclose_wrapper_length) &&
           bytes_match(gzclose_thunk_va, gzclose_thunk, gzclose_thunk_length) &&
           call_targets(save_gzclose_call_va, gzclose_wrapper_va) &&
           call_targets(gzclose_wrapper_return_va - call_length, gzclose_thunk_va);
}
// The track queue: the delete-by-id body (its only caller MOV_FreeMovie's 0x004986f0), the MOV_StopMovie body's
// completion delivery (the slot table layout the queue repeats), the KC layout witnesses (jump handler, loader stores)
// and the play entry's head.
bool queue_windows_match() {
    return bytes_match(free_by_id_va, free_by_id_body, free_by_id_length) && call_targets(free_by_id_caller_va, free_by_id_va) &&
           bytes_match(completion_window_va, completion_window, completion_window_length) &&
           bytes_match(kc_jump_handler_va, kc_jump_handler, kc_jump_handler_length) &&
           bytes_match(kc_loader_code_store_va, kc_loader_code_store, kc_loader_code_store_length) &&
           bytes_match(kc_loader_build_store_va, kc_loader_build_store, kc_loader_build_store_length) &&
           bytes_match(t_play_site_va, play_head, play_head_length);
}
bool keep_windows_match() {
    return bytes_match(stop_all_va, stop_all_head, stop_all_head_length) &&
           bytes_match(a_window_va, a_window, a_window_length) &&
           bytes_match(a_skip_va, a_skip_window, a_skip_length) &&
           bytes_match(a_next_record_va, a_next_record_window, a_next_record_length) &&
           bytes_match(c_pre_va, c_pre_window, c_pre_length) && bytes_match(c_post_va, c_post_window, c_post_length) &&
           bytes_match(play_set_playing_va, play_set_playing_window, play_set_playing_length) &&
           bytes_match(c_target_va, seek_head, seek_head_length) &&
           bytes_match(pause_va, pause_body, pause_body_length) && bytes_match(run_va, run_head, run_head_length) &&
           bytes_match(t_stop_movie_site_va, stop_movie_head, stop_movie_head_length) &&
           call_targets(c_site_va, c_target_va) && call_targets(c_run_call_va, run_va) &&
           call_targets(t_stop_movie_site_va + 0x4f, pause_va); // 0x0049885f: MOV_StopMovie's own pause call
}
bool callers_match() {
    for (const auto& c : stop_callers)
        if (!call_targets(c.call_va, stop_all_va) || c.return_va != c.call_va + call_length) return false;
    for (const auto& c : play_callers)
        if (!call_targets(c.call_va, play_va) || c.return_va != c.call_va + call_length) return false;
    for (const auto& c : stop_movie_callers)
        if (!call_targets(c.call_va, t_stop_movie_site_va) || c.return_va != c.call_va + call_length) return false;
    return true;
}
bool trace_windows_match() {
    return bytes_match(t_stop_all_site_va, stop_all_head, stop_all_head_length) &&
           bytes_match(t_play_site_va, play_head, play_head_length) &&
           bytes_match(t_stop_movie_site_va, stop_movie_head, stop_movie_head_length);
}
engine_patch::SiteSpec entry_spec() {
    return spec_for("music_stop_all_entry", t_stop_all_site_va, stop_all_head, t_stop_all_length);
}
engine_patch::SiteSpec stop_movie_spec() {
    return spec_for("music_stop_movie_entry", t_stop_movie_site_va, stop_movie_head, t_stop_movie_length);
}
engine_patch::SiteSpec play_spec() {
    return spec_for("music_play_entry", t_play_site_va, play_head, t_play_length);
}
// loading_trace's bind of the save-write observer: the gzwrite row must sit on the verified IAT slot (the thunk
// `jmp [0x005323e0]` matched), hold zlib1.dll's own gzwrite as its original and be live, the gzclose row too (on slot
// 0x005323f0, the close wrapper's thunk), Patch E
// live and the install window still open. Otherwise the save keeps the paused mode.
void save_pump_bound(void* const* write_slot, void* write_original, bool write_live, bool close_live,
                     void* const* close_slot) {
    save_pump_bind_called_ = true;
    write_row_live_ = write_live;
    close_row_live_ = close_live;
    const HMODULE zlib = GetModuleHandleW(L"zlib1.dll");
    const FARPROC exported = zlib ? GetProcAddress(zlib, "gzwrite") : nullptr;
    if (!keep_armed_ || !e_live_)
        save_pump_state_ = "keep_not_armed";
    else if (!engine_patch::install_window_open())
        save_pump_state_ = "late_claim";
    else if (!write_live || !close_live)
        save_pump_state_ = "rows_not_live";
    else if (std::uintptr_t(write_slot) != gzwrite_iat_va || std::uintptr_t(close_slot) != gzclose_iat_va)
        save_pump_state_ = "iat_slot_mismatch";
    else if (!bytes_match(gzwrite_thunk_va, gzwrite_thunk, gzwrite_thunk_length))
        save_pump_state_ = "thunk_mismatch";
    else if (!exported || reinterpret_cast<void*>(exported) != write_original)
        save_pump_state_ = "original_mismatch";
    else
        save_pump_state_ = "ok";
    save_pump_ = !std::strcmp(save_pump_state_, "ok");
    sync_pumping();
}
// A, then C, then D (optional: a refusal only disables skip_all), then E (optional: a refusal keeps the save
// paused), then the two
// shared entries; a later failure puts the earlier ones back. A live site that
// cannot be restored keeps the module registered (patched_) so shutdown()
// tries again, and armed_ stays false.
bool install_keep(const char** reason) {
    if (!engine_patch::install_window_open()) {
        *reason = "late_claim";
        return false;
    }
    if (!keep_windows_match()) {
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
    x3m_music_keep_pause_record = 0;
    x3m_music_stop_all_skip = a_skip_va;
    x3m_music_stop_all_next = a_next_record_va;
    x3m_music_seek_original = c_target_va;
    x3m_music_pause_fn = pause_va;
    x3m_music_status_original = d_target_va;
    x3m_music_run_original = e_target_va;
    x3m_music_pump_fn = pump_va;
    x3m_music_free_fn = free_by_id_va;
    x3m_music_keep_resume_ms = 0;
    x3m_music_keep_save_pumping = 0;
    pump_.reset();
    resume_ = ResumeMemory{};
    queue_ = TrackQueue{};
    kc_ = KcPatch{};
    kc_sites_ = x3m::story::QueueSites{};
    kc_candidate_ = x3m::story::QueueSites{};
    kc_retries_ = x3m::story::Retries{};
    combat_track_ = 0;
    format_stories();
    queue_armed_ = false;
    queue_state_ = queue_requested_ ? "disabled" : "not_requested";
    e_live_ = save_pump_ = false;
    e_state_ = "disabled";
    LARGE_INTEGER frequency{};
    frequency_ = QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 ? std::uint64_t(frequency.QuadPart) : 0;
    resume_ok_ = bytes_match(position_reader_va, position_reader, position_reader_length);
    resume_state_ = resume_ok_ ? "ok" : "bytes_mismatch";
    status_gate_ = false;
    status_gate_state_ = "disabled";
    gated_ = 0;
    last_active_ = 2;
    bool ok = hook_site(a_site_, spec_for("music_keep_a", a_site_va, a_window + a_site_offset, a_site_length),
                        &x3m_music_keep_a_stub, x3m_music_stop_all_continue, reason);
    if (ok) {
        c_site_ = engine_patch::CallSite{};
        ok = engine_patch::claim_call(c_site_, c_site_va, c_target_va,
                                      reinterpret_cast<void*>(&x3m_music_keep_c_thunk));
        if (!ok) *reason = c_site_.status;
    }
    if (ok) {
        d_site_ = engine_patch::CallSite{};
        if (!status_windows_match())
            status_gate_state_ = "bytes_mismatch";
        else if (engine_patch::claim_call(d_site_, d_site_va, d_target_va,
                                          reinterpret_cast<void*>(&x3m_music_keep_status_thunk)))
            status_gate_state_ = "ok";
        else
            status_gate_state_ = d_site_.status;
    }
    if (ok) { // Patch E, optional: a refusal keeps the save's paused mode
        e_site_ = engine_patch::CallSite{};
        if (!frequency_)
            e_state_ = "no_clock";
        else if (!save_windows_match())
            e_state_ = "bytes_mismatch";
        else if (engine_patch::claim_call(e_site_, e_site_va, e_target_va,
                                          reinterpret_cast<void*>(&x3m_music_keep_run_thunk)))
            e_state_ = "ok";
        else
            e_state_ = e_site_.status;
    }
    if (ok) {
        ok = acquire_shared(entry_site_, keep_entry_acquired_, entry_spec(), &x3m_music_keep_entry_stub,
                            x3m_music_keep_entry_continue, reason);
    }
    if (ok) {
        ok = acquire_shared(stop_movie_site_, keep_stop_movie_acquired_, stop_movie_spec(),
                            &x3m_music_keep_stop_movie_stub, x3m_music_keep_stop_movie_continue, reason);
    }
    if (ok && queue_requested_) { // the track queue, optional: a refusal leaves the keep without it
        const char* why = "ok";
        if (!queue_windows_match())
            queue_state_ = "bytes_mismatch";
        else if (!acquire_shared(play_site_, keep_play_acquired_, play_spec(), &x3m_music_queue_play_stub,
                                 x3m_music_queue_play_continue, &why))
            queue_state_ = why;
        else
            queue_state_ = "ok";
    }
    if (!ok) {
        // Reverse order; restore_call/restore are no-ops on a site that is not live, so a claim that failed
        // after going live (rollback_failed) is retried here too. Shared sites: this feature's own use, or an
        // orphaned live claim, whatever stage was reached.
        bool back = true;
        back = release_for(play_site_, keep_play_acquired_) && back;
        back = release_for(stop_movie_site_, keep_stop_movie_acquired_) && back;
        back = release_for(entry_site_, keep_entry_acquired_) && back;
        back = engine_patch::restore_call(e_site_) && back;
        back = engine_patch::restore_call(d_site_) && back;
        back = engine_patch::restore_call(c_site_) && back;
        back = unhook_site(a_site_) && back;
        if (!back) {
            keep_patched_ = true;
            *reason = "rollback_failed";
        }
        return false;
    }
    keep_patched_ = true;
    keep_armed_ = true;
    status_gate_ = d_site_.patched_in && !std::strcmp(status_gate_state_, "ok"); // skip_all only with a complete gate
                                                                                 // claim (a rolled-back or unrestorable
                                                                                 // claim forwards)
    e_live_ = e_site_.patched_in && !std::strcmp(e_state_, "ok");
    queue_armed_ = keep_play_acquired_ && !std::strcmp(queue_state_, "ok"); // only with the play entry chained
    if (e_live_) { // the save pump binds once loading_trace has patched the gzwrite / gzclose rows
        x3m::loading_trace::SaveWriteObserver observer;
        observer.active = &x3m_music_keep_save_pumping;
        observer.write = &x3m_music_keep_gz_write;
        observer.close = &x3m_music_keep_gz_close;
        observer.bound = &save_pump_bound;
        x3m::loading_trace::set_save_write_observer(observer);
    }
    *reason = "ok";
    return true;
}
// Play entry (own), then the shared stop-movie and stop-all entries.
bool install_trace(const char** reason) {
    if (!engine_patch::install_window_open()) {
        *reason = "late_claim";
        return false;
    }
    if (!trace_windows_match()) {
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
    bool ok = acquire_shared(play_site_, trace_play_acquired_, play_spec(), &x3m_music_trace_play_stub,
                             x3m_music_trace_play_continue, reason);
    if (ok) {
        ok = acquire_shared(stop_movie_site_, trace_stop_movie_acquired_, stop_movie_spec(),
                            &x3m_music_trace_stop_movie_stub, x3m_music_trace_stop_movie_continue, reason);
    }
    if (ok) {
        ok = acquire_shared(entry_site_, trace_entry_acquired_, entry_spec(), &x3m_music_trace_stop_stub,
                            x3m_music_trace_stop_continue, reason);
    }
    if (!ok) {
        bool back = true; // this feature's own uses (the keep's users stay counted) or an orphaned live claim
        back = release_for(entry_site_, trace_entry_acquired_) && back;
        back = release_for(stop_movie_site_, trace_stop_movie_acquired_) && back;
        back = release_for(play_site_, trace_play_acquired_) && back;
        if (!back) {
            trace_patched_ = true;
            *reason = "rollback_failed";
        }
        return false;
    }
    trace_patched_ = true;
    trace_armed_ = true;
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
    if (keep_patched_ || trace_patched_) {
        SetLastError(error);
        return true;
    }
    bool keep_present = false, trace_present = false, queue_present = false;
    const bool keep_requested = requested(L"X3M_MUSIC_KEEP", &keep_present),
               trace_requested = requested(L"X3M_MUSIC_TRACE", &trace_present) ||
                                 log_tier::debug(); // X3M_MUSIC_TRACE=1
                                                    // or X3M_DEBUG=1
    trace_present = trace_present || trace_requested;
    queue_variable_ = requested(L"X3M_MUSIC_QUEUE", &queue_present);
    queue_requested_ = queue_variable_ && keep_requested; // a no-op without the keep
    queue_state_ = queue_present && !keep_requested ? "keep_off" : "disabled";
    if (!keep_present && !trace_present && !queue_present) {
        keep_state_ = trace_state_ = "disabled";
        SetLastError(error);
        return false;
    }
    lines_ = seq_ = suppressed_ = walks_cut_ = 0;
    queue_lines_ = queue_suppressed_ = 0;
    queue_cap_noted_ = false;
    queue_free_pending_ = 0;
    cap_noted_ = false;
    keep_present_ = keep_present;
    const bool image_ok = object_trace::executable_verified();
    bool keep_applied = false, trace_applied = false;
    if (!keep_requested)
        keep_state_ = "disabled";
    else if (!image_ok)
        keep_state_ = "executable_mismatch";
    else
        keep_applied = install_keep(&keep_state_);
    if (!trace_requested)
        trace_state_ = "disabled";
    else if (!image_ok)
        trace_state_ = "executable_mismatch";
    else
        trace_applied = install_trace(&trace_state_);
    if (keep_present)
        log("music_keep requested=%u patched=%u armed=%u reason=%s site_a=0x%08lx site_c=0x%08lx site_entry=0x%08lx site_stop_movie=0x%08lx write_a=%s write_c=%s write_entry=%s write_stop_movie=%s "
            "status_gate=%s site_d=0x%08lx write_d=%s thunk_d=0x%08lx alt_tab_mode=%s "
            "patch_e=%s site_e=0x%08lx write_e=%s thunk_e=0x%08lx save_pump=%s resume=%s "
            "stub_a=0x%08lx thunk_c=0x%08lx stub_entry=0x%08lx stub_stop_movie=0x%08lx skip=0x%08lx next_record=0x%08lx seek=0x%08lx pause=0x%08lx holds=%u walk_limit=%u",
            keep_requested ? 1u : 0u, keep_patched_ ? 1u : 0u, keep_armed_ ? 1u : 0u, keep_state_,
            static_cast<unsigned long>(a_site_va), static_cast<unsigned long>(c_site_va),
            static_cast<unsigned long>(t_stop_all_site_va), static_cast<unsigned long>(t_stop_movie_site_va),
            write_kind(a_site_.patched_in, a_site_.atomic_write), write_kind(c_site_.patched_in, c_site_.atomic_write),
            write_kind(entry_site_.site.patched_in, entry_site_.site.atomic_write),
            write_kind(stop_movie_site_.site.patched_in, stop_movie_site_.site.atomic_write), status_gate_state_,
            static_cast<unsigned long>(d_site_va), write_kind(d_site_.patched_in, d_site_.atomic_write),
            static_cast<unsigned long>(address_of(&x3m_music_keep_status_thunk)),
            mode_name(status_gate_ ? StopMode::skip_all : StopMode::paused), e_state_,
            static_cast<unsigned long>(e_site_va), write_kind(e_site_.patched_in, e_site_.atomic_write),
            static_cast<unsigned long>(address_of(&x3m_music_keep_run_thunk)), e_live_ ? "pending" : e_state_,
            resume_state_, static_cast<unsigned long>(address_of(&x3m_music_keep_a_stub)),
            static_cast<unsigned long>(address_of(&x3m_music_keep_c_thunk)),
            static_cast<unsigned long>(address_of(&x3m_music_keep_entry_stub)),
            static_cast<unsigned long>(address_of(&x3m_music_keep_stop_movie_stub)),
            static_cast<unsigned long>(a_skip_va), static_cast<unsigned long>(a_next_record_va),
            static_cast<unsigned long>(c_target_va), static_cast<unsigned long>(pause_va), hold_capacity,
            list_walk_limit);
    if (trace_present)
        log("music_trace requested=%u patched=%u armed=%u reason=%s site_stop_all=0x%08lx site_play=0x%08lx site_stop_movie=0x%08lx write_stop_all=%s write_play=%s write_stop_movie=%s "
            "stub_stop_all=0x%08lx stub_play=0x%08lx stub_stop_movie=0x%08lx cap=%u",
            trace_requested ? 1u : 0u, trace_patched_ ? 1u : 0u, trace_armed_ ? 1u : 0u, trace_state_,
            static_cast<unsigned long>(t_stop_all_site_va), static_cast<unsigned long>(t_play_site_va),
            static_cast<unsigned long>(t_stop_movie_site_va),
            write_kind(entry_site_.site.patched_in, entry_site_.site.atomic_write),
            write_kind(play_site_.site.patched_in, play_site_.site.atomic_write),
            write_kind(stop_movie_site_.site.patched_in, stop_movie_site_.site.atomic_write),
            static_cast<unsigned long>(address_of(&x3m_music_trace_stop_stub)),
            static_cast<unsigned long>(address_of(&x3m_music_trace_play_stub)),
            static_cast<unsigned long>(address_of(&x3m_music_trace_stop_movie_stub)), trace_line_cap);
    if (queue_present)
        log("music_queue requested=%u armed=%u reason=%s site_play=0x%08lx write_play=%s stub_play=0x%08lx free=0x%08lx vm=0x%08lx "
            "detect=runtime expected=%s kc_patch=%s key_base=0x%08lx stack_depth=%u cells=-%u,-%u,-%u,-%u,-%u,-%u",
            queue_variable_ ? 1u : 0u, queue_armed_ ? 1u : 0u, queue_state_,
            static_cast<unsigned long>(t_play_site_va), write_kind(play_site_.site.patched_in, play_site_.site.atomic_write),
            static_cast<unsigned long>(address_of(&x3m_music_queue_play_stub)), static_cast<unsigned long>(free_by_id_va),
            static_cast<unsigned long>(vm_ptr_va), stories_, queue_armed_ ? "pending" : "off",
            static_cast<unsigned long>(queue_key_base), x3m::story::fork_child_stack_depth,
            x3m::story::fork_child_cell_depths[0], x3m::story::fork_child_cell_depths[1], x3m::story::fork_child_cell_depths[2],
            x3m::story::fork_child_cell_depths[3], x3m::story::fork_child_cell_depths[4], x3m::story::fork_child_cell_depths[5]);
    SetLastError(error);
    return keep_applied || trace_applied;
}
bool shutdown() {
    if (!keep_patched_ && !trace_patched_) return true;
    const DWORD error = GetLastError();
    bool ok = true;
    keep_armed_ = trace_armed_ = queue_armed_ = false;
    if (keep_patched_) {
        // The KC bytes go back when the window still holds exactly the queue's patch (the story object is the game's own
        // heap buffer; a reloaded object is vanilla already).
        const x3m::story::QueueSites* s = kc_.applied_story;
        if ((kc_.result == KcResult::applied || kc_.result == KcResult::already) && s && kc_.story == s) {
            unsigned char window[warp_window_length]{};
            const std::uint32_t at = kc_.code + s->warp_fork_offset;
            bool ours = kc_.code && x3m::engine_memory::read(at, window, warp_window_length);
            for (unsigned i = 0; ours && i < warp_window_length; ++i) ours = window[i] == s->warp_patch_memory[i];
            if (ours) std::memcpy(reinterpret_cast<void*>(std::uintptr_t(at)), s->warp_window_memory, warp_window_length);
        }
        const bool back_q = release_for(play_site_, keep_play_acquired_),
                   back_sm = release_for(stop_movie_site_, keep_stop_movie_acquired_),
                   back_e = release_for(entry_site_, keep_entry_acquired_);
        const bool back_pe = engine_patch::restore_call(e_site_), back_d = engine_patch::restore_call(d_site_),
                   back_c = engine_patch::restore_call(c_site_), back_a = unhook_site(a_site_);
        keep_patched_ = false;
        status_gate_ = false;
        save_pump_ = e_live_ = false;
        x3m_music_keep_save_pumping = 0; // the gzwrite / gzclose observer stays registered and inert
        pump_.reset();
        queue_ = TrackQueue{};
        queue_free_pending_ = 0;
        holds_.clear(); // the stubs stay in the DLL (pinned); the claim tails stay in the arena
        keep_state_ = back_q && back_sm && back_e && back_pe && back_d && back_c && back_a ? "restored" : "restore_failed";
        ok = ok && back_q && back_sm && back_e && back_pe && back_d && back_c && back_a;
    }
    if (trace_patched_) {
        const bool back_e = release_for(entry_site_, trace_entry_acquired_),
                   back_sm = release_for(stop_movie_site_, trace_stop_movie_acquired_),
                   back_p = release_for(play_site_, trace_play_acquired_);
        trace_patched_ = false;
        trace_state_ = back_e && back_sm && back_p ? "restored" : "restore_failed";
        ok = ok && back_e && back_sm && back_p;
    }
    SetLastError(error);
    return ok;
}
const char* state() {
    return keep_state_;
}
void save_pump_report() {
    if (!keep_present_ || save_pump_reported_) return;
    save_pump_reported_ = true;
    const DWORD error = GetLastError();
    const char* reason = !keep_armed_                 ? keep_state_
                         : !e_live_                   ? e_state_
                         : !save_pump_bind_called_    ? "iat_unbound"
                                                      : save_pump_state_;
    log("music_keep_save_pump_hook reason=%s save_mode=%s iat=0x%08lx thunk=0x%08lx write_row=%u close_row=%u site_e=0x%08lx write_e=%s pump=0x%08lx "
        "interval_ms=%u stride=%u bound_ms=%u resume=%s combat=alert_edge",
        reason, save_pump_ ? "keep_running" : "paused", static_cast<unsigned long>(gzwrite_iat_va),
        static_cast<unsigned long>(gzwrite_thunk_va), write_row_live_ ? 1u : 0u, close_row_live_ ? 1u : 0u,
        static_cast<unsigned long>(e_site_va), write_kind(e_site_.patched_in, e_site_.atomic_write),
        static_cast<unsigned long>(pump_va), pump_interval_ms, pump_poll_stride, pump_bound_ms, resume_state_);
    SetLastError(error);
}
const char* trace_state() {
    return trace_state_;
}
// The save_load_complete marker (capture.cpp Present path, the event fov::loaded and music_alert::loaded take): opens the
// KC patch's load gate for the next Present's check.
void loaded() {
    if (queue_armed_) kc_.load_complete();
}
// Stores the frame counter; with the keep armed, samples the engine's active flag [0x00608adc] (one load) and writes
// one music_keep_active line on each change, so a flight measures how long the flag stays 0 around an alt-tab.
void present(unsigned long long, unsigned long long frame, bool) {
    if (!keep_patched_ && !trace_patched_) return;
    current_frame_ = frame;
    if (!keep_armed_) return;
    if (queue_armed_) { // the KC patch (one image load, the fields and the window on change or every 8th frame) and,
                        // while a track is queued, the committed alert level
        if (queue_free_pending_) music_queue_free_present();
        music_queue_kc_present();
        if (queue_.active()) music_queue_alert_present();
    }
    if (pump_.kept && !pump_.pumping && !pump_.run_marker) music_keep_replay_check(); // after gzclose, before the replay
    const unsigned active = load32(active_flag_va) != 0 ? 1u : 0u;
    if (active == last_active_) return;
    const DWORD error = GetLastError();
    const std::uint32_t pointer = load32(input_flags_ptr_va);
    write_line("music_keep_active frame=%llu seq=%lu qpc=%llu from=%s to=%u run_in_background=%u gated=%lu\n", frame,
               ++seq_, qpc(),
               last_active_ == 2 ? "unsampled"
               : last_active_    ? "1"
                                 : "0",
               active, pointer && (load32(pointer) & run_in_background_bit) ? 1u : 0u, gated_);
    last_active_ = active;
    SetLastError(error);
}
}
