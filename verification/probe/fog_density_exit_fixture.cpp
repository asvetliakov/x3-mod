// ExitProcess with a live stored-density worker. A hang under the loader lock is the failure
// mode, so every case is a child process of this executable with a watchdog: a child that has
// not exited in time is terminated and reported. Usage: fog_density_exit_fixture.exe <dll> <dir>
//   idle / midfill, abandon in DllMain (the production order): must exit promptly, with the
//     DllMain mark before the module's static destructor (the CRT order the proxy relies on);
//   control, no abandon: reported, not gated. It shows what the static destructor's join does
//     on this backend after the OS has ended the worker.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
namespace {
unsigned checks = 0, failures = 0;
void require(const char* name, bool value) {
    ++checks;
    failures += !value;
    std::printf("CHECK %s %s\n", name, value ? "PASS" : "FAIL");
}
std::string slurp(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::string text;
    char buffer[512];
    DWORD n = 0;
    while (ReadFile(f, buffer, sizeof buffer, &n, nullptr) && n) text.append(buffer, n);
    CloseHandle(f);
    return text;
}
struct Outcome {
    bool started = false, exited = false;
    DWORD code = 0, wait_ms = 0;
    long exit_ms = -1;
    std::string order;
    int detach = -1, destructor = -1, destructor_end = -1;
};
Outcome run(const std::wstring& self, const std::wstring& dll, const std::wstring& dir, const wchar_t* name,
            int abandon, int midfill, DWORD watchdog_ms) {
    Outcome out;
    const std::wstring marks = dir + L"\\exit-" + name + L".marks";
    std::wstring command = L"\"" + self + L"\" --child \"" + dll + L"\" \"" + marks + L"\" " +
                           (abandon ? L"1 " : L"0 ") + (midfill ? L"1" : L"0");
    std::vector<wchar_t> line(command.begin(), command.end());
    line.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return out;
    out.started = true;
    const DWORD begin = GetTickCount();
    out.exited = WaitForSingleObject(pi.hProcess, watchdog_ms) == WAIT_OBJECT_0;
    const DWORD end = GetTickCount();
    out.wait_ms = end - begin;
    if (!out.exited)
        TerminateProcess(pi.hProcess, 99);
    else
        GetExitCodeProcess(pi.hProcess, &out.code);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    const std::string text = slurp(marks);
    unsigned index = 0;
    size_t at = 0;
    while (at < text.size()) {
        const size_t stop = text.find("\r\n", at);
        const std::string row = text.substr(at, stop == std::string::npos ? std::string::npos : stop - at);
        const std::string label = row.substr(0, row.find(' '));
        if (!out.order.empty()) out.order += ',';
        out.order += label;
        if (label == "ready")
            out.exit_ms = long(end - DWORD(std::strtoul(row.c_str() + row.find("tick=") + 5, nullptr, 10)));
        if (label == "dllmain_detach_process_exit") out.detach = int(index);
        if (label == "static_destructor_begin") out.destructor = int(index);
        if (label == "static_destructor_end") out.destructor_end = int(index);
        ++index;
        if (stop == std::string::npos) break;
        at = stop + 2;
    }
    std::printf("EXIT case=%ls abandon=%d midfill=%d started=%u exited=%u code=%lu wait_ms=%lu exit_ms=%ld order=%s\n",
                name, abandon, midfill, unsigned(out.started), unsigned(out.exited), out.code, out.wait_ms, out.exit_ms,
                out.order.c_str());
    return out;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 6 && !std::wcscmp(argv[1], L"--child")) {
        HMODULE dll = LoadLibraryW(argv[2]);
        if (!dll) return 3;
        using Start = int(__cdecl*)(const wchar_t*, int, int);
        FARPROC address = GetProcAddress(dll, "fixture_start");
        Start start = nullptr;
        std::memcpy(&start, &address, sizeof start);
        if (!start || !start(argv[3], argv[4][0] == L'1', argv[5][0] == L'1')) return 4;
        ExitProcess(0); // no FreeLibrary, no CRT exit of this image: the DLL sees DLL_PROCESS_DETACH with the worker
                        // already ended by the OS
    }
    if (argc != 3) {
        std::fprintf(stderr, "usage: fog_density_exit_fixture.exe <dll> <directory>\n");
        return 2;
    }
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    constexpr long bound_ms = 3000;
    for (const wchar_t* name : {L"idle", L"midfill"}) {
        const Outcome o = run(self, argv[1], argv[2], name, 1, name[0] == L'm', 60000);
        const std::string tag = name[0] == L'm' ? "midfill" : "idle";
        require(("exit_" + tag + "_worker_abandoned_returns_in_bound").c_str(),
                o.started && o.exited && o.code == 0 && o.exit_ms >= 0 && o.exit_ms < bound_ms);
        require(("exit_" + tag + "_dllmain_detach_precedes_static_destructor").c_str(),
                o.detach >= 0 && o.destructor > o.detach && o.destructor_end > o.destructor);
    }
    for (const wchar_t* name : {L"control-idle", L"control-midfill"}) {
        const Outcome o = run(self, argv[1], argv[2], name, 0, name[8] == L'm', 40000);
        std::printf("EXIT_CONTROL case=%ls hung=%u reached_static_destructor=%u finished_static_destructor=%u\n", name,
                    unsigned(o.started && !o.exited), unsigned(o.destructor >= 0), unsigned(o.destructor_end >= 0));
    }
    std::printf("RESULT fog_density_exit checks=%u %s\n", checks, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
