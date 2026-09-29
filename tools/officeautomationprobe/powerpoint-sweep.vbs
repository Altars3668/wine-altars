' A sweep over PowerPoint features that lean on Windows components: slides from the layouts with text in two scripts,
' a table with a style, a chart, SmartArt, WordArt, shapes with effects, pictures in each format found in a directory,
' an SVG picture and a 3D model written by the sweep itself, a hyperlink, notes, a comment, a transition, an animation,
' sections, find and replace, a slide exported as images, saving in each format PowerPoint writes, a video, a
' password-encrypted save that is opened again, the saved files opened again, marking as final and a slide show.
' Each step is logged with its result or its error, and one failing does not stop the others.  Nothing is printed,
' mailed or sent to a cloud service.
' Usage: cscript powerpoint-sweep.vbs <output directory> <progress log> [picture directory]
Option Explicit
Dim fso, progress, outdir, pictures, app, presentation, slide, shape, rng, found, n, i, file, pres2, started, stream
If WScript.Arguments.Count < 2 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
outdir = WScript.Arguments(0)
progress = WScript.Arguments(1)
pictures = ""
If WScript.Arguments.Count > 2 Then pictures = WScript.Arguments(2)
Sub mark(text)
    Dim stream
    Set stream = fso.OpenTextFile(progress, 8, True, -1)
    stream.WriteLine text
    stream.Close
End Sub
' logs the step with what it found, or its error; the value is worked out before, in a statement of its own, as an
' error while evaluating the arguments would skip the call altogether
Sub result(what, found)
    If Err.Number <> 0 Then
        mark "FAIL " & what & ": " & Hex(Err.Number) & " " & Err.Description
        Err.Clear
    Else
        mark "ok   " & what & ": " & found
    End If
End Sub
Function size_of(path)
    If fso.FileExists(path) Then size_of = fso.GetFile(path).Size Else size_of = -1
End Function
Function count_files(path)
    If fso.FolderExists(path) Then count_files = fso.GetFolder(path).Files.Count Else count_files = -1
End Function

mark "start"
On Error Resume Next
started = False
Set app = CreateObject("PowerPoint.Application")
If Err.Number <> 0 Then
    mark "activation failed: " & Hex(Err.Number)
    WScript.Quit 3
End If
started = True
app.Visible = True
app.DisplayAlerts = 1       ' ppAlertsNone
Err.Clear
mark "activated, PowerPoint " & app.Version & " build " & app.Build

Set presentation = app.Presentations.Add(True)
result "Presentations.Add", presentation.Name
found = presentation.PageSetup.SlideWidth & "x" & presentation.PageSetup.SlideHeight & " points"
result "slide size", found

' a title slide, and bullets at two levels, in Latin and Chinese
Set slide = presentation.Slides.Add(1, 1)      ' ppLayoutTitle
slide.Shapes.Title.TextFrame.TextRange.Text = "Sweep"
slide.Shapes(2).TextFrame.TextRange.Text = "PowerPoint " & ChrW(&H529F) & ChrW(&H80FD) & ChrW(&H666E) & ChrW(&H67E5)
result "title slide", slide.Shapes.Count & " shapes"
Set slide = presentation.Slides.Add(2, 2)      ' ppLayoutText
slide.Shapes.Title.TextFrame.TextRange.Text = "Bullets"
slide.Shapes(2).TextFrame.TextRange.Text = "First point with colour" & vbCr & "A second level" & vbCr & _
    ChrW(&H7B2C) & ChrW(&H4E09) & ChrW(&H70B9) & vbCr & "Colours and colors"
slide.Shapes(2).TextFrame.TextRange.Paragraphs(2).IndentLevel = 2
found = slide.Shapes(2).TextFrame.TextRange.Paragraphs.Count & " paragraphs, level " & _
    slide.Shapes(2).TextFrame.TextRange.Paragraphs(2).IndentLevel
