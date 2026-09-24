#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#define WINAPI
using DWORD = uint32_t;
using UINT = uint32_t;
using ULONG = uint32_t;
using HRESULT = int32_t;
using HWND = void*;
using D3DDEVTYPE = uint32_t;

constexpr HRESULT S_OK = 0;
#ifndef FALSE
#define FALSE 0
#endif
constexpr HRESULT S_FALSE = 1;
constexpr HRESULT E_FAIL = static_cast<HRESULT>(0x80004005u);
constexpr HRESULT E_NOINTERFACE = static_cast<HRESULT>(0x80004002u);
constexpr HRESULT D3DERR_DEVICELOST = static_cast<HRESULT>(0x88760868u);
constexpr DWORD D3DCREATE_PUREDEVICE = 0x10u;
#define SUCCEEDED(value) ((value) >= 0)
#define FAILED(value) ((value) < 0)

struct D3DPRESENT_PARAMETERS {
    UINT BackBufferWidth{};
    UINT BackBufferHeight{};
    UINT BackBufferFormat{};
    UINT Windowed{};
    UINT MultiSampleType{};
    UINT PresentationInterval{};
    HWND hDeviceWindow{};
    UINT witness{};
};

struct IDirect3DDevice9 {
    virtual ULONG Release() = 0;
    virtual ~IDirect3DDevice9() = default;
};

struct IDirect3D9 {
    void* context{};
};

namespace test {
int scenarios;
int checks;
int failures;

void check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "line %d: %s\n", line, expression);
    }
}

void scenario() { ++scenarios; }
}  // namespace test

#define CHECK(expression) test::check((expression), #expression, __LINE__)

struct NativeDevice final : IDirect3DDevice9 {
    ULONG releases{};
    ULONG Release() override { return ++releases; }
};

namespace ownership_host {

enum class Output { Untouched, Null, Device };

struct NativeScript {
    HRESULT result{S_OK};
    Output output{Output::Device};
    NativeDevice* device{};
    int calls{};
    UINT adapter{};
    D3DDEVTYPE type{};
    HWND window{};
    DWORD flags{};
    D3DPRESENT_PARAMETERS* pp{};
    bool received_output{};
    bool mutate_pp{};
};

NativeScript native_script;

template <typename T>
T* untouched_output() {
    static unsigned char marker;
    return reinterpret_cast<T*>(&marker);
}

struct NativeFactory {
    HRESULT CreateDevice(UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
                         D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
        ++native_script.calls;
        native_script.adapter = adapter;
        native_script.type = type;
        native_script.window = window;
        native_script.flags = flags;
        native_script.pp = pp;
        native_script.received_output = out != nullptr;
        if (native_script.mutate_pp && pp) {
            pp->BackBufferWidth += 11;
            pp->witness = 0xcafeu;
        }
        if (out) {
            if (native_script.output == Output::Null) *out = nullptr;
            if (native_script.output == Output::Device) *out = native_script.device;
        }
        return native_script.result;
    }
};

struct Options {
    bool track_execution_state{};
};

struct Factory {
    NativeFactory* native_{};
    Options options{};
};

struct Execution {
    int calls{};
    bool value{};
    void initialize(bool requested) {
        ++calls;
        value = requested;
    }
};

struct Device final : IDirect3DDevice9 {
    Options options{};
    Execution execution{};
    ULONG releases{};
    ULONG Release() override { return ++releases; }
};

enum class Kind { Device };
constexpr int IID_IDirect3DDevice9 = 9;
constexpr int IID_IDirect3DDevice9Ex = 90;

struct AdoptScript {
    HRESULT result{S_OK};
    bool ex{};
    int calls{};
    Factory* parent{};
    IDirect3DDevice9* native{};
    int iid{};
    Options* options{};
    Device wrapped{};
    int finite_calls{};
    int copy_depth_calls{};
    D3DPRESENT_PARAMETERS requested{};
};

AdoptScript adopt_script;

bool has_ex(IDirect3DDevice9*, int iid) {
    CHECK(iid == IID_IDirect3DDevice9Ex);
    return adopt_script.ex;
}

HRESULT adopt(Factory* parent, Kind kind, IDirect3DDevice9* native, int iid,
              void** out, Options* options) {
    ++adopt_script.calls;
    adopt_script.parent = parent;
    adopt_script.native = native;
    adopt_script.iid = iid;
    adopt_script.options = options;
    CHECK(kind == Kind::Device);
    if (FAILED(adopt_script.result)) {
        if (out) *out = nullptr;
        return adopt_script.result;
    }
    adopt_script.wrapped.options = *options;
    *out = &adopt_script.wrapped;
    return adopt_script.result;
}

void initialize_finite(Device* device) {
    CHECK(device == &adopt_script.wrapped);
    ++adopt_script.finite_calls;
}

void initialize_copy_depth(Device* device, const D3DPRESENT_PARAMETERS& requested) {
    CHECK(device == &adopt_script.wrapped);
    ++adopt_script.copy_depth_calls;
    adopt_script.requested = requested;
}

#include "ownership_create_device_under_test_inc.h"

void reset(NativeDevice& native) {
    native_script = {};
    native_script.device = &native;
    adopt_script = {};
}

}  // namespace ownership_host

