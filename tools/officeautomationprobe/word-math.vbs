' Whether Word builds equations: a document with two equations written in Word's linear format, built up into
' professional form, the number of equations Word then counts and their text, exported to PDF so that the page can be
' looked at; with a font, the document's equations are in it.  Starts its own Word and quits it without saving.
' Usage: cscript word-math.vbs <output.pdf> <progress log> [math font or ""] [seconds to show the document]
Option Explicit
Dim fso, progress, destination, word, document, range, math, i
If WScript.Arguments.Count < 2 Or WScript.Arguments.Count > 4 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
destination = WScript.Arguments(0)
progress = WScript.Arguments(1)
Sub mark(text)
    Dim stream
    Set stream = fso.OpenTextFile(progress, 8, True, -1)
    stream.WriteLine text
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
Set document = word.Documents.Add()
check "Documents.Add"
document.Content.Text = "Equations:"
mark "math font " & document.OMathFontName
If WScript.Arguments.Count >= 3 Then
    If WScript.Arguments(2) <> "" Then document.OMathFontName = WScript.Arguments(2)
    check "OMathFontName"
    If WScript.Arguments(2) <> "" Then mark "math font now " & document.OMathFontName
End If
' each equation in a paragraph of its own, in linear format, then built up
Set range = document.Content
range.InsertParagraphAfter
Set range = document.Paragraphs(document.Paragraphs.Count).Range
range.Text = "a^2+b^2=c^2"
Set math = document.OMaths.Add(range)
check "OMaths.Add"
math.OMaths(1).BuildUp
check "BuildUp"
Set range = document.Content
range.InsertParagraphAfter
Set range = document.Paragraphs(document.Paragraphs.Count).Range
range.Text = "x=(-b" & ChrW(&HB1) & ChrW(&H221A) & "(b^2-4ac))/2a"
Set math = document.OMaths.Add(range)
check "OMaths.Add 2"
math.OMaths(1).BuildUp
check "BuildUp 2"
mark "equations " & document.OMaths.Count
For i = 1 To document.OMaths.Count
    mark "  " & i & ": type " & document.OMaths(i).Type & ", functions " & document.OMaths(i).Functions.Count & _
        ", text " & document.OMaths(i).Range.Text
Next
check "reading the equations"
If WScript.Arguments.Count = 4 Then
    word.Visible = True
    document.Activate
    mark "showing"
    WScript.Sleep CLng(WScript.Arguments(3)) * 1000
End If
document.ExportAsFixedFormat destination, 17
check "ExportAsFixedFormat"
mark "exported"
document.Close False
word.Quit
mark "done"
