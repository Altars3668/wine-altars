' A sweep over Excel features that lean on Windows components: formulas old and new (dynamic arrays, XLOOKUP, LET,
' LAMBDA), number formats, conditional formats, charts of the classic and the newer kinds, sparklines, a table, a
' pivot table, sorting and filtering, data validation, notes, hyperlinks, pictures in each format found in a
' directory, shapes and SmartArt, find and replace, text to columns, goal seek, saving in each format Excel writes, a
' password-encrypted save that is opened again, and protection.  Each step is logged with its result or its error,
' and one failing does not stop the others.  Nothing is printed, mailed or sent to a cloud service.
' Usage: cscript excel-sweep.vbs <output directory> <progress log> [picture directory]
Option Explicit
Dim fso, progress, outdir, pictures, app, book, sheet, data, rng, chart, shape, found, n, i, file, pivot, cache, table
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
Set app = CreateObject("Excel.Application")
If Err.Number <> 0 Then
    mark "activation failed: " & Hex(Err.Number)
    WScript.Quit 3
End If
app.DisplayAlerts = False
app.Visible = True
mark "activated, Excel " & app.Version & " build " & app.Build
Set book = app.Workbooks.Add()
Set sheet = book.Worksheets(1)
result "Workbooks.Add", book.Name

' data to work on
Dim regions
sheet.Range("A1:D1").Value = Array("Region", "Month", "Sales", "Cost")
regions = Array("North", "South", "East")
For i = 0 To 11
    sheet.Cells(i + 2, 1).Value = regions(i Mod 3)
    sheet.Cells(i + 2, 2).Value = DateSerial(2026, (i Mod 4) + 1, 1)
    sheet.Cells(i + 2, 3).Value = 100 + i * 13 - (i Mod 5) * 7
    sheet.Cells(i + 2, 4).Value = 60 + i * 5
Next
Set data = sheet.Range("A1:D13")
result "data", data.Address

' formulas, old and new
sheet.Range("F1").Formula = "=SUM(C2:C13)"
found = sheet.Range("F1").Value
result "SUM", found
sheet.Range("F2").Formula = "=PMT(0.05/12,360,-200000)"
found = Round(sheet.Range("F2").Value, 2)
result "PMT", found
sheet.Range("F3").Formula = "=TEXT(DATE(2026,9,28),""yyyy-mm-dd dddd"")"
found = sheet.Range("F3").Text
result "TEXT of a date", found
sheet.Range("H1").Formula2 = "=SORT(UNIQUE(A2:A13))"
found = sheet.Range("H1").Value & "," & sheet.Range("H2").Value & "," & sheet.Range("H3").Value & " spill " & _
    sheet.Range("H1").SpillingToRange.Address
result "dynamic array SORT(UNIQUE)", found
sheet.Range("I1").Formula2 = "=SEQUENCE(3,2,10,5)"
found = sheet.Range("I1").SpillingToRange.Address & " last " & sheet.Range("J3").Value
result "SEQUENCE", found
Err.Clear
sheet.Range("F4").Formula2 = "=XLOOKUP(""East"",A2:A13,C2:C13)"
found = sheet.Range("F4").Value
result "XLOOKUP", found
sheet.Range("F5").Formula2 = "=LET(x,SUM(C2:C13),y,SUM(D2:D13),x-y)"
found = sheet.Range("F5").Value
result "LET", found
sheet.Range("F6").Formula2 = "=LAMBDA(a,b,a*b+1)(6,7)"
found = sheet.Range("F6").Value
result "LAMBDA", found
sheet.Range("F7").Formula2 = "=TEXTJOIN(""-"",TRUE,FILTER(C2:C13,C2:C13>200))"
found = sheet.Range("F7").Value
result "TEXTJOIN(FILTER)", found
sheet.Range("F8").Formula = "=STDEV.S(C2:C13)"
found = Round(sheet.Range("F8").Value, 4)
result "STDEV.S", found
book.Names.Add "Twice", "=LAMBDA(x,x*2)"
sheet.Range("F9").Formula2 = "=Twice(21)"
found = sheet.Range("F9").Value
result "named LAMBDA", found