namespace capture_host {

std::vector<char> events;
std::recursive_mutex mutex;
unsigned capture_lock_depth;
struct LightCallBoundary { LightCallBoundary() {} ~LightCallBoundary() {} }; // mirrors capture.cpp: inert, non-trivial so the scoped variable is not "unused"
namespace cull_small_parts { inline void set_backbuffer_width(unsigned) {} } // X3M_CULL_SMALL_PARTS_PX pixel scale at CreateDevice (src/proxy/cull_small_parts.h); inert on the host
namespace window_mode { inline void apply(const char*, HWND, HWND, bool, UINT, UINT) {} } // X3M_WINDOW_MONITOR_RECT move at create_before (src/proxy/window_mode.h); inert on the host
namespace proxy_identity { inline void log_loaded_module(const wchar_t*) {} } // mirrors the loaded_module line (inert)
struct CpuCallBoundary {
    CpuCallBoundary() { events.push_back('C'); }
    void before_original() { events.push_back('B'); }
    void after_original() { events.push_back('A'); }
};

namespace ownership {
struct Monitor {};
Monitor monitor;
Monitor& process_admission_monitor() { return monitor; }
struct ApplicationAdmissionAbi {
    explicit ApplicationAdmissionAbi(Monitor&) { events.push_back('I'); }
};
}  // namespace ownership

struct HookGuard {
    std::unique_lock<std::recursive_mutex> lock{mutex};
    HookGuard() {
        ++capture_lock_depth;
        events.push_back('L');
    }
    ~HookGuard() {
        --capture_lock_depth;
        lock.unlock();
        events.push_back('U');
    }
};

namespace telemetry {
enum class Metric { CreateDevice };
using Clock = uint64_t;
Clock clock;
Clock now() { return ++clock; }
int process() { return 0; }
void record(int, Metric, Clock, bool) {}
}  // namespace telemetry

void log(const char*, ...) {}
void presentation_parameters(const char*, int, HWND, D3DPRESENT_PARAMETERS*) {}

bool motion_output_requested;

struct FactoryHooks {
    void* slots[17]{};
    template <typename Function>
    Function get(size_t index) const {
        return reinterpret_cast<Function>(slots[index]);
    }
};

std::map<IDirect3D9*, std::unique_ptr<FactoryHooks>> factories;

struct HookRecord {
    int calls{};
    IDirect3DDevice9* device{};
    HWND device_window{};
    HWND focus_window{};
    DWORD native_flags{};
};
HookRecord hook_record;

void hook_device(IDirect3DDevice9* device, HWND device_window, HWND focus_window) {
    CHECK(capture_lock_depth > 0);
    events.push_back('H');
    ++hook_record.calls;
    hook_record.device = device;
    hook_record.device_window = device_window;
    hook_record.focus_window = focus_window;
}

enum class Output { Untouched, Null, Device };
struct DirectScript {
    HRESULT result{S_OK};
    Output output{Output::Device};
    IDirect3DDevice9* device{};
    int calls{};
    IDirect3D9* d{};
    UINT adapter{};
    D3DDEVTYPE type{};
    HWND window{};
    DWORD flags{};
    D3DPRESENT_PARAMETERS* pp{};
    IDirect3DDevice9** out{};
    bool mutate_pp{};
    HWND mutated_window{};
};
DirectScript direct_script;

HRESULT WINAPI direct_native(IDirect3D9* d, UINT adapter, D3DDEVTYPE type, HWND window,
                             DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    events.push_back('N');
    ++direct_script.calls;
    direct_script.d = d;
    direct_script.adapter = adapter;
    direct_script.type = type;
    direct_script.window = window;
    direct_script.flags = flags;
    direct_script.pp = pp;
    direct_script.out = out;
    hook_record.native_flags = flags;
    if (direct_script.mutate_pp && pp) {
        pp->BackBufferHeight += 7;
        pp->hDeviceWindow = direct_script.mutated_window;
        pp->witness = 0xbeefu;
    }
    if (out) {
        if (direct_script.output == Output::Null) *out = nullptr;
        if (direct_script.output == Output::Device) *out = direct_script.device;
    }
    return direct_script.result;
}

HRESULT WINAPI ownership_bridge(IDirect3D9* d, UINT adapter, D3DDEVTYPE type, HWND window,
                                DWORD flags, D3DPRESENT_PARAMETERS* pp,
                                IDirect3DDevice9** out) {
    events.push_back('N');
    hook_record.native_flags = flags;
    return ownership_host::create_device(static_cast<ownership_host::Factory*>(d->context),
                                         adapter, type, window, flags, pp, out);
}

#include "capture_create_device_under_test_inc.h"

void reset(IDirect3D9& d, NativeDevice& device,
           HRESULT(WINAPI* callback)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                     D3DPRESENT_PARAMETERS*, IDirect3DDevice9**)) {
    events.clear();
    hook_record = {};
    direct_script = {};
    direct_script.device = &device;
    factories.clear();
    auto hooks = std::make_unique<FactoryHooks>();
    hooks->slots[16] = reinterpret_cast<void*>(callback);
    factories.emplace(&d, std::move(hooks));
}

}  // namespace capture_host

