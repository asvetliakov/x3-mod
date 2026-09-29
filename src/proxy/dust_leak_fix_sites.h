#pragma once
#include <cstdint>
#include <cstring>

// Portable core of the dust-scene leak fix (docs/reverse-engineering/
// object-lifetimes.md, "Run383: the 27-nodes-per-frame creator" and its
// "Patch" subsection): the verified window at the end of the fill loop of the
// dust-scene update 0x0041efc0, the claimed span, the release-and-leave stub
// and the X3M_DUST_LEAK_FIX parser. No Windows dependency so the host test and
// the site verifier compile it directly.
//
// The fill loop 0x0041f328..0x0041f4dc allocates a node (0x00486d10, appended
// to the render manager's unattached list R+0x28 and inserted into R+0xc) and
// validates the dust body afterwards. Three branches skip the bind and the
// attach and land on the loop's common tail with the node still in EDI:
// 0x0041f3d1 (jl, negative id), 0x0041f436 (jne, the body slot's load-failed
// flag) and 0x0041f447 (je, the body load returned 0). The node is then dropped
// and stays on R+0x28 for ever (model id -1); the next iteration and the next
// frame repeat it. The fourth source of the tail, 0x0041f336 (je), arrives with
// EDI = 0 (allocation failure).
//
// 0041f4b7  8d 77 40                lea  esi,[edi+0x40]                <- window (37 bytes)
// 0041f4ba  e8 b1 0d 0d 00          call 0x004f0270                     ; random roll into +0x40
// 0041f4bf  8b 44 24 6c             mov  eax,[esp+0x6c]                 ; arg1 (dust camera), after three pushes
// 0041f4c3  8b 48 1c                mov  ecx,[eax+0x1c]                 ; the dust scene
// 0041f4c6  83 c4 0c                add  esp,0xc
// 0041f4c9  51                      push ecx
// 0041f4ca  8b c7                   mov  eax,edi
// 0041f4cc  e8 cf a8 06 00          call 0x00489da0                     ; attach: unlinks from R+0x28, +0x18 = 0, +0x1c = scene
// 0041f4d1  83 6c 24 20 01          sub  dword [esp+0x20],1             <- site (five bytes, whole instruction)
// 0041f4d6  0f 85 4c fe ff ff       jne  0x0041f328                     ; loop while nodes remain to be filled
// 0041f4dc  8b 4c 24 64             mov  ecx,[esp+0x64]                 ; the loop exit (window end)
//
// The claim displaces the SUB into the engine_patch tail (one whole
// instruction, no relative branch; the five bytes lie in the aligned qword
// 0x0041f4d0, one lock cmpxchg8b) and the stub runs first, with the loop
// tail's register state: EDI = the node or 0, ESP = the function's frame
// (`sub esp,0x4c` plus four pushes, [ESP+0x20] = nodes still to fill). When
// EDI != 0 and node+0x1c == 0 (never attached: the three failure branches; an
// attached node carries the dust scene there) the stub calls the engine's own
// node release 0x00487be0 (cdecl; what the render-manager clear 0x00470f50
// runs per R+0x28 node at 0x00471050: unlink from the list the node is on,
// Remove from R+0xc by id, free the 0x270 block) with EAX/ECX/EDX saved
// around it, sets [ESP+0x20] = 1 and counts the hit; the displaced SUB then
// makes the counter 0, the JNE falls through and the function leaves the loop
// through its own exit 0x0041f4dc (at most one failed attempt per frame). On
// every other arrival (EDI = 0, or an attached node) the stub changes nothing.
// The flags the stub writes are dead: the SUB in the tail rewrites them before
// the JNE reads them. EAX, ECX and EDX are restored by the pops (they are dead
// at 0x0041f4dc anyway: every register is written before it is read there);
// EBX, ESI, EDI and EBP are callee-saved through 0x00487be0 (push/pop pairs,
// `mov esp,ebp`); ESP is balanced. [ESP+0x20] is read only by the site's SUB
// after the loop (verify_dust_leak_fix_site.py checks the tail 0x0041f4dc..
// 0x0041f658 for ESP-relative reads of that slot), so the epilogue (pop edi/
// esi/ebp/ebx, add esp,0x4c, ret 8 at 0x0041f658) is unchanged. The release
// routine is what the engine runs for the same nodes on a game-state teardown;
// a never-bound node satisfies every branch it takes (+0x1a8/+0x170/+0x1f4/
// +0x1c/+0x130/+0x20/+0x24 are 0 from the constructor's memset, the child
// list +0xc is empty, +0/+4 link it into R+0x28). It calls the allocator's free
// and nothing that reads LastError; the engine's own loop already called
// malloc/memset in the same iteration, so LastError is not live at the site.
namespace x3m::dust_leak_fix::sites {
constexpr std::uintptr_t function_va = 0x0041efc0, function_end_va = 0x0041f65b; // ret 8 at 0x0041f658
constexpr std::uintptr_t loop_va = 0x0041f328, window_va = 0x0041f4b7, site_va = 0x0041f4d1, jne_va = 0x0041f4d6;
constexpr std::uintptr_t exit_va = 0x0041f4dc, ret_va = 0x0041f658, attach_va = 0x00489da0;
constexpr std::uintptr_t release_va = 0x00487be0; // node release (cdecl, node on the stack, ret at 0x00487e23)
constexpr std::uintptr_t clear_call_va = 0x00471050; // the render-manager clear's per-node `push esi; call 0x00487be0`
constexpr std::uintptr_t failure_sources[4] = {0x0041f336, 0x0041f3d1, 0x0041f436, 0x0041f447}; // branches to the site
constexpr unsigned window_length = 37, site_offset = 0x1a, site_length = 5;
constexpr unsigned char expected_window[window_length] = {
    0x8d, 0x77, 0x40, 0xe8, 0xb1, 0x0d, 0x0d, 0x00, 0x8b, 0x44, 0x24, 0x6c, 0x8b, 0x48, 0x1c, 0x83, 0xc4, 0x0c, 0x51,
    0x8b, 0xc7, 0xe8, 0xcf, 0xa8, 0x06, 0x00, 0x83, 0x6c, 0x24, 0x20, 0x01, 0x0f, 0x85, 0x4c, 0xfe, 0xff, 0xff};
constexpr unsigned char expected_site[site_length] = {0x83, 0x6c, 0x24, 0x20, 0x01}; // SUB dword [ESP+0x20],1
constexpr std::uint32_t scene_offset = 0x1c, model_offset = 0x140, remaining_slot = 0x20, node_size = 0x270;

// The stub pushed in front of the chain: 45 bytes with three fields filled at
// emission (the rel32 of the release call, the abs32 of the hit counter and
// of the continuation slot; both words live in the arena after the stub).
//   +00 85 ff                      TEST EDI,EDI
//   +02 74 23                      JE   +0x27          ; no node (allocation failed): unchanged
//   +04 83 7f 1c 00                CMP  dword [EDI+0x1c],0
//   +08 75 1d                      JNE  +0x27          ; attached (the dust scene): unchanged
//   +0a 50 51 52                   PUSH EAX; PUSH ECX; PUSH EDX
//   +0d 57                         PUSH EDI            ; the node (cdecl argument)
//   +0e e8 <rel32>                 CALL 0x00487be0     ; unlink, Remove, free
//   +13 83 c4 04                   ADD  ESP,4
//   +16 5a 59 58                   POP  EDX; POP ECX; POP EAX
//   +19 c7 44 24 20 01 00 00 00    MOV  dword [ESP+0x20],1  ; the displaced SUB makes it 0: JNE falls through
//   +21 ff 05 <abs32>              INC  dword [hits]
//   +27 ff 25 <abs32>              JMP  [slot]         ; tail: SUB; JMP 0x0041f4d6
constexpr unsigned stub_code_length = 45, stub_length = 45;
constexpr unsigned call_rel32_offset = 0x0f, hits_abs32_offset = 0x23, slot_abs32_offset = 0x29;
constexpr unsigned char stub_code[stub_code_length] = {
    0x85, 0xff, 0x74, 0x23, 0x83, 0x7f, 0x1c, 0x00, 0x75, 0x1d, 0x50, 0x51, 0x52, 0x57, 0xe8,
    0x00, 0x00, 0x00, 0x00, 0x83, 0xc4, 0x04, 0x5a, 0x59, 0x58, 0xc7, 0x44, 0x24, 0x20, 0x01,
    0x00, 0x00, 0x00, 0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 0xff, 0x25, 0x00, 0x00, 0x00, 0x00};
inline void put32(unsigned char* at, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) at[i] = static_cast<unsigned char>(v >> (8 * i));
}
// The stub as emitted at `stub_at`: the call's rel32 reaches `release` from the
// byte after the field, the two abs32 name the counter and the slot.
inline void encode_stub(std::uint32_t stub_at, std::uint32_t release, std::uint32_t hits, std::uint32_t slot,
                        unsigned char out[stub_length]) {
    std::memcpy(out, stub_code, stub_code_length);
    put32(out + call_rel32_offset, release - (stub_at + call_rel32_offset + 4));
    put32(out + hits_abs32_offset, hits);
    put32(out + slot_abs32_offset, slot);
}
// The host model of one arrival at the site (the Wine fixture executes the
// bytes): whether the stub releases the node and leaves the loop.
inline bool detours(std::uint32_t node, std::uint32_t node_scene) {
    return node != 0 && node_scene == 0;
}

