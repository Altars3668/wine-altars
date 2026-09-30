' powerpoint-chartexcel.vbs: which Excel processes a chart PowerPoint inserts starts, and when they go.
'
' Under Wine, PowerPoint starts "EXCEL.EXE /Automation -Embedding /K" itself and asks COM for the
' ChartDataSourceFactory class 14 ms later; the class is not registered yet, so COM starts the class's
' LocalServer32, "EXCEL.EXE /automation -Embedding", a second Excel that registers only
' Excel.Application and stays after PowerPoint quits.  This prints the Office processes, with their
' arguments, after AddChart2, after the chart's workbook is closed, after Quit, and every 10 seconds
' for two minutes.  It measures nothing if Office is already running, and at the end ends only the
' Excel and PowerPoint processes started for automation (-Embedding) that are still there.
Option Explicit
Dim wmi, app, pres, slide, shape, t0, i, p, before
Set wmi = GetObject("winmgmts:\\.\root\cimv2")

Function office_procs()
    Dim q, s, cmd
    s = ""
    For Each q In wmi.ExecQuery("select ProcessId, Name, CommandLine from Win32_Process where Name = 'EXCEL.EXE' or Name = 'POWERPNT.EXE'")
        cmd = "" & q.CommandLine
        If InStr(UCase(cmd), ".EXE") > 0 Then cmd = Mid(cmd, InStr(UCase(cmd), ".EXE") + 4)
        s = s & "    " & q.ProcessId & " " & q.Name & cmd & vbCrLf
    Next
    office_procs = s
End Function

Sub show(what)
    WScript.Echo what & " (" & FormatNumber(Timer - t0, 1) & " s):" & vbCrLf & office_procs()
End Sub

before = office_procs()
If before <> "" Then
    WScript.Echo "Office is running, nothing measured:" & vbCrLf & before
    WScript.Quit 1
End If
t0 = Timer
Set app = CreateObject("PowerPoint.Application")
app.Visible = True
Set pres = app.Presentations.Add(True)
Set slide = pres.Slides.Add(1, 12)
Set shape = slide.Shapes.AddChart2(-1, 51, 20, 20, 400, 300)
show "after AddChart2"
shape.Chart.ChartData.Workbook.Close
show "after closing the chart's workbook"
Set shape = Nothing
Set slide = Nothing
pres.Close
Set pres = Nothing
app.Quit
Set app = Nothing
show "after Quit"
For i = 1 To 12
    WScript.Sleep 10000
    show "later"
    If office_procs() = "" Then Exit For
Next
For Each p In wmi.ExecQuery("select ProcessId, Name, CommandLine from Win32_Process where Name = 'EXCEL.EXE' or Name = 'POWERPNT.EXE'")
    If InStr(LCase("" & p.CommandLine), "-embedding") > 0 Then
        WScript.Echo "ending " & p.ProcessId & " " & p.Name
        p.Terminate
    End If
Next