namespace {

constexpr UINT kAdapter = 3;
constexpr D3DDEVTYPE kType = 7;
HWND kFocus = reinterpret_cast<HWND>(uintptr_t{0x1234});
HWND kDeviceWindow = reinterpret_cast<HWND>(uintptr_t{0x5678});

D3DPRESENT_PARAMETERS parameters() {
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = 1920;
    pp.BackBufferHeight = 1080;
    pp.BackBufferFormat = 21;
    pp.Windowed = 1;
    pp.MultiSampleType = 4;
    pp.PresentationInterval = 2;
    pp.hDeviceWindow = kDeviceWindow;
    pp.witness = 99;
    return pp;
}

void capture_flags_case(IDirect3D9& factory, NativeDevice& device, DWORD requested,
                        bool route, DWORD expected) {
    test::scenario();
    capture_host::reset(factory, device, capture_host::direct_native);
    capture_host::motion_output_requested = route;
    auto pp = parameters();
    IDirect3DDevice9* out = nullptr;
    const HRESULT hr = capture_host::create_device(&factory, kAdapter, kType, kFocus,
                                                    requested, &pp, &out);
    CHECK(hr == S_OK);
    CHECK(capture_host::direct_script.calls == 1);
    CHECK(capture_host::direct_script.flags == expected);
    CHECK((requested ^ expected) == (route && (requested & D3DCREATE_PUREDEVICE)
                                         ? D3DCREATE_PUREDEVICE
                                         : 0u));
    CHECK(capture_host::direct_script.adapter == kAdapter);
    CHECK(capture_host::direct_script.type == kType);
    CHECK(capture_host::direct_script.window == kFocus);
    CHECK(capture_host::direct_script.pp == &pp);
    CHECK(capture_host::direct_script.out == &out);
    CHECK(out == &device);
    CHECK(capture_host::hook_record.calls == 1);
}

void capture_result_case(IDirect3D9& factory, NativeDevice& device, HRESULT result,
                         capture_host::Output behavior, bool provide_pp, bool provide_out,
                         int expected_hooks) {
    test::scenario();
    capture_host::reset(factory, device, capture_host::direct_native);
    capture_host::motion_output_requested = true;
    capture_host::direct_script.result = result;
    capture_host::direct_script.output = behavior;
    auto pp = parameters();
    IDirect3DDevice9* out = reinterpret_cast<IDirect3DDevice9*>(uintptr_t{0x4444});
    IDirect3DDevice9** out_arg = provide_out ? &out : nullptr;
    const HRESULT hr = capture_host::create_device(&factory, kAdapter, kType, kFocus,
                                                    0x52u, provide_pp ? &pp : nullptr, out_arg);
    CHECK(hr == result);
    CHECK(capture_host::direct_script.calls == 1);
    CHECK(capture_host::direct_script.flags == 0x42u);
    CHECK(capture_host::direct_script.pp == (provide_pp ? &pp : nullptr));
    CHECK(capture_host::direct_script.out == out_arg);
    CHECK(capture_host::hook_record.calls == expected_hooks);
    if (behavior == capture_host::Output::Untouched && provide_out)
        CHECK(out == reinterpret_cast<IDirect3DDevice9*>(uintptr_t{0x4444}));
    if (behavior == capture_host::Output::Null && provide_out) CHECK(out == nullptr);
    if (behavior == capture_host::Output::Device && provide_out) CHECK(out == &device);
    std::vector<char> expected_events{'C', 'I', 'L', 'B', 'N', 'A'};
    if (expected_hooks) expected_events.push_back('H');
    expected_events.push_back('U');
    CHECK(capture_host::events == expected_events);
    CHECK(capture_host::capture_lock_depth == 0);
}

void ownership_success_case(ownership_host::Factory& factory, NativeDevice& native,
                            HRESULT native_result) {
    test::scenario();
    ownership_host::reset(native);
    factory.options.track_execution_state = true;
    ownership_host::native_script.result = native_result;
    ownership_host::native_script.mutate_pp = true;
    auto pp = parameters();
    const auto requested = pp;
    IDirect3DDevice9* out = reinterpret_cast<IDirect3DDevice9*>(uintptr_t{0x5555});
    const HRESULT hr = ownership_host::create_device(&factory, kAdapter, kType, kFocus,
                                                      0x2468u, &pp, &out);
    CHECK(hr == native_result);
    CHECK(ownership_host::native_script.calls == 1);
    CHECK(ownership_host::native_script.flags == 0x2468u);
    CHECK(ownership_host::native_script.pp == &pp);
    CHECK(ownership_host::native_script.received_output);
    CHECK(pp.BackBufferWidth == requested.BackBufferWidth + 11);
    CHECK(pp.witness == 0xcafeu);
    CHECK(ownership_host::adopt_script.calls == 1);
    CHECK(ownership_host::adopt_script.parent == &factory);
    CHECK(ownership_host::adopt_script.native == &native);
    CHECK(ownership_host::adopt_script.iid == ownership_host::IID_IDirect3DDevice9);
    CHECK(ownership_host::adopt_script.options == &factory.options);
    CHECK(out == &ownership_host::adopt_script.wrapped);
    CHECK(ownership_host::adopt_script.wrapped.execution.calls == 1);
    CHECK(ownership_host::adopt_script.wrapped.execution.value);
    CHECK(ownership_host::adopt_script.finite_calls == 1);
    CHECK(ownership_host::adopt_script.copy_depth_calls == 1);
    CHECK(ownership_host::adopt_script.requested.BackBufferWidth == requested.BackBufferWidth);
    CHECK(ownership_host::adopt_script.requested.witness == requested.witness);
    CHECK(native.releases == 0);
}

}  // namespace

