Option Explicit
' Fills a sheet and draws on it what Excel draws through Direct2D and
' DirectWrite -- charts of three kinds, one of them 3-D and one with a shadow,
' data bars, a colour scale, icon sets and sparklines -- saves it, holds it on
' screen so it can be captured, then quits:
'
'   excel-charts.vbs <output.xlsx> <progress log> [seconds]
'
' Every step is applied on its own and its result logged, so one Excel
' refuses does not stop the rest.
Dim fso, progress, destination, seconds, app, book, sheet, chart, i
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
Set app = CreateObject("Excel.Application")
If Err.Number <> 0 Then
    mark "activation failed: " & Hex(Err.Number)
    WScript.Quit 3
End If
On Error GoTo 0
mark "activated"
app.Visible = True
Set book = app.Workbooks.Add
Set sheet = book.Worksheets(1)
mark "workbook created"

On Error Resume Next
sheet.Range("A1").Value = "Month"
sheet.Range("B1").Value = "Sales"
sheet.Range("C1").Value = "Cost"
For i = 1 To 6
    sheet.Cells(i + 1, 1).Value = "M" & i
    sheet.Cells(i + 1, 2).Value = 10 + i * 7 - (i Mod 3) * 5
    sheet.Cells(i + 1, 3).Value = 8 + i * 4
Next
result "data"

Set chart = sheet.Shapes.AddChart2(201, 51, 250, 10, 320, 200)
chart.Chart.SetSourceData sheet.Range("A1:C7")
result "column chart"
chart.Chart.ChartArea.Format.Shadow.Visible = -1
result "chart shadow"

Set chart = sheet.Shapes.AddChart2(227, 4, 250, 220, 320, 200)
chart.Chart.SetSourceData sheet.Range("A1:C7")
result "line chart"

Set chart = sheet.Shapes.AddChart2(262, -4102, 580, 10, 300, 200)
chart.Chart.SetSourceData sheet.Range("A1:B7")
result "3-D pie chart"

sheet.Range("B2:B7").FormatConditions.AddDatabar
result "data bars"
sheet.Range("C2:C7").FormatConditions.AddColorScale 3
result "colour scale"
sheet.Range("D2:D7").Formula = "=B2-C2"
sheet.Range("D2:D7").FormatConditions.AddIconSetCondition
result "icon set"
sheet.Range("E2").SparklineGroups.Add 1, "B2:B7"
sheet.Range("E3").SparklineGroups.Add 2, "C2:C7"
result "sparklines"
On Error GoTo 0

app.DisplayAlerts = False
book.SaveAs destination, 51
mark "saved"
mark "showing"
WScript.Sleep seconds * 1000

On Error Resume Next
book.Close False
result "workbook closed"
app.Quit
result "application quit"
mark "closed"
