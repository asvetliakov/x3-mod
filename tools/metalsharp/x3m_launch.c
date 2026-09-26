/* x3m-launch.exe: MetalSharp launch wrapper for X3: Albion Prelude with the x3m proxy.
 *
 * MetalSharp's Sharp Library launches one registered executable and has no
 * per-game environment editor, while the proxy reads ~180 X3M_* variables.
 * This 32-bit PE is registered as the game executable. It reads, from its own
 * directory,
 *   x3m-launch.env   KEY=VALUE lines (the launcher's dry-run environment)
 *   x3m-launch.cfg   dir=<game directory, Windows path>
 *                    exe=<executable name>
 *                    args=<command-line switches>
 * sets the variables, appends "d3d9=n,b" to WINEDLLOVERRIDES (the CrossOver
 * launcher's --dll d3d9=n,b), starts the game in its directory and waits for
 * it, so MetalSharp's process tracking follows the game's lifetime. It writes
 * x3m-launch.log beside itself: what it set, the Wine/DXMT/MetalSharp
 * variables it inherited, and the game's exit code.
 *
 * Build: i686-w64-mingw32-gcc -O2 -mwindows -o x3m-launch.exe x3m_launch.c
 * Never run this outside MetalSharp's launch: it starts the game.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static FILE* logf;

static void logline(const char* fmt, ...) {
    if (!logf) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fputc('\n', logf);
    fflush(logf);
}

static void chomp(char* s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

static int read_kv(const char* path, const char* prefix, int set_env, char* dir, char* exe, char* args) {
    FILE* f = fopen(path, "r");
    char line[32768];
    int count = 0;
    if (!f) {
        logline("cannot open %s", path);
        return -1;
    }
    while (fgets(line, sizeof line, f)) {
        char* eq;
        chomp(line);
        if (!line[0] || line[0] == '#') continue;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        if (set_env) {
            if (!SetEnvironmentVariableA(line, eq + 1))
                logline("SetEnvironmentVariable %s failed %lu", line, GetLastError());
            else
                count++;
        } else {
            if (!strcmp(line, "dir"))
                strncpy(dir, eq + 1, 32767);
            else if (!strcmp(line, "exe"))
                strncpy(exe, eq + 1, 1023);
            else if (!strcmp(line, "args"))
                strncpy(args, eq + 1, 4095);
            count++;
        }
    }
    fclose(f);
    logline("%s: %d entries from %s", prefix, count, path);
    return count;
}

static void log_inherited(void) {
    static const char* prefixes[] = {"WINE",  "DXMT", "METALSHARP", "ROSETTA",    "MVK_", "VK_",
                                     "DYLD_", "MS_",  "CX_",        "X3M_LAUNCH", NULL};
    char *env = GetEnvironmentStringsA(), *p = env;
    if (!env) return;
    for (; *p; p += strlen(p) + 1) {
        int i;
        for (i = 0; prefixes[i]; i++)
            if (!strncmp(p, prefixes[i], strlen(prefixes[i]))) {
                logline("inherited %s", p);
                break;
            }
    }
    FreeEnvironmentStringsA(env);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline_unused, int show) {
    char self[32768], base[32768], path[32768];
    static char dir[32768], exe[1024], args[4096], overrides[8192], cmdline[8192];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    SYSTEMTIME now;
    (void)inst;
    (void)prev;
    (void)cmdline_unused;
    (void)show;
    GetModuleFileNameA(NULL, self, sizeof self);
    strcpy(base, self);
    *strrchr(base, '\\') = 0;
    snprintf(path, sizeof path, "%s\\x3m-launch.log", base);
    logf = fopen(path, "w");
    GetLocalTime(&now);
    logline("x3m-launch %04u-%02u-%02u %02u:%02u:%02u pid %lu self %s", now.wYear, now.wMonth, now.wDay, now.wHour,
            now.wMinute, now.wSecond, GetCurrentProcessId(), self);
    log_inherited();
    snprintf(path, sizeof path, "%s\\x3m-launch.cfg", base);
    if (read_kv(path, "cfg", 0, dir, exe, args) < 0 || !dir[0] || !exe[0]) {
        logline("cfg incomplete: dir='%s' exe='%s'", dir, exe);
        return 2;
    }
    snprintf(path, sizeof path, "%s\\x3m-launch.env", base);
    if (read_kv(path, "env", 1, NULL, NULL, NULL) < 0) return 3;
    /* Reproduce the CrossOver launcher's --dll d3d9=n,b: the proxy is the native d3d9.dll in the game directory. */
    if (GetEnvironmentVariableA("WINEDLLOVERRIDES", overrides, sizeof overrides) && overrides[0]) {
        if (!strstr(overrides, "d3d9")) strncat(overrides, ";d3d9=n,b", sizeof overrides - strlen(overrides) - 1);
    } else
        strcpy(overrides, "d3d9=n,b");
    SetEnvironmentVariableA("WINEDLLOVERRIDES", overrides);
    logline("WINEDLLOVERRIDES=%s", overrides);
    snprintf(cmdline, sizeof cmdline, "\"%s\\%s\" %s", dir, exe, args);
    logline("cwd %s", dir);
    logline("cmd %s", cmdline);
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        logline("CreateProcess failed %lu", GetLastError());
        return 4;
    }
    logline("game pid %lu", pi.dwProcessId);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    logline("game exit code %lu", code);
    if (logf) fclose(logf);
    return (int)code;
}
