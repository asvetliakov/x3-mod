// FL edition of x3m::sun_occlusion (src/proxy/sun_occlusion.h; the ap target keeps src/proxy/sun_occlusion.cpp):
// the flare probe override and the lens bracket in X3FL.exe (docs/architecture/farnhams-legacy-sun-occlusion.md;
// sites, ABI and gates: docs/reverse-engineering/fl/sun-occlusion.md, "Verified hook sites and ABI").
//
//   base+0x8af0a  call base+0xa3fd0   the flare probe. Register ABI: ECX = record, EDX = probing view, no stack
//                                     argument, EAX = 1 "hide", bare RET; the caller's `test eax,eax` stores 0 to
//                                     record+0x30 on 1. x3m_fl_sun_probe_thunk keeps ECX/EDX, asks
//                                     x3m_fl_sun_probe_decide(record, view): 0 / 1 return as the probe's answer
//                                     (plain RET), 2 forwards with `popfd; jmp [target]`, ESP at the return
//                                     address and ECX/EDX as on entry.
//   base+0x8bcb7  call base+0x97680   the lens draw (every selected lens body of every record). EDX = lens view,
//                                     ECX a padding word the target pushes unread, no stack argument, bare RET.
//                                     x3m_fl_sun_lens_thunk: begin, the original with ECX/EDX and EFLAGS as on
//                                     entry, end. The call straddles a qword: claim_call writes it plainly, so it
//                                     is claimed only inside the install window (never after the first Present).
//
// The decision is AP's core::decide (eligible, then the probe's three early gates) with the config flags word at
// +0xec (AP +0xfc). The main view is the active cockpit's camera (cockpit +0x58, sector-camera bit), found by the
// AP registry walk over X3FL.exe's registry slot, which sector_background_fl admits by its own anchors.
//
// CPU contract. Both thunks save EFLAGS and clear DF around the C handlers; EBX/ESI/EDI/EBP come back by the C ABI.
// The probe handler is integer only and keeps XMM0-7 (Win32 calls may change the volatile ones) and LastError;
// x87 and MXCSR are not touched. begin / end run once per frame and reach the SSE listener (D3D, float math, the
// formatter): XMM0-7 and the full PreserveCpuState image (x87, MXCSR, LastError) around it.
//
// Install: X3FL.exe identity, all 17 generated anchors at the live base (every relocated operand recomputed as
// base + slot RVA), then claim_call on both sites inside the install window; a failure of the second claim restores
// the first. Built -fno-exceptions -mno-sse -mno-mmx -mfpmath=387: no x87 arithmetic and no compiler XMM use; the
// only x87/XMM instructions are the save/restore envelopes (cpu_state.h and XmmSave).
#include "../../proxy/sun_occlusion.h"
#include "../../proxy/config.h"
#include "../../proxy/cpu_state.h"
#include "../../proxy/engine_memory.h"
#include "../../proxy/engine_patch.h"
#include "executable_identity_fl.h"
#include "sector_background_fl.h"
#include "sector_background_fl_inc.h"
#include "sun_occlusion_fl_inc.h"
#include <windows.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace L = x3m::fl::sun_occlusion_fl;
namespace R = x3m::fl::sector_background_fl;
namespace core = x3m::sun_occlusion::core;
namespace engine_patch = x3m::engine_patch;
using x3m::sun_occlusion::Addresses;
using x3m::sun_occlusion::Counters;

// ---- The generated layout is the AP core's, except the config flags word ----
static_assert(L::record_owner == core::record_view && L::record_accumulator == core::record_accumulator &&
                  L::record_x == core::record_x && L::record_y == core::record_y &&
                  L::record_visible == core::record_visible && L::record_size == core::record_size &&
                  L::record_group == core::record_group,
              "record layout as core");
static_assert(L::view_flags == core::view_flags && L::view_rect_top == core::view_rect_top &&
                  L::view_rect_bottom == core::view_rect_bottom && L::view_rect_left == core::view_rect_left &&
                  L::view_rect_right == core::view_rect_right && L::view_fov == core::view_fov &&
                  L::view_scale_x == core::view_scale_x && L::view_scale_y == core::view_scale_y,
              "view layout as core");
static_assert(L::view_flag_probe_hidden == core::view_flag_probe_hidden &&
                  L::video_flag_flares == core::video_flag_flares && L::probe_hidden_answer == core::Hidden &&
                  L::screen_offset == 0x8000,
              "the three gates' constants as core::decide");
