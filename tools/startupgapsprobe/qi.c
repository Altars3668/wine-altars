/* qi: whether an in-process COM class answers QueryInterface for each of the given interfaces, in an MTA and in an
 * STA.  Unknown interfaces seen asked of Wine's objects are told apart from ones Windows has but Wine lacks.
 *
 *   qi.exe {clsid} {iid}...
 *
 * Prints results only. */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

static void ask(const WCHAR *clsid_text, int count, WCHAR **iids, const char *apartment)
{
    IUnknown *object, *other;
    CLSID clsid;
    IID iid;
    HRESULT hr;
    int i;

    CLSIDFromString(clsid_text, &clsid);
    hr = CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&object);
    printf("%s: CoCreateInstance %ls: %#lx\n", apartment, clsid_text, hr);
    if (FAILED(hr)) return;
    for (i = 0; i < count; i++)
    {
        if (FAILED(IIDFromString(iids[i], &iid)))
        {
            printf("    %ls: not an interface id\n", iids[i]);
            continue;
        }
        other = NULL;
        hr = IUnknown_QueryInterface(object, &iid, (void **)&other);
        printf("    %ls: %#lx\n", iids[i], hr);
        if (other) IUnknown_Release(other);
    }
    IUnknown_Release(object);
}

int wmain(int argc, WCHAR **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3)
    {
        printf("usage: qi {clsid} {iid}...\n");
        return 2;
    }
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ask(argv[1], argc - 2, argv + 2, "mta");
    CoUninitialize();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ask(argv[1], argc - 2, argv + 2, "sta");
    CoUninitialize();
    return 0;
}
