Option Explicit
Dim fso, progress, destination, word, document, started
If WScript.Arguments.Count <> 2 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
destination = WScript.Arguments(0)
progress = WScript.Arguments(1)
Sub mark(text)
    Dim stream
    Set stream = fso.OpenTextFile(progress, 8, True)
    stream.WriteLine text
    stream.Close
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
On Error GoTo 0
mark "activated"
Set document = word.Documents.Add()
mark "document created"
document.Content.Text = "wine-altars Word automation smoke"
mark "document edited"
document.SaveAs2 destination, 16
mark "saved"
document.Close False
mark "closed"
' Only a Word this script started is quit; one that was already running is left alone.
If started Then
    mark "quitting application"
    On Error Resume Next
    word.Quit
    If Err.Number <> 0 Then
        mark "application quit failed: " & Err.Number
        WScript.Quit 5
    End If
    On Error GoTo 0
    mark "application quit"
End If
WScript.Echo "saved local DOCX"