result "bullets", found

' a table in a built-in style
Set slide = presentation.Slides.Add(3, 11)     ' ppLayoutTitleOnly
slide.Shapes.Title.TextFrame.TextRange.Text = "Table and chart"
Set shape = slide.Shapes.AddTable(3, 4, 40, 110, 400, 150)
shape.Table.ApplyStyle "{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}", True    ' Medium Style 2 - Accent 1
shape.Table.Cell(1, 1).Shape.TextFrame.TextRange.Text = "Head"
shape.Table.Cell(2, 2).Shape.TextFrame.TextRange.Text = ChrW(&H8868) & ChrW(&H683C)
found = shape.Table.Rows.Count & "x" & shape.Table.Columns.Count & ", style " & shape.Table.Style.Name
result "table with a style", found

found = ""
Set shape = Nothing
Set shape = slide.Shapes.AddChart2(-1, 51, 460, 110, 440, 300)   ' xlColumnClustered
found = shape.HasChart & " " & shape.Chart.ChartType & ", " & shape.Chart.SeriesCollection.Count & " series"
result "chart", found
shape.Chart.ChartData.Workbook.Close
Err.Clear

' SmartArt, WordArt and shapes with effects
Set slide = presentation.Slides.Add(4, 12)     ' ppLayoutBlank
found = ""
Set shape = slide.Shapes.AddSmartArt(app.SmartArtLayouts(1), 20, 20, 400, 250)
found = shape.HasSmartArt & " " & shape.SmartArt.AllNodes.Count & " nodes"
result "SmartArt", found
found = ""
Set shape = slide.Shapes.AddTextEffect(0, "WordArt", "Arial", 36, 0, 0, 450, 20)
found = shape.Type
result "WordArt", found
found = ""
Set shape = slide.Shapes.AddShape(5, 450, 150, 200, 120)   ' msoShapeRoundedRectangle
shape.Fill.TwoColorGradient 1, 1
shape.Shadow.Visible = True
shape.Glow.Radius = 8
shape.SoftEdge.Radius = 4
shape.Reflection.Type = 1
shape.ThreeD.BevelTopType = 3
found = "gradient " & shape.Fill.Type & ", glow " & shape.Glow.Radius & ", bevel " & shape.ThreeD.BevelTopType
result "shape with effects", found

' each picture file, then an SVG this sweep writes itself
Set slide = presentation.Slides.Add(5, 12)
n = 0
If pictures <> "" Then
    If fso.FolderExists(pictures) Then
        For Each file In fso.GetFolder(pictures).Files
            If InStr(".webp.png.jpg.jpeg.gif.bmp.tif.tiff.heic.avif.jxl.ico.cur.emf.wmf.svg", LCase(Mid(file.Name, InStrRev(file.Name, ".")))) = 0 Then
                found = ""
            Else
            found = ""
            Set shape = slide.Shapes.AddPicture(file.Path, False, True, 20 + 110 * (n Mod 8), 20 + 110 * (n \ 8))
            found = shape.Width & "x" & shape.Height & " type " & shape.Type
            result "picture " & file.Name, found
            n = n + 1
            End If
        Next
    Else
        mark "no picture directory " & pictures
    End If
End If
Set stream = fso.CreateTextFile(outdir & "\sweep.svg", True)
stream.Write "<svg xmlns=""http://www.w3.org/2000/svg"" width=""120"" height=""80"" viewBox=""0 0 120 80"">" & _
    "<rect x=""4"" y=""4"" width=""112"" height=""72"" rx=""10"" fill=""#2b6cb0""/>" & _
    "<circle cx=""60"" cy=""40"" r=""24"" fill=""#f6e05e"" stroke=""#1a202c"" stroke-width=""3""/></svg>"
stream.Close
found = ""
Set shape = slide.Shapes.AddPicture(outdir & "\sweep.svg", False, True, 20, 300)
found = shape.Width & "x" & shape.Height & " type " & shape.Type
result "picture sweep.svg", found

