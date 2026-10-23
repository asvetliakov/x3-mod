#pragma once
#include <cstdint>
#include <cstring>

// SETA restore after a gate transit (X3M_SETA_RESTORE=1; docs/architecture/seta-restore.md, mechanism and option (i) of
// docs/reverse-engineering/seta-script-toggle.md section 3): the engine sites, the byte windows the installer compares,
// the story build the script constants belong to, the task-stack return scan and the arm/cancel state machine, as pure
// code the host test compiles (verification/probe/seta_restore_host.cpp). No Windows header, no allocation, no floating
// point. verification/results/seta-script-toggle/verify_seta_restore_sites.py pins every constant against X3AP.exe.
namespace x3m::seta_restore::core {
// ---- the TI_SetTimeWarpFactor case of the TI native dispatcher (record-only hook) ----
// Thunk 0x004b12d0 -> dispatcher 0x004b1300 (EAX = case <= 15; EBX = argument cells, EBP = task, EDI = [task+8],
// ESI = the TI module, four saved registers) -> `jmp [eax*4 + 0x004b15f8]`; case 9 (TI_SetTimeWarpFactor) starts at
// 0x004b148c with `mov eax,[0x00606f34]; mov edx,[ebx+1]` (8 bytes, two whole position-independent instructions,
// reached only through the table entry). EAX/ECX/EDX and EFLAGS are dead there (written before any read; nothing reads
// the flags before `ret 0x10`). The hook reads the old factor [cfg+0xcc] (not yet stored), the new one [ebx+1], the
// task's IP and its first return record; it never calls the VM.
constexpr std::uint32_t ti_thunk_va = 0x004b12d0, ti_dispatch_va = 0x004b1300, ti_jump_table_va = 0x004b15f8;
constexpr unsigned ti_case_index = 9, ti_case_count = 16;
constexpr std::uint32_t ti_table_entry_va = ti_jump_table_va + 4 * ti_case_index; // 0x004b161c
constexpr std::uint32_t hook_va = 0x004b148c, hook_next_va = 0x004b1494;
constexpr unsigned hook_length = 8;
constexpr unsigned char hook_bytes[hook_length] = {0xa1, 0x34, 0x6f, 0x60, 0x00, 0x8b, 0x53, 0x01};
// The rest of case 9 through its `ret 0x10` (compared, never written): mov ecx,[VM]; push 1; mov [eax+0xcc],edx;
// push ecx; mov eax,ebp; call 0x004a47f0; mov eax,1; pop edi/esi/ebp/ebx; ret 0x10.
constexpr std::uint32_t case_rest_va = hook_next_va;
constexpr unsigned case_rest_length = 34;
constexpr unsigned char case_rest[case_rest_length] = {0x8b, 0x0d, 0xe4, 0x85, 0x60, 0x00, 0x6a, 0x01, 0x89, 0x90, 0xcc, 0x00,
                                                       0x00, 0x00, 0x51, 0x8b, 0xc5, 0xe8, 0x46, 0x33, 0xff, 0xff, 0xb8, 0x01,
                                                       0x00, 0x00, 0x00, 0x5f, 0x5e, 0x5d, 0x5b, 0xc2, 0x10, 0x00};
// Dispatcher head: cmp eax,0xf; push ebx; mov ebx,[esp+0x14]; push ebp; mov ebp,[esp+0x10]; push esi; push edi;
// mov edi,[ebp+8]; mov esi,ecx; ja 0x004b15bd; jmp [eax*4+0x004b15f8].
constexpr unsigned dispatch_length = 33;
constexpr unsigned char dispatch_head[dispatch_length] = {0x83, 0xf8, 0x0f, 0x53, 0x8b, 0x5c, 0x24, 0x14, 0x55, 0x8b, 0x6c,
                                                          0x24, 0x10, 0x56, 0x57, 0x8b, 0x7d, 0x08, 0x8b, 0xf1, 0x0f, 0x87,
                                                          0xa3, 0x02, 0x00, 0x00, 0xff, 0x24, 0x85, 0xf8, 0x15, 0x4b, 0x00};
// Thunk: the native's (a, task, case, b, args) become the dispatcher's (a, task, b, args) with EAX = case.
constexpr unsigned thunk_length = 36;
constexpr unsigned char thunk_body[thunk_length] = {0x8b, 0x44, 0x24, 0x14, 0x8b, 0x54, 0x24, 0x10, 0x8b, 0x0d, 0xec, 0x85,
                                                    0x60, 0x00, 0x50, 0x8b, 0x44, 0x24, 0x0c, 0x52, 0x8b, 0x54, 0x24, 0x0c,
                                                    0x50, 0x8b, 0x44, 0x24, 0x18, 0x52, 0xe8, 0x0d, 0x00, 0x00, 0x00, 0xc3};
constexpr unsigned char table_entry[4] = {0x8c, 0x14, 0x4b, 0x00}; // the dword 0x004b148c

// ---- the interpreter's native call (opcode 0x82) and its return scan: where IP and the return record come from ----
// 0x004a3880: movzx ecx,[edi]; ...; add edi,2; ...; add edi,2 (the opcode's two u16 operands) and 0x004a38d5: [task+0x18]
// = the argument cell index; [task+0x1c] = edi - code: the task IP during a native is the native call's offset + 5
// (StopFastForward's TI_SetTimeWarpFactor at 15588 reads IP 1558d).
constexpr std::uint32_t native_args_va = 0x004a3880, native_ip_va = 0x004a38d5;
constexpr unsigned native_args_length = 27, native_ip_length = 15;
constexpr unsigned char native_args[native_args_length] = {0x0f, 0xb7, 0x0f, 0x8b, 0x43, 0x01, 0x0f, 0xb7, 0x57,
                                                           0x02, 0x8b, 0x75, 0x08, 0x83, 0xc7, 0x02, 0x89, 0x44,
                                                           0x24, 0x24, 0x66, 0x89, 0x46, 0x22, 0x83, 0xc7, 0x02};
constexpr unsigned char native_ip[native_ip_length] = {0x8b, 0x54, 0x24, 0x18, 0x89, 0x46, 0x18, 0x8b,
                                                       0xc7, 0x2b, 0x42, 0x08, 0x89, 0x46, 0x1c};
// 0x004a39b3: the return opcode's scan: a tag-10 cell directly above a tag-3 cell is a record; return offset 0 ends the
// task; a null context or one whose class [ctx+8] is 0 is skipped.
constexpr std::uint32_t return_scan_va = 0x004a39b3;
constexpr unsigned return_scan_length = 43;
constexpr unsigned char return_scan[return_scan_length] = {0x80, 0x3b, 0x0a, 0x75, 0x26, 0x80, 0x7b, 0x05, 0x03, 0x75, 0x20,
                                                           0x8b, 0x4b, 0x06, 0x85, 0xc9, 0x8b, 0x43, 0x01, 0x89, 0x44, 0x24,
                                                           0x28, 0x89, 0x4c, 0x24, 0x2c, 0x0f, 0x84, 0x59, 0x0a, 0x00, 0x00,
                                                           0x85, 0xc0, 0x74, 0x06, 0x83, 0x78, 0x08, 0x00, 0x75, 0x1e};
constexpr unsigned task_capacity = 0x10, task_stack_base = 0x14, task_stack_index = 0x18, task_ip = 0x1c;
constexpr unsigned scan_cells = 128; // 640 bytes; StopFastForward's frame is a few cells deep

// ---- the restart call: 0x0049f4c0(0, 0, 0x96, "StartFastForward", 1, 0) ----
// The by-key wrapper the EXE itself uses for class 150 (StopFastForward at 0x0041212e, InputAction at 0x0040f9b2):
// cdecl, six dwords for argc 0, caller pops 0x18; resolves through 0x0049f330, runs the task synchronously through
// 0x0049f430 and returns AL = 1, or AL = 0 when the method does not resolve. The chase restore's 0x0049f570 takes the
// same arguments and adds a post-run lookup of the task id in [*0x006085e4] plus a read-only stack scan (0x004a21a0);
// the class-static calls of the input routine that need no result use 0x0049f4c0 (seta-script-toggle.md section 3 (i)).
constexpr std::uint32_t script_call_va = 0x0049f4c0, call_key = 0x96, call_return_kind = 1, call_argc = 0;
inline constexpr char call_method[] = "StartFastForward"; // mod-supplied: the EXE has no such literal
constexpr unsigned wrapper_length = 173;                   // the whole wrapper 0x0049f4c0..0x0049f56c
// clang-format off
constexpr unsigned char wrapper_body[wrapper_length] = {
    0x83, 0xec, 0x0c, 0x53, 0x55, 0x56, 0x57, 0x8d, 0x44, 0x24, 0x18, 0x50, 0x8b, 0x44, 0x24, 0x34, 0x8d, 0x4c, 0x24, 0x18,
    0x51, 0x8b, 0x4c, 0x24, 0x34, 0x8d, 0x54, 0x24, 0x3c, 0x52, 0x8b, 0x54, 0x24, 0x30, 0x50, 0x8b, 0x44, 0x24, 0x30, 0x51,
    0x52, 0x33, 0xdb, 0x50, 0x8b, 0x44, 0x24, 0x44, 0x89, 0x5c, 0x24, 0x30, 0x89, 0x5c, 0x24, 0x34, 0xe8, 0x33, 0xfe, 0xff,
    0xff, 0x83, 0xc4, 0x1c, 0x84, 0xc0, 0x74, 0x5f, 0x8b, 0x74, 0x24, 0x14, 0x3b, 0xf3, 0x74, 0x57, 0x8b, 0x6c, 0x24, 0x18,
    0x3b, 0xeb, 0x74, 0x4f, 0x8b, 0x7c, 0x24, 0x34, 0x33, 0xc9, 0x3b, 0xfb, 0x76, 0x22, 0x8d, 0x54, 0x24, 0x34, 0x8b, 0x46,
    0x18, 0x8b, 0x5a, 0x04, 0x03, 0xc1, 0x83, 0xc2, 0x04, 0x8d, 0x04, 0x80, 0x03, 0x46, 0x14, 0x83, 0xc1, 0x01, 0x3b, 0xcf,
    0xc6, 0x00, 0x01, 0x89, 0x58, 0x01, 0x72, 0xe2, 0x83, 0x46, 0x18, 0xff, 0x8b, 0x46, 0x18, 0x8b, 0x4d, 0x0c, 0x8d, 0x04,
    0x80, 0x03, 0x46, 0x14, 0xc6, 0x00, 0x01, 0x89, 0x48, 0x01, 0xe8, 0xd5, 0xfe, 0xff, 0xff, 0x5f, 0x5e, 0x5d, 0x5b, 0x83,
    0xc4, 0x0c, 0xc3, 0x5f, 0x5e, 0x5d, 0x32, 0xc0, 0x5b, 0x83, 0xc4, 0x0c, 0xc3};
// clang-format on
// The EXE's own class-150 call of the sibling method (push ebx(0); push 1; push "StopFastForward"; push 0x96; push ebx;
// push ebx; call 0x0049f4c0; add esp,0x18): the shape this call reproduces with the mod's name.
constexpr std::uint32_t stop_call_va = 0x0041211f;
constexpr unsigned stop_call_length = 23;
constexpr unsigned char stop_call[stop_call_length] = {0x53, 0x6a, 0x01, 0x68, 0x1c, 0x5e, 0x55, 0x00, 0x68, 0x96, 0x00, 0x00,
                                                       0x00, 0x53, 0x53, 0xe8, 0x8d, 0xd3, 0x08, 0x00, 0x83, 0xc4, 0x18};
constexpr std::uint32_t inject_site_va = 0x00410100; // the chase restore's shared injection site (not claimed here)

// ---- the installer's comparison: every window above, in this order, each read whole ----
// The windows are 4..173 bytes; engine_patch::verify_bytes compares 5..16-byte prologues only and refused case_rest in
// Run 189 although the image matched (docs/verification/seta-restore.md). No other claim of the tree overlaps a window
// (test_seta_restore pins it with the claim collector; the chase restore's and the music features' sites lie elsewhere),
// so the bytes are compared raw: any difference, a foreign patch included, refuses. Read: bool(std::uint32_t, void*,
// unsigned) reads code bytes.
struct Window {
    const char* name;
    std::uint32_t va;
    const unsigned char* bytes;
    unsigned length;
};
constexpr Window windows[] = {{"hook", hook_va, hook_bytes, hook_length},
                              {"case_rest", case_rest_va, case_rest, case_rest_length},
                              {"dispatch_head", ti_dispatch_va, dispatch_head, dispatch_length},
                              {"thunk_body", ti_thunk_va, thunk_body, thunk_length},
                              {"table_entry", ti_table_entry_va, table_entry, sizeof table_entry},
                              {"native_args", native_args_va, native_args, native_args_length},
                              {"native_ip", native_ip_va, native_ip, native_ip_length},
                              {"return_scan", return_scan_va, return_scan, return_scan_length},
                              {"wrapper_body", script_call_va, wrapper_body, wrapper_length},
                              {"stop_call", stop_call_va, stop_call, stop_call_length}};
constexpr unsigned window_count = sizeof windows / sizeof windows[0];
constexpr unsigned window_max = wrapper_length; // the longest window
constexpr bool windows_bounded() {
    for (const Window& w : windows)
        if (!w.length || w.length > window_max) return false;
    return true;
}
static_assert(windows_bounded(), "every window fits the comparison buffer");
constexpr unsigned shown_bytes = 8; // the refusal row's expected=/got= prefix of the window
struct Mismatch {
    const Window* window = nullptr; // the first window that differs or cannot be read; null when all match
    bool readable = false;
    unsigned shown = 0; // min(length, shown_bytes)
    unsigned char got[shown_bytes]{};
};
template <class Read> bool compare_windows(Read&& read, Mismatch& out) noexcept {
    out = Mismatch{};
    for (const Window& w : windows) {
        unsigned char actual[window_max];
        const bool readable = read(w.va, actual, w.length);
        if (readable && !std::memcmp(actual, w.bytes, w.length)) continue;
        out.window = &w;
        out.readable = readable;
        out.shown = w.length < shown_bytes ? w.length : shown_bytes;
        if (readable) std::memcpy(out.got, actual, out.shown);
        return false;
    }
    return true;
}

// ---- engine globals (each anchored in executable_identity.h) and fields ----
constexpr std::uint32_t cfg_ptr_va = 0x00606f34, vm_ptr_va = 0x006085e4, manager_ptr_va = 0x0060850c,
                        game_root_va = 0x0057fc60;
constexpr unsigned cfg_factor = 0xcc, vm_build = 0x143c, manager_player = 0x38, object_parent = 0x54, object_id = 8,
                   game_pause = 0x4a0;
constexpr std::uint32_t pause_bit = 1, factor_one = 0x10000, int_tag = 1;
// Object identity for the re-bind (sector-transit-order.md section 1): class word u16 [obj+0x48] (ship 7, sector 1),
// alive marker u16 [obj+0x9c] = 0xcafe. Ship speeds (ship-movement.md section 2), record only: [ship+0x10] current,
// [ship+0x14] desired, int32 position units / s.
constexpr unsigned object_class = 0x48, object_marker = 0x9c, ship_speed = 0x10, ship_desired_speed = 0x14;
constexpr std::uint16_t ship_class = 7, sector_class = 1, alive_marker = 0xcafe;
// Autopilot, read at the arm: [ship+0x40] & 0x80000, set and cleared by SA_SetAutopilot (`or [eax+0x40],0x80000` at
// 0x00463e88, `and [eax+0x40],0xfff7ffff` at 0x00463ea7) and read by SA_GetFlags (`mov eax,[eax+0x40]` at 0x004637df).
// The manual gate path 2004::CanWarp calls StartGateWarp only when SA_GetFlags & 0x80000 is clear; the autopilot path
// 2004::__FlyToNextSector runs as the ship's script, so SA_SetAutopilot(ship, IsAutopilotActive()) has set it
// (seta-script-toggle.md section 2, ship-navigation-ai.md "Player autopilot"). Only an autopilot transit arms.
constexpr unsigned ship_flags = 0x40;
constexpr std::uint32_t autopilot_bit = 0x80000;
// The ship-script id [ship+0x94] (the key the engine passes to 0x0049f4c0 for the ship's script: CanWarp, CanLand,
// CollisionWarn; SA_GetEventObject 0x004607f4 reads it after the native-id lookup). Run 190: one value (0xfffebb1e,
// the chase restore's ship_id_from/to) across every re-pointed player-ship object of the transits, so the re-bind
// requires it to equal the value read at the arm. 0 (no script object) is no identity. Pinned by the site verifier:
// `mov eax,[eax+0x94]` at 0x004607f4 (SA_GetEventObject) and `mov ecx,[esi+0x94]` at 0x0045d8a3 (the CanLand call).
constexpr unsigned ship_script = 0x94;
constexpr std::uint32_t event_script_read_va = 0x004607f4, can_land_script_read_va = 0x0045d8a3;
constexpr std::uint32_t set_autopilot_on_va = 0x00463e88, set_autopilot_off_va = 0x00463ea7, get_flags_va = 0x004637df;

// ---- the story constants, valid for one story build only (seta_toggle_study.txt) ----
// Installed L/x3story.obj (addon/07.cat, Mayhem): StopFastForward's TI_SetTimeWarpFactor at 15588 (IP 1558d) and
// StartGateWarp's StopFastForward call at 15ef3 (return offset 15ef8). Any other build id refuses to arm (one row).
constexpr std::uint32_t story_build = 1462141839u;
constexpr std::uint32_t stop_ip = 0x1558d, gate_return = 0x15ef8;
struct NamedOffset {
    std::uint32_t offset;
    const char* name;
};
// Return offsets of StopFastForward's callers (section 4 of the note), for the rows only.
constexpr NamedOffset named_returns[] = {{0x15ef8, "gate"},        {0x155ad, "toggle"},     {0x15fa1, "sector_warp"},
                                         {0x16a07, "quick_warp"},  {0xd62e2, "jumpdrive"},  {0xd6402, "jumpdrive"},
                                         {0, "engine_input"}};
inline const char* return_name(std::uint32_t offset, bool bottom) noexcept {
    if (bottom) return "engine_input";
    for (const auto& n : named_returns)
        if (n.offset && n.offset == offset) return n.name;
    return "other";
}

// ---- timing ----
// Expiry, from the arm, wall time only (a frame bound would depend on the frame rate): the warp-in scene and fade run
// at factor 1.0 with frames presented, the stall presents none (sector-transit-order.md section 2); their wall time is
// unmeasured (note section 5), so the bound is generous. The factor is 1.0 from the arm on, so game time runs at wall
// time and no SETA scaling applies to the bound.
constexpr std::uint64_t expiry_ms = 60000;
constexpr std::uint64_t due_frames = 2; // presented frames after the destination is Ready (the chase restore's count)

// ---- task-stack return scan (the return opcode's rule, 0x004a39b3) ----
enum class Scan : unsigned { none = 0, found = 1, bottom = 2, truncated = 3, unreadable = 4 };
inline const char* scan_name(Scan s) noexcept {
    switch (s) {
    case Scan::none: return "none";
    case Scan::found: return "found";
    case Scan::bottom: return "bottom";
    case Scan::truncated: return "truncated";
    case Scan::unreadable: return "unreadable";
    }
    return "?";
}
struct Return {
    Scan scan = Scan::none;
    std::uint32_t offset = 0;
};
// Task stack: 5-byte cells (tag byte + dword) below [task+0x14], signed top index [task+0x18] (<= 0), capacity
// [task+0x10]. From the top: the first tag-10 cell directly above a tag-3 cell whose return offset is non-zero and whose
// context is live (non-null, class [ctx+8] non-zero) is the frame the running method returns into. The record order is
// the one chase_transition_restore_core.h's restore_caller_context reads. Reader: bool(std::uintptr_t, void*, unsigned).
template <class Reader> Return first_return(std::uint32_t task, Reader bytes) noexcept {
    auto field = [&](std::uintptr_t at, auto& out) { return bytes(at, &out, unsigned(sizeof out)); };
    Return r;
    r.scan = Scan::unreadable;
    std::uint32_t base = 0, capacity = 0;
    std::int32_t index = 0;
    if (!task || (task & 3) || !field(task + task_stack_base, base) || !field(task + task_capacity, capacity) ||
        !field(task + task_stack_index, index) || index > 0 || capacity > 1048576 || index < -std::int32_t(capacity))
        return r;
    const auto used = std::uint32_t(-std::int64_t(index));
    if (used > base / 5) return r;
    const unsigned cells = used < scan_cells ? used : scan_cells;
    unsigned char stack[scan_cells * 5];
    if (cells && !bytes(base - used * 5, stack, cells * 5)) return r;
    for (unsigned i = 0; i + 1 < cells; ++i) {
        if (stack[i * 5] != 10 || stack[(i + 1) * 5] != 3) continue;
        std::uint32_t context = 0, ret = 0, desc = 0;
        std::memcpy(&context, stack + i * 5 + 1, 4);
        std::memcpy(&ret, stack + (i + 1) * 5 + 1, 4);
        if (!ret) {
            r.scan = Scan::bottom;
            return r;
        }
        if (!context) continue;
        if ((context & 3) || !field(context + 8, desc)) return r;
        if (!desc) continue;
        r.scan = Scan::found;
        r.offset = ret;
        return r;
    }
    r.scan = used > cells ? Scan::truncated : Scan::bottom;
    return r;
}

// ---- the state machine ----
// idle -> armed (the gate's StopFastForward write, SETA on) -> due (destination Ready with the player ship parented to
// it) -> calling (StartFastForward handed out at the injection site) -> idle (restored row). Every other path is a
// cancel row back to idle. One ticket at a time; one arm per departure sector object (a transit). While armed or due,
// one player-ship change that passes rebind_check re-binds the ticket (rebound row, phase kept).
enum class Phase : unsigned { idle = 0, armed = 1, due = 2, calling = 3 };
enum Cancel : unsigned {
    cancel_none = 0,
    cancel_factor_write = 1, // any later TI_SetTimeWarpFactor (user key, cut-scene, speech, cheat)
    cancel_ship_change = 2,  // the player ship *(M+0x38) is unreadable, or changed and the change is no re-bind
    cancel_vm_clear = 3,     // 0x0049ea80 (session start)
    cancel_vm_load = 4,      // 0x004a0880 (save load)
    cancel_epoch = 5,        // the EH adapter 0x0052f298: a script error unwound
    cancel_expiry = 6,
    cancel_sector_left = 7,  // the ship left the Ready sector before the call
    cancel_call_unwound = 8, // the call never reported back
    cancel_count = 9
};
inline const char* cancel_name(unsigned c) noexcept {
    static const char* const names[cancel_count] = {"none",   "factor_write", "ship_change", "vm_clear",    "vm_load",
                                                    "epoch",  "expiry",       "sector_left", "call_unwound"};
    return c < cancel_count ? names[c] : "?";
}
enum Refusal : unsigned {
    refuse_none = 0,
    refuse_story_build = 1,  // the VM's story build is not story_build: the feature stays off for the process
    refuse_same_transit = 2, // a second gate stop from the departure sector of the previous arm
    refuse_unreadable = 3,   // player ship, its sector, the sector id, its flags or (to arm) its script id unreadable/0
    refuse_build_unreadable = 4, // the VM's build id unreadable at a SETA stop: not this write (row once, not latched)
    refuse_count = 5
};
enum Skip : unsigned { skip_none = 0, skip_manual = 1, skip_count = 2 }; // a gate stop that is not this feature's
inline const char* skip_name(unsigned k) noexcept {
    static const char* const names[skip_count] = {"none", "manual"};
    return k < skip_count ? names[k] : "?";
}
inline const char* refusal_name(unsigned r) noexcept {
    static const char* const names[refuse_count] = {"none", "story_build", "same_transit", "unreadable",
                                                   "build_unreadable"};
    return r < refuse_count ? names[r] : "?";
}
// The player-ship check at the injection site when *(M+0x38) is not the ticket's ship. The gate transit re-points
// *(M+0x38) once (Run 190: every arm saw exactly one change, at the first entry after the loading stall): one change to
// a live ship object (class 7, marker 0xcafe) with the arm's ship-script id, parented to a live sector (class 1, marker
// 0xcafe) whose id is not the departure sector's, re-binds the ticket. A live ship with that id whose parent is not yet
// a live sector (no_sector) defers: no cancel, the re-bind not consumed, polled again at later entries until the parent
// is live or the ticket expires (one rebind_pending row). Every other outcome cancels (ship_change) and names itself.
enum class ShipCheck : unsigned {
    none = 0,
    rebound = 1,
    unreadable = 2,   // *(M+0x38) unreadable, null or misaligned, or the object's class/marker or script id unreadable
    not_ship = 3,     // readable, but not a live class-7 object
    no_sector = 4,    // its parent unreadable, null, not a live sector, or the sector id unreadable/0 (defers)
    same_sector = 5,  // its sector is still the departure sector
    second = 6,       // the ticket was already re-bound once
    identity = 7,     // its ship-script id [+0x94] is not the one read at the arm
    count = 8
};
inline const char* ship_check_name(ShipCheck c) noexcept {
    static const char* const names[unsigned(ShipCheck::count)] = {"none",      "rebound",     "unreadable", "not_ship",
                                                                   "no_sector", "same_sector", "second",     "identity"};
    return unsigned(c) < unsigned(ShipCheck::count) ? names[unsigned(c)] : "?";
}
// restored: the wrapper ran StartFastForward (AL 1); restore_failed: AL 0, the method did not resolve; rebound: the
// ticket follows the transit's new player-ship object; rebind_pending: the new object passed the identity check but its
// sector is not live yet (once per ticket); skipped: a gate stop not armed (a manual gate pass).
enum class Row : unsigned {
    none = 0,
    armed = 1,
    cancelled = 2,
    refused = 3,
    restored = 4,
    restore_failed = 5,
    rebound = 6,
    skipped = 7,
    rebind_pending = 8
};
struct Clock {
    std::uint64_t frame = 0, ms = 0; // Present count, GetTickCount64
    std::uint32_t epoch = 0;         // the EH-adapter epoch
};
// One TI_SetTimeWarpFactor call as read at the case entry.
struct Write {
    bool readable = false;       // cfg, the argument cell and the task IP were read
    bool build_readable = false; // [*0x006085e4+0x143c] was read (only for a SETA stop or while active)
    std::uint32_t old_factor = 0, new_factor = 0, tag = 0, ip = 0, build = 0;
    Return ret;
};
// The row a transition produces; the caller formats it outside every lock.
struct Report {
    Row row = Row::none;
    unsigned reason = 0;
    std::uint64_t frame = 0, frames_since_arm = 0, ms_since_arm = 0, frames_after_transit = 0;
    std::uint32_t old_factor = 0, new_factor = 0, ip = 0, ret = 0, build = 0, ship = 0, sector = 0, sector_id = 0,
                  ready_sector = 0, result = 0, skipped = 0;
    Scan scan = Scan::none;
    // the re-bind (rebound and rebind_pending rows; new_ship also on a ship_change cancel): the ticket's ship id +8
    // read at the arm (0: unreadable) and its ship-script id +0x94, the new player ship, its id, its script id, its
    // sector and that sector's id (0 where not read); record only, the new ship's flags +0x40 (rebound row)
    ShipCheck ship_check = ShipCheck::none;
    std::uint32_t ship_id = 0, new_ship = 0, new_ship_id = 0, new_sector = 0, new_sector_id = 0;
    std::uint32_t script_id = 0, new_script_id = 0, new_flags = 0;
    bool new_flags_readable = false;
    // record only (record_speed): the current player ship's [+0x10]/[+0x14] at the row
    bool speed_readable = false;
    std::uint32_t speed_ship = 0, speed = 0, desired_speed = 0;
    // the ship's flags [+0x40] read at a gate stop (armed, skipped rows)
    std::uint32_t flags = 0;
    bool autopilot = false;
};
// What the injection site's probe answers (checked engine reads in production, a table in the host test):
//   bool player(std::uint32_t& ship)                    *(M+0x38)
//   bool parent(std::uint32_t ship, std::uint32_t& s)   [ship+0x54]
//   bool sector_id(std::uint32_t sector, std::uint32_t& id)
//   bool paused()                                        [*0x0057fc60+0x4a0] & 1 (unreadable = paused: defer)
//   bool ready(std::uint32_t departure_id, std::uint32_t& node)   the sector-transit-order.md section 5 walk
//                                                        (fog_prefill::walk): Found with id != departure_id
//   bool object_id(std::uint32_t obj, std::uint32_t& id)  [obj+8]
//   bool kind(std::uint32_t obj, std::uint16_t& cls, std::uint16_t& marker)   u16 [obj+0x48], u16 [obj+0x9c]
//   bool speed(std::uint32_t ship, std::uint32_t& cur, std::uint32_t& desired)   [ship+0x10], [ship+0x14]
//   bool flags(std::uint32_t ship, std::uint32_t& f)     [ship+0x40]
//   bool script(std::uint32_t ship, std::uint32_t& id)   [ship+0x94]
struct State {
    Phase phase = Phase::idle;
    bool disabled = false;            // story build refused (latched)
    bool build_unreadable_logged = false;
    std::uint32_t ship = 0, sector = 0, sector_id = 0, epoch = 0, ready_sector = 0, skipped = 0, ship_id = 0;
    std::uint32_t script_id = 0; // the ticket's ship-script id [+0x94] at the arm (non-zero); a re-bind must match it
    bool rebound = false;        // this ticket has followed one player-ship change
    bool pending = false;        // this ticket logged its rebind_pending row (a re-bind waits for the new ship's sector)
    std::uint32_t transit_id = 0;     // departure sector id of the last arm (one arm per transit)
    std::uint64_t arm_frame = 0, arm_ms = 0, ready_frame = 0, walk_frame = ~std::uint64_t(0);
    std::uint64_t arms = 0, restores = 0, restore_failures = 0, rebinds = 0, pendings = 0, cancels[cancel_count]{},
                  refusals[refuse_count]{}, skips[skip_count]{};

