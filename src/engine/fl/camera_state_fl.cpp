// FL edition of x3m::camera_state (src/proxy/camera_state.h): the engine's current view and
// perspective matrices of X3FL.exe, read through the renderer's two pointer slots
// (docs/architecture/farnhams-legacy-camera.md; candidate addresses from
// docs/reverse-engineering/fl/camera-buffers.md). Read-only: no patch, no hook. Every address is
// fl::module_base() + RVA (the image is ASLR-relocatable); the slots are admitted only after the
// two publication sites verify as whole instructions with the relocated operand recomputed for the
// live base. Same gate, failure codes, pointer cache and LastError discipline as the AP file, plus
// one fl.camera row at initialize and one fl.camera_sample row per X3M_FL_CAMERA_LOG seconds.
#include "../../proxy/cull_small_parts_core.h"
#include "../../proxy/config.h"
#include "../../proxy/camera_state.h"
#include "camera_sample_sanity.h"
#include "executable_identity_fl.h"
#include <windows.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "Verified x86 image layout only");
namespace x3m::camera_state {
namespace {
struct CameraAnchor {
    std::uint32_t site_rva, slot_rva, operand_offset, length;
    std::uint8_t bytes[17];
};
#include "camera_state_fl_inc.h"
static_assert(view_anchor.length <= sizeof view_anchor.bytes && projection_anchor.length <= sizeof projection_anchor.bytes,
              "anchor span fits its byte array");
static_assert(view_anchor.operand_offset + 4 <= view_anchor.length && projection_anchor.operand_offset + 4 <= projection_anchor.length,
              "relocated operand inside the span");
static_assert(view_anchor.slot_rva != orthographic_slot_rva && projection_anchor.slot_rva != orthographic_slot_rva,
              "the orthographic slot is never read");
// Slot addresses (each holds a pointer to a 64-byte matrix buffer), module_base() + RVA.
uintptr_t projection_slot = 0, view_slot = 0;
bool active = false;
bool extra_consumer = false; // request_consumer(): an option the DLL's parser accepted
const char* state = "disabled";
const char* logged_state = nullptr; // the status the fl.camera row last reported
struct Cache {
    uintptr_t pointer = 0;
    bool valid = false;
};
Cache caches[2];
// The periodic diagnostic rows: QPC checked once per read outside a burst; when the period elapses the
// next two consecutive reads are logged as k=0 and k=1 (under the current motion_output order the
// frame's first read is the background view at the latching Clear, the second the scene view at the
// depth-only Clear, so one row alone would nearly always show the background camera).
struct Diagnostic {
    long long period = 0; // QPC ticks between bursts; 0 off
    long long due = 0;
    unsigned long reads = 0; // reads since the previous k=0 row
    unsigned burst = 0;      // rows left in the current burst
    bool have_previous[2]{};
    float previous[2][32]{}; // per k: a scene row compares with the previous scene row
} diagnostic;
bool readable(uintptr_t address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof info) != sizeof info)
        return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const DWORD readable_protection = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                      PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
    if (!(info.Protect & readable_protection)) return false;
    const uintptr_t region_end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    return address + size > address && address + size <= region_end;
}
// Fault-free bounded read of this process (documented ReadProcessMemory on the current process).
bool read_self(uintptr_t address, void* out, size_t size) {
    SIZE_T done = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &done) &&
           done == size;
}
// The publication site at base + site_rva holds the anchored instructions with the store's disp32
// equal to base + slot_rva (the loader applied the HIGHLOW relocation for the live base).
bool anchor_matches(uintptr_t base, const CameraAnchor& anchor) {
    unsigned char actual[sizeof anchor.bytes]{};
    if (!read_self(base + anchor.site_rva, actual, anchor.length)) return false;
    std::uint32_t operand = 0;
    std::memcpy(&operand, actual + anchor.operand_offset, sizeof operand);
    if (operand != base + anchor.slot_rva) return false;
    std::memset(actual + anchor.operand_offset, 0, 4);
    return std::memcmp(actual, anchor.bytes, anchor.length) == 0;
}
// Reads one 64-byte buffer through its slot: the slot was validated at
// initialize; the buffer pointer is re-validated whenever its value changes.
bool read_buffer(unsigned index, uintptr_t slot, float* out, uintptr_t* address, std::uint32_t* failure) {
    uintptr_t pointer = 0;
    std::memcpy(&pointer, reinterpret_cast<const void*>(slot), sizeof pointer);
    *address = pointer;
    if (!pointer) {
        *failure = NullPointer;
        return false;
    }
    Cache& cache = caches[index];
    if (cache.pointer != pointer || !cache.valid) {
        cache.pointer = pointer;
        cache.valid = readable(pointer, 64);
    }
    if (!cache.valid) {
        *failure = Unreadable;
        return false;
    }
    std::memcpy(out, reinterpret_cast<const void*>(pointer), 64);
    return true;
}
// X3M_FL_CAMERA_LOG: seconds between fl.camera_sample rows, 0 off, unset 10. Digits only, else the default.
unsigned log_seconds() {
    wchar_t text[8]{};
    const DWORD length = x3m::config::get(L"X3M_FL_CAMERA_LOG", text, 8);
    if (length == 0 || length >= 8) return 10;
    unsigned value = 0;
    for (DWORD i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return 10;
        value = value * 10 + unsigned(text[i] - L'0');
    }
    return value > 3600 ? 3600 : value;
}
void arm_diagnostic() {
    diagnostic = Diagnostic{};
    const unsigned seconds = log_seconds();
    LARGE_INTEGER frequency{};
    if (seconds && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
        diagnostic.period = frequency.QuadPart * static_cast<long long>(seconds);
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        diagnostic.due = now.QuadPart; // the first read logs
    }
}
unsigned long slot_value(uintptr_t slot) {
    uintptr_t pointer = 0;
    if (slot) std::memcpy(&pointer, reinterpret_cast<const void*>(slot), sizeof pointer);
    return static_cast<unsigned long>(pointer);
}
void log_status(uintptr_t base) {
    if (logged_state == state) return;
    logged_state = state;
    log("fl.camera status=%s base=0x%08lx view_slot=0x%08lx proj_slot=0x%08lx view_buffer=0x%08lx proj_buffer=0x%08lx log_s=%u",
        state, static_cast<unsigned long>(base), static_cast<unsigned long>(view_slot),
        static_cast<unsigned long>(projection_slot), slot_value(view_slot), slot_value(projection_slot),
        log_seconds());
}
// Two rows (k=0, k=1) every period: enough to tell a live perspective scene camera from a wrong slot by
// the log alone. changed= compares with the previous row of the same k, ignoring P[10]/P[14].
void log_sample(const Sample& sample) {
    if (!diagnostic.period) return;
    ++diagnostic.reads;
    if (!diagnostic.burst) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        if (now.QuadPart < diagnostic.due) return;
        diagnostic.due = now.QuadPart + diagnostic.period;
        diagnostic.burst = 2;
    }
    const unsigned k = 2 - diagnostic.burst;
    --diagnostic.burst;
    fl::camera_sanity::Summary s;
    fl::camera_sanity::summarize(sample.projection_raw, sample.view_raw, s);
    float* previous = diagnostic.previous[k];
    const bool changed = diagnostic.have_previous[k] &&
                         fl::camera_sanity::changed(sample.projection_raw, sample.view_raw, previous);
    fl::camera_sanity::remember(sample.projection_raw, sample.view_raw, previous);
    log("fl.camera_sample k=%u status=%s read_failure=%lu failure=%lu reads=%lu proj=0x%08lx view=0x%08lx shape=%s"
        " finite=%u m00=%.7g m11=%.7g m22=%.9g m23=%.7g m32=%.9g aspect=%.4f ortho_dot=%.3g ortho_len=%.3g t_len=%.7g"
        " changed=%u",
        k, state, static_cast<unsigned long>(sample.read_failure), static_cast<unsigned long>(sample.failure),
        diagnostic.reads, static_cast<unsigned long>(sample.projection), static_cast<unsigned long>(sample.view),
        fl::camera_sanity::shape_name(s.shape), s.finite, double(sample.projection_raw[0]),
        double(sample.projection_raw[5]), double(sample.projection_raw[10]), double(sample.projection_raw[11]),
        double(sample.projection_raw[14]), s.aspect, s.ortho_dot_max, s.ortho_len_err_max, s.translation_length,
        diagnostic.have_previous[k] ? unsigned(changed) : 0u);
    diagnostic.have_previous[k] = true;
    if (k == 0) diagnostic.reads = 0;
}
}
bool initialize() {
    if (active) return true;
    const DWORD error = GetLastError();
    wchar_t setting[4]{};
    // The same latch as the AP file: the route's temporal consumer, the shadow-replay switches, a consumer
    // armed through request_consumer, or the small-parts cull's P[0] read.
    const bool motion = x3m::config::get(L"X3M_MOTION_OUTPUT", setting, 4) == 1 && setting[0] == L'1';
    const bool consumer = (x3m::config::get(L"X3M_TAA", setting, 4) == 1 && setting[0] == L'1') ||
                          (x3m::config::get(L"X3M_SHADOW_REPLAY_CANDIDATES", setting, 4) == 1 && setting[0] == L'1') ||
                          (x3m::config::get(L"X3M_SHADOW_REPLAY_DEPTH", setting, 4) == 1 && setting[0] == L'1') ||
                          extra_consumer;
    wchar_t px[16]{};
    const DWORD px_length = x3m::config::get(L"X3M_CULL_SMALL_PARTS_PX", px, 16);
    char px_text[16]{};
    bool px_ascii = px_length >= 1 && px_length < 16;
    for (DWORD i = 0; px_ascii && i < px_length; ++i) {
        px_ascii = px[i] >= 0x21 && px[i] <= 0x7e;
        px_text[i] = static_cast<char>(px[i]);
    }
    double px_value = 0;
    const bool small_parts = px_ascii && cull_small_parts::core::parse_px(px_text, &px_value) &&
                             cull_small_parts::core::valid_px(px_value);
    const bool requested = (motion && consumer) || small_parts;
    if (!requested) {
        state = "disabled";
        log_status(0);
        SetLastError(error);
        return false;
    }
    if (!x3m::fl::verified()) {
        state = "executable_mismatch";
        log_status(0);
        SetLastError(error);
        return false;
    }
    const uintptr_t base = x3m::fl::module_base();
    if (!anchor_matches(base, view_anchor) || !anchor_matches(base, projection_anchor)) {
        state = "anchor_mismatch";
        log_status(base);
        SetLastError(error);
        return false;
    }
    projection_slot = base + projection_anchor.slot_rva;
    view_slot = base + view_anchor.slot_rva;
    if (!readable(projection_slot, 4) || !readable(view_slot, 4)) {
        state = "slots_unreadable";
        projection_slot = view_slot = 0;
        log_status(base);
        SetLastError(error);
        return false;
    }
    arm_diagnostic();
    active = true;
    state = "active";
    log_status(base);
    SetLastError(error);
    return true;
}
bool available() {
    return active;
}
const char* status() {
    return state;
}
bool request_consumer() {
    extra_consumer = true;
    return initialize();
}
void reset() {
    caches[0] = Cache{};
    caches[1] = Cache{};
}
bool read(Sample* out) {
    if (!out) return false;
    *out = Sample{};
    if (!active) {
        out->read_failure = Unavailable;
        return false;
    }
    const DWORD error = GetLastError();
    bool ok = read_buffer(0, projection_slot, out->projection_raw, &out->projection, &out->read_failure) &&
              read_buffer(1, view_slot, out->view_raw, &out->view, &out->read_failure);
    if (ok) {
        ok = renderer::camera_state_from_matrices(out->projection_raw, out->view_raw, out->state, &out->failure);
        if (!ok) out->read_failure = Invalid;
    }
    log_sample(*out);
    SetLastError(error);
    return ok;
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
void fixture_install(const float* const* projection, const float* const* view) {
    projection_slot = reinterpret_cast<uintptr_t>(projection);
    view_slot = reinterpret_cast<uintptr_t>(view);
    reset();
    active = projection_slot && view_slot;
    state = active ? "fixture" : "disabled";
    if (active) arm_diagnostic();
}
#endif
}
