' What a WbemScripting.SWbemObjectPath of its own answers: every property after Path is set to each of a set of
' made-up paths, the type of each key's value, and what each setter does to the rest.  Prints only.
' Usage: cscript //nologo pathobject.vbs
Option Explicit
Dim p

' an expression on p, or the error it raises
Function value(expression)
    Dim v
    On Error Resume Next
    Err.Clear
    v = Eval(expression)
    If Err.Number <> 0 Then
        value = "error " & Hex(Err.Number)
    ElseIf IsNull(v) Then
        value = "null"
    ElseIf IsEmpty(v) Then
        value = "empty"
    ElseIf IsObject(v) Then
        value = "object"
    ElseIf IsArray(v) Then
        value = "array"
    Else
        value = "[" & CStr(v) & "] " & TypeName(v)
    End If
End Function

Sub show(what)
    Dim name, k, count
    WScript.Echo what
    For Each name In Array("Path", "RelPath", "Server", "Namespace", "ParentNamespace", "Class", "IsClass", _
                           "IsSingleton", "DisplayName", "Locale", "Authority", "Security_.ImpersonationLevel", _
                           "Security_.AuthenticationLevel")
        WScript.Echo "  " & name & " = " & value("p." & name)
    Next
    WScript.Echo "  Keys.Count = " & value("p.Keys.Count")
    On Error Resume Next
    Err.Clear
    For Each k In p.Keys
        WScript.Echo "    key [" & k.Name & "] = [" & CStr(k.Value) & "] " & TypeName(k.Value)
    Next
    If Err.Number <> 0 Then WScript.Echo "    keys: error " & Hex(Err.Number)
    On Error GoTo 0
End Sub

' run a statement on p, saying what it raised
Sub act(statement)
    On Error Resume Next
    Err.Clear
    Execute statement
    If Err.Number <> 0 Then
        WScript.Echo statement & ": error " & Hex(Err.Number)
    Else
        WScript.Echo statement & ": ok"
    End If
End Sub

Sub with_path(text)
    act "p.Path = """ & Replace(text, """", """""") & """"
    show "Path = " & text
End Sub

Set p = CreateObject("WbemScripting.SWbemObjectPath")
show "new"
with_path "\\srv\root\cimv2:Win32_Process.Handle=""4"""
with_path "Win32_LogicalDisk.DeviceID=""C:"""
with_path "Class.A=5,B=""b"",C=-1,D=4294967296,E=TRUE"
with_path "Class.A=""x,y"",B=""q\""uote"""
with_path "Win32_OperatingSystem=@"
with_path "Win32_LogicalDisk=""C:"""
with_path "root\cimv2:Class"
with_path "\\.\root:Class"
with_path "Class"
with_path "\\srv\root\cimv2"
with_path "Class.A = 5"
with_path ""

with_path "\\srv\root\cimv2:Class.K=1"
act "p.Class = ""Other"""
show "after Class = Other"
act "p.Server = ""srv2"""
show "after Server = srv2"
act "p.Namespace = ""root\default"""
show "after Namespace = root\default"
act "p.RelPath = ""Third.K=""""v"""""""
show "after RelPath = Third.K=""v"""
act "p.Keys.Add ""N"", 5"
show "after Keys.Add N, 5"
act "p.Keys.Add ""S"", ""s,t"""
show "after Keys.Add S, s,t"
act "p.Keys.Remove ""K"""
show "after Keys.Remove K"
act "p.SetAsSingleton"
show "after SetAsSingleton"
act "p.SetAsClass"
show "after SetAsClass"
act "p.Locale = ""ms_409"""
act "p.Authority = ""kerberos:srv"""
show "after Locale and Authority"
act "p.DisplayName = ""winmgmts:{impersonationLevel=impersonate}!\\srv\root\cimv2:Win32_X.K=1"""
show "after DisplayName = winmgmts:{impersonationLevel=impersonate}!\\srv\root\cimv2:Win32_X.K=1"
act "p.DisplayName = ""winmgmts:root\cimv2:Win32_Y"""
show "after DisplayName = winmgmts:root\cimv2:Win32_Y"
act "p.Security_.ImpersonationLevel = 3"
act "p.Security_.AuthenticationLevel = 6"
show "after Security_ impersonate, pktPrivacy"
