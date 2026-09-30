/*
 * inkprobe - which ink components Windows has, where they live, and what the system says about pens.
 *
 * Office refers to the Tablet PC ink objects (InkDisp, InkRenderer, InkCollector, InkDrawingAttributes,
 * InkTransform, InkRectangle, InkRecognizers, RealTimeStylus) and to Windows 10's InkDesktopHost and
 * InkD2DRenderer; Wine has none of them.  PowerPoint's slide show pen draws nothing under Wine, and
 * Word shows no Draw tab.  This prints, for each class, its InprocServer32 and threading model and
 * what CoCreateInstance answers on an STA thread (the object is released at once), then the system
 * metrics an application can read about pens and touch, and the pointer devices.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

static const struct { const char *name; const char *clsid; } classes[] =
{
    {"InkDisp", "{937C1A34-151D-4610-9CA6-A8CC9BDB5D83}"},
    {"InkRenderer", "{9C1CC6E4-D7EB-4EEB-9091-15A7C8791ED9}"},
    {"InkCollector", "{43FB1553-AD74-4EE8-88E4-3E6DAAC915DB}"},
    {"InkOverlay", "{65D00646-CDE3-4A88-9163-6769F0F1A97D}"},
    {"InkDrawingAttributes", "{D8BF32A2-05A5-44C3-B3AA-5E80AC7D2576}"},
    {"InkTransform", "{E3D5D93C-1663-4A78-A1A7-22375DFEBAEE}"},
    {"InkRectangle", "{43B07326-AAE0-4B62-A83D-5FD768B7353C}"},
    {"InkRecognizers", "{9FD4E808-F6E6-4E65-98D3-AA39054C1255}"},
    {"InkStrokes", "{48F491BC-240E-4860-B079-A1E94D3D2C86}"},
    {"RealTimeStylus", "{E26B366D-F998-43CE-836F-CB6D904432B0}"},
    {"StrokeBuilder", "{E810CEE7-6E51-4CB0-AA3A-0B985B70DAF7}"},
    {"DynamicRenderer", "{ECD32AEA-746F-4DCB-BF68-082757FAFF18}"},
    {"GestureRecognizer", "{EA30C654-C62C-441F-AC00-95F9A196782C}"},
    {"InkDesktopHost", "{062584A6-F830-4BDC-A4D2-0A10AB062B1D}"},
    {"InkD2DRenderer", "{4044E60C-7B01-4671-A97C-04E0210A07A5}"},
};

static void reg_value(HKEY root, const char *path, const char *value, char *out, DWORD size)
{
    DWORD type;
    out[0] = 0;
    if (RegGetValueA(root, path, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, &type, out, &size))
        strcpy(out, "-");
}

int main(void)
{
    static const struct { const char *name; int index; } metrics[] =
    {
        {"SM_TABLETPC", 86}, {"SM_MEDIACENTER", 87}, {"SM_DIGITIZER", 94}, {"SM_MAXIMUMTOUCHES", 95},
        {"SM_CONVERTIBLESLATEMODE", 0x2003}, {"SM_SYSTEMDOCKED", 0x2004},
    };
    BOOL (WINAPI *pGetPointerDevices)(UINT32 *, void *);
    unsigned int i;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    printf("CoInitializeEx(STA) %#lx\n", hr);
    for (i = 0; i < ARRAYSIZE(classes); i++)
    {
        char path[256], server[512], model[64], progid[128];
        WCHAR wclsid[64];
        CLSID clsid;
        IUnknown *unk = NULL;

        sprintf(path, "CLSID\\%s\\InprocServer32", classes[i].clsid);
        reg_value(HKEY_CLASSES_ROOT, path, NULL, server, sizeof(server));
        reg_value(HKEY_CLASSES_ROOT, path, "ThreadingModel", model, sizeof(model));
        sprintf(path, "CLSID\\%s\\ProgID", classes[i].clsid);
        reg_value(HKEY_CLASSES_ROOT, path, NULL, progid, sizeof(progid));
        MultiByteToWideChar(CP_ACP, 0, classes[i].clsid, -1, wclsid, ARRAYSIZE(wclsid));
        CLSIDFromString(wclsid, &clsid);
        hr = CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unk);
        printf("%-20s %s create %#lx, server %s, model %s, progid %s\n", classes[i].name, classes[i].clsid, hr, server, model, progid);
        if (SUCCEEDED(hr)) IUnknown_Release(unk);
    }
    for (i = 0; i < ARRAYSIZE(metrics); i++)
        printf("%s = %#x\n", metrics[i].name, GetSystemMetrics(metrics[i].index));
    pGetPointerDevices = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), "GetPointerDevices");
    if (pGetPointerDevices)
    {
        struct { HANDLE device; int type; UINT32 startingCursorId, maxActiveContacts; int orientation; UINT32 monitor_pad[2]; RECT r1, r2; WCHAR desc[520]; } devices[16];
        UINT32 count = 0;
        BOOL ret = pGetPointerDevices(&count, NULL);
        printf("GetPointerDevices(count) %d, %u devices\n", ret, count);
        if (ret && count && count <= 16)
        {
            count = 16;
            if (pGetPointerDevices(&count, devices))
                for (i = 0; i < count; i++)
                    printf("  device %u: type %d (1 integrated pen, 2 external pen, 3 touch, 4 touchpad), max contacts %u\n",
                           i, devices[i].type, devices[i].maxActiveContacts);
        }
    }
    else printf("GetPointerDevices missing\n");
    return 0;
}
