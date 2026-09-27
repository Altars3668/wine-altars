Option Explicit
' Opens a presentation in a visible PowerPoint and holds it on screen, so what
' PowerPoint draws can be captured, then closes it:
'     powerpoint-show.vbs <presentation> <progress log> [seconds]
Dim app, presentation, fso, progress, source, seconds
If WScript.Arguments.Count < 2 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
source = WScript.Arguments(0)
progress = WScript.Arguments(1)
seconds = 20
If WScript.Arguments.Count > 2 Then seconds = CInt(WScript.Arguments(2))
Sub mark(text)
    Dim stream
    Set stream = fso.OpenTextFile(progress, 8, True)
    stream.WriteLine text
    stream.Close
End Sub
mark "start"
On Error Resume Next
Set app = CreateObject("PowerPoint.Application")
If Err.Number <> 0 Then
    mark "activation failed: " & Err.Number
    WScript.Quit 3
End If
On Error GoTo 0
mark "activated"
app.Visible = True
mark "visible"
Set presentation = app.Presentations.Open(source, False, False, True)
mark "opened"
WScript.Sleep seconds * 1000
mark "closing presentation"
On Error Resume Next
presentation.Close
If Err.Number <> 0 Then
    mark "presentation close failed: " & Err.Number
    WScript.Quit 4
End If
app.Quit
If Err.Number <> 0 Then
    mark "quit failed: " & Err.Number
    WScript.Quit 5
End If
On Error GoTo 0
mark "closed"
