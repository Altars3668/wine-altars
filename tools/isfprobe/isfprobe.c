/*
 * isfprobe - what Windows' InkDisp writes and reads as Ink Serialized Format.
 *
 * Wine's inkobj needs IInkDisp::Save and Load: Office loads an ISF stream into a new InkDisp before it creates any
 * other ink object when it finds the ink platform.  This builds strokes with known points, drawing attributes and
 * extended properties, saves them in every persistence format and compression mode and prints the bytes (the
 * GIF ones only by size and header), loads them back, and prints what Load answers for empty, wrong and
 * truncated data, for base64 text with and without its header, and for an ink that already has strokes.  The
 * printed ISF streams are test vectors for Wine's decoder.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <msinkaut.h>
#include <stdio.h>

static const CLSID clsid_ink = {0x937c1a34, 0x151d, 0x4610, {0x9c, 0xa6, 0xa8, 0xcc, 0x9b, 0xdb, 0x5d, 0x83}};
static const IID iid_ink = {0x9d398fa0, 0xc4e2, 0x4fcd, {0x99, 0x73, 0x97, 0x5c, 0xaa, 0xf4, 0x7e, 0xa6}};
static const WCHAR *guid_x = L"{598A6A8F-52C0-4BA0-93AF-AF357411A561}";
static const WCHAR *guid_y = L"{B53F9F75-04E0-4498-A7EE-C30DBB5A9011}";
static const WCHAR *guid_pressure = L"{7307502D-F9F4-4E18-B3F2-2CE1B1A3610C}";
static const WCHAR *guid_custom1 = L"{1D1B7E27-6B3F-4E21-9F4A-2A7C0E5E6F01}";
static const WCHAR *guid_custom2 = L"{1D1B7E27-6B3F-4E21-9F4A-2A7C0E5E6F02}";
static const WCHAR *guid_custom3 = L"{1D1B7E27-6B3F-4E21-9F4A-2A7C0E5E6F03}";

static DWORD WINAPI watchdog(void *arg)
{
    Sleep(60000);
    printf("watchdog: still running after 60 s, exiting\n");
    ExitProcess(1);
}

static void dump(const BYTE *data, unsigned int size, unsigned int max)
{
    unsigned int i;
    for (i = 0; i < size && i < max; i++)
        printf("%s%02x%s", i % 32 ? "" : "    ", data[i], i % 32 == 31 || i + 1 == size || i + 1 == max ? "\n" : " ");
    if (size > max) printf("    ... (%u more)\n", size - max);
}

static IInkDisp *create_ink(void)
{
    IInkDisp *ink = NULL;
    HRESULT hr = CoCreateInstance(&clsid_ink, NULL, CLSCTX_INPROC_SERVER, &iid_ink, (void **)&ink);
    if (FAILED(hr)) printf("CoCreateInstance(InkDisp) %#lx\n", hr);
    return ink;
}

static VARIANT make_longs(const LONG *values, unsigned int count)
{
    VARIANT v;
    SAFEARRAY *sa = SafeArrayCreateVector(VT_I4, 0, count);
    LONG *p;
    SafeArrayAccessData(sa, (void **)&p);
    memcpy(p, values, count * sizeof(*p));
    SafeArrayUnaccessData(sa);
    V_VT(&v) = VT_ARRAY | VT_I4;
    V_ARRAY(&v) = sa;
    return v;
}

static VARIANT make_guids(const WCHAR **guids, unsigned int count)
{
    VARIANT v;
    SAFEARRAY *sa = SafeArrayCreateVector(VT_BSTR, 0, count);
    LONG i;
    for (i = 0; i < count; i++)
    {
        BSTR s = SysAllocString(guids[i]);
        SafeArrayPutElement(sa, &i, s);
        SysFreeString(s);
    }
    V_VT(&v) = VT_ARRAY | VT_BSTR;
    V_ARRAY(&v) = sa;
    return v;
}

static VARIANT make_bytes(const BYTE *data, unsigned int size)
{
    VARIANT v;
    SAFEARRAY *sa = SafeArrayCreateVector(VT_UI1, 0, size);
    BYTE *p;
    SafeArrayAccessData(sa, (void **)&p);
    if (size) memcpy(p, data, size);
    SafeArrayUnaccessData(sa);
    V_VT(&v) = VT_ARRAY | VT_UI1;
    V_ARRAY(&v) = sa;
    return v;
}

static HRESULT add_property(IInkExtendedProperties *props, const WCHAR *guid, VARIANT *value)
{
    IInkExtendedProperty *prop = NULL;
    BSTR name = SysAllocString(guid);
    HRESULT hr = IInkExtendedProperties_Add(props, name, *value, &prop);
    SysFreeString(name);
    if (prop) IInkExtendedProperty_Release(prop);
    return hr;
}

static void print_properties(const char *what, IInkExtendedProperties *props)
{
    LONG count = 0, i;
    IInkExtendedProperties_get_Count(props, &count);
    printf("  %s: %ld extended properties\n", what, count);
    for (i = 0; i < count; i++)
    {
        IInkExtendedProperty *prop = NULL;
        VARIANT index, data;
        BSTR guid = NULL;
        V_VT(&index) = VT_I4;
        V_I4(&index) = i;
        VariantInit(&data);
        if (FAILED(props->lpVtbl->Item(props, index, &prop))) continue; /* the header's macro names a parameter Item */
        IInkExtendedProperty_get_Guid(prop, &guid);
        IInkExtendedProperty_get_Data(prop, &data);
        printf("    %ls vt %#x", guid, V_VT(&data));
        if (V_VT(&data) == VT_I4) printf(" %ld", V_I4(&data));
        else if (V_VT(&data) == VT_R8) printf(" %g", V_R8(&data));
        else if (V_VT(&data) == VT_BSTR) printf(" \"%ls\"", V_BSTR(&data));
        else if (V_VT(&data) == (VT_ARRAY | VT_UI1))
        {
            LONG lb, ub;
            SafeArrayGetLBound(V_ARRAY(&data), 1, &lb);
            SafeArrayGetUBound(V_ARRAY(&data), 1, &ub);
            printf(" %ld bytes", ub - lb + 1);
        }
        printf("\n");
        SysFreeString(guid);
        VariantClear(&data);
        IInkExtendedProperty_Release(prop);
    }
}

