' What SWbemPrivilegeSet and SWbemPrivilege do: adding by identifier and by name, their order, what each privilege
' says of itself, removing and replacing them; and what scripting's ExecQuery gives for a query that leaves the keys
' out, by its flags.  Prints only; the host name is printed as <host>.
' Usage: cscript //nologo privileges.vbs
Option Explicit
Dim host, p, privs, pr, svc, o, n, flags

host = CreateObject("WScript.Network").ComputerName

Function scrub(v)
    scrub = Replace(Replace(v, host, "<host>"), LCase(host), "<host>")
End Function

Function value(expression)
    Dim x
    On Error Resume Next
    Err.Clear
    x = Eval(expression)
    If Err.Number <> 0 Then
        value = "error " & Hex(Err.Number)
    ElseIf IsNull(x) Then
        value = "null"
    ElseIf IsEmpty(x) Then
        value = "empty"
    ElseIf IsObject(x) Then
        value = "object"
    Else
        value = "[" & scrub(CStr(x)) & "] " & TypeName(x)
    End If
End Function

Sub act(statement)
    On Error Resume Next
    Err.Clear
    Execute statement
    If Err.Number <> 0 Then
        WScript.Echo "  " & statement & ": error " & Hex(Err.Number)
    Else
        WScript.Echo "  " & statement & ": ok"
    End If
End Sub

Sub list(what)
    Dim item, line
    On Error Resume Next
    Err.Clear
    line = "  " & what & ": Count " & value("privs.Count") & ":"
    For Each item In privs
        line = line & " " & item.Identifier & " " & item.Name & " " & item.IsEnabled & ";"
    Next
    If Err.Number <> 0 Then line = line & " error " & Hex(Err.Number)
    WScript.Echo line
End Sub

Set p = CreateObject("WbemScripting.SWbemObjectPath")
p.Path = "\\srv\root:C.K=1"
Set privs = p.Security_.Privileges
list "new"
Set pr = privs.Add(19)
WScript.Echo "  Add 19 gives " & TypeName(pr) & ": Identifier " & value("pr.Identifier") & ", Name " & _
    value("pr.Name") & ", IsEnabled " & value("pr.IsEnabled") & ", DisplayName " & value("pr.DisplayName")
act "privs.Add 7, False"
act "privs.AddAsString ""SeShutdownPrivilege"""
act "privs.AddAsString ""sebackupprivilege"", False"
act "privs.Add 19, False"
list "after Add 19, 7 False, Shutdown, backup False, 19 False"
WScript.Echo "  DisplayName " & value("p.DisplayName")
WScript.Echo "  Item(18).Name = " & value("privs.Item(18).Name") & ", privs(7).IsEnabled = " & value("privs(7).IsEnabled")
WScript.Echo "  Item(20) = " & value("privs.Item(20).Name")
act "privs.Add 0"
act "privs.Add 28"
act "privs.Add 27"
act "privs.AddAsString ""SeBogusPrivilege"""
act "privs.AddAsString ""Shutdown"""
act "privs(18).IsEnabled = False"
list "after Add 0, 28, 27, bogus, Shutdown; 18 disabled"
act "privs.Remove 7"
act "privs.Remove 3"
list "after Remove 7, 3"
act "privs.DeleteAll"
list "after DeleteAll"
WScript.Echo "  DisplayName " & value("p.DisplayName")
For Each n In Array(1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 20, 21, 22, 23, 24, 25, 26, 27)
    privs.Add n
Next
WScript.Echo "  DisplayName " & value("p.DisplayName")
list "all"

Set svc = GetObject("winmgmts:{impersonationLevel=impersonate}!\\.\root\cimv2")
WScript.Echo "services: Security_.Privileges.Count " & value("svc.Security_.Privileges.Count")
act "svc.Security_.Privileges.AddAsString ""SeDebugPrivilege"", True"
WScript.Echo "  Count " & value("svc.Security_.Privileges.Count")

WScript.Echo "ExecQuery of SELECT Name FROM Win32_Process WHERE Handle = 4"
For Each flags In Array(16, 0, 48, 256)
    For Each o In svc.ExecQuery("SELECT Name FROM Win32_Process WHERE Handle = 4", "WQL", flags)
        n = ""
        For Each pr In o.Properties_
            n = n & " " & pr.Name
        Next
        WScript.Echo "  flags " & flags & ": __PATH " & value("o.SystemProperties_(""__PATH"").Value") & _
            ", __PROPERTY_COUNT " & value("o.SystemProperties_(""__PROPERTY_COUNT"").Value") & ", Properties_:" & n
    Next
Next
For Each o In svc.InstancesOf("Win32_Process", 0)
    WScript.Echo "  InstancesOf flags 0: Properties_.Count " & value("o.Properties_.Count")
    Exit For
Next
