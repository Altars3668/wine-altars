' Whether VBA macros run: in Word, Excel and PowerPoint a module is added to a new document through the VBA project
' object model and its procedures are run with Application.Run -- a function, one that edits the document, one using
' a COM object and one handling an error, one calling a Win32 function through Declare PtrSafe -- and in Excel a
' user-defined function is used in a cell.  Office has AMSI scan the code first, so this also shows the scanner is
' reachable.  Access to the project object model is switched on for the run and put back as it was.
' Usage: cscript office-vba.vbs <progress log>
Option Explicit
Dim fso, shell, progress, apps, name, code, declared
If WScript.Arguments.Count <> 1 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
progress = WScript.Arguments(0)

Sub mark(text)
    Dim stream
    Set stream = fso.OpenTextFile(progress, 8, True)
    stream.WriteLine text
    stream.Close
End Sub

Sub result(what, value)
    If Err.Number <> 0 Then
        mark "FAIL " & what & ": " & Hex(Err.Number) & " " & Err.Description
        Err.Clear
    Else
        mark "ok   " & what & ": " & value
    End If
End Sub

code = "Function Twice(x)" & vbLf & "    Twice = x * 2" & vbLf & "End Function" & vbLf & _
    "Function Joined(a, b)" & vbLf & "    Joined = UCase(a) & ""-"" & Format(b, ""0.00"")" & vbLf & "End Function" & vbLf & _
    "Function Counted()" & vbLf & "    Dim d" & vbLf & "    Set d = CreateObject(""Scripting.Dictionary"")" & vbLf & _
    "    d.Add ""a"", 1: d.Add ""b"", 2" & vbLf & "    Counted = d.Count" & vbLf & "End Function" & vbLf & _
    "Function Caught()" & vbLf & "    On Error GoTo failed" & vbLf & "    Dim x" & vbLf & "    x = 1 / 0" & vbLf & _
    "    Caught = ""no error""" & vbLf & "    Exit Function" & vbLf & "failed:" & vbLf & _
    "    Caught = ""error "" & Err.Number" & vbLf & "End Function" & vbLf

declared = "Private Declare PtrSafe Function GetCurrentProcessId Lib ""kernel32"" () As Long" & vbLf & _
    "Function ProcessId()" & vbLf & "    ProcessId = GetCurrentProcessId()" & vbLf & "End Function" & vbLf

Function access_key(app)
    access_key = "HKCU\Software\Microsoft\Office\16.0\" & app & "\Security\AccessVBOM"
End Function

' switches access to the project object model on, returning what it was (Empty when unset)
Function allow_access(app)
    Dim before
    On Error Resume Next
    before = shell.RegRead(access_key(app))
    If Err.Number <> 0 Then before = Empty : Err.Clear
    shell.RegWrite access_key(app), 1, "REG_DWORD"
    allow_access = before
End Function

' waits up to 60 s for the application's process to go: an application writes its Trust Center settings back as it
' exits, after Quit has returned, and would put the setting back on again
Sub wait_for_exit(exe)
    Dim wmi, i
    On Error Resume Next
    Set wmi = GetObject("winmgmts:")
    For i = 1 To 120
        If wmi.ExecQuery("select * from Win32_Process where Name = '" & exe & "'").Count = 0 Then Exit Sub
        WScript.Sleep 500
    Next
    mark "FAIL " & exe & " still running after 60 s"
End Sub

Sub restore_access(app, exe, before)
    Dim now
    On Error Resume Next
    wait_for_exit exe
    If IsEmpty(before) Then shell.RegDelete access_key(app) Else shell.RegWrite access_key(app), before, "REG_DWORD"
    If Err.Number <> 0 Then mark "FAIL restoring " & access_key(app) & ": " & Hex(Err.Number) : Err.Clear
    now = shell.RegRead(access_key(app))
    If Err.Number <> 0 Then now = Empty : Err.Clear
    If IsEmpty(now) = IsEmpty(before) And (IsEmpty(now) Or now = before) Then
        mark "ok   " & app & " project access put back"
    Else
        mark "FAIL " & app & " project access is " & now & ", was " & before
    End If
End Sub