static_assert(L::config_video_flags == 0xec && L::config_video_flags != core::config_video_flags,
              "FL reads the flags word at +0xec, not AP's +0xfc");
static_assert(L::mode_scale_x == 0x28 && L::view_layer == 0x29c, "scale fallback and layer as AP");
static_assert(L::probe_site_atomic == (L::probe_site_rva % 8 + 5 <= 8) &&
                  L::lens_site_atomic == (L::lens_site_rva % 8 + 5 <= 8) && L::probe_site_atomic &&
                  !L::lens_site_atomic,
              "one lock cmpxchg8b for the probe call, a plain copy for the lens call");
static_assert(R::registry_map == 0 && R::registry_active_handle == 0x10 && R::map_buckets == 0 &&
                  R::map_capacity == 4 && R::link_next == 0 && R::link_key == 4 && R::link_value == 8 &&
                  R::cockpit_camera == 0x58 && R::camera_flags == core::view_flags &&
                  R::camera_fog_flag == core::view_flag_sector_camera,
              "the cockpit registry walk of core::main_view");
constexpr std::uint64_t fnv1a(const std::uint8_t* p, unsigned n) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
static_assert(sizeof L::probe_gates_bytes == L::probe_gates_length &&
                  fnv1a(L::probe_gates_bytes, L::probe_gates_length) == L::probe_gates_fnv1a_masked,
              "the replicated gate span is the measured one");
static_assert(L::probe_site_rva - L::probe_walk_rva + 5 <= sizeof L::probe_walk_bytes &&
                  L::probe_walk_bytes[L::probe_site_rva - L::probe_walk_rva] == 0xe8,
              "the probe call lies inside the probe_walk anchor");
static_assert(L::lens_site_rva - 0x8bc54u + 5 <= sizeof L::lens_block_bytes &&
                  L::lens_block_bytes[L::lens_site_rva - 0x8bc54u] == 0xe8 &&
                  L::lens_block_bytes[L::lens_site_rva - 0x8bc54u - 2] == 0x8b &&
                  L::lens_block_bytes[L::lens_site_rva - 0x8bc54u - 1] == 0xd6,
              "mov edx,esi; call: the lens call lies inside the lens_block anchor");

bool patched_ = false, override_ = false, log_ = false;
const char* state_ = "disabled";
const char* failed_anchor_ = "none";
const char* block_reason_ = "";
engine_patch::CallSite probe_site_{}, lens_site_{};
Addresses addresses_{};
std::uintptr_t install_base_ = 0, registry_slot_ = 0, mode_slot_ = 0;
void (*listener_begin_)() = nullptr;
void (*listener_end_)() = nullptr;
volatile std::uint32_t frame_ = 0;             // written by present() only
volatile unsigned long long device_frame_ = 0; // the capture layer's number of the frame now being built (rows only)
volatile LONG owner_thread_ = 0;               // the first thread through the probe thunk
volatile LONG foreign_thread_ = 0;             // counted by the foreign threads themselves
volatile LONG reset_requested_ = 0;            // device Reset: consumed by the owner at its next probe or bracket
// Owner thread only from here on.
core::Ready ready_;
core::Deferral deferral_;
core::Latch latch_;
Counters counters_{};
std::uint32_t seen_frame_ = 0;
std::uintptr_t main_view_ = 0;
bool multi_counted_ = false, frame_started_ = false, foreign_logged_ = false;

// XMM0-7 saved for the lifetime of the object (as scene_hook_fl.cpp). movups: no alignment assumption.
struct XmmSave {
    alignas(16) unsigned char xmm[128];
    XmmSave() {
        asm volatile("movups %%xmm0,(%0)\n\tmovups %%xmm1,16(%0)\n\tmovups %%xmm2,32(%0)\n\tmovups %%xmm3,48(%0)\n\t"
                     "movups %%xmm4,64(%0)\n\tmovups %%xmm5,80(%0)\n\tmovups %%xmm6,96(%0)\n\tmovups %%xmm7,112(%0)" ::"r"(
                         xmm)
                     : "memory");
    }
    ~XmmSave() {
        asm volatile("movups (%0),%%xmm0\n\tmovups 16(%0),%%xmm1\n\tmovups 32(%0),%%xmm2\n\tmovups 48(%0),%%xmm3\n\t"
                     "movups 64(%0),%%xmm4\n\tmovups 80(%0),%%xmm5\n\tmovups 96(%0),%%xmm6\n\tmovups 112(%0),%%xmm7" ::"r"(
                         xmm)
                     : "memory");
    }
    XmmSave(const XmmSave&) = delete;
    XmmSave& operator=(const XmmSave&) = delete;
};

