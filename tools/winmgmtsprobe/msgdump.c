/* msgdump: every message in a module's message table, as its language resource file has it: the id and the text,
 * non-ASCII as \uXXXX and line breaks as \r \n.  Prints results only.
 *
 *   msgdump.exe <file>...      e.g. C:\Windows\System32\wbem\en-US\wmiutils.dll.mui
 */
#include <windows.h>
#include <stdio.h>

static void print_text(const WCHAR *text, int len)
{
    int i;
    for (i = 0; i < len && text[i]; i++)
    {
        if (text[i] == '\r') printf("\\r");
        else if (text[i] == '\n') printf("\\n");
        else if (text[i] == '\\') printf("\\\\");
        else if (text[i] >= 0x20 && text[i] < 0x7f) printf("%c", (char)text[i]);
        else printf("\\u%04x", text[i]);
    }
}

int wmain(int argc, WCHAR **argv)
{
    const MESSAGE_RESOURCE_DATA *data;
    const MESSAGE_RESOURCE_ENTRY *entry;
    HMODULE module;
    HRSRC rsrc;
    DWORD i, id;
    int arg;

    for (arg = 1; arg < argc; arg++)
    {
        printf("== %ls\n", argv[arg]);
        module = LoadLibraryExW(argv[arg], NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (!module)
        {
            printf("LoadLibraryEx error %lu\n", GetLastError());
            continue;
        }
        if (!(rsrc = FindResourceW(module, MAKEINTRESOURCEW(1), (const WCHAR *)RT_MESSAGETABLE)))
        {
            printf("no message table, error %lu\n", GetLastError());
            continue;
        }
        data = LockResource(LoadResource(module, rsrc));
        for (i = 0; i < data->NumberOfBlocks; i++)
        {
            entry = (const MESSAGE_RESOURCE_ENTRY *)((const BYTE *)data + data->Blocks[i].OffsetToEntries);
            for (id = data->Blocks[i].LowId; id <= data->Blocks[i].HighId; id++)
            {
                printf("%08lx\t", id);
                if (entry->Flags & MESSAGE_RESOURCE_UNICODE)
                    print_text((const WCHAR *)entry->Text, (entry->Length - 4) / sizeof(WCHAR));
                else printf("(ansi) %.*s", entry->Length - 4, (const char *)entry->Text);
                printf("\n");
                entry = (const MESSAGE_RESOURCE_ENTRY *)((const BYTE *)entry + entry->Length);
            }
        }
        FreeLibrary(module);
    }
    return 0;
}
