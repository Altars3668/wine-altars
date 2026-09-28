' Whether Word takes what is typed into it: a visible new document waits until another program has typed into it (the
' go file appears), then the text of the document goes to the progress log, and the code of every character that is
' not printable ASCII.  Starts its own Word and quits it without saving.
' Usage: cscript word-type.vbs <progress log> <go file>
Option Explicit
Dim fso, progress, go, word, document, waited, text, codes, i, code
If WScript.Arguments.Count <> 2 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
progress = WScript.Arguments(0)
go = WScript.Arguments(1)
Sub mark(line)
    Dim stream
    Set stream = fso.OpenTextFile(progress, 8, True, -1)
    stream.WriteLine line
    stream.Close
End Sub
Sub check(what)
    If Err.Number <> 0 Then
        mark what & " failed: " & Hex(Err.Number) & " " & Err.Description
        Err.Clear
    End If
End Sub
mark "start"
On Error Resume Next
Set word = CreateObject("Word.Application")
If Err.Number <> 0 Then
    mark "activation failed: " & Hex(Err.Number)
    WScript.Quit 3
End If
word.Visible = True
Set document = word.Documents.Add()
check "Documents.Add"
document.Activate
word.Activate
check "Activate"
mark "ready"
waited = 0
Do While Not fso.FileExists(go) And waited < 240
    WScript.Sleep 500
    waited = waited + 1
Loop
text = document.Content.Text
check "reading the text"
codes = ""
For i = 1 To Len(text)
    code = AscW(Mid(text, i, 1)) And &HFFFF&
    If code < 32 Or code > 126 Then codes = codes & " U+" & Right("000" & Hex(code), 4)
Next
mark "text " & text
mark "codes" & codes
document.Close False
check "Close"
word.Quit
check "Quit"
mark "done"
