/* probe_sharedmode: Windows.System.Profile.SharedModeSettings, and the factory behaviour of a
 * statics-only class next to it (RetailInfo). */
#define COBJMACROS
#include <stdio.h>
#include "windef.h"
#include "initguid.h"
#include "winbase.h"
#include "winstring.h"
#include "roapi.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_System_Profile
#include "windows.system.profile.h"

static void probe_factory(const WCHAR *name)
{
    IActivationFactory *factory = NULL;
    IInspectable *instance = (void *)0xdeadbeef;
    HSTRING str, class_name = (void *)0xdeadbeef;
    TrustLevel level = -1;
    IUnknown *unk;
    ULONG count = 0xdead;
    IID *iids = NULL;
    HRESULT hr;

    WindowsCreateString(name, wcslen(name), &str);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    WindowsDeleteString(str);
    printf("%ls: factory %#lx\n", name, hr);
    if (!factory) return;
    hr = IActivationFactory_GetRuntimeClassName(factory, &class_name);
    printf("  GetRuntimeClassName %#lx %p\n", hr, hr == S_OK ? (void *)1 : class_name);
    hr = IActivationFactory_GetTrustLevel(factory, &level);
    printf("  GetTrustLevel %#lx %d\n", hr, level);
    hr = IActivationFactory_GetIids(factory, &count, &iids);
    printf("  GetIids %#lx %lu\n", hr, hr == S_OK ? count : 0);
    if (hr == S_OK)
    {
        ULONG i;
        for (i = 0; i < count; i++)
        {
            WCHAR buf[64];
            StringFromGUID2(&iids[i], buf, 64);
            printf("    %ls\n", buf);
        }
        CoTaskMemFree(iids);
    }
    hr = IActivationFactory_ActivateInstance(factory, &instance);
    printf("  ActivateInstance %#lx %p\n", hr, instance);
    hr = IActivationFactory_QueryInterface(factory, &IID_IAgileObject, (void **)&unk);
    printf("  IAgileObject %#lx\n", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unk);
    hr = IActivationFactory_QueryInterface(factory, &IID_IMarshal, (void **)&unk);
    printf("  IMarshal %#lx\n", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unk);

    if (SUCCEEDED(IActivationFactory_QueryInterface(factory, &IID_ISharedModeSettingsStatics, (void **)&unk)))
    {
        ISharedModeSettingsStatics *statics = (void *)unk;
        boolean value = 2;
        hr = ISharedModeSettingsStatics_get_IsEnabled(statics, &value);
        printf("  IsEnabled %#lx %d\n", hr, value);
        hr = ISharedModeSettingsStatics_GetRuntimeClassName(statics, &class_name);
        printf("  statics GetRuntimeClassName %#lx\n", hr);
        ISharedModeSettingsStatics_Release(statics);
    }
    if (SUCCEEDED(IActivationFactory_QueryInterface(factory, &IID_ISharedModeSettingsStatics2, (void **)&unk)))
    {
        ISharedModeSettingsStatics2 *statics = (void *)unk;
        boolean value = 2;
        hr = ISharedModeSettingsStatics2_get_ShouldAvoidLocalStorage(statics, &value);
        printf("  ShouldAvoidLocalStorage %#lx %d\n", hr, value);
        ISharedModeSettingsStatics2_Release(statics);
    }
    else printf("  no ISharedModeSettingsStatics2\n");
    if (SUCCEEDED(IActivationFactory_QueryInterface(factory, &IID_IRetailInfoStatics, (void **)&unk)))
    {
        IRetailInfoStatics *statics = (void *)unk;
        boolean value = 2;
        hr = IRetailInfoStatics_get_IsDemoModeEnabled(statics, &value);
        printf("  IsDemoModeEnabled %#lx %d\n", hr, value);
        IRetailInfoStatics_Release(statics);
    }
    IActivationFactory_Release(factory);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    RoInitialize(RO_INIT_MULTITHREADED);
    probe_factory(L"Windows.System.Profile.SharedModeSettings");
    probe_factory(L"Windows.System.Profile.RetailInfo");
    RoUninitialize();
    printf("done\n");
    return 0;
}
