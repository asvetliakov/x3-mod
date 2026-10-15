// Ship steering: the stubs (the engine_patch claims' assembled entries), install_all, initialize / shutdown with the key parsing,
// and the continuation slots the stubs jump through (declared in ship_steering_internal.h).
#include "ship_dock_glue.h"
extern "C" {
// The continuation slots, resume addresses and live bytes the stubs below read (declared in ship_steering_internal.h).
void* x3m_ship_steering_next_a0 = nullptr;
void* x3m_ship_steering_next_a1 = nullptr;
void* x3m_ship_steering_next_a2 = nullptr;
void* x3m_ship_steering_next_b = nullptr;
void* x3m_ship_avoid_next_e = nullptr;
void* x3m_ship_gate_next_a = nullptr;
void* x3m_ship_gate_next_b = nullptr;
void* x3m_ship_gate_next_l = nullptr;
void* x3m_ship_gate_resume_a = reinterpret_cast<void*>(sites::kGateStopAResume);
void* x3m_ship_gate_resume_b = reinterpret_cast<void*>(sites::kGateStopBResume);
void* x3m_ship_gate_resume_l = reinterpret_cast<void*>(sites::kGateLaunchResume);
volatile unsigned char x3m_ship_gates_live = 0;
void* x3m_ship_gate_next_t = nullptr;
volatile unsigned char x3m_ship_traffic_live = 0;
}
namespace engine_patch = x3m::engine_patch;

