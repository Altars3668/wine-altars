' A sweep over Word features that lean on Windows components: styles and a table of contents, footnotes, comments,
' tracked changes, table styles, pictures in each format found in a directory, a chart, SmartArt, WordArt, a text box,
' a hyperlink, a page number field, a wildcard search, saving in each format Word writes, the exported PDF opened
' again, a password-encrypted save that is opened again, comparing two documents and restricting editing.  Each step is logged with its result or its error,
' and one failing does not stop the others.  Nothing is printed, mailed or sent to a cloud service.
' Usage: cscript word-sweep.vbs <output directory> <progress log> [picture directory]
Option Explicit
Dim fso, progress, outdir, pictures, word, document, started, rng, n, i, file, shape, doc2, compared, stats, found, style
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

mark "start"
On Error Resume Next
started = False
Set word = CreateObject("Word.Application")
If Err.Number <> 0 Then
    mark "activation failed: " & Hex(Err.Number)
    WScript.Quit 3
End If
started = True
word.DisplayAlerts = 0
mark "activated, Word " & word.Version & " build " & word.Build

Set document = word.Documents.Add()
result "Documents.Add", document.Name

' headings, body text and a table of contents over them
document.Content.Text = "Sweep" & vbCr & "First chapter" & vbCr & "Body text of the first chapter." & vbCr & _
    "Second chapter" & vbCr & "Body text with colour and colours, color and colors." & vbCr & "Section 2.1" & vbCr & _
    "More text." & vbCr
document.Paragraphs(1).Style = document.Styles(-63)   ' wdStyleTitle
document.Paragraphs(2).Style = document.Styles(-2)    ' wdStyleHeading1
document.Paragraphs(4).Style = document.Styles(-2)
document.Paragraphs(6).Style = document.Styles(-3)    ' wdStyleHeading2
result "heading styles", document.Paragraphs(2).Style.NameLocal
Set rng = document.Paragraphs(2).Range
rng.InsertParagraphBefore
Set rng = document.Paragraphs(2).Range
rng.Collapse 1
document.TablesOfContents.Add rng, True, 1, 3
document.TablesOfContents(1).Update
result "table of contents", document.TablesOfContents(1).Range.Paragraphs.Count & " paragraphs"

Set rng = document.Paragraphs(document.Paragraphs.Count - 1).Range
document.Footnotes.Add rng, , "A footnote."
result "footnote", document.Footnotes.Count
document.Endnotes.Add document.Paragraphs(document.Paragraphs.Count - 2).Range, , "An endnote."
result "endnote", document.Endnotes.Count
document.Comments.Add document.Paragraphs(3).Range, "A comment."
result "comment", document.Comments.Count

document.TrackRevisions = True
document.Content.InsertAfter "Tracked insertion." & vbCr
document.TrackRevisions = False
n = document.Revisions.Count
document.Revisions.AcceptAll
result "tracked changes", n & " revisions, then " & document.Revisions.Count

Set rng = document.Content
rng.Collapse 0
Set shape = document.Tables.Add(rng, 3, 3)
' the tenth table style, whatever the names are called in the interface language
n = 0
For Each style In document.Styles
    If style.Type = 3 Then
        n = n + 1
        If n = 10 Then shape.Style = style.NameLocal
    End If
Next
shape.Cell(1, 1).Range.Text = "Head"
shape.Cell(2, 2).Range.Text = ChrW(&H8868) & ChrW(&H683C)
found = shape.Rows.Count & "x" & shape.Columns.Count & ", " & n & " table styles"
result "table with a style", found

' each picture file on its own paragraph
If pictures <> "" Then
    If fso.FolderExists(pictures) Then
        For Each file In fso.GetFolder(pictures).Files
            Set rng = document.Content
            rng.Collapse 0
            rng.InsertParagraphAfter
            Set rng = document.Content
            rng.Collapse 0
            found = ""
            Set shape = document.InlineShapes.AddPicture(file.Path, False, True, rng)
            found = shape.Width & "x" & shape.Height & " type " & shape.Type
            result "picture " & file.Name, found
        Next
    Else
        mark "no picture directory " & pictures
    End If
End If

Set rng = document.Content
rng.Collapse 0
found = ""
Set shape = Nothing
Set shape = document.InlineShapes.AddChart2(-1, 51, rng)   ' xlColumnClustered
found = shape.HasChart & " " & shape.Chart.ChartType & ", " & shape.Chart.SeriesCollection.Count & " series"
result "chart", found
shape.Chart.ChartData.Workbook.Close
Err.Clear

found = ""
Set shape = document.Shapes.AddSmartArt(word.SmartArtLayouts(1), 50, 50, 300, 200)
found = shape.HasSmartArt & " " & shape.SmartArt.AllNodes.Count & " nodes"
result "SmartArt", found
found = ""
Set shape = document.Shapes.AddTextEffect(0, "WordArt", "Arial", 36, False, False, 50, 300)
found = shape.Type
result "WordArt", found
found = ""
Set shape = document.Shapes.AddTextbox(1, 300, 300, 150, 60)
shape.TextFrame.TextRange.Text = ChrW(&H6587) & ChrW(&H672C) & ChrW(&H6846) & " text box"
found = Len(shape.TextFrame.TextRange.Text) & " characters"
result "text box", found

