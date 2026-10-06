/*
 * importprobe -- what IXMLDOMDocument3::importNode does with each kind of node.
 *
 * Wine's conformance test expected importing a document node to fail with E_FAIL, and Windows
 * answered E_INVALIDARG.  The test only asked about the document; this asks about every node type a
 * document can hand out -- the document, its doctype, an entity and a notation from the doctype,
 * an entity reference, an element, an attribute, text, CDATA, a comment, a processing instruction
 * and a fragment -- deep and shallow, in MSXML 3 and 6, and prints the HRESULT, whether the out
 * pointer was cleared, and the clone's xml.  No files, no network.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <stdio.h>
#include <windows.h>
#include <initguid.h>
#include <msxml6.h>

/* msxml6.h has only the version 6 class */
DEFINE_GUID( CLSID_DOMDocument30_probe, 0xf5078f32, 0xc551, 0x11d3, 0x89, 0xb9, 0x00, 0x00, 0xf8, 0x1f, 0xe2, 0x21 );

static const WCHAR source[] =
    L"<?xml version=\"1.0\"?>"
    L"<!DOCTYPE r [<!ENTITY e \"x\"><!NOTATION n SYSTEM \"urn:n\"><!ELEMENT r ANY><!ATTLIST r a CDATA #IMPLIED>]>"
    L"<r a=\"1\">&e;<![CDATA[c]]><!--k--><?p q?>t</r>";

static void put_text( const WCHAR *str )
{
    for (; str && *str; str++)
        if (*str >= 0x20 && *str < 0x7f) putchar( *str );
        else printf( "<%04x>", *str );
}

static void try_import( IXMLDOMDocument3 *target, const char *what, IXMLDOMNode *node )
{
    VARIANT_BOOL deep;

    for (deep = VARIANT_TRUE; ; deep = VARIANT_FALSE)
    {
        IXMLDOMNode *clone = (void *)0xdeadbeef;
        HRESULT hr = IXMLDOMDocument3_importNode( target, node, deep, &clone );

        printf( "  %-26s %s: hr %#lx clone %s", what, deep ? "deep   " : "shallow", hr,
                clone == (void *)0xdeadbeef ? "untouched" : clone ? "set" : "NULL" );
        if (SUCCEEDED(hr) && clone && clone != (void *)0xdeadbeef)
        {
            BSTR xml = NULL;
            DOMNodeType type = -1;
            IXMLDOMNode_get_nodeType( clone, &type );
            printf( " type %d", type );
            if (SUCCEEDED(IXMLDOMNode_get_xml( clone, &xml )))
            {
                printf( " xml " );
                put_text( xml );
                SysFreeString( xml );
            }
            IXMLDOMNode_Release( clone );
        }
        putchar( '\n' );
        if (!deep) break;
    }
}

static IXMLDOMNode *child_of_type( IXMLDOMNode *parent, DOMNodeType want )
{
    IXMLDOMNode *child = NULL, *next;
    DOMNodeType type;

    IXMLDOMNode_get_firstChild( parent, &child );
    while (child)
    {
        IXMLDOMNode_get_nodeType( child, &type );
        if (type == want) return child;
        next = NULL;
        IXMLDOMNode_get_nextSibling( child, &next );
        IXMLDOMNode_Release( child );
        child = next;
    }
    return NULL;
}

static IXMLDOMNode *first_item( IXMLDOMNamedNodeMap *map )
{
    IXMLDOMNode *node = NULL;
    if (map) IXMLDOMNamedNodeMap_get_item( map, 0, &node );
    return node;
}

