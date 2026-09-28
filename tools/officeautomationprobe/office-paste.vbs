' Copying between Excel and Word through the clipboard: a range Excel copies, pasted into Word as it is (a table) and
' as an embedded worksheet; text Word copies, pasted into an Excel cell.  Starts its own Excel and Word and quits them.
' Run it on a display whose clipboard is nobody's: it replaces what is on the clipboard.
' Usage: cscript office-paste.vbs <progress log>
Option Explicit
Dim fso, progress, excel, book, sheet, word, document, shape
If WScript.Arguments.Count <> 1 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
progress = WScript.Arguments(0)
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
Function oneline(text)
    oneline = Replace(Replace(Replace(text, vbCr, "|"), vbLf, "|"), vbTab, "\t")
End Function
mark "start"
On Error Resume Next
Set excel = CreateObject("Excel.Application")
If Err.Number <> 0 Then
    mark "Excel activation failed: " & Hex(Err.Number)
    WScript.Quit 3
End If
mark "Excel activated"
Set book = excel.Workbooks.Add()
check "Workbooks.Add"
Set sheet = book.Worksheets(1)
sheet.Range("A1").Value = "alpha"
sheet.Range("B1").Value = "beta"
sheet.Range("A2").Value = 1
sheet.Range("B2").Value = 2
check "cells"
sheet.Range("A1:B2").Copy
check "Range.Copy"
mark "Excel copied A1:B2"

Set word = CreateObject("Word.Application")
If Err.Number <> 0 Then
    mark "Word activation failed: " & Hex(Err.Number)
    Err.Clear
Else
    mark "Word activated"
    Set document = word.Documents.Add()
    check "Documents.Add"
    word.Selection.Paste
    check "Selection.Paste"
    mark "pasted into Word: tables " & document.Tables.Count & ", text " & oneline(document.Content.Text)
    check "reading what was pasted"
    word.Selection.EndKey 6
    word.Selection.TypeParagraph
    word.Selection.PasteSpecial , , , , 0
    check "PasteSpecial as an object"
    mark "pasted as an object: inline shapes " & document.InlineShapes.Count & ", shapes " & document.Shapes.Count & _
        ", fields " & document.Fields.Count & ", characters " & document.Characters.Count
    For Each shape In document.Shapes
        mark "  shape type " & shape.Type & ", class " & shape.OLEFormat.ProgID
    Next
    check "reading the floating object"
    ' and once more in line with the text (wdInLine)
    word.Selection.EndKey 6
    word.Selection.TypeParagraph
    word.Selection.PasteSpecial , , 0, , 0
    check "PasteSpecial as an object in line"
    mark "pasted in line: inline shapes " & document.InlineShapes.Count & ", shapes " & document.Shapes.Count
    For Each shape In document.InlineShapes
        mark "  inline shape type " & shape.Type & ", class " & shape.OLEFormat.ProgID
    Next
    check "reading the object"

    document.Content.Text = "from word"
    document.Content.Copy
    check "Content.Copy"
    mark "Word copied its text"
    sheet.Paste sheet.Range("D1")
    check "Worksheet.Paste"
    mark "pasted into Excel: D1 " & oneline(CStr(sheet.Range("D1").Value))
    check "reading D1"

    document.Close False
    check "Close"
    word.Quit
    check "Word Quit"
End If

excel.CutCopyMode = False
book.Close False
check "Workbook Close"
excel.Quit
check "Excel Quit"
mark "done"