Set rng = document.Content
rng.Collapse 0
document.Hyperlinks.Add rng, "https://example.com/", , , "example link"
result "hyperlink", document.Hyperlinks.Count
document.Sections(1).Footers(1).PageNumbers.Add 2, True
result "page numbers", document.Sections(1).Footers(1).PageNumbers.Count

Set rng = document.Content
With rng.Find
    .ClearFormatting
    .Text = "<colo*r>"
    .MatchWildcards = True
    n = 0
    Do While .Execute
        n = n + 1
        rng.Collapse 0
        If n > 20 Then Exit Do
    Loop
End With
result "wildcard search", n & " matches"
stats = document.ComputeStatistics(0) & " words, " & document.ComputeStatistics(2) & " pages"
result "statistics", stats

' every format Word writes by itself
Dim formats, names, k
formats = Array(16, 0, 6, 2, 10, 9, 23, 14, 19)
names = Array("sweep.docx", "sweep.doc", "sweep.rtf", "sweep.txt", "sweep.htm", "sweep.mht", "sweep.odt", "sweep.dotx", _
    "sweep.xml")
For k = 0 To UBound(formats)
    document.SaveAs2 outdir & "\" & names(k), formats(k)
    result "save " & names(k), size_of(outdir & "\" & names(k)) & " bytes"
Next
document.ExportAsFixedFormat outdir & "\sweep.pdf", 17
result "export sweep.pdf", size_of(outdir & "\sweep.pdf") & " bytes"
document.ExportAsFixedFormat outdir & "\sweep.xps", 18
result "export sweep.xps", size_of(outdir & "\sweep.xps") & " bytes"

' the PDF opened again: Word converts it with PDFREFLOW.EXE, after a notice DisplayAlerts does not hold back, so the
' notice is switched off for this step and the setting put back as it was
Dim shell, pdf_warning, had_warning
Set shell = CreateObject("WScript.Shell")
pdf_warning = "HKCU\Software\Microsoft\Office\" & word.Version & "\Word\Options\DisableConvertPdfWarning"
had_warning = shell.RegRead(pdf_warning)
If Err.Number <> 0 Then had_warning = Empty : Err.Clear
shell.RegWrite pdf_warning, 1, "REG_DWORD"
Set doc2 = word.Documents.Open(outdir & "\sweep.pdf", False, True, False)
If Err.Number <> 0 Then
    result "open sweep.pdf", ""
Else
    found = ""
    found = doc2.Paragraphs.Count & " paragraphs, " & doc2.InlineShapes.Count + doc2.Shapes.Count & " shapes"
    result "open sweep.pdf", found
    doc2.Close False
End If
If IsEmpty(had_warning) Then shell.RegDelete pdf_warning Else shell.RegWrite pdf_warning, had_warning, "REG_DWORD"
Err.Clear

' encrypted with a password, then opened with it
document.SaveAs2 outdir & "\sweep-password.docx", 16, , "Sweep-2026"
result "save with a password", size_of(outdir & "\sweep-password.docx") & " bytes"
document.Close False
Set doc2 = word.Documents.Open(outdir & "\sweep-password.docx", False, True, False, "Sweep-2026")
found = ""
found = doc2.Paragraphs.Count & " paragraphs, first " & Left(doc2.Paragraphs(1).Range.Text, 20)
result "open with the password", found
doc2.Close False
Set doc2 = word.Documents.Open(outdir & "\sweep-password.docx", False, True, False, "wrong")
' 5408 is the refusal; 462 would be Word gone
If Err.Number = 5408 Then
    mark "ok   open with a wrong password refused: " & Hex(Err.Number)
    Err.Clear
ElseIf Err.Number <> 0 Then
    mark "FAIL open with a wrong password: " & Hex(Err.Number) & " " & Err.Description
    Err.Clear
Else
    mark "FAIL open with a wrong password succeeded"
    doc2.Close False
End If

' comparing the document with an edited copy
Set document = word.Documents.Open(outdir & "\sweep.docx")
document.Content.InsertAfter "An added paragraph." & vbCr
document.Paragraphs(3).Range.Delete
document.SaveAs2 outdir & "\sweep-edited.docx", 16
document.Close False
Set document = word.Documents.Open(outdir & "\sweep.docx")
Set doc2 = word.Documents.Open(outdir & "\sweep-edited.docx")
found = ""
Set compared = word.CompareDocuments(document, doc2)
found = compared.Revisions.Count & " revisions"
result "compare documents", found
compared.Close False
doc2.Close False

document.Protect 3, False, "Sweep-2026"   ' wdAllowOnlyReading
result "restrict editing", document.ProtectionType
document.Unprotect "Sweep-2026"
result "remove the restriction", document.ProtectionType
document.Close False

If started Then
    word.Quit
    result "Quit", ""
End If
mark "done"