// ---- diagnostics: one fl.sun_occlusion_sample row per period, at Present ---------------------------------------
long long sample_period = 0, sample_due = 0; // QPC ticks; period 0: off
bool sample_force = false;
unsigned long sample_rows = 0, sample_frames = 0;
Counters sample_last{};
// X3M_FL_SUN_OCCLUSION_LOG (fl_sun_occlusion_log, as fl_scene_log): seconds between rows, 0 off, unset 10, at most
// 3600. Digits only, else the default. Read once at install, never in the hook.
unsigned sample_seconds() {
    wchar_t text[8]{};
    const DWORD length = x3m::config::get(L"X3M_FL_SUN_OCCLUSION_LOG", text, 8);
    if (length == 0 || length >= 8) return 10;
    unsigned value = 0;
    for (DWORD i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return 10;
        value = value * 10 + unsigned(text[i] - L'0');
    }
    return value > 3600 ? 3600 : value;
}
unsigned arm_sample() {
    sample_period = sample_due = 0;
    sample_force = false;
    sample_frames = 0;
    sample_last = counters_; // the first row counts from here
    sample_last.foreign_thread = static_cast<std::uint32_t>(foreign_thread_);
    const unsigned seconds = sample_seconds();
    LARGE_INTEGER frequency{}, now{};
    if (seconds && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 && QueryPerformanceCounter(&now)) {
        sample_period = frequency.QuadPart * static_cast<long long>(seconds);
        sample_due = now.QuadPart + sample_period;
    }
    return seconds;
}

// The 17 anchors at `base`: each relocated disp32 equal to base + its slot RVA, every other byte as generated.
// nullptr when all match, else the first anchor that does not.
const char* gate(std::uintptr_t base) {
    for (unsigned i = 0; i < L::anchor_count; ++i) {
        const L::Anchor& a = L::anchors[i];
        unsigned char actual[160]{};
        if (a.length > sizeof actual || !engine_patch::read_code(base + a.rva, actual, a.length)) return a.name;
        for (unsigned k = 0; k < a.operands; ++k) {
            std::uint32_t value = 0;
            std::memcpy(&value, actual + a.operand_offset[k], 4);
            if (value != std::uint32_t(base + a.operand_rva[k])) return a.name;
            std::memset(actual + a.operand_offset[k], 0, 4);
        }
        if (std::memcmp(actual, a.bytes, a.length)) return a.name;
    }
    return nullptr;
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine calls into it.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
           module != nullptr;
}
bool owner_thread() {
    const LONG thread = static_cast<LONG>(GetCurrentThreadId());
    LONG owner = owner_thread_;
    if (owner == 0) owner = InterlockedCompareExchange(&owner_thread_, thread, 0) == 0 ? thread : owner_thread_;
    return owner == thread;
}
bool read_word(std::uintptr_t base, std::uint32_t offset, std::uint32_t& out) {
    return base && !(base & 3) && x3m::engine_memory::read(base + offset, &out, sizeof out);
}
// core::main_view over X3FL.exe's registry slot (sector_background_fl_inc.h): registry -> +0x10 handle, the map
// {buckets, capacity} at [registry], links {next, key, cockpit}, cockpit +0x58 = camera with the sector-camera bit.
// 0 on any doubt (the override then never answers). Integer only.
std::uintptr_t main_view_walk(std::uintptr_t registry_slot) {
    std::uint32_t registry = 0, handle = 0, header = 0, bucket[2]{}, link = 0, cockpit = 0, camera = 0, flags = 0;
    if (!registry_slot || !x3m::engine_memory::read(registry_slot, &registry, 4) ||
        !read_word(registry, R::registry_active_handle, handle) || !handle)
        return 0;
    if (!read_word(registry, R::registry_map, header) || !header || (header & 3) ||
        !x3m::engine_memory::read(header, bucket, sizeof bucket))
        return 0;
    const std::uint32_t n = bucket[R::map_capacity / 4];
    if (!n || n > 65536 || (n & (n - 1)) || !read_word(bucket[R::map_buckets / 4], 4 * ((n - 1) & handle), link))
        return 0;
    for (unsigned walked = 0; link && walked < 32; ++walked) {
        std::uint32_t row[3]{};
        if ((link & 3) || !x3m::engine_memory::read(link, row, sizeof row)) return 0;
        if (row[R::link_key / 4] == handle) {
            cockpit = row[R::link_value / 4];
            break;
        }
        link = row[R::link_next / 4];
    }
    if (!read_word(cockpit, R::cockpit_camera, camera) || !read_word(camera, R::camera_flags, flags) ||
        !(flags & R::camera_fog_flag))
        return 0;
    return camera;
}
// Once per frame, at the frame's first probe: the state machine advances (twice over a gap) and the main view is
// resolved.
void begin_frame(std::uint32_t frame) {
    if (InterlockedExchange(&reset_requested_, 0) != 0) {
        ready_.reset();
        block_reason_ = "";
    }
    ready_.begin_frame();
    if (frame_started_ && frame - seen_frame_ > 1u) ready_.begin_frame();
    seen_frame_ = frame;
    frame_started_ = true;
    latch_ = core::Latch{};
    multi_counted_ = false;
    main_view_ = addresses_.main_view ? *addresses_.main_view : main_view_walk(registry_slot_);
}
inline std::uint32_t word(const std::uint32_t* base, unsigned offset) {
    return base[offset / 4];
}
int decide_probe(const std::uint32_t* record, const std::uint32_t* view);
}