int main() {
    IDirect3D9 capture_factory{};
    NativeDevice direct_device{};

    capture_flags_case(capture_factory, direct_device, 0x52u, true, 0x42u);
    capture_flags_case(capture_factory, direct_device, 0x52u, false, 0x52u);
    capture_flags_case(capture_factory, direct_device, 0x0u, true, 0x0u);
    // FPU, multithread, all mutually invalid VP choices, window and high policy bits.
    constexpr DWORD preserved = 0x2u | 0x4u | 0x20u | 0x40u | 0x80u | 0x800u |
                                0x2000u | 0x4000u | 0x8000u | 0x10000000u;
    capture_flags_case(capture_factory, direct_device, preserved | 0x10u, true, preserved);

    capture_result_case(capture_factory, direct_device, S_FALSE, capture_host::Output::Device,
                        true, true, 1);
    capture_result_case(capture_factory, direct_device, E_FAIL, capture_host::Output::Device,
                        true, true, 0);
    capture_result_case(capture_factory, direct_device, D3DERR_DEVICELOST,
                        capture_host::Output::Device, true, true, 0);
    capture_result_case(capture_factory, direct_device, S_OK, capture_host::Output::Null,
                        true, true, 0);
    capture_result_case(capture_factory, direct_device, E_FAIL, capture_host::Output::Untouched,
                        true, true, 0);
    capture_result_case(capture_factory, direct_device, S_OK, capture_host::Output::Device,
                        false, false, 0);

    // The hook observes native parameter/output mutations and runs after the original.
    test::scenario();
    capture_host::reset(capture_factory, direct_device, capture_host::direct_native);
    capture_host::motion_output_requested = true;
    capture_host::direct_script.mutate_pp = true;
    capture_host::direct_script.mutated_window = reinterpret_cast<HWND>(uintptr_t{0x9999});
    auto mutated = parameters();
    IDirect3DDevice9* mutated_out = nullptr;
    CHECK(capture_host::create_device(&capture_factory, kAdapter, kType, kFocus, 0x10u,
                                      &mutated, &mutated_out) == S_OK);
    CHECK(capture_host::direct_script.calls == 1);
    CHECK(mutated.BackBufferHeight == 1087);
    CHECK(mutated.witness == 0xbeefu);
    CHECK(mutated_out == &direct_device);
    CHECK(capture_host::hook_record.calls == 1);
    CHECK(capture_host::hook_record.device_window == reinterpret_cast<HWND>(uintptr_t{0x9999}));
    CHECK(capture_host::hook_record.focus_window == kFocus);
    CHECK(capture_host::events.back() == 'U');
    CHECK(capture_host::events[capture_host::events.size()-2] == 'H');

    // Nested CreateDevice releases only its own recursive capture-lock scope;
    // the caller's existing scope remains held after device hooks are installed.
    test::scenario();
    capture_host::reset(capture_factory, direct_device, capture_host::direct_native);
    {
        capture_host::HookGuard outer;
        IDirect3DDevice9* nested_out = nullptr;
        CHECK(capture_host::create_device(&capture_factory, kAdapter, kType, kFocus,
                                          0, &mutated, &nested_out) == S_OK);
        CHECK(capture_host::hook_record.calls == 1);
        CHECK(nested_out == &direct_device);
        CHECK(capture_host::capture_lock_depth == 1);
    }
    CHECK(capture_host::capture_lock_depth == 0);

    ownership_host::NativeFactory native_factory{};
    ownership_host::Factory ownership_factory{&native_factory, {true}};
    NativeDevice owned_native{};
    ownership_success_case(ownership_factory, owned_native, S_OK);
    owned_native.releases = 0;
    ownership_success_case(ownership_factory, owned_native, S_FALSE);

    // Failure with a non-null native output is released and never adopted.
    test::scenario();
    ownership_host::reset(owned_native);
    ownership_host::native_script.result = E_FAIL;
    IDirect3DDevice9* failed_out = reinterpret_cast<IDirect3DDevice9*>(uintptr_t{0x7777});
    CHECK(ownership_host::create_device(&ownership_factory, kAdapter, kType, kFocus, 0x52u,
                                        nullptr, &failed_out) == E_FAIL);
    CHECK(ownership_host::native_script.calls == 1);
    CHECK(failed_out == nullptr);
    CHECK(owned_native.releases == 1);
    CHECK(ownership_host::adopt_script.calls == 0);

    // An untouched native output preserves the application's output, even on failure.
    test::scenario();
    owned_native.releases = 0;
    ownership_host::reset(owned_native);
    ownership_host::native_script.result = D3DERR_DEVICELOST;
    ownership_host::native_script.output = ownership_host::Output::Untouched;
    IDirect3DDevice9* untouched = reinterpret_cast<IDirect3DDevice9*>(uintptr_t{0x8888});
    CHECK(ownership_host::create_device(&ownership_factory, kAdapter, kType, kFocus, 0x52u,
                                        nullptr, &untouched) == D3DERR_DEVICELOST);
    CHECK(untouched == reinterpret_cast<IDirect3DDevice9*>(uintptr_t{0x8888}));
    CHECK(owned_native.releases == 0);
    CHECK(ownership_host::adopt_script.calls == 0);

    // Null success is forwarded without adoption; null pp yields a zero request snapshot.
    test::scenario();
    ownership_host::reset(owned_native);
    ownership_host::native_script.output = ownership_host::Output::Null;
    IDirect3DDevice9* null_out = reinterpret_cast<IDirect3DDevice9*>(uintptr_t{0x9999});
    CHECK(ownership_host::create_device(&ownership_factory, kAdapter, kType, kFocus, 0x52u,
                                        nullptr, &null_out) == S_OK);
    CHECK(null_out == nullptr);
    CHECK(ownership_host::adopt_script.calls == 0);
    CHECK(owned_native.releases == 0);

    // A failed ownership adoption releases the native output and becomes the result.
    test::scenario();
    ownership_host::reset(owned_native);
    ownership_host::adopt_script.result = E_FAIL;
    IDirect3DDevice9* adopt_out = reinterpret_cast<IDirect3DDevice9*>(uintptr_t{0xaaaa});
    CHECK(ownership_host::create_device(&ownership_factory, kAdapter, kType, kFocus, 0x52u,
                                        nullptr, &adopt_out) == E_FAIL);
    CHECK(ownership_host::native_script.calls == 1);
    CHECK(ownership_host::adopt_script.calls == 1);
    CHECK(adopt_out == nullptr);
    CHECK(owned_native.releases == 1);
    CHECK(ownership_host::adopt_script.finite_calls == 0);
    CHECK(ownership_host::adopt_script.copy_depth_calls == 0);

    // Capture normalization reaches the ownership native call before adoption.
    test::scenario();
    owned_native.releases = 0;
    ownership_host::reset(owned_native);
    capture_factory.context = &ownership_factory;
    capture_host::reset(capture_factory, direct_device, capture_host::ownership_bridge);
    capture_host::motion_output_requested = true;
    auto integrated_pp = parameters();
    IDirect3DDevice9* integrated_out = nullptr;
    CHECK(capture_host::create_device(&capture_factory, kAdapter, kType, kFocus, 0x52u,
                                      &integrated_pp, &integrated_out) == S_OK);
    CHECK(ownership_host::native_script.calls == 1);
    CHECK(ownership_host::native_script.flags == 0x42u);
    CHECK(ownership_host::adopt_script.calls == 1);
    CHECK(integrated_out == &ownership_host::adopt_script.wrapped);
    CHECK(capture_host::hook_record.calls == 1);
    CHECK(capture_host::hook_record.native_flags == 0x42u);
    CHECK(owned_native.releases == 0);

    std::printf("capture_device_creation scenarios=%d checks=%d failures=%d\n",
                test::scenarios, test::checks, test::failures);
    return test::failures ? 1 : 0;
}
