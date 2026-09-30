/*
 * inkprobe - which ink components Windows has, where they live, and what the system says about pens.
 *
 * Office refers to the Tablet PC ink objects (InkDisp, InkRenderer, InkCollector, InkDrawingAttributes,
 * InkTransform, InkRectangle, InkRecognizers, RealTimeStylus) and to Windows 10's InkDesktopHost and
 * InkD2DRenderer; Wine has none of them.  PowerPoint's slide show pen draws nothing under Wine, and
 * Word shows no Draw tab.  This prints, for each class, its InprocServer32 and threading model and
 * what CoCreateInstance answers on an STA thread (the object is released at once), then the system
 * metrics an application can read about pens and touch, and the pointer devices.  Office asks
 * InkDisp, a stroke and drawing attributes for interfaces the SDK does not declare; the probe asks
 * Windows' own objects for them, and prints the metrics a stroke made by CreateStroke gives.  For an
 * interface an object has, it prints the module and offset of its vtable and of each slot, and at
 * the end the signature of every module the probe loaded, from which Microsoft's symbol server
 * gives the binary and its public symbols.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#define _WIN32_WINNT 0x0a00
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <msinkaut.h>
#include <psapi.h>
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

static const char *module_name(HMODULE module, char *path, DWORD size)
{
    char *name;
    if (!GetModuleFileNameA(module, path, size)) return "?";
    name = strrchr(path, '\\');
    return name ? name + 1 : path;
}

/* the vtable of an interface, as module+offset, and each slot while it points into the same module */
static void print_vtable(IUnknown *iface)
{
    void **vtbl = *(void ***)iface;
    HMODULE module, slot_module;
    char path[MAX_PATH];
    unsigned int i;

    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (const char *)vtbl, &module))
    {
        printf("    vtable outside any module\n");
        return;
    }
    printf("    vtable %s+%#lx\n", module_name(module, path, sizeof(path)), (unsigned long)((ULONG_PTR)vtbl - (ULONG_PTR)module));
    for (i = 0; i < 40; i++)
    {
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                vtbl[i], &slot_module) || slot_module != module)
            break;
        printf("    slot %2u +%#lx\n", i, (unsigned long)((ULONG_PTR)vtbl[i] - (ULONG_PTR)module));
    }
}

/* where Microsoft's symbol server has a module and its public symbols */
static void print_signature(HMODULE module)
{
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)module + ((IMAGE_DOS_HEADER *)module)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    IMAGE_DEBUG_DIRECTORY *debug = (IMAGE_DEBUG_DIRECTORY *)((BYTE *)module + dir->VirtualAddress);
    char path[MAX_PATH];
    const char *name = module_name(module, path, sizeof(path));
    unsigned int i;

    printf("module %s %s/%08lX%lx", name, name, nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
    for (i = 0; dir->VirtualAddress && i < dir->Size / sizeof(*debug); i++)
    {
        const struct { DWORD signature; GUID guid; DWORD age; char name[1]; } *cv;
        const char *pdb;

        if (debug[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW || !debug[i].AddressOfRawData) continue;
        cv = (const void *)((BYTE *)module + debug[i].AddressOfRawData);
        if (cv->signature != 0x53445352) continue;
        pdb = strrchr(cv->name, '\\') ? strrchr(cv->name, '\\') + 1 : cv->name;
        printf(" %s/%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%lX", pdb, cv->guid.Data1, cv->guid.Data2, cv->guid.Data3,
               cv->guid.Data4[0], cv->guid.Data4[1], cv->guid.Data4[2], cv->guid.Data4[3],
               cv->guid.Data4[4], cv->guid.Data4[5], cv->guid.Data4[6], cv->guid.Data4[7], cv->age);
        break;
    }
    printf("\n");
}

static void query(const char *what, IUnknown *object)
{
    static const struct { const char *name; GUID iid; } iids[] =
    {
        {"{49e015bc-9e7a-4482-a345-a52a9f9d6daa}", {0x49e015bc, 0x9e7a, 0x4482, {0xa3, 0x45, 0xa5, 0x2a, 0x9f, 0x9d, 0x6d, 0xaa}}},
        {"{4a145a90-383d-42b4-9883-36677a41f7f9}", {0x4a145a90, 0x383d, 0x42b4, {0x98, 0x83, 0x36, 0x67, 0x7a, 0x41, 0xf7, 0xf9}}},
        {"{c673d14b-ae8b-40fb-8775-b946baeebd30}", {0xc673d14b, 0xae8b, 0x40fb, {0x87, 0x75, 0xb9, 0x46, 0xba, 0xee, 0xbd, 0x30}}},
        {"{e19c7100-9709-4db7-9373-e7b518b47086}", {0xe19c7100, 0x9709, 0x4db7, {0x93, 0x73, 0xe7, 0xb5, 0x18, 0xb4, 0x70, 0x86}}},
        {"IMarshal", {0x00000003, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}},
        {"IPersistStream", {0x00000109, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}},
        {"IAgileObject", {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}}},
    };
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(iids); i++)
    {
        IUnknown *unk = NULL;
        HRESULT hr = IUnknown_QueryInterface(object, &iids[i].iid, (void **)&unk);
        printf("%s QI %s %#lx\n", what, iids[i].name, hr);
        if (SUCCEEDED(hr) && i < 4) print_vtable(unk);
        if (SUCCEEDED(hr)) IUnknown_Release(unk);
    }
}