' number formats
sheet.Range("F10").Value = 1234567.891
sheet.Range("F10").NumberFormat = "#,##0.00"
found = sheet.Range("F10").Text
sheet.Range("F11").Value = 0.1234
sheet.Range("F11").NumberFormat = "0.0%"
found = found & " | " & sheet.Range("F11").Text
sheet.Range("F12").Value = DateSerial(2026, 9, 28)
' the Chinese date format by code points: cscript reads a script in the ANSI code page, where UTF-8 text is garbage
sheet.Range("F12").NumberFormat = "[$-804]yyyy""" & ChrW(&H5E74) & """m""" & ChrW(&H6708) & """d""" & ChrW(&H65E5) & """;@"
found = found & " | " & sheet.Range("F12").Text
result "number formats", found

' conditional formats
sheet.Range("C2:C13").FormatConditions.AddDatabar
sheet.Range("D2:D13").FormatConditions.AddColorScale 3
sheet.Range("C2:C13").FormatConditions.AddIconSetCondition
found = sheet.Range("C2:C13").FormatConditions.Count & " on C, " & sheet.Range("D2:D13").FormatConditions.Count & " on D"
result "conditional formats", found

' charts, the classic kinds and the newer ones; the newer kinds take their data from the selection, and Excel on
' Windows refuses SetSourceData on them too (445, "object doesn't support this action"), so they are only asked how
' many series they have
Dim kinds, names
kinds = Array(51, 4, 5, -4169, 119, 117, 120, 118, 121, 123)
names = Array("column", "line", "pie", "scatter", "waterfall", "treemap", "sunburst", "histogram", "box and whisker", _
    "funnel")
For i = 0 To UBound(kinds)
    found = ""
    Set shape = Nothing
    sheet.Range("B1:C13").Select
    Set shape = sheet.Shapes.AddChart2(-1, kinds(i), 400 + (i Mod 3) * 260, 10 + (i \ 3) * 180, 250, 170)
    found = shape.HasChart & " type " & shape.Chart.ChartType
    result "chart " & names(i), found
    If i < 4 Then
        shape.Chart.SetSourceData sheet.Range("B1:C13")
        found = shape.Chart.SeriesCollection.Count & " series"
        result "chart " & names(i) & " source", found
    Else
        found = shape.Chart.FullSeriesCollection.Count & " series"
        result "chart " & names(i) & " series", found
    End If
Next

' sparklines
sheet.Range("E2:E4").SparklineGroups.Add 1, "C2:D4"
found = sheet.Range("E2:E4").SparklineGroups.Count
result "sparklines", found

' a table, then a pivot table from it
Set table = sheet.ListObjects.Add(1, data, , 1)
table.TableStyle = "TableStyleMedium2"
table.ShowTotals = True
found = table.Name & " " & table.ListRows.Count & " rows, total " & table.TotalsRowRange.Cells(1, 4).Value
result "table", found
Dim pivotsheet
Set pivotsheet = book.Worksheets.Add
Set cache = book.PivotCaches.Create(1, table.Range)
Set pivot = cache.CreatePivotTable(pivotsheet.Range("A3"), "Pivot1")
pivot.PivotFields("Region").Orientation = 1
pivot.AddDataField pivot.PivotFields("Sales"), "Sum of Sales", -4157
pivot.RefreshTable
found = pivot.RowRange.Rows.Count & " rows, grand total " & pivot.GetPivotData("Sum of Sales").Value
result "pivot table", found

' sorting and filtering
sheet.Activate
table.Sort.SortFields.Clear
table.Sort.SortFields.Add table.ListColumns("Sales").Range, 0, 2
table.Sort.Apply
found = table.DataBodyRange.Cells(1, 3).Value
result "sort", found
table.Range.AutoFilter 1, "North"
found = table.Range.Columns(1).SpecialCells(12).Count
result "filter", found
table.Range.AutoFilter 1
Err.Clear

' data validation, notes, hyperlinks
With sheet.Range("L2").Validation
    .Delete
    .Add 3, 1, 1, "Red,Green,Blue"
End With
found = sheet.Range("L2").Validation.Formula1
result "data validation", found
sheet.Range("L3").AddComment "A note."
found = sheet.Comments.Count
result "note", found
sheet.Hyperlinks.Add sheet.Range("L4"), "https://example.com/", , , "example link"
found = sheet.Hyperlinks.Count
result "hyperlink", found

' each picture file
If pictures <> "" Then
    If fso.FolderExists(pictures) Then
        i = 0
        For Each file In fso.GetFolder(pictures).Files
            found = ""
            Set shape = sheet.Shapes.AddPicture(file.Path, False, True, 10 + i * 80, 600, -1, -1)
            found = Round(shape.Width, 2) & "x" & Round(shape.Height, 2) & " type " & shape.Type
            result "picture " & file.Name, found
            i = i + 1
        Next
    Else
        mark "no picture directory " & pictures
    End If
End If

' shapes
found = ""
Set shape = sheet.Shapes.AddShape(1, 10, 700, 120, 60)
shape.TextFrame2.TextRange.Text = ChrW(&H5F62) & ChrW(&H72B6) & " shape"
found = shape.TextFrame2.TextRange.Text
result "shape with text", found
found = ""
Set shape = sheet.Shapes.AddSmartArt(app.SmartArtLayouts(1), 150, 700, 300, 200)
found = shape.HasSmartArt & " " & shape.SmartArt.AllNodes.Count & " nodes"
result "SmartArt", found

' find and replace, text to columns, goal seek
Set rng = sheet.Range("A1:A13").Find("South")
found = rng.Address
result "find", found
found = sheet.Range("A1:A13").Replace("South", "Sud")
found = found & " " & Application_count(sheet.Range("A1:A13"), "Sud")
result "replace", found
sheet.Range("N1").Value = "alpha,beta,gamma"
sheet.Range("N1").TextToColumns sheet.Range("N1"), 1, , , False, False, True, False
found = sheet.Range("N1").Value & "|" & sheet.Range("O1").Value & "|" & sheet.Range("P1").Value
result "text to columns", found
sheet.Range("N3").Value = 5
sheet.Range("N4").Formula = "=N3*N3+2*N3"
found = sheet.Range("N4").GoalSeek(99, sheet.Range("N3"))
found = found & " " & Round(sheet.Range("N3").Value, 4)
result "goal seek", found
app.CalculateFull
result "calculate full", ""

' every format Excel writes by itself
Dim formats, fnames, k
formats = Array(51, 52, 50, 56, 6, 62, -4158, 46, 60, 44, 45, 54)
fnames = Array("sweep.xlsx", "sweep.xlsm", "sweep.xlsb", "sweep.xls", "sweep.csv", "sweep-utf8.csv", "sweep.txt", _
    "sweep.xml", "sweep.ods", "sweep.htm", "sweep.mht", "sweep.xltx")
For k = 0 To UBound(formats)
    book.SaveAs outdir & "\" & fnames(k), formats(k)
    result "save " & fnames(k), size_of(outdir & "\" & fnames(k)) & " bytes"
Next
book.ExportAsFixedFormat 0, outdir & "\sweep.pdf"
result "export sweep.pdf", size_of(outdir & "\sweep.pdf") & " bytes"
book.ExportAsFixedFormat 1, outdir & "\sweep.xps"
result "export sweep.xps", size_of(outdir & "\sweep.xps") & " bytes"

' encrypted with a password, then opened with it
Err.Clear
book.SaveAs outdir & "\sweep-password.xlsx", 51, "Sweep-2026"
result "save with a password", size_of(outdir & "\sweep-password.xlsx") & " bytes"
book.Close False
Dim again
Err.Clear
Set again = Nothing
Set again = app.Workbooks.Open(outdir & "\sweep-password.xlsx", , True, , "Sweep-2026")
found = again.Worksheets.Count & " sheets"
result "open with the password", found
If Not again Is Nothing Then again.Close False
Err.Clear
Set again = Nothing
Set again = app.Workbooks.Open(outdir & "\sweep-password.xlsx", , True, , "wrong")
' 1004 is the refusal; 462 would be Excel gone
If Err.Number = 1004 Then
    mark "ok   open with a wrong password refused: " & Hex(Err.Number)
    Err.Clear
ElseIf Err.Number <> 0 Then
    mark "FAIL open with a wrong password: " & Hex(Err.Number) & " " & Err.Description
    Err.Clear
Else
    mark "FAIL open with a wrong password succeeded"
    again.Close False
End If

' protection
Err.Clear
Set book = Nothing
Set book = app.Workbooks.Open(outdir & "\sweep.xlsx")
result "open sweep.xlsx", book.Worksheets.Count & " sheets"
book.Worksheets(1).Protect "Sweep-2026"
found = book.Worksheets(1).ProtectContents
result "protect sheet", found
book.Worksheets(1).Unprotect "Sweep-2026"
found = book.Worksheets(1).ProtectContents
result "unprotect sheet", found
book.Protect "Sweep-2026", True
found = book.ProtectStructure
result "protect workbook", found
book.Unprotect "Sweep-2026"
book.Close False

Err.Clear
app.Quit
result "Quit", ""
mark "done"

Function Application_count(r, what)
    Dim c, cell
    c = 0
    For Each cell In r.Cells
        If cell.Value = what Then c = c + 1
    Next
    Application_count = c
End Function
