#pragma once
#include <cstdint>
#include "../../proxy/music_alert_core.h"
#include "code_window_fl.h"

// FL edition of the combat-music trigger's site and bindings (the AP twin is the site part of
// src/proxy/music_alert_core.h, whose engagement policy core::frame, its State, walk, relation and hold rules are
// shared unchanged). Facts: docs/reverse-engineering/fl/music-alert.md sections 1 to 5 and "Port (2026-11-03)";
// verifier verification/probe/verify_music_alert_sites_fl.py. Every value is an RVA added to x3m::fl::module_base().
//
// Alert routine 0x02f910..0x02fbe7 (ECX = registry, plain RET; its only caller the registry update 0x01e550 at
// 0x01e55b, whose only caller is the main loop at 0x0782ca). Seam:
//   02facf  0f bf c7 / 3b 45 08 / 7e 03 / 89 45 08    movsx eax,di; cmp eax,[ebp+8]; jle 02fada; mov [ebp+8],eax
//   02fada  8b 4d 08                                 mov ecx,[ebp+8]     <- claimed (6 bytes, two whole
//   02fadd  8b 45 04                                 mov eax,[ebp+4]        instructions, no relocation)
//   02fae0  3b c8                                    cmp ecx,eax         ; the claim tail resumes here
// EBP = R (the cockpit registry, always, on every path to the seam), EBX = P (the player ship or 0); EAX/ECX are
// rewritten by the replay and EFLAGS by the CMP; ESI/EDI/EBX/EBP and ESP untouched by the stub. The only raw edge into
// the span is the JLE at 0x02fad5 onto its first byte. The five-byte jump at 0x02fada lies in one aligned qword.
//
// Bindings for the shared policy: R = *(base+0x1a82e4) (passed in EBP; [R+4] committed, [R+8] this frame's level),
// M = *(base+0x1a82ec) ([M+0x38] the player, counters +0x40..+0x4c as AP), cfg = *(base+0x1a83b0) with the frame step
// +0x704, the game clock +0x708 (TI_GetAbsTime) and the SETA operands +0xbc/+0xc0: each exactly 0x10 below AP's
// +0x714/+0x718/+0xcc/+0xd0, so the adapter hands core::frame the cfg pointer minus 0x10 (core::frame reads cfg only
// at those four offsets). Every object, list, cockpit and counter offset is AP's (music-alert.md section 2).
namespace x3m::fl::music_alert_fl {
namespace core = x3m::music_alert::core;
constexpr std::uint32_t alert_rva = 0x02f910, alert_end_rva = 0x02fbe7;
constexpr std::uint32_t site_rva = 0x02fada, continue_rva = 0x02fae0, site_branch_rva = 0x02fad5;
constexpr unsigned site_length = 6;
constexpr unsigned char site_bytes[site_length] = {0x8b, 0x4d, 0x08, 0x8b, 0x45, 0x04};
constexpr std::uint32_t registry_update_rva = 0x01e550, alert_call_rva = 0x01e55b;
constexpr std::uint32_t main_loop_call_rva = 0x0782ca;
constexpr std::uint32_t registry_slot_rva = 0x1a82e4, root_slot_rva = 0x1a82ec, cfg_slot_rva = 0x1a83b0;
constexpr unsigned cfg_step = 0x704, cfg_clock = 0x708, cfg_seta_a = 0xbc, cfg_seta_b = 0xc0;
constexpr std::uint32_t cfg_bias = core::cfg_step - cfg_step;
static_assert(core::cfg_step - cfg_bias == cfg_step && core::cfg_clock - cfg_bias == cfg_clock &&
                  core::cfg_seta_a - cfg_bias == cfg_seta_a && core::cfg_seta_b - cfg_bias == cfg_seta_b,
              "the four FL clock fields sit one common offset below AP's");
static_assert(cfg_bias == 0x10, "the measured FL cfg offset");
}
