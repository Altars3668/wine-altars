Option Explicit
' Builds a slide of gradient fills -- linear at an angle, from the centre, from a
' corner, a preset of many stops, stops with transparency over a coloured shape,
' and gradient text -- saves it, and holds it on screen so it can be captured,
' then quits:
'
'   powerpoint-gradients.vbs <output.pptx> <progress log> [seconds]
'
' Every fill is applied on its own and its result logged, so one PowerPoint
' refuses does not stop the rest.
Dim fso, progress, destination, seconds, app, presentation, slide, shape, stops
If WScript.Arguments.Count < 2 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
destination = WScript.Arguments(0)
progress = WScript.Arguments(1)
seconds = 20
If WScript.Arguments.Count > 2 Then seconds = CInt(WScript.Arguments(2))

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
' Linear, red to blue, at 30 degrees.
Set shape = slide.Shapes.AddShape(1, 30, 30, 200, 120)
shape.Line.Visible = 0
shape.Fill.TwoColorGradient 1, 1
shape.Fill.GradientStops(1).Color.RGB = RGB(255, 0, 0)
shape.Fill.GradientStops(2).Color.RGB = RGB(0, 0, 255)
shape.Fill.GradientAngle = 30
result "linear"

' From the centre: a radial gradient, white in the middle.
Set shape = slide.Shapes.AddShape(9, 260, 30, 200, 120)
shape.Line.Visible = 0
shape.Fill.TwoColorGradient 7, 1
shape.Fill.GradientStops(1).Color.RGB = RGB(255, 255, 255)
shape.Fill.GradientStops(2).Color.RGB = RGB(0, 120, 60)
result "from centre"

' From a corner.
Set shape = slide.Shapes.AddShape(1, 490, 30, 200, 120)
shape.Line.Visible = 0
shape.Fill.TwoColorGradient 5, 1
shape.Fill.GradientStops(1).Color.RGB = RGB(255, 220, 0)
shape.Fill.GradientStops(2).Color.RGB = RGB(120, 0, 160)
result "from corner"

' A preset with many stops (rainbow).
Set shape = slide.Shapes.AddShape(1, 30, 180, 200, 120)
shape.Line.Visible = 0
shape.Fill.PresetGradient 1, 1, 16
result "preset"

' Stops fading to transparent, over a dark bar, so what is behind shows through.
Set shape = slide.Shapes.AddShape(1, 260, 220, 430, 40)
shape.Line.Visible = 0
shape.Fill.Solid
shape.Fill.ForeColor.RGB = RGB(20, 20, 20)
Set shape = slide.Shapes.AddShape(1, 260, 180, 430, 120)
shape.Line.Visible = 0
shape.Fill.TwoColorGradient 1, 1
shape.Fill.GradientStops(1).Color.RGB = RGB(255, 140, 0)
shape.Fill.GradientStops(1).Transparency = 0
shape.Fill.GradientStops(2).Color.RGB = RGB(255, 140, 0)
shape.Fill.GradientStops(2).Transparency = 1
shape.Fill.GradientAngle = 0
result "to transparent"

' Three stops, the middle one half transparent.
Set shape = slide.Shapes.AddShape(1, 30, 330, 300, 90)
shape.Line.Visible = 0
shape.Fill.TwoColorGradient 1, 1
Set stops = shape.Fill.GradientStops
stops(1).Color.RGB = RGB(0, 160, 220)
stops(2).Color.RGB = RGB(220, 0, 120)
stops.Insert RGB(255, 255, 255), 0.5, 0.5
shape.Fill.GradientAngle = 0
result "three stops"

' Gradient text.
Set shape = slide.Shapes.AddTextbox(1, 360, 330, 330, 90)
shape.TextFrame2.TextRange.Text = "Gradient"
shape.TextFrame2.TextRange.Font.Size = 54
shape.TextFrame2.TextRange.Font.Bold = -1
shape.TextFrame2.TextRange.Font.Fill.TwoColorGradient 1, 1
shape.TextFrame2.TextRange.Font.Fill.GradientStops(1).Color.RGB = RGB(200, 0, 0)
shape.TextFrame2.TextRange.Font.Fill.GradientStops(2).Color.RGB = RGB(0, 0, 200)
result "text"

presentation.SaveAs destination
result "saved"
On Error GoTo 0

mark "showing"
WScript.Sleep seconds * 1000
On Error Resume Next
presentation.Close
result "closed"
app.Quit
result "quit"