/* two strokes: X,Y and X,Y,NormalPressure; attributes and properties on the second and on the ink */
static IInkDisp *build_ink(void)
{
    static const LONG xy[] = {1000, 1000, 1500, 1200, 2100, 1300, 2600, 1350, 3200, 1360};
    static const LONG xyp[] = {-500, 4000, -300, 4200, 0, 4500, 400, 4700, 900, 4800, 1500, 4850, 2200, 4870};
    static const LONG p[] = {100, 300, 512, 700, 1023, 800, 600};
    const WCHAR *desc3[] = {guid_x, guid_y, guid_pressure};
    IInkDrawingAttributes *attrs = NULL;
    IInkExtendedProperties *props = NULL;
    IInkStrokeDisp *stroke = NULL;
    VARIANT data, desc, value;
    LONG packets[21];
    IInkDisp *ink;
    unsigned int i;
    HRESULT hr;

    if (!(ink = create_ink())) return NULL;
    data = make_longs(xy, ARRAYSIZE(xy));
    V_VT(&desc) = VT_EMPTY;
    hr = IInkDisp_CreateStroke(ink, data, desc, &stroke);
    printf("CreateStroke xy %#lx\n", hr);
    VariantClear(&data);
    if (stroke) IInkStrokeDisp_Release(stroke);

    for (i = 0; i < 7; i++)
    {
        packets[i * 3] = xyp[i * 2];
        packets[i * 3 + 1] = xyp[i * 2 + 1];
        packets[i * 3 + 2] = p[i];
    }
    data = make_longs(packets, 21);
    desc = make_guids(desc3, 3);
    stroke = NULL;
    hr = IInkDisp_CreateStroke(ink, data, desc, &stroke);
    printf("CreateStroke xyp %#lx\n", hr);
    VariantClear(&data);
    VariantClear(&desc);
    if (!stroke) return ink;

    if (SUCCEEDED(IInkStrokeDisp_get_DrawingAttributes(stroke, &attrs)))
    {
        IInkDrawingAttributes_put_Color(attrs, RGB(0x12, 0x34, 0x56));
        IInkDrawingAttributes_put_Width(attrs, 106.5f);
        IInkDrawingAttributes_put_Height(attrs, 30.25f);
        IInkDrawingAttributes_put_PenTip(attrs, IPT_Rectangle);
        IInkDrawingAttributes_put_FitToCurve(attrs, VARIANT_TRUE);
        IInkDrawingAttributes_put_IgnorePressure(attrs, VARIANT_TRUE);
        IInkDrawingAttributes_put_AntiAliased(attrs, VARIANT_FALSE);
        IInkDrawingAttributes_put_Transparency(attrs, 128);
        IInkDrawingAttributes_put_RasterOperation(attrs, IRO_MaskPen);
        if (SUCCEEDED(IInkDrawingAttributes_get_ExtendedProperties(attrs, &props)))
        {
            V_VT(&value) = VT_I4;
            V_I4(&value) = 0x12345678;
            printf("attrs Add VT_I4 %#lx\n", add_property(props, guid_custom1, &value));
            IInkExtendedProperties_Release(props);
        }
        hr = IInkStrokeDisp_putref_DrawingAttributes(stroke, attrs);
        printf("putref_DrawingAttributes %#lx\n", hr);
        IInkDrawingAttributes_Release(attrs);
    }
    if (SUCCEEDED(IInkStrokeDisp_get_ExtendedProperties(stroke, &props)))
    {
        static const BYTE bytes[] = {1, 2, 3, 4, 5, 250, 251, 252};
        V_VT(&value) = VT_BSTR;
        V_BSTR(&value) = SysAllocString(L"ink note");
        printf("stroke Add VT_BSTR %#lx\n", add_property(props, guid_custom2, &value));
        VariantClear(&value);
        value = make_bytes(bytes, sizeof(bytes));
        printf("stroke Add VT_ARRAY|VT_UI1 %#lx\n", add_property(props, guid_custom3, &value));
        VariantClear(&value);
        IInkExtendedProperties_Release(props);
    }
    IInkStrokeDisp_Release(stroke);
    if (SUCCEEDED(IInkDisp_get_ExtendedProperties(ink, &props)))
    {
        V_VT(&value) = VT_R8;
        V_R8(&value) = 2.5;
        printf("ink Add VT_R8 %#lx\n", add_property(props, guid_custom1, &value));
        IInkExtendedProperties_Release(props);
    }
    return ink;
}

