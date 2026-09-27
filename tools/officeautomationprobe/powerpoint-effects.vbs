Option Explicit
' Builds a slide with the shape and picture effects PowerPoint draws through
' Direct2D -- shadow, glow, soft edges, reflection, bevel, gradient, a picture
' recoloured, brightened and blurred, and text effects -- saves it, and holds it
' on screen so it can be captured, then quits:
'
'   powerpoint-effects.vbs <output.pptx> <progress log> <picture> [seconds]
'
' Every effect is applied on its own and its result logged, so one PowerPoint
' refuses does not stop the rest.
Dim fso, progress, destination, picturePath, seconds, app, presentation, slide, shape, pic
If WScript.Arguments.Count < 3 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
destination = WScript.Arguments(0)
progress = WScript.Arguments(1)
picturePath = WScript.Arguments(2)
seconds = 20
If WScript.Arguments.Count > 3 Then seconds = CInt(WScript.Arguments(3))

Sub mark(text)
    Dim stream
    Set stream = fso.OpenTextFile(progress, 8, True)
    stream.WriteLine text
    stream.Close
End Sub

Sub result(name)
    If Err.Number <> 0 Then
        mark name & " failed: " & Hex(Err.Number) & " " & Err.Description
        Err.Clear
    Else
        mark name
    End If
End Sub

mark "start"
On Error Resume Next
Set app = CreateObject("PowerPoint.Application")
If Err.Number <> 0 Then
    mark "activation failed: " & Hex(Err.Number)
    WScript.Quit 3
End If
On Error GoTo 0
mark "activated"
app.Visible = True
Set presentation = app.Presentations.Add(True)
Set slide = presentation.Slides.Add(1, 12)
mark "slide created"

On Error Resume Next
Set shape = slide.Shapes.AddShape(1, 40, 40, 200, 110)
shape.Fill.TwoColorGradient 1, 1
shape.Fill.ForeColor.RGB = RGB(30, 90, 200)
shape.Fill.BackColor.RGB = RGB(240, 200, 60)
result "gradient"
shape.Shadow.Visible = -1
shape.Shadow.Blur = 12
shape.Shadow.OffsetX = 8
shape.Shadow.OffsetY = 8
result "shadow"

Set shape = slide.Shapes.AddShape(9, 300, 40, 160, 110)
shape.Fill.ForeColor.RGB = RGB(40, 160, 90)
shape.Glow.Radius = 14
shape.Glow.Color.RGB = RGB(255, 60, 60)
result "glow"

Set shape = slide.Shapes.AddShape(5, 520, 40, 180, 110)
shape.Fill.ForeColor.RGB = RGB(150, 60, 170)
shape.SoftEdge.Radius = 12
result "soft edge"

Set shape = slide.Shapes.AddShape(1, 40, 200, 200, 90)
shape.Fill.ForeColor.RGB = RGB(230, 120, 30)
shape.Reflection.Type = 1
result "reflection"

Set shape = slide.Shapes.AddShape(1, 300, 200, 160, 90)
shape.Fill.ForeColor.RGB = RGB(60, 140, 220)
shape.ThreeD.BevelTopType = 3
shape.ThreeD.BevelTopInset = 10
shape.ThreeD.BevelTopDepth = 10
result "bevel"

Set pic = slide.Shapes.AddPicture(picturePath, 0, -1, 520, 200, 200, 150)
result "picture"
pic.PictureFormat.ColorType = 2
result "picture grayscale"
pic.PictureFormat.Brightness = 0.65
pic.PictureFormat.Contrast = 0.7
result "picture brightness"
pic.Fill.PictureEffects.Insert 3
result "picture blur"

Set shape = slide.Shapes.AddTextbox(1, 40, 380, 640, 80)
shape.TextFrame2.TextRange.Text = "wine-altars effects"
shape.TextFrame2.TextRange.Font.Size = 44
shape.TextFrame2.TextRange.Font.Fill.ForeColor.RGB = RGB(20, 110, 200)
shape.TextFrame2.TextRange.Font.Glow.Radius = 8
shape.TextFrame2.TextRange.Font.Glow.Color.RGB = RGB(250, 220, 60)
shape.TextFrame2.TextRange.Font.Shadow.Visible = -1
shape.TextFrame2.TextRange.Font.Reflection.Type = 2
result "text effects"
On Error GoTo 0

presentation.SaveAs destination, 24
mark "saved"
mark "showing"
WScript.Sleep seconds * 1000

On Error Resume Next
presentation.Close
result "presentation closed"
app.Quit
result "application quit"
mark "closed"
