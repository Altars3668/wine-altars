' What WMI returns for a class path: the class or an instance, its genus, whether its properties carry values,
' for a class with instances and for classes without any.  Prints only.
' Usage: cscript //nologo classobject.vbs
Option Explicit
Dim svc, obj, p, line, n

' an expression on obj, or the error it raises
Function value(expression)
    Dim v
    On Error Resume Next
    Err.Clear
    v = Eval(expression)
    If Err.Number <> 0 Then
        value = "error " & Hex(Err.Number)
    ElseIf IsNull(v) Then
        value = "null"
    Else
        value = CStr(v)
    End If
End Function

Sub show(namespace, cls, prop)
    On Error Resume Next
    Err.Clear
    Set svc = GetObject("winmgmts:{impersonationLevel=impersonate}!\\.\" & namespace)
    Set obj = svc.Get(cls)
    If Err.Number <> 0 Then
        WScript.Echo namespace & ":" & cls & ": Get " & Hex(Err.Number) & " " & Err.Description
        Exit Sub
    End If
    WScript.Echo namespace & ":" & cls & ": IsClass " & value("obj.Path_.IsClass") & _
        ", __GENUS " & value("obj.SystemProperties_(""__GENUS"").Value") & _
        ", __RELPATH " & value("obj.SystemProperties_(""__RELPATH"").Value") & _
        ", __PATH " & value("obj.SystemProperties_(""__PATH"").Value")
    n = 0
    For Each p In obj.Properties_
        n = n + 1
    Next
    WScript.Echo "  " & n & " properties; " & prop & " " & value("obj.Properties_(""" & prop & """).Value")
    Err.Clear
    Set obj = GetObject("winmgmts:{impersonationLevel=impersonate}!\\.\" & namespace & ":" & cls)
    If Err.Number <> 0 Then
        WScript.Echo "  through the moniker: " & Hex(Err.Number)
    Else
        WScript.Echo "  through the moniker: IsClass " & value("obj.Path_.IsClass") & ", __GENUS " & _
            value("obj.SystemProperties_(""__GENUS"").Value")
    End If
End Sub

show "root\cimv2", "Win32_BIOS", "Manufacturer"
show "root\cimv2", "Win32_Process", "Name"
show "root\cimv2", "StdRegProv", "__CLASS"
show "root\default", "StdRegProv", "__CLASS"
show "root\SecurityCenter2", "AntiSpywareProduct", "displayName"
show "root\SecurityCenter2", "FirewallProduct", "displayName"
show "root\SecurityCenter2", "AntiVirusProduct", "displayName"
show "root\cimv2", "Win32_NoSuchClass", "Name"