static void describe_ink(IInkDisp *ink)
{
    IInkExtendedProperties *props;
    IInkStrokes *strokes = NULL;
    VARIANT_BOOL dirty = 12345;
    LONG count = 0, i, j;

    IInkDisp_get_Dirty(ink, &dirty);
    IInkDisp_get_Strokes(ink, &strokes);
    if (strokes) IInkStrokes_get_Count(strokes, &count);
    printf("  dirty %d, %ld strokes\n", dirty, count);
    for (i = 0; i < count; i++)
    {
        IInkDrawingAttributes *attrs = NULL;
        IInkStrokeDisp *stroke = NULL;
        VARIANT desc, data;
        LONG id = -1, n = 0;
        float width = 0, height = 0;
        LONG color = 0, transparency = 0;
        InkPenTip tip = 0;
        InkRasterOperation rop = 0;
        VARIANT_BOOL fit = 0, ignore = 0, aa = 0;

        if (FAILED(IInkStrokes_Item(strokes, i, &stroke))) continue;
        IInkStrokeDisp_get_ID(stroke, &id);
        IInkStrokeDisp_get_PacketCount(stroke, &n);
        VariantInit(&desc);
        IInkStrokeDisp_get_PacketDescription(stroke, &desc);
        printf("  stroke %ld: id %ld, %ld packets, description", i, id, n);
        if (V_VT(&desc) == (VT_ARRAY | VT_BSTR))
        {
            LONG lb, ub, k;
            SafeArrayGetLBound(V_ARRAY(&desc), 1, &lb);
            SafeArrayGetUBound(V_ARRAY(&desc), 1, &ub);
            for (k = lb; k <= ub; k++)
            {
                BSTR s = NULL;
                SafeArrayGetElement(V_ARRAY(&desc), &k, &s);
                printf(" %.9ls", s);
                SysFreeString(s);
            }
        }
        printf("\n");
        VariantClear(&desc);
        VariantInit(&data);
        if (SUCCEEDED(IInkStrokeDisp_GetPacketData(stroke, 0, -1, &data)) && V_VT(&data) == (VT_ARRAY | VT_I4))
        {
            LONG lb, ub, *p;
            SafeArrayGetLBound(V_ARRAY(&data), 1, &lb);
            SafeArrayGetUBound(V_ARRAY(&data), 1, &ub);
            SafeArrayAccessData(V_ARRAY(&data), (void **)&p);
            printf("    packets");
            for (j = 0; j <= ub - lb; j++) printf(" %ld", p[j]);
            printf("\n");
            SafeArrayUnaccessData(V_ARRAY(&data));
        }
        VariantClear(&data);
        for (j = 0; j < 3; j++)
        {
            const WCHAR *names[] = {guid_x, guid_y, guid_pressure};
            BSTR name = SysAllocString(names[j]);
            LONG min = 0, max = 0;
            TabletPropertyMetricUnit units = 0;
            float res = 0;
            HRESULT hr = IInkStrokeDisp_GetPacketDescriptionPropertyMetrics(stroke, name, &min, &max, &units, &res);
            if (hr == S_OK) printf("    metrics %d: %ld..%ld units %d resolution %f\n", (int)j, min, max, units, res);
            else printf("    metrics %d: %#lx\n", (int)j, hr);
            SysFreeString(name);
        }
        if (SUCCEEDED(IInkStrokeDisp_get_DrawingAttributes(stroke, &attrs)))
        {
            IInkDrawingAttributes_get_Color(attrs, &color);
            IInkDrawingAttributes_get_Width(attrs, &width);
            IInkDrawingAttributes_get_Height(attrs, &height);
            IInkDrawingAttributes_get_PenTip(attrs, &tip);
            IInkDrawingAttributes_get_FitToCurve(attrs, &fit);
            IInkDrawingAttributes_get_IgnorePressure(attrs, &ignore);
            IInkDrawingAttributes_get_AntiAliased(attrs, &aa);
            IInkDrawingAttributes_get_Transparency(attrs, &transparency);
            IInkDrawingAttributes_get_RasterOperation(attrs, &rop);
            printf("    attrs color %#lx width %f height %f tip %d fit %d ignore %d aa %d transparency %ld rop %d\n",
                   color, width, height, tip, fit, ignore, aa, transparency, rop);
            if (SUCCEEDED(IInkDrawingAttributes_get_ExtendedProperties(attrs, &props)))
            {
                print_properties("attrs", props);
                IInkExtendedProperties_Release(props);
            }
            IInkDrawingAttributes_Release(attrs);
        }
        if (SUCCEEDED(IInkStrokeDisp_get_ExtendedProperties(stroke, &props)))
        {
            print_properties("stroke", props);
            IInkExtendedProperties_Release(props);
        }
        IInkStrokeDisp_Release(stroke);
    }
    if (strokes) IInkStrokes_Release(strokes);
    if (SUCCEEDED(IInkDisp_get_ExtendedProperties(ink, &props)))
    {
        print_properties("ink", props);
        IInkExtendedProperties_Release(props);
    }
}

