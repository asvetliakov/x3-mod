// Paired standalone timing of actual BloomPass with retained old/new CSOs.
// EVENT-query fences make each QPC span CPU+driver+GPU-completion wall time;
// these measurements are deliberately not described as pure GPU timestamps.
#define X3M_BLOOM_PASS_FIXTURE_ENTRY bloom_pass_fixture_reference_main
#include "bloom_pass_fixture.cpp"
#undef X3M_BLOOM_PASS_FIXTURE_ENTRY

#include <algorithm>
#include <cmath>

namespace {
constexpr unsigned kProgramCount = 12;
constexpr unsigned kWarmupPairs = 3;
constexpr unsigned kMeasuredPairs = 5;
constexpr DWORD kFenceTimeoutMs = 5000;

struct DiskPrograms {
    std::vector<std::vector<DWORD>> words{kProgramCount};
    BloomPrograms bundle{};
    explicit DiskPrograms(const std::string& directory) {
        BloomBytecode* slots[] = {&bundle.vertex,     &bundle.extract[0], &bundle.extract[1], &bundle.extract[2],
                                  &bundle.extract[3], &bundle.extract[4], &bundle.extract[5], &bundle.down,
                                  &bundle.up,         &bundle.candidate,  &bundle.sharpen,    &bundle.copy};
        static_assert(sizeof(slots) / sizeof(slots[0]) == kProgramCount);
        for (unsigned i = 0; i < kProgramCount; ++i) {
            std::ifstream file(directory + "/" + names[i] + ".cso", std::ios::binary);
            require(bool(file), "Open retained CSO");
            std::vector<char> data{std::istreambuf_iterator<char>(file), {}};
            require(!data.empty() && data.size() % sizeof(DWORD) == 0, "Retained CSO size");
            words[i].resize(data.size() / sizeof(DWORD));
            std::memcpy(words[i].data(), data.data(), data.size());
            const DWORD version = words[i][0];
            require(version == (i == 0 ? 0xfffe0300u : 0xffff0300u), "Retained CSO profile");
            *slots[i] = {words[i].data(), words[i].size()};
        }
    }
};

HRESULT complete_fence(IDirect3DQuery9* query) noexcept {
    HRESULT hr = query->Issue(D3DISSUE_END);
    if (FAILED(hr)) return hr;
    const DWORD begin = GetTickCount();
    do {
        hr = query->GetData(nullptr, 0, D3DGETDATA_FLUSH);
        if (hr != S_FALSE) return hr;
        if (GetTickCount() - begin > kFenceTimeoutMs) return E_FAIL;
        Sleep(0);
    } while (true);
}

struct DrawTimer {
    IDirect3DQuery9* fence = nullptr;
    NativeDraw native = nullptr;
    bool armed = false, captured = false;
    HRESULT failure = S_OK;
    LONGLONG ticks = 0;
} draw_timer;

HRESULT WINAPI timed_draw(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, UINT count, const void* vertices,
                          UINT stride) {
    if (!draw_timer.armed || draw_timer.captured) return draw_timer.native(device, type, count, vertices, stride);
    draw_timer.armed = false;
    HRESULT hr = complete_fence(draw_timer.fence);
    LARGE_INTEGER begin{}, end{};
    if (SUCCEEDED(hr) && !QueryPerformanceCounter(&begin)) hr = E_FAIL;
    HRESULT draw = FAILED(hr) ? hr : draw_timer.native(device, type, count, vertices, stride);
    HRESULT tail = complete_fence(draw_timer.fence);
    if (SUCCEEDED(hr) && FAILED(tail)) hr = tail;
    if (SUCCEEDED(hr) && !QueryPerformanceCounter(&end)) hr = E_FAIL;
    if (SUCCEEDED(hr) && (end.QuadPart <= begin.QuadPart)) hr = E_FAIL;
    if (SUCCEEDED(hr)) draw_timer.ticks = end.QuadPart - begin.QuadPart;
    draw_timer.failure = hr;
    draw_timer.captured = true;
    return FAILED(draw) ? draw : hr;
}

Case timing_case(unsigned width, unsigned height) {
    Case c{};
    c.w = width;
    c.h = height;
    c.mode = 0;
    c.strength = 1;
    c.sharp = 0;
    c.threshold = 1;
    c.exposure = 1;
    c.authored_glow_gain = .1f;
    c.highlight_gain = .05f;
    c.pixels.resize(size_t(width) * height * 4);
    const unsigned short values[][4] = {
        {0x4000, 0x3800, 0x3000, 0x0000}, // 2, .5, .125, alpha 0
        {0x3800, 0x3400, 0x3000, 0x3800}, // .5, .25, .125, alpha .5
        {0x3400, 0x3000, 0x2c00, 0x3c00}, // .25, .125, .0625, alpha 1
        {0x3c00, 0x3800, 0x3400, 0x3c00}, // 1, .5, .25, alpha 1
    };
    for (size_t i = 0; i < size_t(width) * height; ++i)
        std::memcpy(c.pixels.data() + i * 4, values[(i + (i / width)) % 4], sizeof(values[0]));
    return c;
}

struct TimingImage {
    Case source;
    Texture main, scene;
    BloomBoundary boundary{};
    TimingImage(IDirect3DDevice9* device, unsigned width, unsigned height)
        : source(timing_case(width, height))
        , main(device, width, height, D3DFMT_A8R8G8B8, true)
        , scene(device, width, height, D3DFMT_A16B16G16R16F, false, true) {
        scene.upload(source);
        boundary.main = main.surface.p;
        check(main.surface->GetDesc(&boundary.main_desc), "Timing main desc");
        boundary.depth = nullptr;
        boundary.frame = 1;
        boundary.reset = 1;
        boundary.thread = GetCurrentThreadId();
        boundary.admitted = true;
    }
    void bind(IDirect3DDevice9* device) {
        check(device->SetRenderTarget(0, main.surface.p), "Timing RT0");
        check(device->SetRenderTarget(1, nullptr), "Timing RT1");
        check(device->SetDepthStencilSurface(nullptr), "Timing depth");
        D3DVIEWPORT9 viewport{0, 0, source.w, source.h, 0, 1};
        check(device->SetViewport(&viewport), "Timing viewport");
    }
};

BloomPrepare timing_input(TimingImage& image, bool authored) {
    BloomPrepare input{};
    input.scene = image.scene.texture.p;
    input.boundary = image.boundary;
    input.decode = x3::temporal::AgxDecode::gamma22;
    input.filter.levels = 5;
    input.filter.threshold = 1;
    input.filter.knee = .5f;
    input.filter.scatter = .7f;
    input.filter.strength = authored ? 1.f : .05f;
    input.filter.authored_glow_gain = authored ? .1f : 0.f;
    input.filter.highlight_gain = .05f;
    require(x3::temporal::prepare(input.agx, 1, 0, input.decode, x3::temporal::AgxLook::none), "Timing AgX constants");
    return input;
}

HRESULT attach_report(BloomPass& pass, IDirect3DDevice9* device, void* const* native, const D3DCAPS9& caps,
                      const BloomPrograms& programs, const char* metric, const char* label) {
    const HRESULT hr = pass.attach(device, native, caps, programs);
    std::printf("CREATE_PS metric=%s bundle=%s accepted=%u attach_hr=%08lx programs_hr=%08lx\n", metric, label,
                SUCCEEDED(hr) && pass.enabled() ? 1u : 0u, (unsigned long)hr, (unsigned long)pass.caps().programs);
    return hr;
}

void validate_result(const BloomPreparation& prepared, const BloomCommit& committed) {
    require(prepared.ready && prepared.state_preserved, "Timing prepare");
    require(committed.committed && committed.state_preserved && committed.write_attempted, "Timing commit");
}

LONGLONG measure_full(IDirect3DDevice9* device, BloomPass& pass, BloomPrepare& input, TimingImage& image,
                      IDirect3DQuery9* fence) {
    image.bind(device);
    ++image.boundary.frame;
    input.boundary = image.boundary;
    check(device->BeginScene(), "Timing BeginScene");
    check(complete_fence(fence), "Timing leading fence");
    LARGE_INTEGER begin{}, end{};
    require(QueryPerformanceCounter(&begin) != FALSE, "Timing QPC begin");
    const auto prepared = pass.prepare(input);
    const auto committed = pass.commit(prepared.candidate, image.boundary);
    check(complete_fence(fence), "Timing trailing fence");
    require(QueryPerformanceCounter(&end) != FALSE && end.QuadPart > begin.QuadPart, "Timing QPC end");
    check(device->EndScene(), "Timing EndScene");
    validate_result(prepared, committed);
    return end.QuadPart - begin.QuadPart;
}

LONGLONG measure_extract(IDirect3DDevice9* device, BloomPass& pass, BloomPrepare& input, TimingImage& image,
                         IDirect3DQuery9* fence) {
    image.bind(device);
    ++image.boundary.frame;
    input.boundary = image.boundary;
    draw_timer.fence = fence;
    draw_timer.armed = true;
    draw_timer.captured = false;
    draw_timer.failure = S_OK;
    draw_timer.ticks = 0;
    check(device->BeginScene(), "Extract BeginScene");
    const auto prepared = pass.prepare(input);
    const auto committed = pass.commit(prepared.candidate, image.boundary);
    // Exclude later pyramid/composition/commit work from the captured span but
    // complete it before the next alternating sample or resource destruction.
    check(complete_fence(fence), "Extract tail fence");
    check(device->EndScene(), "Extract EndScene");
    validate_result(prepared, committed);
    require(draw_timer.captured && !draw_timer.armed && SUCCEEDED(draw_timer.failure) && draw_timer.ticks > 0,
            "Extraction draw timing");
    return draw_timer.ticks;
}

double milliseconds(LONGLONG ticks, LONGLONG frequency) {
    return double(ticks) * 1000. / double(frequency);
}
double median_ms(const std::vector<LONGLONG>& values, LONGLONG frequency) {
    auto sorted = values;
    std::sort(sorted.begin(), sorted.end());
    return milliseconds(sorted[sorted.size() / 2], frequency);
}

template <class Measure>
void paired_batch(const char* metric, unsigned width, unsigned height, LONGLONG frequency, BloomPass& old_pass,
                  BloomPass& new_pass, BloomPrepare& old_input, BloomPrepare& new_input, TimingImage& image,
                  IDirect3DDevice9* device, IDirect3DQuery9* fence, Measure measure) {
    for (unsigned pair = 0; pair < kWarmupPairs; ++pair) {
        if (pair % 2 == 0) {
            measure(device, old_pass, old_input, image, fence);
            measure(device, new_pass, new_input, image, fence);
        } else {
            measure(device, new_pass, new_input, image, fence);
            measure(device, old_pass, old_input, image, fence);
        }
    }
    std::vector<LONGLONG> old_values, new_values;
    for (unsigned pair = 0; pair < kMeasuredPairs; ++pair) {
        const bool old_first = pair % 2 == 0;
        LONGLONG old_ticks = 0, new_ticks = 0;
        if (old_first) {
            old_ticks = measure(device, old_pass, old_input, image, fence);
            new_ticks = measure(device, new_pass, new_input, image, fence);
        } else {
            new_ticks = measure(device, new_pass, new_input, image, fence);
            old_ticks = measure(device, old_pass, old_input, image, fence);
        }
        old_values.push_back(old_ticks);
        new_values.push_back(new_ticks);
        std::printf(
            "SAMPLE metric=%s width=%u height=%u pair=%u order=%s old_ticks=%lld old_ms=%.6f new_ticks=%lld new_ms=%.6f\n",
            metric, width, height, pair, old_first ? "old-new" : "new-old", (long long)old_ticks,
            milliseconds(old_ticks, frequency), (long long)new_ticks, milliseconds(new_ticks, frequency));
    }
    const double old_median = median_ms(old_values, frequency), new_median = median_ms(new_values, frequency);
    std::printf(
        "SUMMARY metric=%s width=%u height=%u warmup_pairs=%u measured_pairs=%u old_median_ms=%.6f new_median_ms=%.6f ratio=%.6f\n",
        metric, width, height, kWarmupPairs, kMeasuredPairs, old_median, new_median,
        old_median > 0 ? new_median / old_median : 0.);
}

void dimension(IDirect3DDevice9* device, const D3DCAPS9& caps, void* const* actual, const DiskPrograms& old_programs,
               const DiskPrograms& new_programs, unsigned width, unsigned height, LONGLONG frequency) {
    TimingImage image(device, width, height);
    Com<IDirect3DQuery9> fence;
    check(device->CreateQuery(D3DQUERYTYPE_EVENT, &fence.p), "Create EVENT query");
    auto old_input = timing_input(image, false), new_input = timing_input(image, true);
    {
        BloomPass old_pass, new_pass;
        check(attach_report(old_pass, device, actual, caps, old_programs.bundle, "full", "old"), "Attach old full");
        check(attach_report(new_pass, device, actual, caps, new_programs.bundle, "full", "new"), "Attach new full");
        paired_batch("full", width, height, frequency, old_pass, new_pass, old_input, new_input, image, device, fence.p,
                     measure_full);
        old_pass.shutdown();
        new_pass.shutdown();
    }
    {
        void* hooked[119];
        std::memcpy(hooked, actual, sizeof(hooked));
        draw_timer.native = reinterpret_cast<NativeDraw>(actual[83]);
        hooked[83] = reinterpret_cast<void*>(&timed_draw);
        BloomPass old_pass, new_pass;
        check(attach_report(old_pass, device, hooked, caps, old_programs.bundle, "extract", "old"),
              "Attach old extract");
        check(attach_report(new_pass, device, hooked, caps, new_programs.bundle, "extract", "new"),
              "Attach new extract");
        paired_batch("extract_first_draw", width, height, frequency, old_pass, new_pass, old_input, new_input, image,
                     device, fence.p, measure_extract);
        old_pass.shutdown();
        new_pass.shutdown();
    }
    check(complete_fence(fence.p), "Final dimension fence");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "usage: timing_fixture old_cso_directory new_cso_directory");
        const std::string old_directory = argv[1], new_directory = argv[2];
        std::printf("BUNDLE label=old directory=%s\n", old_directory.c_str());
        std::printf("BUNDLE label=new directory=%s\n", new_directory.c_str());
        DiskPrograms old_programs(old_directory), new_programs(new_directory);
        LARGE_INTEGER frequency{};
        require(QueryPerformanceFrequency(&frequency) != FALSE && frequency.QuadPart > 0, "QPC frequency");
        HMODULE module = LoadLibraryA("d3d9.dll");
        require(module, "Load D3D9");
        auto address = GetProcAddress(module, "Direct3DCreate9");
        IDirect3D9*(WINAPI * create)(UINT) = nullptr;
        std::memcpy(&create, &address, sizeof(create));
        require(create, "D3D symbol");
        Com<IDirect3D9> api;
        api.p = create(D3D_SDK_VERSION);
        require(api.p, "Create D3D9");
        HWND window = CreateWindowA("STATIC", "Bloom timing fixture", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128, nullptr,
                                    nullptr, GetModuleHandleA(nullptr), nullptr);
        require(window, "Window");
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferWidth = 64;
        pp.BackBufferHeight = 64;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.hDeviceWindow = window;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device;
        check(api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p),
              "Create timing device");
        D3DCAPS9 caps{};
        check(device->GetDeviceCaps(&caps), "Timing caps");
        std::printf("CAPS pixel_shader_version=%08lx max_ps30_instruction_slots=%u qpc_frequency=%lld\n",
                    (unsigned long)caps.PixelShaderVersion, unsigned(caps.MaxPixelShader30InstructionSlots),
                    (long long)frequency.QuadPart);
        require(caps.PixelShaderVersion >= D3DPS_VERSION(3, 0), "Pixel shader 3.0");
        void** vtable = *reinterpret_cast<void***>(device.p);
        void* actual[119];
        std::memcpy(actual, vtable, sizeof(actual));
        dimension(device.p, caps, actual, old_programs, new_programs, 1280, 768, frequency.QuadPart);
        dimension(device.p, caps, actual, old_programs, new_programs, 1279, 767, frequency.QuadPart);
        DestroyWindow(window);
        std::printf("RESULT PASS dimensions=2 metrics=2 warmup_pairs=3 measured_pairs=5\n");
        return 0;
    } catch (const std::exception& error) {
        std::printf("RESULT FAIL reason=%s\n", error.what());
        return 1;
    }
}