static void probe( const CLSID *clsid, const char *name )
{
    IXMLDOMDocument3 *doc = NULL, *target = NULL;
    IXMLDOMDocumentType *doctype = NULL;
    IXMLDOMNamedNodeMap *map = NULL;
    IXMLDOMDocumentFragment *fragment = NULL;
    IXMLDOMElement *root = NULL;
    IXMLDOMNode *node;
    VARIANT_BOOL ok = VARIANT_FALSE;
    BSTR str;
    HRESULT hr;

    printf( "%s\n", name );
    if (FAILED(hr = CoCreateInstance( clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMDocument3, (void **)&doc ))
            || FAILED(hr = CoCreateInstance( clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMDocument3, (void **)&target )))
    {
        printf( "  CoCreateInstance: %#lx\n", hr );
        if (doc) IXMLDOMDocument3_Release( doc );
        return;
    }
    /* MSXML 6 refuses a DTD unless asked */
    {
        VARIANT v;
        V_VT( &v ) = VT_BOOL;
        V_BOOL( &v ) = VARIANT_TRUE;
        str = SysAllocString( L"ProhibitDTD" );
        V_BOOL( &v ) = VARIANT_FALSE;
        IXMLDOMDocument3_setProperty( doc, str, v );
        SysFreeString( str );
    }
    IXMLDOMDocument3_put_validateOnParse( doc, VARIANT_FALSE );
    str = SysAllocString( source );
    hr = IXMLDOMDocument3_loadXML( doc, str, &ok );
    SysFreeString( str );
    printf( "  loadXML: %#lx %d\n", hr, ok );
    if (hr != S_OK) goto done;

    try_import( target, "document", (IXMLDOMNode *)doc );
    if (SUCCEEDED(IXMLDOMDocument3_get_doctype( doc, &doctype )) && doctype)
    {
        try_import( target, "doctype", (IXMLDOMNode *)doctype );
        if (SUCCEEDED(IXMLDOMDocumentType_get_entities( doctype, &map )) && (node = first_item( map )))
        {
            try_import( target, "entity", node );
            IXMLDOMNode_Release( node );
        }
        if (map) IXMLDOMNamedNodeMap_Release( map );
        map = NULL;
        if (SUCCEEDED(IXMLDOMDocumentType_get_notations( doctype, &map )) && (node = first_item( map )))
        {
            try_import( target, "notation", node );
            IXMLDOMNode_Release( node );
        }
        if (map) IXMLDOMNamedNodeMap_Release( map );
        IXMLDOMDocumentType_Release( doctype );
    }
    else printf( "  no doctype\n" );

    if (SUCCEEDED(IXMLDOMDocument3_get_documentElement( doc, &root )) && root)
    {
        static const struct { DOMNodeType type; const char *name; } kinds[] =
        {
            { NODE_ENTITY_REFERENCE, "entity reference" },
            { NODE_CDATA_SECTION, "CDATA section" },
            { NODE_COMMENT, "comment" },
            { NODE_PROCESSING_INSTRUCTION, "processing instruction" },
            { NODE_TEXT, "text" },
        };
        IXMLDOMAttribute *attr = NULL;
        unsigned int i;

        try_import( target, "element", (IXMLDOMNode *)root );
        str = SysAllocString( L"a" );
        if (SUCCEEDED(IXMLDOMElement_getAttributeNode( root, str, &attr )) && attr)
        {
            try_import( target, "attribute", (IXMLDOMNode *)attr );
            IXMLDOMAttribute_Release( attr );
        }
        SysFreeString( str );
        for (i = 0; i < ARRAYSIZE(kinds); i++)
        {
            if ((node = child_of_type( (IXMLDOMNode *)root, kinds[i].type )))
            {
                try_import( target, kinds[i].name, node );
                IXMLDOMNode_Release( node );
            }
            else printf( "  no %s\n", kinds[i].name );
        }
        IXMLDOMElement_Release( root );
    }
    if (SUCCEEDED(IXMLDOMDocument3_createDocumentFragment( doc, &fragment )) && fragment)
    {
        IXMLDOMElement *element = NULL;
        str = SysAllocString( L"f" );
        if (SUCCEEDED(IXMLDOMDocument3_createElement( doc, str, &element )))
        {
            IXMLDOMNode *added = NULL;
            IXMLDOMDocumentFragment_appendChild( fragment, (IXMLDOMNode *)element, &added );
            if (added) IXMLDOMNode_Release( added );
            IXMLDOMElement_Release( element );
        }
        SysFreeString( str );
        try_import( target, "fragment", (IXMLDOMNode *)fragment );
        IXMLDOMDocumentFragment_Release( fragment );
    }
    /* a node of the target itself */
    try_import( target, "the target document", (IXMLDOMNode *)target );

done:
    IXMLDOMDocument3_Release( target );
    IXMLDOMDocument3_Release( doc );
}

int main(void)
{
    CoInitialize( NULL );
    probe( &CLSID_DOMDocument30_probe, "Msxml2.DOMDocument.3.0" );
    probe( &CLSID_DOMDocument60, "Msxml2.DOMDocument.6.0" );
    CoUninitialize();
    return 0;
}
