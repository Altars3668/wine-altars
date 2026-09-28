/*
 * What IInternetSecurityManager::ProcessUrlAction() answers, with PUAF_NOUI, for actions whose policy in the
 * Internet zone is to allow, to ask and to refuse, for a file URL the way the Click-to-Run service asks, and for
 * a plain path with PUAF_ISFILE.  Nothing is asked without PUAF_NOUI, so nothing can put up a prompt; but see
 * PUAF_WARN_IF_DENIED below.
 */
#define COBJMACROS
#include <windows.h>
#include <urlmon.h>
#include <stdio.h>

#ifndef MUTZ_ISFILE
#define MUTZ_ISFILE 0x00000002
#endif

static IInternetSecurityManager *manager;

static void action(const WCHAR *url, DWORD action, DWORD flags)
{
    DWORD policy = 0xdeadbeef, zone = 0xdeadbeef;
    HRESULT hr, zone_hr;

    zone_hr = IInternetSecurityManager_MapUrlToZone(manager, url, &zone, (flags & PUAF_ISFILE) ? MUTZ_ISFILE : 0);
    hr = IInternetSecurityManager_ProcessUrlAction(manager, url, action, (BYTE *)&policy, sizeof(policy),
            NULL, 0, flags, 0);
    printf("%ls action %#lx flags %#lx: zone %lu (%#lx), hr %#lx, policy %#lx\n",
            url, action, flags, zone, zone_hr, hr, policy);
}

int main(void)
{
    static const WCHAR internet[] = L"http://www.example.com/";
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitialize(NULL);
    if (FAILED(hr = CoInternetCreateSecurityManager(NULL, &manager, 0)))
    {
        printf("CoInternetCreateSecurityManager %#lx\n", hr);
        return 1;
    }

    /* Internet zone: allow, ask, refuse. */
    action(internet, URLACTION_CREDENTIALS_USE, PUAF_NOUI);
    action(internet, URLACTION_DOWNLOAD_SIGNED_ACTIVEX, PUAF_NOUI);
    action(internet, URLACTION_DOWNLOAD_UNSIGNED_ACTIVEX, PUAF_NOUI);
    action(internet, URLACTION_ACTIVEX_OVERRIDE_OBJECT_SAFETY, PUAF_NOUI);
    action(internet, URLACTION_SHELL_FILE_DOWNLOAD, PUAF_NOUI);
    action(internet, URLACTION_DOWNLOAD_SIGNED_ACTIVEX, PUAF_NOUI | PUAF_WARN_IF_DENIED);
    /* PUAF_WARN_IF_DENIED with a refusal puts up a warning even with PUAF_NOUI: in a service session the call
     * never returns, so it is not asked. */
    /* What the Click-to-Run service asks. */
    action(L"file:///C:/Windows/win.ini", URLACTION_SHELL_FILE_DOWNLOAD, PUAF_NOUI);
    action(L"C:\\Windows\\win.ini", URLACTION_SHELL_FILE_DOWNLOAD, PUAF_NOUI | PUAF_ISFILE);
    /* With no URL. */
    action(NULL, URLACTION_SHELL_FILE_DOWNLOAD, PUAF_NOUI);

    IInternetSecurityManager_Release(manager);
    CoUninitialize();
    printf("done\n");
    return 0;
}