' a hyperlink, notes, a comment, a transition, an animation and sections
Set slide = presentation.Slides(2)
slide.Shapes.Title.TextFrame.TextRange.ActionSettings(1).Hyperlink.Address = "https://example.com/"
found = slide.Shapes.Title.TextFrame.TextRange.ActionSettings(1).Hyperlink.Address
result "hyperlink", found
slide.NotesPage.Shapes.Placeholders(2).TextFrame.TextRange.Text = "Speaker notes " & ChrW(&H5907) & ChrW(&H6CE8)
found = Len(slide.NotesPage.Shapes.Placeholders(2).TextFrame.TextRange.Text) & " characters"
result "notes", found
found = ""
slide.Comments.Add 10, 10, "Sweep", "SW", "A comment."
found = slide.Comments.Count
result "comment", found
slide.SlideShowTransition.EntryEffect = 1793     ' ppEffectFade
found = slide.SlideShowTransition.EntryEffect
result "transition", found
presentation.Slides(3).SlideShowTransition.EntryEffect = 3955    ' ppEffectMorphByObject
found = presentation.Slides(3).SlideShowTransition.EntryEffect
result "morph transition", found
slide.TimeLine.MainSequence.AddEffect slide.Shapes(2), 2       ' msoAnimEffectFly
found = slide.TimeLine.MainSequence.Count & " effects"
result "animation", found
presentation.SectionProperties.AddBeforeSlide 1, "Opening"
presentation.SectionProperties.AddBeforeSlide 3, "Content"
found = presentation.SectionProperties.Count & " sections"
result "sections", found

found = ""
Set rng = presentation.Slides(2).Shapes(2).TextFrame.TextRange.Replace("colour", "color")
If Not rng Is Nothing Then found = "replaced at " & rng.Start Else found = "nothing replaced"
result "find and replace", found

' a slide as images
presentation.Slides(4).Export outdir & "\slide4.png", "PNG", 1280, 720
result "export slide4.png", size_of(outdir & "\slide4.png") & " bytes"
presentation.Slides(4).Export outdir & "\slide4.jpg", "JPG"
result "export slide4.jpg", size_of(outdir & "\slide4.jpg") & " bytes"
presentation.Slides(4).Export outdir & "\slide4.svg", "SVG"
result "export slide4.svg", size_of(outdir & "\slide4.svg") & " bytes"
presentation.Slides(4).Export outdir & "\slide4.emf", "EMF"
result "export slide4.emf", size_of(outdir & "\slide4.emf") & " bytes"

' every format PowerPoint writes by itself
Dim formats, names, k
formats = Array(24, 1, 25, 26, 28, 38, 35, 34, 32, 33)
names = Array("sweep.pptx", "sweep.ppt", "sweep.pptm", "sweep.potx", "sweep.ppsx", "sweep-strict.pptx", "sweep.odp", _
    "sweep.xml", "sweep.pdf", "sweep.xps")