namespace x3m::sun_occlusion::detail {
volatile bool bracket_open = false;
}

extern "C" {
std::uint32_t x3m_fl_sun_probe_target = 0, x3m_fl_sun_lens_target = 0;
void x3m_fl_sun_probe_thunk();
void x3m_fl_sun_lens_thunk();
// The thread's last error and XMM0-7 come back exactly as the engine left them on every path: the frame's first
// probe resolves the main view through engine_memory (VirtualQuery), the log mode runs the original and the
// formatter.
int __cdecl x3m_fl_sun_probe_decide(const std::uint32_t* record, const std::uint32_t* view) {
    XmmSave xmm;
    const DWORD error = GetLastError();
    const int decision = decide_probe(record, view);
    SetLastError(error);
    return decision;
}
}
namespace {
int decide_probe(const std::uint32_t* record, const std::uint32_t* view) {
    if (!owner_thread()) {
        InterlockedIncrement(&foreign_thread_);
        return core::Original;
    }
    ++counters_.probes;
    if (!record || !view) {
        ++counters_.original;
        return core::Original;
    }
    const std::uint32_t frame = frame_;
    if (!frame_started_ || frame != seen_frame_) begin_frame(frame);
    core::ProbeInputs in;
    in.record_owner = word(record, L::record_owner);
    in.view = reinterpret_cast<std::uintptr_t>(view);
    in.main_view = main_view_;
    in.x = std::int32_t(word(record, L::record_x));
    in.y = std::int32_t(word(record, L::record_y));
    in.view_flags = word(view, L::view_flags);
    in.top = std::int32_t(word(view, L::view_rect_top));
    in.bottom = std::int32_t(word(view, L::view_rect_bottom));
    in.left = std::int32_t(word(view, L::view_rect_left));
    in.right = std::int32_t(word(view, L::view_rect_right));
    // The owner's flags decide eligibility: one validated read, only where the cheap tests already hold.
    std::uint32_t owner_flags = 0;
    std::int32_t owner_layer = -1;
    if (main_view_ != 0 && in.view == main_view_ && in.record_owner != 0 && in.record_owner != in.view &&
        !(in.record_owner & 3)) {
        if (!x3m::engine_memory::read(in.record_owner + L::view_flags, &owner_flags, 4)) owner_flags = 0;
        if (log_ && !x3m::engine_memory::read(in.record_owner + L::view_layer, &owner_layer, 4)) owner_layer = -1;
    }
    in.owner_flags = owner_flags;
    const bool own = core::eligible(in.record_owner, in.view, main_view_, owner_flags);
    if (own) {
        ++counters_.own;
        in.ready = ready_.probe(reinterpret_cast<std::uintptr_t>(record),
                                override_ && deferral_.answers(reinterpret_cast<std::uintptr_t>(record)));
        if (ready_.records == 1) {
            latch_.record = reinterpret_cast<std::uintptr_t>(record);
            latch_.x = in.x;
            latch_.y = in.y;
            latch_.size = std::int32_t(word(record, L::record_size));
            latch_.accumulator = std::int32_t(word(record, L::record_accumulator));
            latch_.top = in.top;
            latch_.bottom = in.bottom;
            latch_.left = in.left;
            latch_.right = in.right;
            latch_.fov = word(view, L::view_fov);
            latch_.owner = in.record_owner;
            latch_.owner_flags = owner_flags;
            latch_.owner_layer = owner_layer;
            std::int32_t scale = std::int32_t(word(view, L::view_scale_x));
            if (scale <= 0) { // the engine's fallback pair: [*mode_slot + 0x28]
                std::uint32_t mode = 0, value = 0;
                if (mode_slot_ && x3m::engine_memory::read(mode_slot_, &mode, 4) && read_word(mode, L::mode_scale_x, value))
                    scale = std::int32_t(value);
            }
            latch_.scale_x = scale;
            latch_.valid = true;
        } else {
            latch_.valid = false; // more than one background-owned sun probed by the main view: vanilla
            if (!multi_counted_) {
                multi_counted_ = true;
                ++counters_.multi_record_frames;
            }
        }
        if (in.ready) { // gate 1's word, read as the probe reads it: [config slot] + 0xec
            const std::uint32_t config = addresses_.config_global
                                             ? *reinterpret_cast<const std::uint32_t*>(addresses_.config_global)
                                             : 0;
            if (config && !(config & 3))
                in.video_flags = word(reinterpret_cast<const std::uint32_t*>(config), L::config_video_flags);
            else
                in.ready = false;
        }
    }
    int decision = core::decide(in);
    if (decision == core::Visible) {
        ++counters_.answered_visible;
        ready_.answered = true;
    } else if (decision == core::Hidden) {
        ++counters_.answered_hidden;
        ready_.answered = true;
    } else
        ++counters_.original;
    if (log_) {
        // The diagnostic always runs the original (register ABI: ECX = record, EDX = view, as fastcall) and returns
        // its answer itself where the decision was "original", so it never runs twice.
        using Probe = int(__attribute__((fastcall)) *)(const std::uint32_t*, const std::uint32_t*);
        const int vanilla = reinterpret_cast<Probe>(x3m_fl_sun_probe_target)(record, view);
        x3m::log(
            "sun_probe frame=%llu view=%08lx record=%08lx owner=%08lx owner_flags270=%08lx owner_layer=%ld main=%08lx eligible=%u acc=%ld x=%ld y=%ld visible=%lu size=%ld group=%lu flags270=%08lx layer=%ld records=%u ready=%u vanilla=%d answer=%d",
            static_cast<unsigned long long>(device_frame_), static_cast<unsigned long>(in.view),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(record)),
            static_cast<unsigned long>(in.record_owner), static_cast<unsigned long>(owner_flags),
            static_cast<long>(owner_layer), static_cast<unsigned long>(main_view_), own ? 1u : 0u,
            static_cast<long>(std::int32_t(word(record, L::record_accumulator))), static_cast<long>(in.x),
            static_cast<long>(in.y), static_cast<unsigned long>(word(record, L::record_visible)),
            static_cast<long>(std::int32_t(word(record, L::record_size))),
            static_cast<unsigned long>(word(record, L::record_group)), static_cast<unsigned long>(in.view_flags),
            static_cast<long>(std::int32_t(word(view, L::view_layer))), ready_.records, in.ready ? 1u : 0u, vanilla,
            decision == core::Original ? vanilla : decision);
        if (decision == core::Original) decision = vanilla ? core::Hidden : core::Visible;
    }
    return decision;
}
}
extern "C" {
// Once per lens call (once per frame): XMM0-7 and the full CPU image around the SSE listener.
__attribute__((force_align_arg_pointer)) void __cdecl x3m_fl_sun_lens_begin() {
    XmmSave xmm;
    x3m::PreserveCpuState cpu;
    if (!owner_thread_ || static_cast<LONG>(GetCurrentThreadId()) != owner_thread_)
        return; // no probe yet, or not the render thread: no bracket
    ++counters_.brackets;
    if (listener_begin_) listener_begin_();
    // After the listener (its frame_inputs() consumes a pending Reset). While the record is deferred the original
    // probe ran for it, so record+0x30 is the vanilla verdict (the walk stores 0 on "hide" at base+0x8af13).
    const std::uintptr_t single =
        override_ && frame_started_ && seen_frame_ == frame_ && ready_.single() && latch_.valid ? ready_.record : 0;
    std::uint32_t visible = 0;
    if (deferral_.deferred(single) && !x3m::engine_memory::read(single + L::record_visible, &visible, 4)) visible = 0;
    deferral_.bracket_begin(single, visible != 0);
    x3m::sun_occlusion::detail::bracket_open = true;
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_fl_sun_lens_end() {
    XmmSave xmm;
    x3m::PreserveCpuState cpu;
    if (!x3m::sun_occlusion::detail::bracket_open) return;
    x3m::sun_occlusion::detail::bracket_open = false;
    if (listener_end_) listener_end_();
    const std::uintptr_t record = deferral_.record;
    if (deferral_.bracket_end() && deferral_.lift_logged())
        x3m::log("sun_occlusion_released frame=%llu record=%08lx lifts=%u starts=%u note=body_classified_vanilla_visible",
                 static_cast<unsigned long long>(device_frame_), static_cast<unsigned long>(record), deferral_.lifts,
                 deferral_.starts);
}
}
// Probe, in: [ESP] = the return address (site + 5), ECX = record, EDX = view, no stack argument. decide() = 0 / 1 is
// the probe's answer (plain RET: the caller's `test eax,eax` follows); 2 forwards to the original with ECX, EDX,
// EFLAGS and ESP exactly as at the site. ECX/EDX are dead after the call on both paths; they are kept anyway.
// Lens, in: [ESP] = the return address, EDX = view, ECX = the target's padding word. begin; the original with ECX,
// EDX and EFLAGS as at the site; end with the original's EAX/ECX/EDX and EFLAGS kept (dead at the site, kept
// anyway); plain RET. Only the 4-byte stack contract is
// assumed; DF is cleared around the C handlers.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_fl_sun_probe_thunk
_x3m_fl_sun_probe_thunk:
    pushfd
    push ecx
    push edx
    cld
    push edx
    push ecx
    call _x3m_fl_sun_probe_decide
    add esp, 8
    pop edx
    pop ecx
    cmp eax, 2
    je 1f
    popfd
    ret
1:  popfd
    jmp dword ptr [_x3m_fl_sun_probe_target]
    .p2align 4
    .globl _x3m_fl_sun_lens_thunk
_x3m_fl_sun_lens_thunk:
    pushfd
    push ecx
    push edx
    cld
    call _x3m_fl_sun_lens_begin
    pop edx
    pop ecx
    popfd
    call dword ptr [_x3m_fl_sun_lens_target]
    pushfd
    push eax
    push ecx
    push edx
    cld
    call _x3m_fl_sun_lens_end
    pop edx
    pop ecx
    pop eax
    popfd
    ret
    .att_syntax
)");

