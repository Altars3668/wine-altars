/*
 * wsemptyprobe - what WWSAPI hands back for an element that is there but has
 * no text: <f></f>, <f/>, next to a missing element and one with content.
 *
 * Office's services catalog carries <o:Scope></o:Scope>, and Office rejects
 * the whole catalog if that field reads back as NULL.  Run on Windows and
 * under Wine and diff the output.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -municode \
 *       -o wsemptyprobe.exe wsemptyprobe.c -lwebservices
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <webservices.h>
#include <stdio.h>
#include <string.h>

#define XS(s) { sizeof(s) - 1, (BYTE *)(s), NULL, 0 }
static WS_XML_STRING ns = XS("urn:t"), s_v = XS("v"), s_f = XS("f");

static const struct { WS_TYPE type; const char *name, *text; } kinds[] =
{
    { WS_WSZ_TYPE, "wsz", "x" },
    { WS_STRING_TYPE, "string", "x" },
    { WS_XML_STRING_TYPE, "xmlstring", "x" },
    { WS_BYTES_TYPE, "bytes", "eA==" },
    { WS_INT32_TYPE, "int32", "5" },
    { WS_BOOL_TYPE, "bool", "true" },
};
static const struct { ULONG options; const char *name; } fieldopts[] =
{
    { 0, "required" },
    { WS_FIELD_OPTIONAL, "optional" },
    { WS_FIELD_NILLABLE, "nillable" },
    { WS_FIELD_POINTER, "pointer" },
    { WS_FIELD_POINTER | WS_FIELD_OPTIONAL, "pointer|optional" },
};
static const struct { WS_READ_OPTION option; const char *name; } readopts[] =
{
    { WS_READ_REQUIRED_VALUE, "required-value" },
    { WS_READ_REQUIRED_POINTER, "required-pointer" },
    { WS_READ_OPTIONAL_POINTER, "optional-pointer" },
    { WS_READ_NILLABLE_POINTER, "nillable-pointer" },
};

static ULONG value_size(WS_TYPE type)
{
    switch (type)
    {
    case WS_WSZ_TYPE: return sizeof(WCHAR *);
    case WS_STRING_TYPE: return sizeof(WS_STRING);
    case WS_XML_STRING_TYPE: return sizeof(WS_XML_STRING);
    case WS_BYTES_TYPE: return sizeof(WS_BYTES);
    case WS_INT32_TYPE: return sizeof(INT32);
    default: return sizeof(BOOL);
    }
}

static void show(WS_TYPE type, BOOL pointer, const void *p)
{
    if (pointer)
    {
        p = *(const void * const *)p;
        if (!p) { printf("ptr NULL"); return; }
        printf("ptr -> ");
    }
    switch (type)
    {
    case WS_WSZ_TYPE:
    {
        const WCHAR *s = *(const WCHAR * const *)p;
        if (!s) printf("NULL");
        else printf("\"%ls\" (%u chars)", s, (unsigned int)wcslen(s));
        break;
    }
    case WS_STRING_TYPE:
    {
        const WS_STRING *s = p;
        printf("length %lu chars %s", s->length, s->chars ? "set" : "NULL");
        break;
    }
    case WS_XML_STRING_TYPE:
    {
        const WS_XML_STRING *s = p;
        printf("length %lu bytes %s", s->length, s->bytes ? "set" : "NULL");
        break;
    }
    case WS_BYTES_TYPE:
    {
        const WS_BYTES *b = p;
        printf("length %lu bytes %s", b->length, b->bytes ? "set" : "NULL");
        break;
    }
    case WS_INT32_TYPE: printf("%d", *(const INT32 *)p); break;
    default: printf("%d", *(const BOOL *)p); break;
    }
}

static WS_XML_READER *open_reader(const char *xml)
{
    WS_XML_READER_TEXT_ENCODING enc = {{ WS_XML_READER_ENCODING_TYPE_TEXT }, WS_CHARSET_UTF8};
    WS_XML_READER_BUFFER_INPUT input = {{ WS_XML_READER_INPUT_TYPE_BUFFER }};
    WS_XML_READER *reader;

    WsCreateReader(NULL, 0, &reader, NULL);
    input.encodedData = (void *)xml;
    input.encodedDataSize = strlen(xml);
    WsSetInput(reader, &enc.encoding, &input.input, NULL, 0, NULL);
    WsFillReader(reader, strlen(xml), NULL, NULL);
    return reader;
}

/* <v><f>...</f></v> read as a struct whose one field is element <f> -- the way Office reads its catalog. */
static void field_case(unsigned int k, unsigned int o, const char *form, const char *label)
{
    WS_FIELD_DESCRIPTION field = { WS_ELEMENT_FIELD_MAPPING, &s_f, &ns, kinds[k].type, NULL, 0, fieldopts[o].options };
    WS_FIELD_DESCRIPTION *fields[] = { &field };
    WS_STRUCT_DESCRIPTION desc = { 64, 8, fields, 1, &s_v, &ns };
    char xml[256], inner[64];
    BYTE buf[64];
    WS_XML_READER *reader;
    WS_HEAP *heap;
    HRESULT hr;

    snprintf(inner, sizeof(inner), form, kinds[k].text);
    snprintf(xml, sizeof(xml), "<v xmlns=\"urn:t\">%s</v>", inner);
    reader = open_reader(xml);
    WsCreateHeap(1 << 16, 0, NULL, 0, &heap, NULL);
    memset(buf, 0xcc, sizeof(buf));
    WsReadToStartElement(reader, NULL, NULL, NULL, NULL);
    hr = WsReadType(reader, WS_ELEMENT_TYPE_MAPPING, WS_STRUCT_TYPE, &desc, WS_READ_REQUIRED_VALUE,
                    heap, buf, desc.size, NULL);
    printf("field %-9s %-16s %-9s hr %#010lx ", kinds[k].name, fieldopts[o].name, label, hr);
    /* a wsz is a pointer already; what WS_FIELD_POINTER adds to it is not worth guessing at */
    if (hr == S_OK && kinds[k].type == WS_WSZ_TYPE && (fieldopts[o].options & WS_FIELD_POINTER))
        printf("%s", *(void **)buf ? "set" : "NULL");
    else if (hr == S_OK) show(kinds[k].type, !!(fieldopts[o].options & WS_FIELD_POINTER), buf);
    printf("\n");
    WsFreeHeap(heap);
    WsFreeReader(reader);
}

