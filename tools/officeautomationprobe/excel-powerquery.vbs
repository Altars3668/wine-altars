' Power Query in Excel, which evaluates its M formulas in .NET Framework container processes
' (Microsoft.Mashup.Container.NetFX45.exe): a query made from an M formula is loaded into a table through the
' Mashup OLE DB provider and refreshed synchronously, and the cells it wrote are read back.  Also lists the COM
' add-ins with whether each one is connected, which says whether a VSTO add-in such as OfficePLUS loaded.
' Each step is logged with its result or its error; nothing is saved, printed, mailed or sent to a cloud service.
' Usage: cscript excel-powerquery.vbs <progress log>
Option Explicit
Dim fso, progress, app, book, sheet, query, table, addin, found, r, c
If WScript.Arguments.Count < 1 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
progress = WScript.Arguments(0)
Sub mark(text)
    Dim stream
    Set stream = fso.OpenTextFile(progress, 8, True, -1)
    stream.WriteLine text
    stream.Close
End Sub
' logs the step with what it found, or its error
Sub result(what, found)
    If Err.Number <> 0 Then
        mark "FAIL " & what & ": " & Hex(Err.Number) & " " & Err.Description
        Err.Clear
    Else
        mark "ok   " & what & ": " & found
    End If
End Sub

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

found = ""
For Each addin In app.COMAddIns
    found = found & addin.ProgId & "=" & CInt(addin.Connect) & " "
Next
result "COMAddIns", found

Set book = app.Workbooks.Add()
Set sheet = book.Worksheets(1)
result "Workbooks.Add", book.Name

Set query = book.Queries.Add("WineAltarsQuery", _
    "let Source = #table({""a"", ""b""}, {{1, 2}, {3, 4}}), " & _
    "Added = Table.AddColumn(Source, ""c"", each [a] * 10 + [b]) in Added")
result "Queries.Add", query.Name

' xlSrcExternal, xlYes
Set table = sheet.ListObjects.Add(0, "OLEDB;Provider=Microsoft.Mashup.OleDb.1;Data Source=$Workbook$;" & _
    "Location=WineAltarsQuery;Extended Properties=""""", , 1, sheet.Range("$A$1"))
result "ListObjects.Add", table.Name
' xlCmdSql
table.QueryTable.CommandType = 2
table.QueryTable.CommandText = Array("SELECT * FROM [WineAltarsQuery]")
table.QueryTable.BackgroundQuery = False
result "QueryTable set up", table.QueryTable.CommandType
table.QueryTable.Refresh False
result "Refresh", table.QueryTable.ResultRange.Address

found = ""
For r = 1 To 3
    For c = 1 To 3
        found = found & sheet.Cells(r, c).Value & IIf(c < 3, ",", "")
    Next
    found = found & IIf(r < 3, " | ", "")
Next
' expected: a,b,c | 1,2,12 | 3,4,34
result "cells A1:C3", found

Err.Clear
book.Close False
result "Close", ""
app.Quit
result "Quit", ""
mark "done"

Function IIf(test, yes, no)
    If test Then IIf = yes Else IIf = no
End Function
