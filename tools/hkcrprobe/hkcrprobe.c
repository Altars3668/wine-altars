/* hkcrprobe - the parts of HKEY_CLASSES_ROOT's merged view upstream's tests leave open.
 *
 * Creates a class key both under HKLM\Software\Classes (values "m", "both"; subkeys "M", "B") and
 * under HKCU\Software\Classes (values "u", "both"; subkeys "U", "B"), then through HKCR prints: the
 * handle mark, what a query of each value answers (and from which side), what RegQueryInfoKey counts,
 * the order the values and subkeys enumerate in, and which subkeys open.  Then where the 32-bit view
 * finds a class the user registered: under Classes\Wow6432Node\CLSID or under Classes\CLSID\Wow6432Node.
 * Then what a handle opened through HKCR is while HKCR is overridden with a plain key, and whether the user's
 * classes are also HKEY_USERS\<sid>_Classes (the SID itself is not printed).
 * Everything it creates is removed again.  Writing under HKLM needs an elevated process; without one it
 * says so and stops.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <winternl.h>
#include <sddl.h>
#include <stdio.h>

static const char name[] = "WineAltarsHkcrProbe";

static void set(HKEY key, const char *value, const char *data)
{
    RegSetValueExA(key, value, 0, REG_SZ, (const BYTE *)data, strlen(data) + 1);
}

static void cleanup(void)
{
    char path[64];

    sprintf(path, "Software\\Classes\\%s", name);
    RegDeleteTreeA(HKEY_CURRENT_USER, path);
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, path);
}

int main(void)
{
    DWORD subkeys, values, len, type, i;
    HKEY hklm, hkcu, hkcr, sub;
    char path[64], buf[64];
    LSTATUS res;

    cleanup();
    sprintf(path, "Software\\Classes\\%s", name);
    if ((res = RegCreateKeyExA(HKEY_LOCAL_MACHINE, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hklm, NULL)))
    {
        printf("cannot create the machine's key: %ld\n", res);
        return 1;
    }
    set(hklm, "m", "machine");
    set(hklm, "both", "machine");
    RegCreateKeyA(hklm, "M", &sub); RegCloseKey(sub);
    RegCreateKeyA(hklm, "B", &sub); RegCloseKey(sub);
    RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkcu, NULL);
    set(hkcu, "u", "user");
    set(hkcu, "both", "user");
    RegCreateKeyA(hkcu, "U", &sub); RegCloseKey(sub);
    RegCreateKeyA(hkcu, "B", &sub); RegCloseKey(sub);

    res = RegOpenKeyExA(HKEY_CLASSES_ROOT, name, 0, KEY_READ, &hkcr);
    printf("open through HKCR: %ld, mark %d\n", res, res ? -1 : (int)((UINT_PTR)hkcr & 3));
    if (!res)
    {
        const char *queries[] = { "m", "u", "both", "none" };

        for (i = 0; i < ARRAYSIZE(queries); i++)
        {
            len = sizeof(buf); buf[0] = 0;
            res = RegQueryValueExA(hkcr, queries[i], NULL, &type, (BYTE *)buf, &len);
            printf("query %-4s: %ld %s\n", queries[i], res, res ? "" : buf);
        }
        subkeys = values = ~0u;
        res = RegQueryInfoKeyA(hkcr, NULL, NULL, NULL, &subkeys, NULL, NULL, &values, NULL, NULL, NULL, NULL);
        printf("info: %ld, %lu subkeys, %lu values\n", res, subkeys, values);
        printf("values:");
        for (i = 0; ; i++)
        {
            len = sizeof(buf);
            if (RegEnumValueA(hkcr, i, buf, &len, NULL, NULL, NULL, NULL)) break;
            printf(" %s", buf);
        }
        printf("\nsubkeys:");
        for (i = 0; ; i++)
        {
            if (RegEnumKeyA(hkcr, i, buf, sizeof(buf))) break;
            printf(" %s", buf);
        }
        printf("\n");
        for (i = 0; i < 4; i++)
        {
            const char *keys[] = { "M", "U", "B", "none" };

            res = RegOpenKeyExA(hkcr, keys[i], 0, KEY_READ, &sub);
            printf("open %-4s: %ld\n", keys[i], res);
            if (!res) RegCloseKey(sub);
        }
        RegCloseKey(hkcr);
    }
    RegCloseKey(hkcu);
    RegCloseKey(hklm);
    cleanup();

    /* the 32-bit view of a class registered for the user */
    {
        static const char *layouts[] =
        {
            "Software\\Classes\\Wow6432Node\\CLSID\\WineAltarsHkcrWow",
            "Software\\Classes\\CLSID\\Wow6432Node\\WineAltarsHkcrWow",
            "Software\\Classes\\CLSID\\WineAltarsHkcrWow",
        };
        for (i = 0; i < ARRAYSIZE(layouts); i++)
        {
            REGSAM views[] = { KEY_WOW64_32KEY, KEY_WOW64_64KEY };
            DWORD v;

            RegCreateKeyExA(HKEY_CURRENT_USER, layouts[i], 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &sub, NULL);
            RegCloseKey(sub);
            printf("user key %s:", layouts[i]);
            for (v = 0; v < 2; v++)
            {
                res = RegOpenKeyExA(HKEY_CLASSES_ROOT, "CLSID\\WineAltarsHkcrWow", 0, KEY_READ | views[v], &sub);
                printf(" %s view %ld", v ? "64-bit" : "32-bit", res);
                if (!res) RegCloseKey(sub);
            }
            printf("\n");
            RegDeleteKeyExA(HKEY_CURRENT_USER, layouts[i], KEY_WOW64_64KEY, 0);
        }
    }

    /* HKEY_CLASSES_ROOT overridden with a plain key, as a registration is captured */
    {
        static const char plain[] = "Software\\WineAltarsHkcrOverride";
        HKEY override;

        RegDeleteTreeA(HKEY_CURRENT_USER, plain);
        RegCreateKeyExA(HKEY_CURRENT_USER, plain, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &override, NULL);
        res = RegOverridePredefKey(HKEY_CLASSES_ROOT, override);
        printf("override: %ld", res);
        res = RegCreateKeyExA(HKEY_CLASSES_ROOT, "Created", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, NULL);
        printf(", create through HKCR: %ld, mark %d", res, res ? -1 : (int)((UINT_PTR)sub & 3));
        if (!res) RegCloseKey(sub);
        res = RegOpenKeyExA(override, "Created", 0, KEY_READ, &sub);
        printf(", under the override: %ld", res);
        if (!res) RegCloseKey(sub);
        res = RegOpenKeyExA(HKEY_CLASSES_ROOT, "CLSID", 0, KEY_READ, &sub);
        printf(", open CLSID: %ld\n", res);
        if (!res) RegCloseKey(sub);
        RegOverridePredefKey(HKEY_CLASSES_ROOT, NULL);
        RegCloseKey(override);
        RegDeleteTreeA(HKEY_CURRENT_USER, plain);
    }

    /* HKEY_USERS\<sid>_Classes and HKEY_CURRENT_USER\Software\Classes */
    {
        char token_buffer[256], users_path[128], *sid;
        WCHAR name_buffer[512];
        KEY_NAME_INFORMATION *info = (KEY_NAME_INFORMATION *)name_buffer;
        TOKEN_USER *user = (TOKEN_USER *)token_buffer;
        HKEY users_classes, user_classes;
        HANDLE token;
        DWORD size;
        ULONG len;

        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
        GetTokenInformation(token, TokenUser, token_buffer, sizeof(token_buffer), &size);
        CloseHandle(token);
        ConvertSidToStringSidA(user->User.Sid, &sid);
        sprintf(users_path, "%s_Classes", sid);
        LocalFree(sid);
        res = RegOpenKeyExA(HKEY_USERS, users_path, 0, KEY_READ, &users_classes);
        printf("HKEY_USERS\\<sid>_Classes: %ld", res);
        if (!res)
        {
            RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Classes\\WineAltarsHiveProbe", 0, NULL, 0, KEY_ALL_ACCESS,
                            NULL, &sub, NULL);
            RegCloseKey(sub);
            res = RegOpenKeyExA(users_classes, "WineAltarsHiveProbe", 0, KEY_READ, &sub);
            printf(", a key made under HKCU\\Software\\Classes seen there: %ld", res);
            if (!res) RegCloseKey(sub);
            RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Classes\\WineAltarsHiveProbe");
            RegCloseKey(users_classes);
        }
        printf("\n");
        if (!RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Classes", 0, KEY_READ, &user_classes))
        {
            res = NtQueryKey(user_classes, KeyNameInformation, info, sizeof(name_buffer) - sizeof(WCHAR), &len);
            if (!res)
            {
                WCHAR *name = info->Name;
                size_t n = info->NameLength / sizeof(WCHAR);
                name[n] = 0;
                printf("HKCU\\Software\\Classes is named %s\n",
                       n >= 8 && !wcsicmp(name + n - 8, L"_Classes") ? "\\REGISTRY\\USER\\<sid>_Classes" :
                       wcsstr(name, L"\\Software\\Classes") ? "\\REGISTRY\\USER\\<sid>\\Software\\Classes" : "otherwise");
            }
            else printf("NtQueryKey: %#lx\n", (unsigned long)res);
            RegCloseKey(user_classes);
        }
    }
    return 0;
}
