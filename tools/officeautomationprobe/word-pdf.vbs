' Whether Word exports a document to PDF: a document with a heading, a line of English and one of Chinese, a table
' and a shape, exported with ExportAsFixedFormat, and the number of pages Word says it has.
' Usage: cscript word-pdf.vbs <output.pdf> <progress log>
Option Explicit
Dim fso, progress, destination, word, document, started, table, range
If WScript.Arguments.Count <> 2 Then WScript.Quit 2
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
started = False
Set word = GetObject(, "Word.Application")
If Err.Number <> 0 Then
    Err.Clear
    Set word = CreateObject("Word.Application")
    started = (Err.Number = 0)
End If
If Err.Number <> 0 Then
    mark "activation failed: " & Hex(Err.Number)
    WScript.Quit 3
End If
mark "activated"

Set document = word.Documents.Add()
check "Documents.Add"
' "PDF export check", then the same in Chinese
document.Content.Text = "wine-altars PDF export check" & vbCr & "The quick brown fox jumps over the lazy dog." & vbCr & _
    ChrW(&H5BFC) & ChrW(&H51FA) & " PDF " & ChrW(&H6D4B) & ChrW(&H8BD5) & ChrW(&HFF1A) & ChrW(&H4E2D) & ChrW(&H6587) & _
    ChrW(&H6587) & ChrW(&H672C) & vbCr
check "text"
document.Paragraphs(1).Style = document.Styles(-2)
check "heading style"
Set range = document.Content
range.Collapse 0
Set table = document.Tables.Add(range, 2, 3)
check "Tables.Add"
table.Borders.Enable = True
table.Cell(1, 1).Range.Text = "cell one"
table.Cell(2, 3).Range.Text = "cell six"
check "table text"
document.Shapes.AddShape 1, 100, 400, 120, 60
check "AddShape"
mark "document made, pages " & document.ComputeStatistics(2)

document.ExportAsFixedFormat destination, 17
check "ExportAsFixedFormat"
mark "exported"

document.Close False
check "Close"
' Only a Word this script started is quit; one that was already running is left alone.
If started Then
    word.Quit
    check "Quit"
End If
mark "done"
