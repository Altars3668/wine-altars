/*
 * What IXmlReader::Read answers when a document ends early: each document below is fed to a new reader, once as
 * UTF-16 and once as UTF-8, and every Read is printed with its result, the node type, the name and the depth, up to
 * the first result that is not S_OK.  Then Read is called twice more, to see whether an error sticks.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <xmllite.h>
#include <shlwapi.h>
#include <stdio.h>

static const char *type_name(XmlNodeType type)
{
    static const char *names[] =
    {
        "None", "Element", "Attribute", "Text", "CDATA", "5", "6", "ProcessingInstruction", "Comment", "9",
        "DocumentType", "11", "12", "Whitespace", "14", "EndElement", "16", "XmlDeclaration",
    };
    return type < ARRAY_SIZE(names) ? names[type] : "?";
}

/* a stream exactly as long as the document */
static IStream *stream_on(const void *data, SIZE_T size)
{
    return SHCreateMemStream(data, size);
}

static void read_all(const char *label, IStream *stream)
{
    IXmlReader *reader;
    XmlNodeType type;
    const WCHAR *name;
    UINT depth, i, extra = 0;
    HRESULT hr;

    CreateXmlReader(&IID_IXmlReader, (void **)&reader, NULL);
    hr = IXmlReader_SetInput(reader, (IUnknown *)stream);
    printf("%s: SetInput %#lx\n", label, hr);
    for (i = 0; i < 20; ++i)
    {
        type = 0x77;
        hr = IXmlReader_Read(reader, &type);
        name = NULL;
        depth = 0x77;
        if (hr == S_OK)
        {
            IXmlReader_GetLocalName(reader, &name, NULL);
            IXmlReader_GetDepth(reader, &depth);
            printf("  %#lx %s %ls depth %u empty %d", hr, type_name(type), name ? name : L"(null)", depth,
                    IXmlReader_IsEmptyElement(reader));
            if (type != XmlNodeType_Element && type != XmlNodeType_EndElement)
            {
                const WCHAR *value = NULL;
                HRESULT hr2 = IXmlReader_GetValue(reader, &value, NULL);

                if (hr2 == S_OK) printf(" value \"%ls\"", value);
                else printf(" value hr %#lx", hr2);
            }
            printf("\n");
            continue;
        }
        printf("  %#lx type %#x\n", hr, type);
        if (++extra > 2) break;
    }
    IXmlReader_Release(reader);
    IStream_Release(stream);
}

int main(void)
{
    static const char *docs[] =
    {
        "<a><b>",
        "<a><b/>",
        "<a>text",
        "<a><b></b>",
        "<a",
        "<a/>",
        "<a></a>trailing",
        "<a></a><b/>",
        "<?xml version=\"1.0\"?><a>",
        "<a><!-- c -->",
        "<a><b>t</b></a>",
        "",
        " ",
        "<?xml version=\"1.0\"?>",
        "<!-- c -->",
        "<a></a></b>",
        "<a></a><![CDATA[x]]>",
        "<a/><?xml version=\"1.0\"?>",
        "<a></a><!-- c --><?p?> ",
        "<a>&amp;",
        "<a><![CDATA[x",
        "<a><!-- c",
        "<a><?p x",
        "<a><b x=\"1\"",
        "<a></b",
        "<a></a",
        "<a>t<",
        "<a><?xml version=\"1.0\"?></a>",
        "<!-- c --><?xml version=\"1.0\"?><a/>",
        " <?xml version=\"1.0\"?><a/>",
        "<a><?xml?></a>",
        "<a><?XML x?></a>",
        "<a><?xml-stylesheet x?></a>",
        "x<a/>",
        "<a/><",
        "<a>x]]>y</a>",
        "<a>t</a>\r\n",
    };
    unsigned int i, len;
    WCHAR wide[256];
    char label[64];

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitialize(NULL);
    for (i = 0; i < ARRAY_SIZE(docs); ++i)
    {
        len = MultiByteToWideChar(CP_UTF8, 0, docs[i], -1, wide, ARRAY_SIZE(wide)) - 1;
        sprintf(label, "utf-16 %s", docs[i]);
        read_all(label, stream_on(wide, len * sizeof(WCHAR)));
        sprintf(label, "utf-8 %s", docs[i]);
        read_all(label, stream_on(docs[i], strlen(docs[i])));
    }
    printf("done\n");
    return 0;
}
