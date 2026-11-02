#pragma once
#include <cstdint>

// Portable policy of the chase cursor-fire admission (src/proxy/chase_fire.cpp for X3AP.exe,
// src/engine/fl/chase_fire_fl.cpp for X3FL.exe). The editions differ only in where the engine values live; the
// decision over them is this one. No Windows dependency (the native fixtures compile it).
//
// The witness is the last successfully written applied bound chase pose of the player's cockpit (camera update QPC,
// thread). A native fire call whose cursor-active compare found the cursor inactive is admitted to the native cursor
// ray only when: the witness is fresh (same thread, nonzero monotonic QPC, at most two seconds old), the registry's
// active cockpit and the fire call's ship are the witness's, the cockpit still shows that ship through that camera in
// external chase mode 258 with connect 0 and flag 4 clear, and the real native cursor lies on screen and inside the
// camera's normalized viewport. Nothing here writes engine state; the caller only clears the saved ZF.
namespace x3m::chase_fire::core {
struct Context {
    std::uintptr_t cockpit = 0, ship = 0, camera = 0;
    std::uint32_t ship_id = 0, thread = 0;
    std::uint64_t qpc = 0;
};
enum class Result : unsigned { Override, Stale, Identity, View, Cursor, Read, Count };
constexpr std::uint32_t zero_flag = 0x40;      // saved EFLAGS ZF: the native JZ/JE is taken when set
constexpr std::uint32_t main_group = 0;        // the fire call's gun group (native main group)
constexpr std::uint32_t cursor_flags = 0x22;   // the fire call's flags: both bits select the cursor ray natively
constexpr std::uint32_t chase_mode = 258;      // cockpit+0x150 external chase view
// The fire call itself is refused unless it is the main group with both cursor flag bits. (The AP handler spells this
// predicate in place: as an inline call GCC lays out its blocks differently, and the AP object stays identical.)
inline bool call_refused(std::uint32_t group, std::uint32_t flags) {
    return group != main_group || (flags & cursor_flags) != cursor_flags;
}
// The applied pose a camera update may publish as the witness.
inline bool publishable(bool applied, std::uint32_t mode, std::uint32_t connect, std::uint32_t flags,
                        std::uint64_t stamp, std::uintptr_t cockpit, std::uintptr_t ship, std::uintptr_t camera) {
    return applied && mode == chase_mode && connect == 0 && !(flags & 4) && stamp && cockpit && ship && camera &&
           !((cockpit | ship | camera) & 3);
}
// The native cursor in canonical order {active, x, y} against the signed-16 screen and the camera's normalized 16.16
// viewport (y0, y1, x0, x1, the native unprojection's order). The screen bound and x/y checks keep the products below
// INT32_MAX; native-style integer division needs no wide CRT helper.
inline Result cursor_result(const std::int32_t cursor[3], const std::int32_t viewport[4],
                            const std::int16_t dimensions[2]) {
    if (cursor[0] != 0 || dimensions[0] <= 0 || dimensions[1] <= 0 || viewport[0] < 0 || viewport[0] >= viewport[1] ||
        viewport[1] > 65536 || viewport[2] < 0 || viewport[2] >= viewport[3] || viewport[3] > 65536 || cursor[1] < 0 ||
        cursor[1] >= dimensions[0] || cursor[2] < 0 || cursor[2] >= dimensions[1])
        return Result::Cursor;
    const auto x = cursor[1] * 65536 / dimensions[0];
    const auto y = cursor[2] * 65536 / dimensions[1];
    if (x < viewport[2] || x > viewport[3] || y < viewport[0] || y > viewport[1]) return Result::Cursor;
    return Result::Override;
}
// The decision. Env supplies the edition's reads:
//   bool active_cockpit(std::uintptr_t&)       the registry's active control cockpit (bounded walk)
//   bool field(std::uintptr_t, unsigned, T&)   a bounded aligned engine read
//   bool cursor(std::int32_t[3])               the native cursor cells, canonical {active, x, y}
//   std::uintptr_t defaults_slot()             the address of the default view/screen slot
//   std::uint32_t thread()                     the current thread id
//   std::uint64_t frequency()                  the QPC frequency (ticks per second)
template <class Env>
Result decide(const Context& witness, std::uint32_t ship, std::uint64_t stamp, Env& env) {
    if (!witness.qpc || !stamp || stamp < witness.qpc || stamp - witness.qpc > env.frequency() * 2 ||
        witness.thread != env.thread())
        return Result::Stale;
    std::uintptr_t cockpit = 0;
    if (!env.active_cockpit(cockpit)) return Result::Read;
    if (cockpit != witness.cockpit || ship != witness.ship) return Result::Identity;
    std::uint32_t refs[2]{}, camera = 0, id = 0, mode = 0, connect = 0, flags = 0;
    if (!env.field(cockpit, 0xc, refs) || !env.field(cockpit, 0x58, camera) || !env.field(ship, 8, id) ||
        !env.field(cockpit, 0x150, mode) || !env.field(cockpit, 0x1c0, connect) || !env.field(cockpit, 0x1a0, flags))
        return Result::Read;
    if (refs[0] != ship || refs[1] != ship || camera != witness.camera || id != witness.ship_id)
        return Result::Identity;
    if (mode != chase_mode || connect != 0 || (flags & 4)) return Result::View;
    // The default screen record is one dereference behind the defaults slot (+4/+6 signed width/height); the
    // default view-plane block itself is not the screen structure.
    std::int32_t cursor[3]{}, viewport[4]{};
    std::uint32_t defaults = 0, screen = 0;
    std::int16_t dimensions[2]{};
    if (!env.cursor(cursor) || !env.field(camera, 0x288, viewport) || !env.field(env.defaults_slot(), 0, defaults) ||
        !env.field(defaults, 0, screen) || !env.field(screen, 4, dimensions))
        return Result::Read;
    return cursor_result(cursor, viewport, dimensions);
}
}