/* <f>...</f> read directly as a value of the type. */
static void element_case(unsigned int k, unsigned int r, const char *form, const char *label)
{
    char xml[128], inner[64];
    BYTE buf[64];
    WS_XML_READER *reader;
    WS_HEAP *heap;
    BOOL pointer = readopts[r].option != WS_READ_REQUIRED_VALUE;
    HRESULT hr;

    snprintf(inner, sizeof(inner), form, kinds[k].text);
    snprintf(xml, sizeof(xml), "%s", inner);
    /* forms are written for <f>; give the element its namespace */
    if (!strncmp(xml, "<f", 2))
    {
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "<f xmlns=\"urn:t\"%s", xml + 2);
        strcpy(xml, tmp);
    }
    reader = open_reader(xml);
    WsCreateHeap(1 << 16, 0, NULL, 0, &heap, NULL);
    memset(buf, 0xcc, sizeof(buf));
    WsReadToStartElement(reader, NULL, NULL, NULL, NULL);
    hr = WsReadType(reader, WS_ELEMENT_TYPE_MAPPING, kinds[k].type, NULL, readopts[r].option, heap, buf,
                    pointer ? sizeof(void *) : value_size(kinds[k].type), NULL);
    printf("elem  %-9s %-16s %-9s hr %#010lx ", kinds[k].name, readopts[r].name, label, hr);
    if (hr == S_OK) show(kinds[k].type, pointer && kinds[k].type != WS_WSZ_TYPE, buf);
    printf("\n");
    WsFreeHeap(heap);
    WsFreeReader(reader);
}

int wmain(void)
{
    static const struct { const char *form, *label; } forms[] =
    {
        { "<f></f>", "empty" },
        { "<f/>", "selfclose" },
        { "<f> </f>", "space" },
        { "", "absent" },
        { "<f>%s</f>", "text" },
    };
    unsigned int k, o, f;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (k = 0; k < ARRAYSIZE(kinds); k++)
        for (o = 0; o < ARRAYSIZE(fieldopts); o++)
            for (f = 0; f < ARRAYSIZE(forms); f++)
                field_case(k, o, forms[f].form, forms[f].label);
    for (k = 0; k < ARRAYSIZE(kinds); k++)
        for (o = 0; o < ARRAYSIZE(readopts); o++)
            for (f = 0; f < ARRAYSIZE(forms); f++)
                if (forms[f].form[0]) element_case(k, o, forms[f].form, forms[f].label);
    return 0;
}
