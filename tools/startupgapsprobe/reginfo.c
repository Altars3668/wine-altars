/* reginfo: the size of a key's security descriptor that RegQueryInfoKey gives, next to the sizes RegGetKeySecurity
 * gives for each kind of security information, for keys opened with and without READ_CONTROL, a predefined key and
 * HKEY_CLASSES_ROOT.  Prints sizes and errors only. */
#include <windows.h>
#include <stdio.h>

static void show(const char *what, HKEY key)
{
    static const struct { SECURITY_INFORMATION info; const char *name; } infos[] =
    {
        { OWNER_SECURITY_INFORMATION, "owner" },
        { GROUP_SECURITY_INFORMATION, "group" },
        { DACL_SECURITY_INFORMATION, "dacl" },
        { OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, "owner+group+dacl" },
        { SACL_SECURITY_INFORMATION, "sacl" },
        { OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
          SACL_SECURITY_INFORMATION, "all four" },
        { LABEL_SECURITY_INFORMATION, "label" },
    };
    DWORD security = 0xdeadbeef, size, subkeys = 0xdeadbeef;
    unsigned int i;
    LONG ret;

    ret = RegQueryInfoKeyW(key, NULL, NULL, NULL, &subkeys, NULL, NULL, NULL, NULL, NULL, &security, NULL);
    printf("%s: RegQueryInfoKeyW %ld, security %lu%s\n", what, ret, security, subkeys == 0xdeadbeef ? " (no info)" : "");
    security = 0xdeadbeef;
    ret = RegQueryInfoKeyA(key, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &security, NULL);
    printf("  RegQueryInfoKeyA %ld, security %lu\n", ret, security);
    for (i = 0; i < ARRAYSIZE(infos); i++)
    {
        size = 0;
        ret = RegGetKeySecurity(key, infos[i].info, NULL, &size);
        printf("  RegGetKeySecurity %-18s %ld, size %lu\n", infos[i].name, ret, size);
    }
}

int main(void)
{
    HKEY key;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0, KEY_READ, &key))
    {
        show("HKCU\\Software KEY_READ", key);
        RegCloseKey(key);
    }
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0, KEY_QUERY_VALUE, &key))
    {
        show("HKCU\\Software KEY_QUERY_VALUE", key);
        RegCloseKey(key);
    }
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, KEY_READ, &key))
    {
        show("HKLM\\SOFTWARE KEY_READ", key);
        RegCloseKey(key);
    }
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key))
    {
        show("HKLM\\...\\CurrentVersion KEY_READ", key);
        RegCloseKey(key);
    }
    show("HKEY_CURRENT_USER", HKEY_CURRENT_USER);
    show("HKEY_CLASSES_ROOT", HKEY_CLASSES_ROOT);
    if (!RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID", 0, KEY_READ, &key))
    {
        show("HKCR\\CLSID KEY_READ", key);
        RegCloseKey(key);
    }
    return 0;
}
