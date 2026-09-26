Option Explicit
Dim app, book, fso, progress, destination
If WScript.Arguments.Count <> 2 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
destination = WScript.Arguments(0)
progress = WScript.Arguments(1)
Sub mark(text)
    Dim stream
    Set stream = fso.OpenTextFile(progress, 8, True)
    stream.WriteLine text
    stream.Close
End Sub
mark "start"
On Error Resume Next
Set app = CreateObject("Excel.Application")
If Err.Number <> 0 Then
    mark "activation failed: " & Err.Number
    WScript.Quit 3
End If
On Error GoTo 0
mark "activated"
app.Visible = True
mark "visible"
Set book = app.Workbooks.Add()
mark "workbook created"
book.Worksheets(1).Range("A1").Value = "wine-altars Excel automation smoke"
mark "cell edited"
book.SaveAs destination, 51
mark "saved"
book.Close False
app.Quit
mark "closed"
WScript.Echo "saved local XLSX"