static HRESULT save(IInkDisp *ink, InkPersistenceFormat format, InkPersistenceCompressionMode mode, BYTE **out, LONG *size)
{
    VARIANT data;
    HRESULT hr;
    BYTE *p;

    VariantInit(&data);
    hr = IInkDisp_Save(ink, format, mode, &data);
    *out = NULL;
    *size = 0;
    printf("Save format %d compression %d: %#lx vt %#x", format, mode, hr, V_VT(&data));
    if (SUCCEEDED(hr) && V_VT(&data) == (VT_ARRAY | VT_UI1))
    {
        LONG lb, ub;
        SafeArrayGetLBound(V_ARRAY(&data), 1, &lb);
        SafeArrayGetUBound(V_ARRAY(&data), 1, &ub);
        *size = ub - lb + 1;
        *out = malloc(*size);
        SafeArrayAccessData(V_ARRAY(&data), (void **)&p);
        memcpy(*out, p, *size);
        SafeArrayUnaccessData(V_ARRAY(&data));
    }
    printf(", %ld bytes\n", *size);
    VariantClear(&data);
    return hr;
}

static HRESULT load(const char *what, const BYTE *bytes, LONG size, IInkDisp **out)
{
    IInkDisp *ink = create_ink();
    VARIANT data = make_bytes(bytes, size);
    HRESULT hr = IInkDisp_Load(ink, data);
    printf("Load %s (%ld bytes): %#lx\n", what, size, hr);
    VariantClear(&data);
    if (out) *out = ink;
    else IInkDisp_Release(ink);
    return hr;
}

