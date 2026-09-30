/* Windows 10 functions Office looks up and Wine does not have, and what they answer.
 *
 * Word, run with WINEDEBUG=warn+module, asks GetProcAddress for InheritWindowMonitor (user32; OART,
 * the Writing Assistant), DeriveAppContainerSidFromAppContainerName and GetAppContainerFolderPath
 * (userenv; the add-in framework and its web sandbox), NtQueryDirectoryFileEx (ntdll; the App-V
 * layer), and loads isolatedwindowsenvironmentutils.dll.  This prints for each whether the system has
 * it and what it answers: the exports of isolatedwindowsenvironmentutils.dll and whether this process
 * is isolated, the SIDs derived from a few container names (with the SHA-256 derivation computed here
 * next to them), GetAppContainerFolderPath for a SID no package has, and InheritWindowMonitor for
 * valid, missing and foreign windows.  Nothing here is about the user; the folder path is printed
 * without the profile part.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror win10apiprobe.c -o win10apiprobe.exe -luser32 -ladvapi32 -lbcrypt -lole32
 */
#include <windows.h>
#include <sddl.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static void exports(const char *dll)
{
    HMODULE module = LoadLibraryExA(dll, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    IMAGE_EXPORT_DIRECTORY *dir;
    IMAGE_NT_HEADERS *nt;
    DWORD *names, i;

    printf("%s: %s", dll, module ? "loaded" : "missing");
    if (!module) { printf(" (%lu)\n", GetLastError()); return; }
    nt = (IMAGE_NT_HEADERS *)((BYTE *)module + ((IMAGE_DOS_HEADER *)module)->e_lfanew);
    dir = (IMAGE_EXPORT_DIRECTORY *)((BYTE *)module + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    names = (DWORD *)((BYTE *)module + dir->AddressOfNames);
    printf(", %lu named exports:", dir->NumberOfNames);
    for (i = 0; i < dir->NumberOfNames; i++) printf(" %s", (char *)module + names[i]);
    printf("\n");
}

static void has(const char *dll, const char *name)
{
    HMODULE module = GetModuleHandleA(dll);

    if (!module) module = LoadLibraryExA(dll, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    printf("%s!%s: %s\n", dll, name, module && GetProcAddress(module, name) ? "present" : "absent");
}

/* S-1-15-2 followed by the first seven DWORDs of SHA-256 over the lower-case name in UTF-16LE */
static void derive_here(const WCHAR *name, char *out)
{
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_HASH_HANDLE hash;
    WCHAR lower[256];
    DWORD digest[8];
    unsigned int i;

    wcscpy(lower, name);
    CharLowerW(lower);
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0);
    BCryptHashData(hash, (UCHAR *)lower, wcslen(lower) * sizeof(WCHAR), 0);
    BCryptFinishHash(hash, (UCHAR *)digest, sizeof(digest), 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    out += sprintf(out, "S-1-15-2");
    for (i = 0; i < 7; i++) out += sprintf(out, "-%lu", digest[i]);
}

int main(void)
{
    static const WCHAR *names[] = {L"microsoft.windows.cortana_cw5n1h2txyewy", L"WineAltars.Probe", L"winealtars.probe", L"a"};
    HRESULT (WINAPI *derive)(const WCHAR *, PSID *);
    HRESULT (WINAPI *folder)(const WCHAR *, WCHAR **);
    HRESULT (WINAPI *isolated)(BOOL *);
    BOOL (WINAPI *inherit)(HWND, HWND);
    HMODULE userenv, iwe;
    HWND a, b;
    unsigned int i;

    exports("isolatedwindowsenvironmentutils.dll");
    has("user32.dll", "InheritWindowMonitor");
    has("ntdll.dll", "NtQueryDirectoryFileEx");
    has("ole32.dll", "DllCanUnloadNow");
    has("userenv.dll", "DeriveAppContainerSidFromAppContainerName");
    has("userenv.dll", "GetAppContainerFolderPath");
    has("userenv.dll", "GetAppContainerRegistryLocation");
    has("userenv.dll", "DeleteAppContainerProfile");
    has("kernelbase.dll", "AppContainerDeriveSidFromMoniker");

    iwe = LoadLibraryExA("isolatedwindowsenvironmentutils.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    isolated = iwe ? (void *)GetProcAddress(iwe, "IsProcessInIsolatedWindowsEnvironment") : NULL;
    if (isolated)
    {
        BOOL result = 0xcc;
        HRESULT hr = isolated(&result);
        printf("IsProcessInIsolatedWindowsEnvironment: %#lx, %d\n", hr, result);
    }

    userenv = LoadLibraryExA("userenv.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    derive = (void *)GetProcAddress(userenv, "DeriveAppContainerSidFromAppContainerName");
    folder = (void *)GetProcAddress(userenv, "GetAppContainerFolderPath");
    for (i = 0; derive && i < ARRAYSIZE(names); i++)
    {
        char here[128];
        PSID sid = NULL;
        char *text = NULL;
        HRESULT hr = derive(names[i], &sid);

        derive_here(names[i], here);
        if (sid) ConvertSidToStringSidA(sid, &text);
        printf("derive(%ls): %#lx %s; SHA-256 %s: %s\n", names[i], hr, text ? text : "-", here,
               text && !strcmp(text, here) ? "same" : "differs");
        if (folder && text && i == 1)
        {
            WCHAR *path = NULL, *wtext;
            ConvertSidToStringSidW(sid, &wtext);
            hr = folder(wtext, &path);
            printf("GetAppContainerFolderPath(no package): %#lx %s\n", hr, path ? "(a path)" : "-");
            if (path)
            {
                const WCHAR *tail = wcsstr(path, L"\\Packages\\");
                printf("    ends with %ls\n", tail ? tail : L"(no \\Packages\\)");
                CoTaskMemFree(path);
            }
            LocalFree(wtext);
        }
        LocalFree(text);
        if (sid) FreeSid(sid);
    }
    if (derive)
    {
        PSID sid = (PSID)0xdeadbeef;
        printf("derive(NULL): %#lx\n", derive(NULL, &sid));
        printf("derive(\"\"): %#lx\n", derive(L"", &sid));
    }

    inherit = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), "InheritWindowMonitor");
    if (inherit)
    {
        a = CreateWindowA("static", "a", WS_POPUP, 100, 100, 50, 50, NULL, NULL, NULL, NULL);
        b = CreateWindowA("static", "b", WS_POPUP, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
        SetLastError(0xdeadbeef);
        printf("InheritWindowMonitor(b, a): %d (%lu)\n", inherit(b, a), GetLastError());
        SetLastError(0xdeadbeef);
        printf("InheritWindowMonitor(b, NULL): %d (%lu)\n", inherit(b, NULL), GetLastError());
        SetLastError(0xdeadbeef);
        printf("InheritWindowMonitor(NULL, a): %d (%lu)\n", inherit(NULL, a), GetLastError());
        SetLastError(0xdeadbeef);
        printf("InheritWindowMonitor(b, desktop): %d (%lu)\n", inherit(b, GetDesktopWindow()), GetLastError());
        SetLastError(0xdeadbeef);
        printf("InheritWindowMonitor(b, shell): %d (%lu)\n", inherit(b, GetShellWindow()), GetLastError());
        SetLastError(0xdeadbeef);
        printf("InheritWindowMonitor(b, (HWND)0x1234): %d (%lu)\n", inherit(b, (HWND)0x1234), GetLastError());
        DestroyWindow(b);
        DestroyWindow(a);
    }
    return 0;
}
