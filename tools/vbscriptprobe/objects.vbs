Option Explicit
Dim fso, out, r, d, f, rx, n
Set fso = CreateObject("Scripting.FileSystemObject")
Set out = fso.CreateTextFile(WScript.Arguments(0), True, True)
Set d = CreateObject("Scripting.Dictionary")
Set f = fso.GetFolder(".")
Set rx = New RegExp
Sub show(what)
    If Err.Number = 0 Then
        out.WriteLine what & vbTab & "ok" & vbTab & TypeName(r) & vbTab & r
    Else
        out.WriteLine what & vbTab & Err.Number
    End If
    Err.Clear
End Sub
On Error Resume Next
r = Empty : r = IsObject(d) : show "IsObject(d)"
r = Empty : r = TypeName(d) : show "TypeName(d)"
r = Empty : r = VarType(d) : show "VarType(d)"
r = Empty : r = IsEmpty(d) : show "IsEmpty(d)"
r = Empty : r = IsNull(d) : show "IsNull(d)"
r = Empty : r = IsArray(d) : show "IsArray(d)"
r = Empty : r = IsNumeric(d) : show "IsNumeric(d)"
r = Empty : r = IsDate(d) : show "IsDate(d)"
r = Empty : r = VarType(rx) : show "VarType(rx)"
r = Empty : r = TypeName(rx) : show "TypeName(rx)"
r = Empty : r = IsNumeric(rx) : show "IsNumeric(rx)"
r = Empty : r = VarType(f) : show "VarType(f)"
r = Empty : r = TypeName(f) : show "TypeName(f)"
r = Empty : r = IsNumeric(f) : show "IsNumeric(f)"
r = Empty : r = UBound(d) : show "UBound(d)"
r = Empty : r = Join(d) : show "Join(d)"
r = Empty : r = Filter(d, "a") : show "Filter(d, ""a"")"
r = Empty : r = InStr(d, "a") : show "InStr(d, ""a"")"
r = Empty : r = Replace(d, "a", "b") : show "Replace(d, ""a"", ""b"")"
r = Empty : r = Split(d) : show "Split(d)"
r = Empty : r = Eval(d) : show "Eval(d)"
r = Empty : r = Mid(d, 1) : show "Mid(d, 1)"
r = Empty : r = Left(d, 1) : show "Left(d, 1)"
r = Empty : r = Asc(d) : show "Asc(d)"
r = Empty : r = Chr(d) : show "Chr(d)"
r = Empty : r = Space(d) : show "Space(d)"
r = Empty : r = String(2, d) : show "String(2, d)"
r = Empty : r = Round(d) : show "Round(d)"
r = Empty : r = Int(d) : show "Int(d)"
r = Empty : r = Sgn(d) : show "Sgn(d)"
r = Empty : r = Sqr(d) : show "Sqr(d)"
r = Empty : r = DateAdd("d", 1, d) : show "DateAdd(""d"", 1, d)"
r = Empty : r = Year(d) : show "Year(d)"
r = Empty : r = FormatNumber(d) : show "FormatNumber(d)"
r = Empty : r = MonthName(d) : show "MonthName(d)"
r = Empty : r = RGB(d, 0, 0) : show "RGB(d, 0, 0)"
r = Empty : r = Array(d)(0) Is d : show "Array(d)(0) Is d"
r = Empty : r = ScriptEngineMajorVersion() : show "ScriptEngineMajorVersion()"
r = Empty : r = Escape(d) : show "Escape(d)"
r = Empty : r = CStr(Array(1)) : show "CStr(Array(1))"
r = Empty : r = Len(Array(1)) : show "Len(Array(1))"
r = Empty : r = IsNumeric(Array(1)) : show "IsNumeric(Array(1))"
r = Empty : r = VarType(Array(1)) : show "VarType(Array(1))"
out.Close
