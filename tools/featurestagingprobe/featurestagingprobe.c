/* What the feature staging functions answer when nothing is staged for a feature.
 *
 * WinAppSDK's CoreMessagingXP.dll, which Office ships, imports UnsubscribeFeatureStateChangeNotification
 * from shcore; WIL's feature flags go through GetFeatureEnabledState, GetFeatureVariant and the
 * subscription functions.  This prints where each function is found (shcore and the two API sets),
 * what GetFeatureEnabledState and GetFeatureVariant answer for feature ids nothing stages, and what
 * SubscribeFeatureStateChangeNotification hands back.  RecordFeatureUsage and RecordFeatureError are
 * only looked up, not called: they record usage.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror featurestagingprobe.c -o featurestagingprobe.exe
 */
#include <windows.h>
#include <stdio.h>

static const char *names[] = {"GetFeatureEnabledState", "GetFeatureVariant", "RecordFeatureError", "RecordFeatureUsage",
                              "SubscribeFeatureStateChangeNotification", "UnsubscribeFeatureStateChangeNotification"};
static const char *modules[] = {"shcore.dll", "api-ms-win-core-featurestaging-l1-1-0.dll", "api-ms-win-core-featurestaging-l1-1-1.dll",
                                "kernelbase.dll"};

static void WINAPI changed(void *context)
{
    printf("callback %p\n", context);
}

int main(void)
{
    int (WINAPI *enabled)(UINT32, int);
    UINT32 (WINAPI *variant)(UINT32, int, UINT32 *, BOOL *);
    void (WINAPI *subscribe)(HANDLE *, void (WINAPI *)(void *), void *);
    void (WINAPI *unsubscribe)(HANDLE);
    static const UINT32 ids[] = {0, 1, 12345678, 0xffffffff};
    HMODULE shcore = LoadLibraryA("shcore.dll");
    unsigned int i, j;

    for (i = 0; i < ARRAYSIZE(modules); i++)
    {
        HMODULE module = LoadLibraryExA(modules[i], NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        char path[MAX_PATH] = "";

        if (module) GetModuleFileNameA(module, path, MAX_PATH);
        printf("%s: %s\n", modules[i], module ? strrchr(path, '\\') + 1 : "missing");
        for (j = 0; module && j < ARRAYSIZE(names); j++)
            printf("    %s: %s\n", names[j], GetProcAddress(module, names[j]) ? "present" : "absent");
    }

    enabled = (void *)GetProcAddress(shcore, "GetFeatureEnabledState");
    variant = (void *)GetProcAddress(shcore, "GetFeatureVariant");
    subscribe = (void *)GetProcAddress(shcore, "SubscribeFeatureStateChangeNotification");
    unsubscribe = (void *)GetProcAddress(shcore, "UnsubscribeFeatureStateChangeNotification");
    for (i = 0; enabled && i < ARRAYSIZE(ids); i++)
        for (j = 0; j < 4; j++) printf("GetFeatureEnabledState(%#x, %u): %d\n", ids[i], j, enabled(ids[i], j));
    for (i = 0; variant && i < ARRAYSIZE(ids); i++)
    {
        UINT32 payload = 0xdeadbeef, ret;
        BOOL notify = 0xcc;

        ret = variant(ids[i], 0, &payload, &notify);
        printf("GetFeatureVariant(%#x): %u, payload %#x, notification %d\n", ids[i], ret, payload, notify);
    }
    if (subscribe)
    {
        HANDLE sub = (HANDLE)0xdeadbeef, sub2 = (HANDLE)0xdeadbeef;

        subscribe(&sub, changed, (void *)0x1234);
        subscribe(&sub2, changed, (void *)0x5678);
        printf("SubscribeFeatureStateChangeNotification: %s, a second one %s\n",
               sub == (HANDLE)0xdeadbeef ? "left alone" : sub ? "a handle" : "NULL",
               sub2 == sub ? "the same" : "another");
        Sleep(200);
        if (unsubscribe && sub != (HANDLE)0xdeadbeef)
        {
            unsubscribe(sub);
            if (sub2 != (HANDLE)0xdeadbeef) unsubscribe(sub2);
            printf("unsubscribed\n");
        }
    }
    return 0;
}