For k = 0 To UBound(formats)
    presentation.SaveCopyAs outdir & "\" & names(k), formats(k)
    result "save " & names(k), size_of(outdir & "\" & names(k)) & " bytes"
Next
presentation.SaveCopyAs outdir & "\sweep-png", 18       ' ppSaveAsPNG, a folder of slides
result "save as PNG", count_files(outdir & "\sweep-png") & " files"

' encrypted with a password, then opened with it and with a wrong one
presentation.Password = "Sweep-2026"
presentation.SaveCopyAs outdir & "\sweep-password.pptx", 24
result "save with a password", size_of(outdir & "\sweep-password.pptx") & " bytes"
presentation.Password = ""
Err.Clear
found = ""
Set pres2 = app.Presentations.Open(outdir & "\sweep-password.pptx::Sweep-2026::", True, False, False)
found = pres2.Slides.Count & " slides"
result "open with the password", found
pres2.Close
Err.Clear
Set pres2 = Nothing
Set pres2 = app.Presentations.Open(outdir & "\sweep-password.pptx::wrong::", True, False, False)
If Err.Number <> 0 Then
    mark "ok   open with a wrong password refused: " & Hex(Err.Number)
    Err.Clear
Else
    mark "FAIL open with a wrong password succeeded"
    pres2.Close
End If

' the saved files opened again
For Each k In Array("sweep.pptx", "sweep.ppt", "sweep.odp", "sweep-strict.pptx")
    found = ""
    Set pres2 = Nothing
    Set pres2 = app.Presentations.Open(outdir & "\" & k, True, False, False)
    found = pres2.Slides.Count & " slides, " & pres2.Slides(4).Shapes.Count & " shapes on the fourth"
    result "open " & k, found
    pres2.Close
    Err.Clear
Next

' marking as final saves the presentation, so it gets a file first rather than a Save As dialog
presentation.SaveAs outdir & "\sweep-final.pptx", 24
presentation.Final = True
result "mark as final", presentation.Final
presentation.Final = False
Err.Clear

' a slide show, two steps in and out again
presentation.SlideShowSettings.Run
found = ""
found = "at slide " & presentation.SlideShowWindow.View.Slide.SlideIndex
result "slide show", found
presentation.SlideShowWindow.View.Next
presentation.SlideShowWindow.View.Next
found = "at slide " & presentation.SlideShowWindow.View.Slide.SlideIndex
result "slide show next", found
presentation.SlideShowWindow.View.Exit
result "slide show exit", app.SlideShowWindows.Count & " windows"

' a 3D model, last, as it is the step most likely to take PowerPoint down
Set slide = presentation.Slides(5)
Set stream = fso.CreateTextFile(outdir & "\sweep.obj", True)
stream.Write "v 0 0 0" & vbLf & "v 1 0 0" & vbLf & "v 1 1 0" & vbLf & "v 0 1 0" & vbLf & "v 0 0 1" & vbLf & _
    "v 1 0 1" & vbLf & "v 1 1 1" & vbLf & "v 0 1 1" & vbLf & "f 1 2 3 4" & vbLf & "f 5 8 7 6" & vbLf & _
    "f 1 5 6 2" & vbLf & "f 2 6 7 3" & vbLf & "f 3 7 8 4" & vbLf & "f 5 1 4 8" & vbLf
stream.Close
found = ""
Set shape = slide.Shapes.Add3DModel(outdir & "\sweep.obj", False, True, 300, 300, 150, 150)
found = shape.Width & "x" & shape.Height & " type " & shape.Type
result "3D model", found

' an animated GIF and a video, last, as PowerPoint makes them in the background and may stay busy
presentation.SaveCopyAs outdir & "\sweep.gif", 40       ' ppSaveAsAnimatedGIF
For i = 1 To 120
    If size_of(outdir & "\sweep.gif") > 0 Then Exit For
    WScript.Sleep 1000
Next
result "save sweep.gif", size_of(outdir & "\sweep.gif") & " bytes after " & i & " s"
' a video, which PowerPoint makes in the background
presentation.CreateVideo outdir & "\sweep.mp4", False, 1, 360, 15, 60
If Err.Number <> 0 Then
    result "video", ""
Else
    For i = 1 To 240
        If presentation.CreateVideoStatus <> 1 And presentation.CreateVideoStatus <> 2 Then Exit For   ' in progress, queued
        WScript.Sleep 1000
    Next
    found = "status " & presentation.CreateVideoStatus & " after " & i & " s, " & size_of(outdir & "\sweep.mp4") & " bytes"
    result "video", found
End If

presentation.Saved = True
presentation.Close
result "Close", ""
If started Then
    app.Quit
    result "Quit", ""
End If
mark "done"