int main(void)
{
    static const BYTE empty[] = {0, 0};
    static const BYTE version1[] = {1, 0};
    IInkDisp *ink, *loaded, *empty_ink;
    BYTE *bytes, *isf = NULL;
    LONG size, isf_size = 0;
    unsigned int format, mode;
    VARIANT data;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (!(empty_ink = create_ink())) return 0;
    printf("empty ink:\n");
    save(empty_ink, IPF_InkSerializedFormat, IPCM_Default, &bytes, &size);
    dump(bytes, size, 64);
    free(bytes);
    VariantInit(&data);
    hr = IInkDisp_Save(empty_ink, IPF_InkSerializedFormat, IPCM_Default, NULL);
    printf("Save NULL data %#lx\n", hr);
    hr = IInkDisp_Save(empty_ink, 17, IPCM_Default, &data);
    printf("Save format 17 %#lx vt %#x\n", hr, V_VT(&data));
    VariantClear(&data);
    hr = IInkDisp_Save(empty_ink, IPF_InkSerializedFormat, 17, &data);
    printf("Save compression 17 %#lx vt %#x\n", hr, V_VT(&data));
    VariantClear(&data);

    if (!(ink = build_ink())) return 0;
    printf("built ink:\n");
    describe_ink(ink);
    for (format = IPF_InkSerializedFormat; format <= IPF_Base64GIF; format++)
        for (mode = IPCM_Default; mode <= IPCM_NoCompression; mode++)
        {
            if (FAILED(save(ink, format, mode, &bytes, &size))) continue;
            if (format == IPF_InkSerializedFormat) dump(bytes, size, 4096);
            else if (format == IPF_Base64InkSerializedFormat) printf("    \"%.*s\"%s\n", (int)(size > 400 ? 400 : size), bytes, size > 400 ? "..." : "");
            else dump(bytes, size, 16);
            if (format == IPF_InkSerializedFormat && mode == IPCM_Default)
            {
                isf = bytes;
                isf_size = size;
                bytes = NULL;
            }
            else if (format != IPF_InkSerializedFormat || mode != IPCM_Default)
            {
                char what[64];
                sprintf(what, "saved format %u mode %u", format, mode);
                if (SUCCEEDED(load(what, bytes, size, &loaded)) && format == IPF_Base64InkSerializedFormat && mode == IPCM_Default)
                    describe_ink(loaded);
                IInkDisp_Release(loaded);
                if (format == IPF_Base64InkSerializedFormat && size > 7)
                {
                    load("base64 without its last byte", bytes, size - 1, NULL);
                    if (!memcmp(bytes, "base64:", 7)) load("base64 without its header", bytes + 7, size - 7, NULL);
                }
            }
            free(bytes);
        }

    if (isf)
    {
        if (SUCCEEDED(load("saved ISF", isf, isf_size, &loaded)))
        {
            describe_ink(loaded);
            hr = IInkDisp_Load(loaded, (data = make_bytes(isf, isf_size), data));
            printf("Load again into the loaded ink %#lx\n", hr);
            VariantClear(&data);
            describe_ink(loaded);
        }
        IInkDisp_Release(loaded);
        load("ISF without its last byte", isf, isf_size - 1, NULL);
        load("ISF's first half", isf, isf_size / 2, NULL);
        hr = IInkDisp_Load(ink, (data = make_bytes(isf, isf_size), data));
        printf("Load into the ink with strokes %#lx\n", hr);
        VariantClear(&data);
        free(isf);
    }
    load("empty stream {0,0}", empty, sizeof(empty), NULL);
    load("version 1", version1, sizeof(version1), NULL);
    load("no bytes", empty, 0, NULL);
    {
        IInkDisp *other = create_ink();
        VARIANT v;
        V_VT(&v) = VT_I4;
        V_I4(&v) = 0;
        printf("Load VT_I4 %#lx\n", IInkDisp_Load(other, v));
        V_VT(&v) = VT_EMPTY;
        printf("Load VT_EMPTY %#lx\n", IInkDisp_Load(other, v));
        IInkDisp_Release(other);
    }
    IInkDisp_Release(ink);
    IInkDisp_Release(empty_ink);
    CoUninitialize();
    return 0;
}
