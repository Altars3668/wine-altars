Option Explicit
' word-embed.vbs <output.docx> <progress log> [file to embed] [seconds to show] [options]
' Embeds objects Word activates through COM in a new document -- an Excel worksheet, and a package of a file
' when one is given -- saves it, opens it again, activates the worksheet in place and reads its object model, so
' what the activation filter Office registers is asked about, and what it answers, shows in a trace.  Each step
' records its result on its own.  Given seconds to show, Word is made visible and the worksheet stays active in
' place that long, for a screenshot.  Options, any of them separated by commas: "open" opens the worksheet in a
' window of its own (OLEIVERB_OPEN, as the context menu's Open does) instead of in place; "new" starts a Word of
' its own even when one is running, instead of using that one, and quits only that; "existing" opens the output
' document as it is, made by an earlier run, instead of making it (Documents.Add can wait on a prompt nobody
' answers: the first new document of an account that has not chosen where new files are saved asks for that);
' "wait" marks "waiting" before the activation and waits until a file named as the progress log with ".go" added
' exists, so that a debugger can be attached first.
' An activation that fails is tried twice more, ten seconds apart: a worksheet server that loads add-ins answers
' too late for Word the first time.
' ActiveX controls are not tried: Microsoft 365 refuses to insert them by policy ("because of your policy
' settings"), before anything is activated.
Dim fso, progress, destination, embedded, word, document, started, shape, seconds, options
If WScript.Arguments.Count < 2 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
destination = WScript.Arguments(0)
progress = WScript.Arguments(1)
embedded = ""
If WScript.Arguments.Count > 2 Then embedded = WScript.Arguments(2)
seconds = 0
If WScript.Arguments.Count > 3 Then seconds = CInt(WScript.Arguments(3))
options = ""
If WScript.Arguments.Count > 4 Then options = "," & WScript.Arguments(4) & ","
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
If InStr(options, ",new,") = 0 Then Set word = GetObject(, "Word.Application")
If InStr(options, ",new,") > 0 Or Err.Number <> 0 Then
    Err.Clear
    Set word = CreateObject("Word.Application")
    started = (Err.Number = 0)
End If
If Err.Number <> 0 Then
    mark "activation failed: " & Err.Number
    WScript.Quit 3
End If
mark "activated"
If InStr(options, ",existing,") = 0 Then
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
End If
Set document = word.Documents.Open(destination)
result "opened again"
mark "inline shapes: " & document.InlineShapes.Count
For Each shape In document.InlineShapes
    mark "class " & shape.OLEFormat.ClassType
    result "read class"
Next
If document.InlineShapes.Count > 0 Then
    Dim loaded, attempt
    ' the object is activated first, as double-clicking it would, and then gives its object model
    If seconds > 0 Then word.Visible = True
    If InStr(options, ",wait,") > 0 Then
        mark "waiting"
        attempt = 0
        Do While Not fso.FileExists(progress & ".go") And attempt < 1500
            WScript.Sleep 200
            attempt = attempt + 1
        Loop
    End If
    For attempt = 1 To 3
        If InStr(options, ",open,") > 0 Then
            document.InlineShapes(1).OLEFormat.DoVerb -2
        Else
            document.InlineShapes(1).OLEFormat.Activate
        End If
        If Err.Number = 0 Or attempt = 3 Then Exit For
        result "worksheet activated"
        WScript.Sleep 10000
    Next
    result "worksheet activated"
    If seconds > 0 Then
        WScript.Sleep seconds * 1000
        mark "shown"
    End If
    Set loaded = document.InlineShapes(1).OLEFormat.Object
    result "worksheet loaded"
    mark "sheet " & loaded.Worksheets(1).Name
    result "sheet named"
    Set loaded = Nothing
End If
document.Close False
result "closed again"
If started Then
    word.Quit
    result "application quit"
End If
WScript.Echo "done"
