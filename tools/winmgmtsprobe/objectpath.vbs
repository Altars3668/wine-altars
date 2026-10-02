' What SWbemObject.Path_ and SystemProperties_ give for an instance and for a class, and what Get gives for paths
' with and without keys.  The host name is printed as <host>.  Prints only.
' Usage: cscript //nologo objectpath.vbs
Option Explicit
Dim host, svc
host = CreateObject("WScript.Network").ComputerName

Function scrub(v)
    scrub = Replace(Replace(v, host, "<host>"), LCase(host), "<host>")
End Function

' an expression on obj, or the error it raises
Function value(obj, expression)
    Dim v
    On Error Resume Next
    Err.Clear
    v = Eval(expression)
    If Err.Number <> 0 Then
        value = "error " & Hex(Err.Number)
    ElseIf IsNull(v) Then
        value = "null"
    ElseIf IsObject(v) Then
        value = "object"
    ElseIf IsArray(v) Then
        value = "array"
    Else
        value = scrub(CStr(v))
    End If
End Function

Sub show(what, obj)
    Dim p, names, k
    WScript.Echo what
    For Each p In Array("Path", "RelPath", "Server", "Namespace", "ParentNamespace", "Class", "IsClass", _
                        "IsSingleton", "DisplayName", "Locale", "Authority")
        WScript.Echo "  Path_." & p & " = " & value(obj, "obj.Path_." & p)
    Next
    WScript.Echo "  Path_.Keys.Count = " & value(obj, "obj.Path_.Keys.Count")
    On Error Resume Next
    For Each k In obj.Path_.Keys
        WScript.Echo "    key " & k.Name & " = " & scrub(CStr(k.Value))
    Next
    Err.Clear
    names = ""
    For Each p In obj.SystemProperties_
        names = names & " " & p.Name
    Next
    If Err.Number <> 0 Then names = names & " error " & Hex(Err.Number)
    On Error GoTo 0
    WScript.Echo "  SystemProperties_.Count = " & value(obj, "obj.SystemProperties_.Count") & ":" & names
    For Each p In Array("__GENUS", "__CLASS", "__SUPERCLASS", "__DYNASTY", "__RELPATH", "__PROPERTY_COUNT", _
                        "__DERIVATION", "__SERVER", "__NAMESPACE", "__PATH")
        WScript.Echo "  " & p & " = " & value(obj, "obj.SystemProperties_(""" & p & """).Value")
    Next
    WScript.Echo "  Properties_.Count = " & value(obj, "obj.Properties_.Count") & _
        ", Methods_.Count = " & value(obj, "obj.Methods_.Count")
End Sub

Sub get_path(path)
    Dim obj
    On Error Resume Next
    Err.Clear
    Set obj = svc.Get(path)
    If Err.Number <> 0 Then
        WScript.Echo "Get(" & path & "): error " & Hex(Err.Number)
        Exit Sub
    End If
    On Error GoTo 0
    show "Get(" & path & ")", obj
End Sub

Set svc = GetObject("winmgmts:{impersonationLevel=impersonate}!\\.\root\cimv2")
get_path "Win32_OperatingSystem"
get_path "Win32_Process"
get_path "Win32_Process.Handle=""4"""
get_path "Win32_LogicalDisk.DeviceID=""C:"""
get_path "Win32_ComputerSystem"
Dim inst
For Each inst In svc.InstancesOf("Win32_OperatingSystem")
    show "InstancesOf(Win32_OperatingSystem)", inst
    Exit For
Next
For Each inst In svc.ExecQuery("SELECT Name, Version FROM Win32_OperatingSystem")
    show "SELECT Name, Version FROM Win32_OperatingSystem", inst
    Exit For
Next