// A1-A3 (X3M_SS_LEAF): save EAX/ECX/EDX and XMM0-7, call the handler with the
// Frame* and the leaf index, restore everything (EDX from the frame: the
// step-2 seam may redirect it), then `ret 4` when handled or continue into
// the claim tail. `lea esp` and the pops keep the handler's `test eax,eax`
// flags for the `jz`. B: the asm class-7 + autopilot filter first (three
// instructions for every bullet and asteroid), then the same envelope around
// the clamp; the flags it writes are dead (the tail's replayed `cmp` rewrites
// them before the `jne`). Both envelopes execute `cld`: DF leaves cleared,
// the state every compiled caller assumes between string operations (the
// engine never calls a leaf with DF set), and the handlers rely on it.
// E (step 2): the same envelope around the suppression
// lookup with the stdcall arguments A = [esp+4], B = [esp+8] re-pushed from
// the saved frame (0x8c bytes above); a hit returns EAX = 0 through `ret 0x10`
// with ECX/EDX and EFLAGS dead at every caller (ship_steering_sites.h (e)),
// a miss continues into the claim tail (the original warn test).
asm(R"(
    .intel_syntax noprefix
    .text
    .macro X3M_SS_SAVE
    push eax
    push ecx
    push edx
    cld
    sub esp, 0x80
    movups [esp], xmm0
    movups [esp+0x10], xmm1
    movups [esp+0x20], xmm2
    movups [esp+0x30], xmm3
    movups [esp+0x40], xmm4
    movups [esp+0x50], xmm5
    movups [esp+0x60], xmm6
    movups [esp+0x70], xmm7
    .endm
    .macro X3M_SS_RESTORE
    movups xmm0, [esp]
    movups xmm1, [esp+0x10]
    movups xmm2, [esp+0x20]
    movups xmm3, [esp+0x30]
    movups xmm4, [esp+0x40]
    movups xmm5, [esp+0x50]
    movups xmm6, [esp+0x60]
    movups xmm7, [esp+0x70]
    lea esp, [esp+0x80]
    pop edx
    pop ecx
    pop eax
    .endm
    .macro X3M_SS_LEAF name, k, slot
    .p2align 4
    .globl \name
\name:
    X3M_SS_SAVE
    mov eax, esp
    push \k
    push eax
    call _x3m_ship_steering_enter
    add esp, 8
    test eax, eax
    X3M_SS_RESTORE
    jz 1f
    ret 4
1:  jmp dword ptr [\slot]
    .endm
    X3M_SS_LEAF _x3m_ship_steering_a0_stub, 0, _x3m_ship_steering_next_a0
    X3M_SS_LEAF _x3m_ship_steering_a1_stub, 1, _x3m_ship_steering_next_a1
    X3M_SS_LEAF _x3m_ship_steering_a2_stub, 2, _x3m_ship_steering_next_a2
    .p2align 4
    .globl _x3m_ship_steering_b_stub
_x3m_ship_steering_b_stub:
    cmp word ptr [ebx+0x48], 7
    jne 1f
    test dword ptr [ebx+0x40], 0x80000
    jz 1f
    X3M_SS_SAVE
    push ebx
    call _x3m_ship_steering_clamp
    add esp, 4
    X3M_SS_RESTORE
1:  jmp dword ptr [_x3m_ship_steering_next_b]
    .p2align 4
    .globl _x3m_ship_avoid_e_stub
_x3m_ship_avoid_e_stub:
    X3M_SS_SAVE
    push dword ptr [esp+0x94]
    push dword ptr [esp+0x94]
    call _x3m_ship_avoid_suppress
    add esp, 8
    test eax, eax
    X3M_SS_RESTORE
    jnz 1f
    jmp dword ptr [_x3m_ship_avoid_next_e]
1:  xor eax, eax
    ret 0x10
    .p2align 4
    .globl _x3m_ship_gate_p2_stub
_x3m_ship_gate_p2_stub:
    cmp byte ptr [_x3m_ship_gates_live], 0
    je 1f
    X3M_SS_SAVE
    push ebx
    call _x3m_ship_gate_stop_a
    add esp, 4
    X3M_SS_RESTORE
    jmp dword ptr [_x3m_ship_gate_resume_a]
1:  jmp dword ptr [_x3m_ship_gate_next_a]
    .p2align 4
    .globl _x3m_ship_gate_p4_stub
_x3m_ship_gate_p4_stub:
    cmp byte ptr [_x3m_ship_gates_live], 0
    je 1f
    X3M_SS_SAVE
    push ebx
    call _x3m_ship_gate_stop_b
    add esp, 4
    X3M_SS_RESTORE
    jmp dword ptr [_x3m_ship_gate_resume_b]
1:  jmp dword ptr [_x3m_ship_gate_next_b]
    .p2align 4
    .globl _x3m_ship_gate_p5_stub
_x3m_ship_gate_p5_stub:
    cmp byte ptr [_x3m_ship_gates_live], 0
    je 1f
    X3M_SS_SAVE
    push dword ptr [esp+0x88]
    push ebx
    call _x3m_ship_gate_launch
    add esp, 8
    mov [esp+0x88], eax
    X3M_SS_RESTORE
    jmp dword ptr [_x3m_ship_gate_resume_l]
1:  jmp dword ptr [_x3m_ship_gate_next_l]
    .p2align 4
    .globl _x3m_ship_gate_arrival_stub
_x3m_ship_gate_arrival_stub:
    cmp byte ptr [_x3m_ship_traffic_live], 0
    je 1f
    X3M_SS_SAVE
    push edi
    push ebx
    call _x3m_ship_gate_arrival
    add esp, 8
    X3M_SS_RESTORE
1:  jmp dword ptr [_x3m_ship_gate_next_t]
    .att_syntax
)");
namespace {
const char* state_ = "not_initialized";
engine_patch::Site patches_[sites::Count];
engine_patch::Site warn_site_{};
engine_patch::Site gate_patches_[sites::GateCount];
engine_patch::Site traffic_patch_;
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine jumps into it.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&initialized_), &module) != FALSE &&
           module != nullptr;
}
void* stub_of(unsigned i) {
    switch (i) {
    case sites::LeafYawPitch: return reinterpret_cast<void*>(&x3m_ship_steering_a0_stub);
    case sites::LeafRollYaw: return reinterpret_cast<void*>(&x3m_ship_steering_a1_stub);
    case sites::LeafRollPitch: return reinterpret_cast<void*>(&x3m_ship_steering_a2_stub);
    default: return reinterpret_cast<void*>(&x3m_ship_steering_b_stub);
    }
}
void** slot_of(unsigned i) {
    switch (i) {
    case sites::LeafYawPitch: return &x3m_ship_steering_next_a0;
    case sites::LeafRollYaw: return &x3m_ship_steering_next_a1;
    case sites::LeafRollPitch: return &x3m_ship_steering_next_a2;
    default: return &x3m_ship_steering_next_b;
    }
}
// The install transaction (the stamp::install_group shape, with the
// continuation slots in this module's .data): refused outside the install
// window, every span preflighted before any claim, claims in table order,
// every patched site rolled back in reverse on the first failure. The module
// stays off unless all four sites are live.
bool install_all(const char** status, char (&detail)[64]) {
    detail[0] = 0;
    if (!engine_patch::install_window_open()) {
        *status = "late_claim";
        return false;
    }
    for (unsigned i = 0; i < sites::Count; ++i)
        if (!engine_patch::verify_bytes(sites::kSites[i].address, sites::kSites[i].expected, sites::kSites[i].length)) {
            *status = "bytes_mismatch";
            std::snprintf(detail, sizeof detail, "%s", sites::kSites[i].name);
            return false;
        }
    if (!pin_self()) {
        *status = "pin_failed";
        return false;
    }
    for (unsigned i = 0; i < sites::Count; ++i) {
        patches_[i] = engine_patch::Site{};
        if (engine_patch::claim(patches_[i], sites::kSites[i])) {
            void** slot = slot_of(i);
            *slot = *patches_[i].entry; // the tail: the engine's own bytes until the stub is chained
            std::atomic_thread_fence(std::memory_order_release);
            if (engine_patch::push_front(patches_[i], stub_of(i))) continue;
            *status = "stub_chain_failed";
        } else
            *status = patches_[i].status;
        std::snprintf(detail, sizeof detail, "%s", sites::kSites[i].name);
        bool restored = true;
        for (unsigned j = sites::Count; j-- > 0;)
            if (patches_[j].patched_in && !engine_patch::restore(patches_[j])) restored = false;
        // A site whose restore failed stays live with our stub chained: both handlers pass through while
        // installed_ is false, and shutdown() retries the restore (patched_in stays set).
        if (!restored) *status = "rollback_failed_live_sites_pass_through";
        return false;
    }
    *status = "ok";
    return true;
}
const char* write_name(const engine_patch::Site& s) {
    return s.patched_in ? (s.atomic_write ? "atomic" : "plain") : "none";
}
void** gate_slot_of(unsigned i) {
    switch (i) {
    case sites::GateStopA: return &x3m_ship_gate_next_a;
    case sites::GateStopB: return &x3m_ship_gate_next_b;
    default: return &x3m_ship_gate_next_l;
    }
}
void* gate_stub_of(unsigned i) {
    switch (i) {
    case sites::GateStopA: return reinterpret_cast<void*>(&x3m_ship_gate_p2_stub);
    case sites::GateStopB: return reinterpret_cast<void*>(&x3m_ship_gate_p4_stub);
    default: return reinterpret_cast<void*>(&x3m_ship_gate_p5_stub);
    }
}
} // namespace
namespace x3m::ship_steering {
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized_) {
        SetLastError(error);
        return installed_.load(std::memory_order_acquire);
    }
    initialized_ = true;
    wchar_t text[8]{};
    const DWORD length = x3m::config::get(L"X3M_SHIP_STEERING", text, 8);
    bool on = false;
    const char* reason = nullptr;
    char detail[64] = {};
    if (length >= 8 || !core::parse_switch(text, length, &on))
        reason = "invalid_setting";
    else if (!on) {
        state_ = "off";
        SetLastError(error);
        return false; // unset or 0: nothing claimed, no row
    }
    wchar_t classes[sites::kSettingCapacity]{};
    const DWORD class_length = x3m::config::get(L"X3M_SHIP_STEERING_CLASSES", classes, sites::kSettingCapacity);
    if (!reason) {
        if (class_length >= sites::kSettingCapacity || !core::parse_class_mask(classes, class_length, &class_mask_))
            reason = "invalid_classes";
        else if (!object_trace::executable_verified())
            reason = "executable_mismatch";
        else if (install_all(&reason, detail))
            reason = nullptr;
    }
    if (reason) {
        state_ = reason;
        log("ship_steering_refused reason=%s%s%s sites=%u", reason, detail[0] ? "_" : "", detail, unsigned(sites::Count));
        SetLastError(error);
        return false;
    }
    // Step 2: X3M_SHIP_AVOIDANCE=1 (requires the steering above). Site E is claimed separately; a refusal there
    // leaves avoidance running without the suppression (the reactive loop stays as today).
    wchar_t avoid_text[8]{};
    const DWORD avoid_length = x3m::config::get(L"X3M_SHIP_AVOIDANCE", avoid_text, 8);
    bool avoid_on = false;
    if (avoid_length >= 8 || !core::parse_switch(avoid_text, avoid_length, &avoid_on)) {
        avoid_on = false;
        log("ship_avoidance_refused reason=invalid_setting");
    }
    avoidance_on_ = avoid_on;
    if (avoidance_on_) {
        warn_site_ = engine_patch::Site{};
        if (!engine_patch::verify_bytes(sites::kWarnSite.address, sites::kWarnSite.expected, sites::kWarnSite.length))
            warn_state_ = "bytes_mismatch";
        else if (engine_patch::claim(warn_site_, sites::kWarnSite)) {
            x3m_ship_avoid_next_e = *warn_site_.entry;
            std::atomic_thread_fence(std::memory_order_release);
            if (engine_patch::push_front(warn_site_, reinterpret_cast<void*>(&x3m_ship_avoid_e_stub))) {
                warn_state_ = "active";
                warn_live_.store(true, std::memory_order_release);
            } else {
                warn_state_ = engine_patch::restore(warn_site_) ? "stub_chain_failed" : "stub_chain_failed_live_pass_through";
            }
        } else
            warn_state_ = warn_site_.status;
    }
    // Step 3: X3M_SHIP_STEERING_GATES (default on with the steering). The three gate sites are claimed as a group:
    // every span preflighted, claims in table order, every claimed gate site rolled back on the first failure; the
    // gate policy (P1) runs only with all three live. Steering and avoidance are unaffected by a refusal here.
    wchar_t gates_text[8]{};
    const DWORD gates_length = x3m::config::get(L"X3M_SHIP_STEERING_GATES", gates_text, 8);
    bool gates_on = true;
    if (gates_length == 0)
        gates_on = true;
    else if (gates_length >= 8 || !core::parse_switch(gates_text, gates_length, &gates_on)) {
        gates_on = false;
        gates_state_ = "invalid_setting";
        log("ship_steering_gates_refused reason=invalid_setting");
    }
    gates_on_ = gates_on;
    if (gates_on_) {
        const char* why = nullptr;
        char gate_detail[64] = {};
        for (unsigned i = 0; i < sites::GateCount && !why; ++i)
            if (!engine_patch::verify_bytes(sites::kGateSites[i].address, sites::kGateSites[i].expected, sites::kGateSites[i].length)) {
                why = "bytes_mismatch";
                std::snprintf(gate_detail, sizeof gate_detail, "%s", sites::kGateSites[i].name);
            }
        for (unsigned i = 0; i < sites::GateCount && !why; ++i) {
            gate_patches_[i] = engine_patch::Site{};
            void** slot = gate_slot_of(i);
            void* stub = gate_stub_of(i);
            if (engine_patch::claim(gate_patches_[i], sites::kGateSites[i])) {
                *slot = *gate_patches_[i].entry;
                std::atomic_thread_fence(std::memory_order_release);
                if (engine_patch::push_front(gate_patches_[i], stub)) continue;
                why = "stub_chain_failed";
            } else
                why = gate_patches_[i].status;
            std::snprintf(gate_detail, sizeof gate_detail, "%s", sites::kGateSites[i].name);
            bool restored = true;
            for (unsigned j = sites::GateCount; j-- > 0;)
                if (gate_patches_[j].patched_in && !engine_patch::restore(gate_patches_[j])) restored = false;
            if (!restored) why = "rollback_failed_live_sites_pass_through"; // the stubs pass through: the live byte stays 0
        }
        if (why) {
            gates_state_ = why;
            log("ship_steering_gates_refused reason=%s%s%s sites=%u", why, gate_detail[0] ? "_" : "", gate_detail, unsigned(sites::GateCount));
        } else {
            gates_state_ = "active";
            gates_live_.store(true, std::memory_order_release);
            x3m_ship_gates_live = 1;
        }
    } else if (gates_state_[0] != 'i')
        gates_state_ = "off";
    // Amendment 13: X3M_SHIP_STEERING_GATES_CAPITAL (default on with the gates; needs the gate group live: P2 is where the
    // speed is kept). Set without the gates, or with the gates refused, it is refused with a row; off = the amendment-10 path.
    {
        wchar_t cap_text[8]{};
        const DWORD cap_length = x3m::config::get(L"X3M_SHIP_STEERING_GATES_CAPITAL", cap_text, 8);
        bool cap_on = true;
        const char* cap_why = nullptr;
        if (cap_length == 0)
            cap_on = true;
        else if (cap_length >= 8 || !core::parse_switch(cap_text, cap_length, &cap_on)) {
            cap_on = false;
            cap_why = "invalid_setting";
        }
        if (cap_on && !gates_on_) {
            cap_on = false;
            if (cap_length) cap_why = "needs_gates"; // set explicitly without the gates: the refusal row; unset: silently off
        } else if (cap_on && !gates_live_.load(std::memory_order_relaxed)) {
            cap_on = false;
            cap_why = "gates_refused";
        }
        gates_capital_on_ = cap_on;
        if (cap_why) {
            gates_capital_state_ = cap_why;
            log("ship_steering_gates_capital_refused reason=%s", cap_why);
        } else
            gates_capital_state_ = cap_on ? "active" : "off";
    }
    // The gate traffic controller, stage A (gate-traffic-controller.md D.8): X3M_SHIP_STEERING_GATES_TRAFFIC (default on; needs the
    // capital key active). The launch window 0x00444606 is claimed alone, all or nothing: byte-verified, claimed and chained, or rolled
    // back and the controller stays off (amendment 17 exactly); refused outside the install window. Set without the capital key: the
    // refusal row; unset: silently off.
    {
        wchar_t tr_text[8]{};
        const DWORD tr_length = x3m::config::get(L"X3M_SHIP_STEERING_GATES_TRAFFIC", tr_text, 8);
        bool tr_on = true;
        const char* tr_why = nullptr;
        if (tr_length && (tr_length >= 8 || !core::parse_switch(tr_text, tr_length, &tr_on))) {
            tr_on = false;
            tr_why = "invalid_setting";
        }
        if (tr_on && !gates_capital_on_) {
            tr_on = false;
            if (tr_length) tr_why = "needs_gates_capital";
        }
        if (tr_on && !avoidance_on_) { // B.5: the live set is filled by the avoidance walks; without them no member is ever proven
            tr_on = false;
            tr_why = "needs_avoidance";
        }
        if (tr_on && !engine_patch::install_window_open()) {
            tr_on = false;
            tr_why = "late_claim";
        }
        if (tr_on && !engine_patch::verify_bytes(sites::kArrivalSite.address, sites::kArrivalSite.expected, sites::kArrivalSite.length)) {
            tr_on = false;
            tr_why = "bytes_mismatch";
        }
        if (tr_on) {
            traffic_patch_ = engine_patch::Site{};
            if (engine_patch::claim(traffic_patch_, sites::kArrivalSite)) {
                x3m_ship_gate_next_t = *traffic_patch_.entry; // the tail: the engine's own bytes until the stub is chained
                std::atomic_thread_fence(std::memory_order_release);
                if (!engine_patch::push_front(traffic_patch_, reinterpret_cast<void*>(&x3m_ship_gate_arrival_stub))) {
                    tr_on = false;
                    tr_why = "stub_chain_failed";
                }
            } else {
                tr_on = false;
                tr_why = traffic_patch_.status;
            }
            // the stub passes through while the live byte is 0: a site whose restore failed stays harmless; shutdown() retries
            if (!tr_on && traffic_patch_.patched_in && !engine_patch::restore(traffic_patch_)) tr_why = "rollback_failed_live_site_passes_through";
        }
        traffic_on_ = tr_on;
        if (tr_on) {
            traffic_state_ = "active";
            traffic_live_.store(true, std::memory_order_release);
            x3m_ship_traffic_live = 1;
        } else if (tr_why) {
            traffic_state_ = tr_why;
            log("ship_steering_gates_traffic_refused reason=%s", tr_why);
        } else
            traffic_state_ = "off";
    }
    // Lane stage 1 (convoy-gate-transit.md section 1): X3M_SHIP_STEERING_GATES_LANE (default on since 2026-10-15, as the schema's and the
    // launcher's; 0: the stage-A certificate only, stage B was deleted on 2026-10-15; needs the traffic controller active). No site of its
    // own: the lane runs in the controller's frame boundary, its order-4 point rewrite and its speed in the controller's site A / site B
    // paths. Set to 1 without the controller: the refusal row; unset without it: silently off.
    {
        wchar_t ln_text[8]{};
        const DWORD ln_length = x3m::config::get(L"X3M_SHIP_STEERING_GATES_LANE", ln_text, 8);
        bool ln_on = true;
        const char* ln_why = nullptr;
        if (ln_length && (ln_length >= 8 || !core::parse_switch(ln_text, ln_length, &ln_on))) {
            ln_on = false;
            ln_why = "invalid_setting";
        }
        if (ln_on && !traffic_on_) {
            ln_on = false;
            if (ln_length) ln_why = "needs_traffic";
        }
        lane_on_ = ln_on;
        ctl_.lane = ln_on ? 1 : 0; // every gate board acquired from here runs gc::lane_frame (gc::acquire_gate)
        lane_state_ = ln_on ? "active" : ln_why ? ln_why : "off";
        if (ln_why) log("ship_steering_gates_lane_refused reason=%s", ln_why);
        else if (ln_on) log("ship_steering_gates_lane status=active");
    }
    // Amendment 18 (docking-approach.md item 6): X3M_SHIP_STEERING_DOCK (default on; needs X3M_SHIP_AVOIDANCE). No site of its own: the
    // row runs in site E's handler (inert while E is not chained), the part obstacles in the avoidance. Set without the avoidance: the
    // refusal row; unset: silently off.
    {
        wchar_t dk_text[8]{};
        const DWORD dk_length = x3m::config::get(L"X3M_SHIP_STEERING_DOCK", dk_text, 8);
        bool dk_on = true;
        const char* dk_why = nullptr;
        if (dk_length && (dk_length >= 8 || !core::parse_switch(dk_text, dk_length, &dk_on))) {
            dk_on = false;
            dk_why = "invalid_setting";
        }
        if (dk_on && !avoidance_on_) {
            dk_on = false;
            if (dk_length) dk_why = "needs_avoidance";
        }
        dock_on_ = dk_on;
        if (dk_why) {
            dock_state_ = dk_why;
            log("ship_steering_dock_refused reason=%s", dk_why);
        } else
            dock_state_ = dk_on ? (warn_live_.load(std::memory_order_relaxed) ? "active" : "active_no_e") : "off";
        if (dk_on)
            log("ship_steering_dock installed=1 status=%s margin=%.1f*r+%.0f brake_cap_m=%.0f carrier_inner_r=%.1f idle_near_r=%.1f row_ring=32",
                dock_state_, double(av::kDockMarginFactor), double(av::kDockMarginM), double(av::kDockBrakeCapM), double(av::kDockCarrierInner),
                double(av::kDockIdleNear));
    }
    LARGE_INTEGER f{};
    qpc_frequency_ = QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 0;
    ctl_ceiling_ticks_ = qpc_frequency_ * gc::kFrameCeilingUs / 1000000u; // B.3: the decisions' time ceiling (0 without QPC)
    perf_ = log_tier::cached_perf;
    debug_ = log_tier::cached_debug;
    // Step 4 item 7: X3M_SHIP_TRACE (default off; --debug tier) and X3M_SHIP_TRACE_MAX (1..16, default 8 since amendment 17)
    {
        wchar_t trace_text[8]{}, max_text[8]{};
        const DWORD trace_length = x3m::config::get(L"X3M_SHIP_TRACE", trace_text, 8);
        bool trace_on = false;
        if (trace_length && (trace_length >= 8 || !core::parse_switch(trace_text, trace_length, &trace_on))) {
            trace_on = false;
            log("ship_trace_refused reason=invalid_setting");
        }
        trace_on_ = trace_on && debug_;
        if (trace_on && !debug_) log("ship_trace_refused reason=needs_debug");
        const DWORD max_length = x3m::config::get(L"X3M_SHIP_TRACE_MAX", max_text, 8);
        if (max_length) {
            wchar_t* end = nullptr;
            const unsigned long m = max_length < 8 ? wcstoul(max_text, &end, 10) : 0;
            if (max_length >= 8 || !end || *end != 0 || m < 1 || m > kTraceMax) {
                trace_max_ = 8u;
                log("ship_trace_refused reason=invalid_max");
            } else
                trace_max_ = unsigned(m);
        }
        if (trace_on_)
            log("ship_trace installed=1 max_ships=%u ring=%u idle_ms=%lu bytes_per_row_est=390", trace_max_, kTraceRing,
                static_cast<unsigned long>(kTraceIdleMs));
    }
    timing_ = (perf_ || debug_) && qpc_frequency_ != 0;
    state_ = "installed";
    installed_.store(true, std::memory_order_release);
    log("ship_steering installed=1 sites=%u class_mask=0x%04lx timing=%u tau_s=%.2f tick_s=%.2f floor_ms=%.0f bank_deg=35 "
        "handler_a=0x%08lx handler_b=0x%08lx",
        unsigned(sites::Count), static_cast<unsigned long>(class_mask_), timing_ ? 1u : 0u, double(core::kTau),
        double(core::kTickS), double(core::kFloorMs),
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_steering_enter)),
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_steering_clamp)));
    for (unsigned i = 0; i < sites::Count; ++i)
        log("ship_steering_site index=%u site=0x%08lx name=%s status=%s write=%s stub=0x%08lx tail=0x%08lx", i,
            static_cast<unsigned long>(sites::kSites[i].address), sites::kSites[i].name, patches_[i].status,
            write_name(patches_[i]), static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(stub_of(i))),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(*slot_of(i))));
    if (avoidance_on_)
        log("ship_avoidance installed=1 warn_site=0x%08lx warn_status=%s write=%s snapshot_capacity=%u snapshot_slots=%u "
            "station_capacity=%u parts_per_station=%u candidates=%u resolved_age_ms=%lu handler_e=0x%08lx",
            static_cast<unsigned long>(sites::kWarnSite.address), warn_state_, write_name(warn_site_), av::kSnapshotCapacity,
            sites::kSnapshotSlots, av::kStationCapacity, av::kMaxParts, av::kMaxCandidates,
            static_cast<unsigned long>(av::kResolvedAgeMs),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_avoid_suppress)));
    if (gates_on_) {
        log("ship_steering_gates installed=%u status=%s sites=%u lead_in_deg=14.17 cross_fraction=%.2f look_s=%.2f look_min_r=%.2f join_r=%.1f "
            "large_cap_ms=%.0f spread_r=%.2f ring=%u capital_tol_deg=%.2f handler_p2=0x%08lx handler_p4=0x%08lx handler_p5=0x%08lx",
            gates_live_.load(std::memory_order_relaxed) ? 1u : 0u, gates_state_, unsigned(sites::GateCount), double(core::kGateCrossFraction),
            double(core::kGateLookS), double(core::kGateLookMinR), double(core::kGateJoinR), double(core::kGateLargeCapMs),
            double(core::kGateSpreadR), core::kGateSpreadRing, double(core::kGateRotateTolDeg),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_gate_stop_a)),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_gate_stop_b)),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_gate_launch)));
        for (unsigned i = 0; i < sites::GateCount; ++i) {
            const std::uintptr_t resume = i == sites::GateStopA ? sites::kGateStopAResume : i == sites::GateStopB ? sites::kGateStopBResume
                                                                                                                  : sites::kGateLaunchResume;
            log("ship_steering_gate_site index=%u site=0x%08lx name=%s status=%s write=%s stub=0x%08lx tail=0x%08lx resume=0x%08lx", i,
                static_cast<unsigned long>(sites::kGateSites[i].address), sites::kGateSites[i].name, gate_patches_[i].status,
                write_name(gate_patches_[i]), static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(gate_stub_of(i))),
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(*gate_slot_of(i))), static_cast<unsigned long>(resume));
        }
        log("ship_steering_gates_capital installed=%u status=%s join_r=%.1f inner_r=%.1f look_turns=%.1f tol_deg=%.2f small_ratio=%.3f "
            "fresh_ms=%lu fresh_frames=4 join_candidates_ms=100,60,40,25 join_eval_ms=%lu join_step_s=%.2f join_margin=%.0f/%.0f/%.0f/%.0f "
            "fallback_entry_ms=%.0f keep_vel_deg=%.0f member_capacity=%u member_expiry_ms=%lu guard_r=%.2f/%.2f width=lod_all "
            "width_retry_ms=%lu width_retries=%u small_dock_legs=%u small_any_age=%u",
            gates_capital_on_ ? 1u : 0u, gates_capital_state_, double(core::kGateCapitalJoinR), double(core::kGateCapitalJoinInnerR),
            double(core::kGateCapitalLookTurns), double(core::kGateRotateTolDeg), double(av::kSmallShipRatio),
            static_cast<unsigned long>(av::kSteeredFreshMs), static_cast<unsigned long>(core::kJoinEvalMs), double(core::kJoinStepS),
            double(core::kJoinNoseDeg), double(core::kJoinVelDeg), double(core::kJoinLatM), double(core::kJoinOverM), double(core::kJoinEntryMs),
            double(core::kJoinKeepVelDeg), av::kMemberCapacity, static_cast<unsigned long>(av::kMemberExpiryMs), double(ln::kCapitalGuardR),
            double(ln::kSmallGuardR), static_cast<unsigned long>(ln::kWidthRetryMs), ln::kWidthRetries, gates_capital_on_ ? 1u : 0u,
            gates_capital_on_ ? 1u : 0u);
        log("ship_steering_gates_traffic installed=%u status=%s site=0x%08lx name=%s write=%s stub=0x%08lx tail=0x%08lx resume=0x%08lx "
            "boards=%u members=%u samples=%u step_s=%.2f sample_s=%.2f margin_m=%.0f drift_ms=%.1f cert_ms=%lu exclude_ms=%lu decide_ms=%lu "
            "state_ms=%lu decisions_per_frame=%u rollouts_per_frame=%u yield=%.0f arrival_window_ms=%lu evict_keep_ms=%lu arrival_offset_r=%.2f "
            "terran_h_r=%.3f side_spawn_r=%.2f,%.2f side_spawn_max_w_r=%.2f snapshot_capacity=%u e_rows=%u e_pairs=%u warn_slots=%u "
            "handler=0x%08lx",
            traffic_live_.load(std::memory_order_relaxed) ? 1u : 0u, traffic_state_, static_cast<unsigned long>(sites::kArrivalSite.address),
            sites::kArrivalSite.name, write_name(traffic_patch_),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_gate_arrival_stub)),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(x3m_ship_gate_next_t)), static_cast<unsigned long>(sites::kArrivalResume),
            gc::kGates, gc::kMembers, gc::kSamples, double(gc::kStepS), double(gc::kSampleS), double(gc::kMarginM), double(gc::kDriftMs),
            static_cast<unsigned long>(gc::kCertMaxAgeMs), static_cast<unsigned long>(gc::kExcludeMaxAgeMs),
            static_cast<unsigned long>(gc::kDecideMs), static_cast<unsigned long>(gc::kStateMs), gc::kDecisionsPerFrame, gc::kRolloutsPerFrame,
            double(gc::kYieldFloorMs), static_cast<unsigned long>(gc::kArrivalWindowMs),
            static_cast<unsigned long>(gc::kEvictKeepMs), double(gc::kArrivalOffsetR), double(gc::kTerranHoleR), double(gc::kSideSpawnR),
            double(gc::kSideSpawnUpR), double(gc::kSideSpawnMaxWR), av::kSnapshotCapacity, kERows, kELogged, kCapWarnSlots,
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_gate_arrival)));
    }
    SetLastError(error);
    return true;
}
bool installed() {
    return installed_.load(std::memory_order_acquire);
}
const char* state() {
    return state_;
}
bool shutdown() {
    // Every site still registered as patched (an installed module, or a site left live by a failed rollback) is
    // restored in reverse order; a site whose restore fails again stays registered for the next attempt.
    bool any = warn_site_.patched_in;
    for (unsigned i = 0; i < sites::Count; ++i) any = any || patches_[i].patched_in;
    for (unsigned i = 0; i < sites::GateCount; ++i) any = any || gate_patches_[i].patched_in;
    any = any || traffic_patch_.patched_in;
    if (!any) return true;
    const DWORD error = GetLastError();
    installed_.store(false, std::memory_order_release); // the handlers pass through from here on
    warn_live_.store(false, std::memory_order_release);
    gates_live_.store(false, std::memory_order_release);
    traffic_live_.store(false, std::memory_order_release);
    x3m_ship_gates_live = 0; // the gate stubs pass through to their tails from here on
    x3m_ship_traffic_live = 0;
    bool restored = true;
    if (traffic_patch_.patched_in && !engine_patch::restore(traffic_patch_)) restored = false;
    for (unsigned i = sites::GateCount; i-- > 0;)
        if (gate_patches_[i].patched_in && !engine_patch::restore(gate_patches_[i])) restored = false;
    if (warn_site_.patched_in && !engine_patch::restore(warn_site_)) restored = false;
    for (unsigned i = sites::Count; i-- > 0;)
        if (patches_[i].patched_in && !engine_patch::restore(patches_[i])) restored = false;
    state_ = restored ? "restored" : "restore_failed_retry_at_next_shutdown";
    SetLastError(error);
    return restored;
}
} // namespace x3m::ship_steering