    bool active() const noexcept { return phase != Phase::idle; }

    // At the TI_SetTimeWarpFactor case entry (the factor not yet stored).
    template <class Probe> Report on_write(const Write& w, const Clock& now, Probe& p) noexcept {
        Report r = base(now);
        r.old_factor = w.old_factor;
        r.new_factor = w.new_factor;
        r.ip = w.ip;
        r.ret = w.ret.offset;
        r.scan = w.ret.scan;
        r.build = w.build;
        if (disabled) return r;
        if (phase == Phase::armed || phase == Phase::due) {
            cancel(r, cancel_factor_write, now);
            return r;
        }
        if (phase == Phase::calling) return r; // inside our own StartFastForward (it writes no factor)
        if (!w.readable || w.tag != int_tag || w.old_factor <= factor_one || w.new_factor != factor_one) return r;
        if (!w.build_readable) { // unknown story: not this write; one row, the feature stays armed for later writes
            if (build_unreadable_logged) return r;
            build_unreadable_logged = true;
            return refuse(r, refuse_build_unreadable);
        }
        if (w.build != story_build) { // a readable foreign build: the offsets are not bound to it
            disabled = true;
            return refuse(r, refuse_story_build);
        }
        if (w.ip != stop_ip || w.ret.scan != Scan::found || w.ret.offset != gate_return) return r; // another stop
        std::uint32_t s = 0, sec = 0, id = 0;
        if (!p.player(s) || !s || !p.parent(s, sec) || !sec || !p.sector_id(sec, id) || !id)
            return refuse(r, refuse_unreadable);
        r.ship = s;
        r.sector = sec;
        r.sector_id = id;
        std::uint32_t f = 0;
        if (!p.flags(s, f)) return refuse(r, refuse_unreadable);
        r.flags = f;
        r.autopilot = (f & autopilot_bit) != 0;
        if (id == transit_id) return refuse(r, refuse_same_transit);
        const bool script_read = p.script(s, r.script_id) && r.script_id;
        if (!script_read) r.script_id = 0;
        if (!r.autopilot) { // a manual gate pass: nothing armed, the transit not recorded
            if (!p.object_id(s, r.ship_id)) r.ship_id = 0; // record only
            r.row = Row::skipped;
            r.reason = skip_manual;
            ++skips[skip_manual];
            return r;
        }
        if (!script_read) return refuse(r, refuse_unreadable); // no identity for the transit's re-bind
        phase = Phase::armed;
        ship = s;
        ship_id = 0;
        if (!p.object_id(s, ship_id)) ship_id = 0; // record only
        r.ship_id = ship_id;
        script_id = r.script_id;
        rebound = false;
        pending = false;
        sector = sec;
        sector_id = transit_id = id;
        epoch = now.epoch;
        arm_frame = now.frame;
        arm_ms = now.ms;
        ready_sector = 0;
        ready_frame = 0;
        skipped = 0;
        walk_frame = ~std::uint64_t(0);
        ++arms;
        r.row = Row::armed;
        return r;
    }
    // At the injection site's entry while active. Returns true when StartFastForward is to be called now; *out
    // carries a row (cancel) when one is due.
    template <class Probe>
    bool on_inject(const Clock& now, std::uint32_t controlled, Probe& p, Report& out) noexcept {
        out = base(now);
        if (phase == Phase::idle) return false;
        if (phase == Phase::calling) { // the previous call never reported back: it unwound
            cancel(out, cancel_call_unwound, now);
            return false;
        }
        if (now.epoch != epoch) {
            cancel(out, cancel_epoch, now);
            return false;
        }
        if (now.ms - arm_ms > expiry_ms) {
            cancel(out, cancel_expiry, now);
            return false;
        }
        std::uint32_t player = 0;
        const bool player_read = p.player(player);
        if (!player_read || player != ship) {
            out.new_ship = player; // ship= stays the ticket's own (base)
            out.ship_check = player_read ? rebind_check(player, p, out) : ShipCheck::unreadable;
            if (out.ship_check == ShipCheck::no_sector) { // the right ship, its sector not live yet: poll again
                if (!pending) {
                    pending = true;
                    ++pendings;
                    out.row = Row::rebind_pending;
                }
                return false;
            }
            if (out.ship_check != ShipCheck::rebound) {
                cancel(out, cancel_ship_change, now);
                return false;
            }
            out.row = Row::rebound; // the row carries the old ship (base) and the new one; the next entry continues
            ship = player;
            ship_id = out.new_ship_id;
            rebound = true;
            ++rebinds;
            return false;
        }
        if (p.paused()) return false; // defer, the expiry keeps running
        if (phase == Phase::armed) {
            if (now.frame == walk_frame) return false; // one walk per presented frame
            walk_frame = now.frame;
            std::uint32_t node = 0, parent = 0;
            if (!p.ready(sector_id, node) || !node || !p.parent(ship, parent) || parent != node) return false;
            phase = Phase::due;
            ready_sector = node;
            ready_frame = now.frame;
            return false;
        }
        // due
        if (now.frame < ready_frame + due_frames) return false;
        std::uint32_t parent = 0;
        if (!p.parent(ship, parent) || parent != ready_sector) {
            out.ready_sector = parent;
            cancel(out, cancel_sector_left, now);
            return false;
        }
        if (controlled != ship) { // the engine controls another object this call (a remote view): wait
            ++skipped;
            return false;
        }
        phase = Phase::calling;
        return true;
    }
    // The wrapper's AL after StartFastForward (0 = the method did not resolve).
    Report on_result(unsigned al, const Clock& now) noexcept {
        Report r = base(now);
        if (phase != Phase::calling) return r; // cancelled during the call (VM clear/load)
        r.result = al & 0xff;
        r.row = r.result ? Row::restored : Row::restore_failed;
        r.frames_after_transit = now.frame - ready_frame;
        ++(r.result ? restores : restore_failures);
        phase = Phase::idle;
        return r;
    }
    // VM clear (session start) or load: kind 1 clear, 2 load (the chase restore's site kinds).
    Report on_vm(unsigned kind, const Clock& now) noexcept {
        Report r = base(now);
        transit_id = 0; // sector ids start again with the new session
        if (phase != Phase::idle) cancel(r, kind == 1 ? cancel_vm_clear : cancel_vm_load, now);
        return r;
    }

private:
    // A player-ship change while active: one change to a live ship with the arm's ship-script id in a live sector other
    // than the departure sector re-binds (no_sector defers, see ShipCheck); the reads fill out's new_* fields as far as
    // they got.
    template <class Probe> ShipCheck rebind_check(std::uint32_t player, Probe& p, Report& out) noexcept {
        if (rebound) return ShipCheck::second;
        std::uint16_t cls = 0, marker = 0;
        if (!player || (player & 3) || !p.kind(player, cls, marker)) return ShipCheck::unreadable;
        if (cls != ship_class || marker != alive_marker) return ShipCheck::not_ship;
        if (!p.object_id(player, out.new_ship_id)) out.new_ship_id = 0; // record only
        if (!p.script(player, out.new_script_id)) {
            out.new_script_id = 0;
            return ShipCheck::unreadable;
        }
        if (!script_id || out.new_script_id != script_id) return ShipCheck::identity;
        std::uint32_t sec = 0, id = 0;
        if (!p.parent(player, sec) || !sec || (sec & 3)) return ShipCheck::no_sector;
        out.new_sector = sec;
        if (!p.kind(sec, cls, marker) || cls != sector_class || marker != alive_marker || !p.sector_id(sec, id) || !id)
            return ShipCheck::no_sector;
        out.new_sector_id = id;
        if (id == sector_id) return ShipCheck::same_sector; // ids are unique within a session (monotonic)
        out.new_flags_readable = p.flags(player, out.new_flags); // record only (the autopilot bit)
        if (!out.new_flags_readable) out.new_flags = 0;
        return ShipCheck::rebound;
    }
    Report base(const Clock& now) const noexcept {
        Report r;
        r.frame = now.frame;
        r.ship = ship;
        r.ship_id = ship_id;
        r.script_id = script_id;
        r.sector = sector;
        r.sector_id = sector_id;
        r.ready_sector = ready_sector;
        r.skipped = skipped;
        if (phase != Phase::idle) {
            r.frames_since_arm = now.frame - arm_frame;
            r.ms_since_arm = now.ms - arm_ms;
        }
        return r;
    }
    void cancel(Report& r, unsigned reason, const Clock& now) noexcept {
        r.row = Row::cancelled;
        r.reason = reason;
        r.frames_since_arm = now.frame - arm_frame;
        r.ms_since_arm = now.ms - arm_ms;
        ++cancels[reason < cancel_count ? reason : 0];
        phase = Phase::idle;
    }
    Report& refuse(Report& r, unsigned reason) noexcept {
        r.row = Row::refused;
        r.reason = reason;
        ++refusals[reason < refuse_count ? reason : 0];
        return r;
    }
};
// Record only: the current player ship's speeds into an armed, skipped, rebound, cancelled, restored or restore_failed
// row (the next flight tells whether the speed is already 0 at the gate stop or zeroed after the arrival). Unreadable
// leaves speed_readable false. Called by the caller after the transition, never by the state machine.
template <class Probe> void record_speed(Report& r, Probe& p) noexcept {
    if (r.row == Row::none || r.row == Row::refused) return;
    std::uint32_t s = 0;
    if (!p.player(s) || !s || (s & 3)) return;
    r.speed_ship = s;
    r.speed_readable = p.speed(s, r.speed, r.desired_speed);
    if (!r.speed_readable) r.speed = r.desired_speed = 0;
}
}
