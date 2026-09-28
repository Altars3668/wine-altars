Option Explicit
' word-embed.vbs <output.docx> <progress log> [file to embed]
' Embeds objects Word activates through COM in a new document -- an Excel worksheet, and a package of a file
' when one is given -- saves it, opens it again and loads the worksheet, so what the activation filter Office
' registers is asked about, and what it answers, shows in a trace.  Each step records its result on its own.
' ActiveX controls are not tried: Microsoft 365 refuses to insert them by policy ("because of your policy
' settings"), before anything is activated.
Dim fso, progress, destination, embedded, word, document, started, shape
If WScript.Arguments.Count < 2 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
destination = WScript.Arguments(0)
progress = WScript.Arguments(1)
embedded = ""
If WScript.Arguments.Count > 2 Then embedded = WScript.Arguments(2)
Sub mark(text)
    Dim stream
    Set stream = fso.OpenTextFile(progress, 8, True)
    stream.WriteLine text
    stream.Close
End Sub
Sub result(what)
    If Err.Number <> 0 Then
        mark what & " failed: " & Hex(Err.Number) & " " & Err.Description
        Err.Clear
    Else
        mark what
    End If
End Sub
mark "start"
On Error Resume Next
started = False
Set word = GetObject(, "Word.Application")
If Err.Number <> 0 Then
    Err.Clear
    Set word = CreateObject("Word.Application")
    started = (Err.Number = 0)
End If
If Err.Number <> 0 Then
    mark "activation failed: " & Err.Number
    WScript.Quit 3
End If
mark "activated"
Set document = word.Documents.Add()
result "document created"
Set shape = document.InlineShapes.AddOLEObject("Excel.Sheet.12")
result "worksheet embedded"
If embedded <> "" Then
    Set shape = document.InlineShapes.AddOLEObject(, embedded, False, False)
    result "file embedded"
End If
mark "inline shapes: " & document.InlineShapes.Count
document.SaveAs2 destination, 16
result "saved"
document.Close False
result "closed"
Set document = word.Documents.Open(destination)
result "opened again"
mark "inline shapes: " & document.InlineShapes.Count
For Each shape In document.InlineShapes
    mark "class " & shape.OLEFormat.ClassType
    result "read class"
Next
If document.InlineShapes.Count > 0 Then
    Dim loaded
    Set loaded = document.InlineShapes(1).OLEFormat.Object
    result "worksheet loaded"
    Set loaded = Nothing
End If
document.Close False
result "closed again"
If started Then
    word.Quit
    result "application quit"
End If
WScript.Echo "done"