namespace {
const char* write_name(const engine_patch::CallSite& site) {
    return site.patched_in ? (site.atomic_write ? "atomic" : "plain") : "none";
}
void log_status(bool requested, bool log_requested, bool bracket_wanted, unsigned seconds) {
    x3m::log("fl.sun_occlusion status=%s base=0x%08lx probe=base+0x%lx lens=base+0x%lx probe_write=%s lens_write=%s "
             "override=%u log=%u bracket=%u main_view=%s anchors=%u failed_anchor=%s log_s=%u",
             state_, static_cast<unsigned long>(install_base_), static_cast<unsigned long>(L::probe_site_rva),
             static_cast<unsigned long>(L::lens_site_rva), write_name(probe_site_), write_name(lens_site_),
             unsigned(requested), unsigned(log_requested), unsigned(bracket_wanted),
             registry_slot_ ? "registry" : "none", L::anchor_count, failed_anchor_, seconds);
}
}

namespace x3m::sun_occlusion {
bool install_at(const Addresses& a, bool override_enabled_value, bool log_enabled) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason, bool ok) {
        state_ = reason;
        SetLastError(error);
        return ok;
    };
    if (patched_) return done("already_installed", false);
    if (!a.probe_site || !a.probe_target || !a.lens_site || !a.lens_target ||
        (!a.config_global && override_enabled_value))
        return done("invalid_site", false);
    if (!engine_patch::install_window_open()) return done("late_claim", false);
    if (!pin_self()) return done("pin_failed", false);
    addresses_ = a;
    override_ = override_enabled_value;
    log_ = log_enabled;
    x3m_fl_sun_probe_target = static_cast<std::uint32_t>(a.probe_target);
    x3m_fl_sun_lens_target = static_cast<std::uint32_t>(a.lens_target);
    ready_ = core::Ready{};
    deferral_ = core::Deferral{};
    latch_ = core::Latch{};
    counters_ = Counters{};
    foreign_logged_ = false;
    owner_thread_ = 0;
    foreign_thread_ = 0;
    reset_requested_ = 0;
    seen_frame_ = frame_;
    main_view_ = 0;
    frame_started_ = false;
    multi_counted_ = false;
    detail::bracket_open = false;
    block_reason_ = "";
    probe_site_ = engine_patch::CallSite{};
    lens_site_ = engine_patch::CallSite{};
    // The lens call first (its write is the non-atomic one); a failed probe claim takes it back.
    if (!engine_patch::claim_call(lens_site_, a.lens_site, a.lens_target,
                                  reinterpret_cast<void*>(&x3m_fl_sun_lens_thunk))) {
        if (lens_site_.patched_in) {
            patched_ = true;
            return done("rollback_failed", false);
        } // registered: shutdown() tries again
        return done(lens_site_.status, false);
    }
    if (!engine_patch::claim_call(probe_site_, a.probe_site, a.probe_target,
                                  reinterpret_cast<void*>(&x3m_fl_sun_probe_thunk))) {
        const char* reason = probe_site_.status;
        const bool lens_back = engine_patch::restore_call(lens_site_);
        if (probe_site_.patched_in || !lens_back) {
            patched_ = true;
            return done("rollback_failed", false);
        }
        return done(reason, false);
    }
    patched_ = true;
    return done("active", true);
}
// The X3FL.exe identity and the 17 anchors at the live base; then, inside the engine_patch install window only
// (capture's initialize_log on the backend-load path), both claims. One fl.sun_occlusion row whenever any of the
// three requests is set.
bool initialize(bool bracket_wanted) {
    const DWORD error = GetLastError();
    if (patched_) {
        SetLastError(error);
        return true;
    }
    wchar_t setting[4]{};
    const bool requested = x3m::config::get(L"X3M_SUN_OCCLUSION", setting, 4) == 1 && setting[0] == L'1';
    const bool log_requested = x3m::config::get(L"X3M_SUN_OCCLUSION_LOG", setting, 4) == 1 && setting[0] == L'1';
    if (!requested && !log_requested && !bracket_wanted) {
        state_ = "disabled";
        SetLastError(error);
        return false;
    }
    bool applied = false;
    install_base_ = registry_slot_ = mode_slot_ = 0;
    failed_anchor_ = "none";
    if (!x3m::fl::verified())
        state_ = "executable_mismatch";
    else {
        const std::uintptr_t base = x3m::fl::module_base();
        install_base_ = base;
        if (const char* anchor = gate(base)) {
            state_ = "anchor_mismatch";
            failed_anchor_ = anchor;
        } else {
            // The main view needs the cockpit registry slot, admitted by the sector-background anchors; without it
            // the override never answers (the bracket and the observe-only probe stay).
            registry_slot_ = x3m::fl::sector_background_fl::admitted() ? base + R::cockpit_registry_slot_rva : 0;
            mode_slot_ = base + L::mode_slot_rva;
            applied = install_at(Addresses{base + L::probe_site_rva, base + L::probe_target_rva,
                                           base + L::lens_site_rva, base + L::lens_target_rva,
                                           base + L::config_slot_rva, nullptr},
                                 requested, log_requested);
        }
    }
    const unsigned seconds = applied ? arm_sample() : 0;
    log_status(requested, log_requested, bracket_wanted, seconds);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    const bool probe_back = engine_patch::restore_call(probe_site_);
    const bool lens_back = engine_patch::restore_call(lens_site_);
    patched_ = probe_site_.patched_in || lens_site_.patched_in; // a failed restoration stays for a later retry
    detail::bracket_open = false;
    state_ = probe_back && lens_back ? "restored" : "restore_failed";
    SetLastError(error);
    return probe_back && lens_back;
}
const char* state() {
    return state_;
}
bool installed() {
    return patched_;
}
bool override_enabled() {
    return patched_ && override_;
}
bool logging() {
    return patched_ && log_;
}
void set_listener(void (*begin)(), void (*end)()) {
    listener_begin_ = begin;
    listener_end_ = end;
}
// The frame boundary (capture's Present): also closes a bracket an unwind left open, and writes one
// fl.sun_occlusion_sample row per X3M_FL_SUN_OCCLUSION_LOG seconds (one QueryPerformanceCounter per frame).
void present(unsigned long long next_device_frame) {
    if (!patched_) return;
    frame_ = frame_ + 1;
    device_frame_ = next_device_frame;
    detail::bracket_open = false; // an unwind past the lens thunk must not leave the draws' window open
    if (!foreign_logged_ && foreign_thread_ != 0) {
        foreign_logged_ = true;
        log("sun_occlusion_foreign_thread frame=%llu owner=%lu calls=%ld note=probes_from_other_threads_run_the_original",
            next_device_frame, static_cast<unsigned long>(owner_thread_), static_cast<long>(foreign_thread_));
    }
    if (!sample_period) return;
    const DWORD error = GetLastError();
    ++sample_frames;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (!sample_force && now.QuadPart < sample_due) {
        SetLastError(error);
        return;
    }
    sample_force = false;
    sample_due = now.QuadPart + sample_period;
    const Counters c = counters();
    const Counters& p = sample_last;
    ++sample_rows;
    log("fl.sun_occlusion_sample row=%lu status=%s frames=%lu probes=%lu own=%lu visible=%lu hidden=%lu original=%lu "
        "foreign=%lu brackets=%lu multi=%lu blocked=%lu deferred=%u starts=%u lifts=%u main_view=0x%08lx",
        sample_rows, state_, sample_frames, static_cast<unsigned long>(c.probes - p.probes),
        static_cast<unsigned long>(c.own - p.own), static_cast<unsigned long>(c.answered_visible - p.answered_visible),
        static_cast<unsigned long>(c.answered_hidden - p.answered_hidden),
        static_cast<unsigned long>(c.original - p.original), static_cast<unsigned long>(c.foreign_thread),
        static_cast<unsigned long>(c.brackets - p.brackets),
        static_cast<unsigned long>(c.multi_record_frames - p.multi_record_frames),
        static_cast<unsigned long>(c.blocked), unsigned(deferral_.record != 0), deferral_.starts, deferral_.lifts,
        static_cast<unsigned long>(main_view_));
    sample_last = c;
    sample_frames = 0;
    SetLastError(error);
}
void device_reset() {
    if (patched_) InterlockedExchange(&reset_requested_, 1);
}
FrameInputs frame_inputs() {
    FrameInputs out;
    out.frame = frame_;
    if (!patched_) return out;
    if (InterlockedExchange(&reset_requested_, 0) != 0) {
        ready_.reset();
        latch_ = core::Latch{};
        block_reason_ = "";
        return out;
    }
    if (!frame_started_ || seen_frame_ != frame_) return out; // no probe in this frame: nothing latched
    out.latch = latch_;
    out.single = ready_.single() && latch_.valid;
    out.answered = ready_.answered;
    return out;
}
void report_pass(bool ok) {
    if (patched_ && frame_started_ && seen_frame_ == frame_) ready_.pass_ok = ok;
}
void block(const char* reason) {
    if (!patched_ || ready_.blocked) return;
    ready_.blocked = true;
    block_reason_ = reason ? reason : "";
    ++counters_.blocked;
    log("sun_occlusion_blocked frame=%llu reason=%s", static_cast<unsigned long long>(device_frame_), block_reason_);
}
bool lens_attributable() {
    return patched_ && detail::bracket_open && deferral_.bracket_record != 0;
}
int lens_body_unclassifiable(bool first_or_cache_full, bool* row) {
    const core::Deferral::Result r =
        patched_ && detail::bracket_open ? deferral_.body_unclassifiable() : core::Deferral::NotAttributed;
    *row = deferral_.logged(first_or_cache_full, r);
    return int(r);
}
void lens_body_core() {
    if (patched_ && detail::bracket_open) deferral_.body_core();
}
Counters counters() {
    Counters c = counters_;
    c.foreign_thread = static_cast<std::uint32_t>(foreign_thread_);
    return c;
}
#ifdef X3M_SUN_OCCLUSION_FL_FIXTURE
const char* fixture_gate(std::uintptr_t base) {
    return gate(base);
}
void fixture_slots(std::uintptr_t registry_slot, std::uintptr_t mode_slot) {
    registry_slot_ = registry_slot;
    mode_slot_ = mode_slot;
}
std::uintptr_t fixture_main_view() {
    return main_view_walk(registry_slot_);
}
unsigned fixture_arm_sample() {
    return arm_sample();
}
void fixture_force_sample() {
    sample_force = true;
}
void fixture_log_status() {
    log_status(override_, log_, false, 0);
}
#endif
}
