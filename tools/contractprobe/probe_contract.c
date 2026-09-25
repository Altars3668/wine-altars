/* probe_contract: RoIsApiContractPresent / RoIsApiContractMajorVersionPresent, across the
 * contracts and versions an application may ask about. */
#include <stdio.h>
#include <windows.h>
#include <winternl.h>

typedef HRESULT (WINAPI *major_fn)(const WCHAR *, UINT16, BOOL *);
typedef HRESULT (WINAPI *full_fn)(const WCHAR *, UINT16, UINT16, BOOL *);

int main(void)
{
    static const WCHAR *names[] =
    {
        L"Windows.Foundation.UniversalApiContract",
        L"Windows.Foundation.FoundationContract",
        L"Windows.ApplicationModel.Calls.CallsPhoneContract",
        L"Windows.Networking.Connectivity.WwanContract",
        L"Windows.System.Profile.ProfileSharedModeContract",
        L"Windows.System.Profile.SystemManufacturers.SystemManufacturersContract",
        L"Windows.UI.ViewManagement.ViewManagementViewScalingContract",
        L"Windows.Security.Isolation.IsolatedWindowsEnvironmentContract",
        L"Windows.Phone.PhoneContract",
        L"Windows.Services.Store.StoreContract",
        L"Windows.Globalization.GlobalizationJapanesePhoneticAnalyzerContract",
        L"Windows.ApplicationModel.StartupTaskContract",
        L"Windows.Foundation.UniversalApiContractX",
        L"windows.foundation.universalapicontract",
        L"Windows.Foundation.UniversalApiContract ",
        L"Windows.Foundation",
        L"",
    };
    HMODULE combase = LoadLibraryW(L"combase.dll"), wintypes = LoadLibraryW(L"wintypes.dll");
    major_fn major = (major_fn)GetProcAddress(wintypes, "RoIsApiContractMajorVersionPresent");
    full_fn full = (full_fn)GetProcAddress(wintypes, "RoIsApiContractPresent");
    RTL_OSVERSIONINFOW info = {sizeof(info)};
    NTSTATUS (WINAPI *get_version)(RTL_OSVERSIONINFOW *) = (void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    BOOL present;
    HRESULT hr;
    int i, v;

    setvbuf(stdout, NULL, _IONBF, 0);
    get_version(&info);
    printf("os %lu.%lu.%lu; combase %p wintypes %p, major %p full %p\n", info.dwMajorVersion, info.dwMinorVersion,
           info.dwBuildNumber, combase, wintypes, major, full);
    if (!major || !full) return 1;

    for (i = 0; i < ARRAY_SIZE(names); i++)
    {
        int last = -1;
        printf("%ls:", names[i]);
        for (v = 0; v <= 25; v++)
        {
            present = 2;
            hr = major(names[i], v, &present);
            if (hr) { printf(" [%d: %#lx]", v, hr); continue; }
            if (present) last = v;
        }
        printf(" majors present up to %d", last);
        if (last >= 0)
        {
            int m, last_minor = -1;
            for (m = 0; m <= 5; m++)
            {
                present = 2;
                hr = full(names[i], last, m, &present);
                if (hr) { printf(" [minor %d: %#lx]", m, hr); continue; }
                if (present) last_minor = m;
            }
            printf(", minor up to %d", last_minor);
            present = 2;
            hr = full(names[i], last + 1, 0, &present);
            printf(", %d.0 %#lx %d", last + 1, hr, present);
            present = 2;
            hr = full(names[i], 0, 0, &present);
            printf(", 0.0 %#lx %d", hr, present);
            present = 2;
            hr = full(names[i], 1, 65535, &present);
            printf(", 1.65535 %#lx %d", hr, present);
        }
        printf("\n");
    }
    present = 2;
    hr = major(NULL, 1, &present);
    printf("NULL name: %#lx %d\n", hr, present);
    hr = full(NULL, 1, 0, &present);
    printf("NULL name (full): %#lx %d\n", hr, present);
    printf("done\n");
    return 0;
}
