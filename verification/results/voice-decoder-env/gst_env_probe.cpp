#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <cwchar>
static const GUID clsid_wma = {0x2eeb4adf, 0x4578, 0x4d10, {0xbc, 0xa7, 0xbb, 0x95, 0x5f, 0x56, 0x32, 0x0a}};
int wmain(int argc, wchar_t **argv) {
    // argv: mode(set|inherit) plugin registry registry_check_winpath
    if (argc < 5) return 2;
    if (!wcscmp(argv[1], L"set")) {
        wchar_t v[1024];
        if (GetEnvironmentVariableW(L"PROBE_PLUGIN", v, 1024)) SetEnvironmentVariableW(L"GST_PLUGIN_PATH_1_0", v);
        if (GetEnvironmentVariableW(L"PROBE_REGISTRY", v, 1024)) SetEnvironmentVariableW(L"GST_REGISTRY_1_0", v);
    }
    if (!wcscmp(argv[1], L"spawn")) {
        wchar_t v[1024];
        if (GetEnvironmentVariableW(L"PROBE_PLUGIN", v, 1024)) SetEnvironmentVariableW(L"GST_PLUGIN_PATH_1_0", v);
        if (GetEnvironmentVariableW(L"PROBE_REGISTRY", v, 1024)) SetEnvironmentVariableW(L"GST_REGISTRY_1_0", v);
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        wchar_t cmd[2048];
        swprintf(cmd, 2048, L"\"%ls\" inherit x x \"%ls\"", self, argv[4]);
        STARTUPINFOW si = {sizeof si};
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(self, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { printf("spawn_failed=%lu\n", GetLastError()); return 3; }
        WaitForSingleObject(pi.hProcess, 60000);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        printf("parent_done=1\n");
        return 0;
    }
    wchar_t buf[1024];
    DWORD n = GetEnvironmentVariableW(L"GST_PLUGIN_PATH_1_0", buf, 1024);
    wprintf(L"env_plugin=%ls len=%lu\n", n ? buf : L"-", n);
    n = GetEnvironmentVariableW(L"GST_REGISTRY_1_0", buf, 1024);
    wprintf(L"env_registry=%ls len=%lu\n", n ? buf : L"-", n);
    printf("gst_loaded_before=%d\n", GetModuleHandleW(L"winegstreamer.dll") != nullptr);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUnknown *u = nullptr;
    HRESULT hr = CoCreateInstance(clsid_wma, nullptr, CLSCTX_INPROC_SERVER, IID_IUnknown, (void **)&u);
    printf("create_hr=%08lx\n", (unsigned long)hr);
    if (u) u->Release();
    printf("gst_loaded_after=%d\n", GetModuleHandleW(L"winegstreamer.dll") != nullptr);
    DWORD a = GetFileAttributesW(argv[4]);
    printf("registry_exists=%d\n", a != INVALID_FILE_ATTRIBUTES);
    fflush(stdout);
    CoUninitialize();
    return 0;
}
