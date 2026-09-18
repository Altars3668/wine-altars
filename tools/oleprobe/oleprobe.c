/*
 * oleprobe -- does an embeddable object actually activate?
 *
 * Word's Insert > Object does three things: look the ProgID up, create the
 * class, and ask it for IOleObject.  A registration can look complete and
 * still fail at any of those -- most often because a 32-bit installer wrote
 * the class only under WOW6432Node, where a 64-bit Word never looks.  Build
 * this for both bitnesses and the difference says which view is missing.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o oleprobe.exe oleprobe.c -lole32 -loleaut32 -luuid
 */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <stdio.h>

/* Wide output through the console comes back mangled here, so everything is
 * converted to UTF-8 and written with plain printf. */
static const char *u8(const WCHAR *w)
{
    static char buf[4][512];
    static int n;
    char *p = buf[n = (n + 1) & 3];
    if (!w) return "(null)";
    WideCharToMultiByte(CP_UTF8, 0, w, -1, p, sizeof(buf[0]), NULL, NULL);
    return p;
}

static const char *ctx_name(DWORD ctx)
{
    switch (ctx)
    {
    case CLSCTX_INPROC_SERVER: return "进程内";
    case CLSCTX_LOCAL_SERVER:  return "本地服务器";
    default:                   return "任意";
    }
}

static void probe(const WCHAR *progid)
{
    CLSID clsid;
    HRESULT hr;
    OLECHAR *str = NULL;

    printf("\n=== %s ===\n", u8(progid));

    hr = CLSIDFromProgID(progid, &clsid);
    if (FAILED(hr))
    {
        printf("  ProgID 解析      失败 0x%08lx\n", (unsigned long)hr);
        return;
    }
    StringFromCLSID(&clsid, &str);
    printf("  ProgID 解析      %s\n", u8(str));
    CoTaskMemFree(str);

    /* Try the two contexts separately: which one works tells you whether the
     * class is served in-process or out, and an out-of-process server is the
     * one a 64-bit client can still drive when the binary is 32-bit. */
    for (int i = 0; i < 2; i++)
    {
        DWORD ctx = i ? CLSCTX_LOCAL_SERVER : CLSCTX_INPROC_SERVER;
        IUnknown *unk = NULL;

        hr = CoCreateInstance(&clsid, NULL, ctx, &IID_IUnknown, (void **)&unk);
        printf("  创建(%-12s) %s 0x%08lx\n", ctx_name(ctx),
               SUCCEEDED(hr) ? "成功" : "失败", (unsigned long)hr);
        if (FAILED(hr)) continue;

        IOleObject *ole = NULL;
        hr = IUnknown_QueryInterface(unk, &IID_IOleObject, (void **)&ole);
        printf("    IOleObject       %s 0x%08lx\n",
               SUCCEEDED(hr) ? "有" : "没有", (unsigned long)hr);
        if (SUCCEEDED(hr))
        {
            LPOLESTR name = NULL;
            if (SUCCEEDED(IOleObject_GetUserType(ole, USERCLASSTYPE_FULL, &name)) && name)
            {
                printf("    对象名           %s\n", u8(name));
                CoTaskMemFree(name);
            }
            IOleObject_Release(ole);
        }
        IUnknown_Release(unk);
    }
}

int wmain(int argc, WCHAR **argv)
{
    HRESULT hr = OleInitialize(NULL);
    if (FAILED(hr)) { printf("OleInitialize 失败 0x%08lx\n", (unsigned long)hr); return 1; }

    printf("oleprobe (%d 位)\n", (int)(sizeof(void *) * 8));
    if (argc > 1)
        for (int i = 1; i < argc; i++) probe(argv[i]);
    else
    {
        probe(L"Equation.DSMT4");
        probe(L"Equation.AxMath");
        probe(L"Equation.3");
    }
    OleUninitialize();
    return 0;
}