/* a stroke of two points from CreateStroke, and the metrics it says its X and Y have */
static void probe_objects(void)
{
    static const GUID clsid_ink = {0x937c1a34, 0x151d, 0x4610, {0x9c, 0xa6, 0xa8, 0xcc, 0x9b, 0xdb, 0x5d, 0x83}};
    static const GUID clsid_attributes = {0xd8bf32a2, 0x05a5, 0x44c3, {0xb3, 0xaa, 0x5e, 0x80, 0xac, 0x7d, 0x25, 0x76}};
    static const GUID iid_ink = {0x9d398fa0, 0xc4e2, 0x4fcd, {0x99, 0x73, 0x97, 0x5c, 0xaa, 0xf4, 0x7e, 0xa6}};
    static const GUID iid_attributes = {0xbf519b75, 0x0a15, 0x4623, {0xad, 0xc9, 0xc0, 0x0d, 0x43, 0x6a, 0x80, 0x92}};
    static const WCHAR *names[] = {L"{598A6A8F-52C0-4BA0-93AF-AF357411A561}", L"{B53F9F75-04E0-4498-A7EE-C30DBB5A9011}"};
    IInkDrawingAttributes *attributes;
    VARIANT data, description;
    IInkStrokeDisp *stroke;
    SAFEARRAY *array;
    IInkDisp *ink;
    LONG *values, min, max;
    TabletPropertyMetricUnit units;
    float resolution;
    unsigned int i;
    HRESULT hr;

    if (FAILED(hr = CoCreateInstance(&clsid_ink, NULL, CLSCTX_INPROC_SERVER, &iid_ink, (void **)&ink)))
    {
        printf("InkDisp %#lx\n", hr);
        return;
    }
    query("InkDisp", (IUnknown *)ink);
    array = SafeArrayCreateVector(VT_I4, 0, 4);
    SafeArrayAccessData(array, (void **)&values);
    values[0] = 1000; values[1] = 1000; values[2] = 2000; values[3] = 1500;
    SafeArrayUnaccessData(array);
    V_VT(&data) = VT_ARRAY | VT_I4;
    V_ARRAY(&data) = array;
    V_VT(&description) = VT_EMPTY;
    hr = IInkDisp_CreateStroke(ink, data, description, &stroke);
    printf("CreateStroke %#lx\n", hr);
    VariantClear(&data);
    if (SUCCEEDED(hr))
    {
        query("stroke", (IUnknown *)stroke);
        for (i = 0; i < 2; i++)
        {
            BSTR name = SysAllocString(names[i]);
            hr = IInkStrokeDisp_GetPacketDescriptionPropertyMetrics(stroke, name, &min, &max, &units, &resolution);
            printf("metrics %s: %#lx, min %ld, max %ld, units %d, resolution %f\n", i ? "Y" : "X", hr, min, max, units, resolution);
            SysFreeString(name);
        }
        IInkStrokeDisp_Release(stroke);
    }
    IInkDisp_Release(ink);

    if (SUCCEEDED(CoCreateInstance(&clsid_attributes, NULL, CLSCTX_INPROC_SERVER, &iid_attributes, (void **)&attributes)))
    {
        query("InkDrawingAttributes", (IUnknown *)attributes);
        IInkDrawingAttributes_Release(attributes);
    }
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
    probe_objects();
    for (i = 0; i < ARRAYSIZE(metrics); i++)
        printf("%s = %#x\n", metrics[i].name, GetSystemMetrics(metrics[i].index));
    pGetPointerDevices = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), "GetPointerDevices");
    if (pGetPointerDevices)
    {
        POINTER_DEVICE_INFO devices[16];
        UINT32 count = 0;
        BOOL ret = pGetPointerDevices(&count, NULL);
        printf("GetPointerDevices(count) %d, %u devices\n", ret, count);
        if (ret && count && count <= 16)
        {
            count = 16;
            if (pGetPointerDevices(&count, devices))
                for (i = 0; i < count; i++)
                    printf("  device %u: type %d (1 integrated pen, 2 external pen, 3 touch, 4 touchpad), max contacts %u, "
                           "starting cursor %lu, orientation %lu, \"%ls\"\n", i, devices[i].pointerDeviceType,
                           devices[i].maxActiveContacts, devices[i].startingCursorId, devices[i].displayOrientation,
                           devices[i].productString);
        }
    }
    else printf("GetPointerDevices missing\n");

    {
        HMODULE modules[256];
        DWORD size = 0;

        if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &size))
            for (i = 1; i < size / sizeof(HMODULE) && i < ARRAYSIZE(modules); i++)
                print_signature(modules[i]);
    }
    return 0;
}