Sub add_module(project, what)
    On Error Resume Next
    Dim module
    Set module = project.VBComponents.Add(1)
    result what & " module added", module.Name
    ' a Declare belongs to the declarations section, before every procedure
    module.CodeModule.AddFromString declared & code
    result what & " code added", module.CodeModule.CountOfLines & " lines"
End Sub

Sub run_common(app, what)
    On Error Resume Next
    Dim v
    v = Empty : v = app.Run("Twice", 21) : result what & " Twice(21)", v
    v = Empty : v = app.Run("Joined", "abc", 3.14159) : result what & " Joined", v
    v = Empty : v = app.Run("Counted") : result what & " Counted", v
    v = Empty : v = app.Run("Caught") : result what & " Caught", v
    v = Empty : v = (app.Run("ProcessId") > 0) : result what & " ProcessId via Declare", v
End Sub

Sub word_macros()
    On Error Resume Next
    Dim word, doc, before
    before = allow_access("Word")
    Set word = CreateObject("Word.Application")
    result "Word started", word.Build
    word.DisplayAlerts = 0
    Set doc = word.Documents.Add
    result "Word document", doc.Name
    add_module doc.VBProject, "Word"
    run_common word, "Word"
    doc.VBProject.VBComponents(doc.VBProject.VBComponents.Count).CodeModule.AddFromString _
        "Sub Fill()" & vbLf & "    ActiveDocument.Content.Text = ""written by a macro""" & vbLf & "End Sub" & vbLf
    word.Run "Fill"
    result "Word macro edited the document", Left(doc.Content.Text, 18)
    doc.Close False
    word.Quit
    result "Word quit", ""
    ' an application only goes once nothing holds it any more
    Set doc = Nothing : Set word = Nothing
    restore_access "Word", "WINWORD.EXE", before
End Sub

Sub excel_macros()
    On Error Resume Next
    Dim excel, book, before
    before = allow_access("Excel")
    Set excel = CreateObject("Excel.Application")
    result "Excel started", excel.Build
    excel.DisplayAlerts = False
    Set book = excel.Workbooks.Add
    result "Excel workbook", book.Name
    add_module book.VBProject, "Excel"
    run_common excel, "Excel"
    book.Worksheets(1).Range("A1").Formula = "=Twice(21)+1"
    excel.Calculate
    result "Excel user-defined function in a cell", book.Worksheets(1).Range("A1").Value
    book.Close False
    excel.Quit
    result "Excel quit", ""
    Set book = Nothing : Set excel = Nothing
    restore_access "Excel", "EXCEL.EXE", before
End Sub

Sub powerpoint_macros()
    On Error Resume Next
    Dim ppt, pres, before
    before = allow_access("PowerPoint")
    Set ppt = CreateObject("PowerPoint.Application")
    result "PowerPoint started", ppt.Build
    Set pres = ppt.Presentations.Add(0)
    result "PowerPoint presentation", pres.Name
    add_module pres.VBProject, "PowerPoint"
    ' PowerPoint's Run wants the procedure qualified with its module, whose name is in the interface language
    Dim v, module
    module = pres.VBProject.VBComponents(pres.VBProject.VBComponents.Count).Name
    v = Empty : v = ppt.Run(module & ".Twice", 21) : result "PowerPoint Twice(21)", v
    v = Empty : v = ppt.Run(module & ".Counted") : result "PowerPoint Counted", v
    v = Empty : v = (ppt.Run(module & ".ProcessId") > 0) : result "PowerPoint ProcessId via Declare", v
    pres.Close
    ppt.Quit
    result "PowerPoint quit", ""
    Set pres = Nothing : Set ppt = Nothing
    restore_access "PowerPoint", "POWERPNT.EXE", before
End Sub

On Error Resume Next
mark "start"
apps = Array("word", "excel", "powerpoint")
For Each name In apps
    Select Case name
    Case "word": word_macros
    Case "excel": excel_macros
    Case "powerpoint": powerpoint_macros
    End Select
    If Err.Number <> 0 Then mark "FAIL " & name & ": " & Hex(Err.Number) & " " & Err.Description : Err.Clear
Next
mark "done"
