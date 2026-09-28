/*
 * stringtableprobe <dll> [first] [last]: prints, one line each, every string in a module's string table as the
 * current UI language has it, "id<TAB>text" in UTF-8 with tabs, newlines and backslashes escaped.  The module is
 * mapped as a resource file only, so none of its code runs.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    unsigned int first = 0, last = 65535, id;
    WCHAR name[MAX_PATH], text[4096];
    char out[16384];
    HMODULE module;
    int len, i, o;

    if (argc < 2)
    {
        fprintf(stderr, "usage: stringtableprobe <dll> [first] [last]\n");
        return 2;
    }
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, name, MAX_PATH);
    if (argc > 2) first = strtoul(argv[2], NULL, 0);
    if (argc > 3) last = strtoul(argv[3], NULL, 0);
    if (!(module = LoadLibraryExW(name, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)))
    {
        fprintf(stderr, "cannot load %s: %lu\n", argv[1], GetLastError());
        return 1;
    }
    for (id = first; id <= last; id++)
    {
        if (!(len = LoadStringW(module, id, text, ARRAY_SIZE(text)))) continue;
        for (i = o = 0; i < len && o < (int)sizeof(out) - 16; i++)
        {
            WCHAR c = text[i];
            if (c == '\\') { out[o++] = '\\'; out[o++] = '\\'; }
            else if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
            else if (c == '\r') { out[o++] = '\\'; out[o++] = 'r'; }
            else if (c == '\t') { out[o++] = '\\'; out[o++] = 't'; }
            else o += WideCharToMultiByte(CP_UTF8, 0, &c, 1, out + o, sizeof(out) - o, NULL, NULL);
        }
        out[o] = 0;
        printf("%u\t%s\n", id, out);
    }
    FreeLibrary(module);
    return 0;
}