// X3M_DUST_LEAK_FIX: on = the stub (the DLL default when the variable is
// unset; the launcher sends on), off = the engine's bytes.
enum class Mode : unsigned char { off = 0, on = 1 };
constexpr Mode default_mode = Mode::on;
constexpr unsigned setting_capacity = 32; // 1..31 characters; 32 or more is too_long
inline const char* mode_name(Mode m) {
    return m == Mode::on ? "on" : "off";
}
// Exactly "on" or "off" (lower case, nothing else).
template <class Char> inline bool parse_mode(const Char* text, Mode* out) {
    if (!text) return false;
    auto equals = [text](const char* word) {
        unsigned i = 0;
        for (; word[i]; ++i)
            if (text[i] != Char(word[i])) return false;
        return text[i] == Char(0);
    };
    if (equals("on")) {
        *out = Mode::on;
        return true;
    }
    if (equals("off")) {
        *out = Mode::off;
        return true;
    }
    return false;
}
// The install decision on the bytes read at the window: nullptr when the
// window is exactly the engine's, else the refusal reason (a patched or
// otherwise changed window is refused).
inline const char* plan(const unsigned char current[window_length]) {
    return std::memcmp(current, expected_window, window_length) ? "bytes_mismatch" : nullptr;
}
// The engine_patch claim of the span (one whole instruction, no relative branch; ret_pop names the
// function's `ret 8`, informational).
struct ClaimSpec {
    const char* name;
    std::uintptr_t address;
    unsigned char expected[site_length];
    unsigned length, ret_pop, rel32_offset;
};
constexpr ClaimSpec claim_spec = {"dust_fill_failed_body", 0x0041f4d1, {0x83, 0x6c, 0x24, 0x20, 0x01}, 5, 8, 0};

