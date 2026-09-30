/* What RegQueryInfoKey() gives for the length of a key's security descriptor, next to what
 * RegGetKeySecurity() needs for each kind of information.
 *
 * Office asks for that length at start; Wine logs "security argument not supported" and gives 0.
 * For a few keys (and one this creates under HKCU\Software and removes again) this prints the length
 * RegQueryInfoKeyW reports and the length RegGetKeySecurity asks for with the owner, group and DACL,
 * with the label added, and with the SACL added (which needs a privilege the caller may not have).
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror regsecprobe.c -o regsecprobe.exe -ladvapi32
 */
#include <windows.h>
#include <stdio.h>

static void show(const char *name, HKEY root, const char *path)
{
    static const struct { const char *name; SECURITY_INFORMATION info; } kinds[] =
    {
        {"owner", OWNER_SECURITY_INFORMATION},
        {"owner+group+dacl", OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION},
        {"+label", OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION},
        {"+sacl", OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION},
    };
    DWORD security = 0xdeadbeef, len, i;
    HKEY key = root;
    LSTATUS res;
    BYTE buf[1];

    if (path && (res = RegOpenKeyExA(root, path, 0, KEY_READ, &key)))
    {
        printf("%s: open %ld\n", name, res);
        return;
    }
    res = RegQueryInfoKeyW(key, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &security, NULL);
    printf("%s: RegQueryInfoKeyW %ld, security %lu;", name, res, security);
    for (i = 0; i < ARRAYSIZE(kinds); i++)
    {
        len = 0;
        res = RegGetKeySecurity(key, kinds[i].info, (PSECURITY_DESCRIPTOR)buf, &len);
        printf(" %s %ld/%lu", kinds[i].name, res, len);
    }
    printf("\n");
    if (key != root) RegCloseKey(key);
}

int main(void)
{
    HKEY key;

    show("HKCU", HKEY_CURRENT_USER, NULL);
    show("HKCU\\Software", HKEY_CURRENT_USER, "Software");
    show("HKLM\\Software", HKEY_LOCAL_MACHINE, "Software");
    show("HKCR\\CLSID", HKEY_CLASSES_ROOT, "CLSID");
    show("HKLM\\SYSTEM\\CurrentControlSet\\Control", HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control");
    if (!RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\WineAltarsRegSec", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL))
    {
        RegCloseKey(key);
        show("new key under HKCU\\Software", HKEY_CURRENT_USER, "Software\\WineAltarsRegSec");
        RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\WineAltarsRegSec");
    }
    return 0;
}
