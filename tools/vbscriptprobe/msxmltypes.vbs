' msxmltypes.vbs <output file>: TypeName() of the MSXML objects a script gets, which is the class
' name when the object provides class info (IProvideClassInfo) and its interface's name otherwise.
Option Explicit
Dim fso, out, progids, p, o, d, e
Set fso = CreateObject("Scripting.FileSystemObject")
Set out = fso.CreateTextFile(WScript.Arguments(0), True, True)
Sub show(what, obj)
    If Err.Number = 0 Then
        out.WriteLine what & vbTab & TypeName(obj)
    Else
        out.WriteLine what & vbTab & Err.Number
    End If
    Err.Clear
End Sub
On Error Resume Next
progids = Array("Microsoft.XMLDOM", "Msxml2.DOMDocument", "Msxml2.DOMDocument.3.0", "Msxml2.DOMDocument.6.0", _
    "Msxml2.FreeThreadedDOMDocument", "Msxml2.FreeThreadedDOMDocument.3.0", "Msxml2.FreeThreadedDOMDocument.6.0", _
    "Microsoft.XMLHTTP", "Msxml2.XMLHTTP.3.0", "Msxml2.XMLHTTP.6.0", "Msxml2.ServerXMLHTTP.6.0", _
    "Msxml2.SAXXMLReader.3.0", "Msxml2.SAXXMLReader.6.0", "Msxml2.MXXMLWriter.3.0", "Msxml2.MXXMLWriter.6.0", _
    "Msxml2.SAXAttributes.6.0", "Msxml2.MXNamespaceManager.6.0", "Msxml2.XSLTemplate.3.0", "Msxml2.XSLTemplate.6.0", _
    "Msxml2.XMLSchemaCache.3.0", "Msxml2.XMLSchemaCache.6.0")
For Each p In progids
    Set o = Nothing
    Set o = CreateObject(p)
    show p, o
Next
For Each p In Array("Msxml2.DOMDocument.3.0", "Msxml2.DOMDocument.6.0")
    Set d = CreateObject(p)
    d.loadXML "<a b='1'>t<!--c--><?p d?><![CDATA[x]]><e/></a>"
    Set e = d.documentElement
    show p & " documentElement", e
    show p & " attributes", e.attributes
    show p & " attribute node", e.getAttributeNode("b")
    show p & " childNodes", e.childNodes
    show p & " text node", e.childNodes.item(0)
    show p & " comment", e.childNodes.item(1)
    show p & " processing instruction", e.childNodes.item(2)
    show p & " CDATA section", e.childNodes.item(3)
    show p & " selectNodes", d.selectNodes("//e")
    show p & " parseError", d.parseError
    show p & " implementation", d.implementation
    show p & " createDocumentFragment", d.createDocumentFragment()
Next
out.Close