constexpr bool window_holds_site() {
    for (unsigned i = 0; i < site_length; ++i)
        if (expected_window[site_offset + i] != expected_site[i] || claim_spec.expected[i] != expected_site[i])
            return false;
    return true;
}
static_assert(window_va + site_offset == site_va && site_va + site_length == jne_va && jne_va + 6 == exit_va &&
                  window_va + window_length == exit_va,
              "offsets");
static_assert(window_holds_site() && claim_spec.address == site_va && claim_spec.length == site_length,
              "the window carries the claimed bytes");
static_assert((site_va & ~std::uintptr_t(7)) == ((site_va + 4) & ~std::uintptr_t(7)),
              "the five patch bytes lie in one aligned 8-byte word (atomic write)");
static_assert(jne_va + 6 - 0x1b4 == loop_va && expected_window[window_length - 4] == 0x4c &&
                  expected_window[window_length - 3] == 0xfe,
              "JNE rel32 -0x1b4 reaches the loop head");
static_assert(expected_site[3] == remaining_slot, "the SUB decrements the nodes-to-fill slot");
static_assert(function_va < loop_va && loop_va < window_va && exit_va < ret_va && ret_va < function_end_va,
              "inside the dust-scene update");
static_assert(stub_code[2] == 0x74 && 4 + stub_code[3] == slot_abs32_offset - 2 && stub_code[8] == 0x75 &&
                  10 + stub_code[9] == slot_abs32_offset - 2,
              "both skips reach the JMP [slot]");
static_assert(stub_code[call_rel32_offset - 1] == 0xe8 && stub_code[hits_abs32_offset - 2] == 0xff &&
                  stub_code[hits_abs32_offset - 1] == 0x05 && stub_code[slot_abs32_offset - 2] == 0xff &&
                  stub_code[slot_abs32_offset - 1] == 0x25 && slot_abs32_offset + 4 == stub_code_length,
              "field positions");
static_assert(stub_code[0x1c] == remaining_slot && stub_code[6] == scene_offset, "the stub's slot and scene offsets");
}
