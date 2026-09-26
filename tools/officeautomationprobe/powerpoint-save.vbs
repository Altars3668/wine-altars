Option Explicit
Dim app, presentation, slide, fso, progress, destination
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
Set app = CreateObject("PowerPoint.Application")
If Err.Number <> 0 Then
    mark "activation failed: " & Err.Number
    WScript.Quit 3
End If
On Error GoTo 0
mark "activated"
app.Visible = True
mark "visible"
Set presentation = app.Presentations.Add(True)
mark "presentation created"
Set slide = presentation.Slides.Add(1, 1)
mark "slide created"
slide.Shapes.Title.TextFrame.TextRange.Text = "wine-altars PowerPoint automation smoke"
mark "slide edited"
presentation.SaveAs destination, 24
mark "saved"
presentation.Close
app.Quit
mark "closed"
WScript.Echo "saved local PPTX"
