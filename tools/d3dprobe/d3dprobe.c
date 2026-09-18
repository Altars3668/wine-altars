/*
 * d3dprobe - can Direct2D get the device it insists on?
 *
 * Wine's d2d1 creates every render target -- including the WIC (software)
 * ones Office uses when hardware acceleration is disabled -- through
 * d2d_factory_get_device, which calls D3D10CreateDevice1 with
 * D3D10_DRIVER_TYPE_HARDWARE and nothing else. If that call fails there is no
 * software path to fall back to: every DrawTextLayout fails and the window is
 * blank. This asks the same question with no Office involved, so "d2d is
 * broken" and "the client asked for something unusual" stay separate facts.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <d3d10_1.h>
#include <stdio.h>

/* running under explorer /desktop the console output goes nowhere, so say it
 * in a file as well as on stdout */
static FILE *out;
#define say(...) do { printf(__VA_ARGS__); if (out) fprintf(out, __VA_ARGS__); } while (0)

/* mingw ships the header but no import library for d3d10_1 */
typedef HRESULT (WINAPI *create_fn)(IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT,
                                    D3D10_FEATURE_LEVEL1, UINT, ID3D10Device1 **);
static create_fn create_device;

static void try_one(const char *label, D3D10_DRIVER_TYPE type, UINT flags, D3D10_FEATURE_LEVEL1 level)
{
    ID3D10Device1 *dev = NULL;
    HRESULT hr = create_device(NULL, type, NULL, flags, level, D3D10_1_SDK_VERSION, &dev);
    say("  %-42s -> hr %#lx%s\n", label, hr, SUCCEEDED(hr) ? "  OK" : "");
    if (dev) ID3D10Device1_Release(dev);
}

int main(void)
{
    out = fopen("Z:\\tmp\\d3dprobe.txt", "w");
    HMODULE mod = LoadLibraryA("d3d10_1.dll");
    if (!mod) { printf("d3d10_1.dll did not load (%lu)\n", GetLastError()); return 1; }
    if (!(create_device = (create_fn)GetProcAddress(mod, "D3D10CreateDevice1"))) {
        printf("no D3D10CreateDevice1 export\n"); return 1;
    }
    say("D3D10CreateDevice1:\n");
    try_one("HARDWARE + BGRA (what d2d1 asks for)", D3D10_DRIVER_TYPE_HARDWARE,
            D3D10_CREATE_DEVICE_BGRA_SUPPORT, D3D10_FEATURE_LEVEL_10_0);
    try_one("HARDWARE, no flags",                   D3D10_DRIVER_TYPE_HARDWARE, 0, D3D10_FEATURE_LEVEL_10_0);
    try_one("HARDWARE + BGRA, feature level 9_1",   D3D10_DRIVER_TYPE_HARDWARE,
            D3D10_CREATE_DEVICE_BGRA_SUPPORT, D3D10_FEATURE_LEVEL_9_1);
    try_one("WARP + BGRA",                          D3D10_DRIVER_TYPE_WARP,
            D3D10_CREATE_DEVICE_BGRA_SUPPORT, D3D10_FEATURE_LEVEL_10_0);
    try_one("REFERENCE + BGRA",                     D3D10_DRIVER_TYPE_REFERENCE,
            D3D10_CREATE_DEVICE_BGRA_SUPPORT, D3D10_FEATURE_LEVEL_10_0);
    if (out) fclose(out);
    return 0;
}
