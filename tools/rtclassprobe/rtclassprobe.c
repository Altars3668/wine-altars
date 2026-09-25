/*
 * rtclassprobe - which of the Windows Runtime classes Word asks for as it
 * starts can be activated, and which of them hand out IActivationFactory.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>

DEFINE_GUID(IID_IActivationFactory_, 0x00000035, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

static const WCHAR *classes[] =
{
    L"Windows.UI.Composition.Interactions.VisualInteractionSource",
    L"Windows.Security.EnterpriseData.ProtectionPolicyManager",
    L"Windows.UI.Composition.Interactions.CompositionConditionalValue",
    L"Windows.Globalization.Language",
    L"Windows.ApplicationModel.UserActivities.UserActivityRequestManager",
    L"Windows.UI.Composition.Interactions.InteractionTracker",
    L"Windows.System.DispatcherQueue",
    L"Windows.Security.Authentication.OnlineId.OnlineIdSystemAuthenticator",
    L"Windows.Security.Authentication.OnlineId.OnlineIdServiceTicketRequest",
    L"Windows.Foundation.Uri",
    L"Windows.Foundation.PropertyValue",
    L"Windows.UI.ViewManagement.UISettings",
    L"Windows.UI.Composition.Core.CompositorController",
    L"Windows.System.Profile.SharedModeSettings",
    L"Windows.Storage.Streams.DataWriter",
    L"Windows.Storage.Streams.DataReader",
    L"Windows.Storage.Streams.Buffer",
    L"Windows.Networking.Sockets.MessageWebSocket",
    L"Windows.Networking.HostName",
    L"Windows.Networking.Connectivity.NetworkInformation",
    L"Windows.Management.Deployment.PackageManager",
    L"Windows.Management.Deployment.AddPackageOptions",
    L"Windows.Foundation.Collections.PropertySet",
    L"Windows.ApplicationModel.Core.CoreApplication",
    L"Microsoft.UI.Dispatching.DispatcherQueue",
};

int main(void)
{
    unsigned int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    RoInitialize(RO_INIT_MULTITHREADED);
    for (i = 0; i < ARRAYSIZE(classes); i++)
    {
        HSTRING name;
        IUnknown *factory = NULL;
        HRESULT hr;

        WindowsCreateString(classes[i], wcslen(classes[i]), &name);
        hr = RoGetActivationFactory(name, &IID_IActivationFactory_, (void **)&factory);
        printf("%-70ls %#lx\n", classes[i], hr);
        if (factory) IUnknown_Release(factory);
        WindowsDeleteString(name);
    }
    printf("done\n");
    return 0;
}
