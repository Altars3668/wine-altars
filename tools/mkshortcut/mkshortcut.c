/*
 * mkshortcut -- create a Start Menu shortcut, the way an installer does.
 *
 * A prefix built by copying an installed Office has no .lnk files, because no
 * installer ever ran.  Wine's menu and icon support is driven entirely by
 * those: winemenubuilder is invoked per link, reads the target, extracts its
 * icon, derives the window class from the executable name and writes a
 * .desktop file carrying all three.  With no link there is nothing for it to
 * do, which is why such a prefix has file-type handlers but no applications.
 *
 * So this creates the link rather than the .desktop.  Saving it through
 * IPersistFile is what an installer's shell calls do, and shell32 answers it
 * by running winemenubuilder itself -- the entry, its icon and its
 * StartupWMClass then come from Wine, not from a script that has to be kept in
 * step with it.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o mkshortcut.exe mkshortcut.c -lole32 -luuid -lshell32
 *
 *   mkshortcut <link.lnk> <target.exe> [description] [icon.exe,index]
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>

static void mkdir_p(const WCHAR *path)
{
    WCHAR dir[MAX_PATH];
    WCHAR *p;

    lstrcpynW(dir, path, MAX_PATH);
    if (!(p = wcsrchr(dir, '\\'))) return;
    *p = 0;
    SHCreateDirectoryExW(NULL, dir, NULL);
}

int wmain(int argc, WCHAR **argv)
{
    IShellLinkW *link = NULL;
    IPersistFile *file = NULL;
    HRESULT hr;
    WCHAR dir[MAX_PATH], *p;

    if (argc < 3)
    {
        printf("用法: mkshortcut <link.lnk> <target.exe> [说明] [图标路径,序号]\n");
        return 2;
    }

    hr = CoInitialize(NULL);
    if (FAILED(hr)) { printf("CoInitialize 失败 0x%08lx\n", (unsigned long)hr); return 1; }

    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IShellLinkW, (void **)&link);
    if (FAILED(hr)) { printf("创建 ShellLink 失败 0x%08lx\n", (unsigned long)hr); return 1; }

    IShellLinkW_SetPath(link, argv[2]);

    /* The working directory matters: Office resolves some of its own
     * components relative to it, and a link without one starts the program in
     * whatever directory the menu happened to be launched from. */
    lstrcpynW(dir, argv[2], MAX_PATH);
    if ((p = wcsrchr(dir, '\\'))) { *p = 0; IShellLinkW_SetWorkingDirectory(link, dir); }

    if (argc > 3 && argv[3][0]) IShellLinkW_SetDescription(link, argv[3]);

    /* Default the icon to the target itself, which is where winemenubuilder
     * will look for it anyway; an explicit "file,index" overrides that. */
    if (argc > 4 && argv[4][0])
    {
        WCHAR icon[MAX_PATH];
        int idx = 0;
        lstrcpynW(icon, argv[4], MAX_PATH);
        if ((p = wcsrchr(icon, ','))) { *p = 0; idx = _wtoi(p + 1); }
        IShellLinkW_SetIconLocation(link, icon, idx);
    }
    else IShellLinkW_SetIconLocation(link, argv[2], 0);

    mkdir_p(argv[1]);

    hr = IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&file);
    if (SUCCEEDED(hr))
    {
        hr = IPersistFile_Save(file, argv[1], TRUE);
        IPersistFile_Release(file);
    }
    IShellLinkW_Release(link);
    CoUninitialize();

    if (FAILED(hr)) { printf("保存失败 0x%08lx\n", (unsigned long)hr); return 1; }
    printf("已创建 %ls -> %ls\n", argv[1], argv[2]);
    return 0;
}
