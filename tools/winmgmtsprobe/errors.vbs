' What VBScript's Err says when a WbemScripting call fails -- number, description and source -- and what
' SWbemLastError holds then.  Non-ASCII text is printed as \uXXXX.  Prints only.
' Usage: cscript //nologo errors.vbs
Option Explicit
Dim svc, p, o, privs, e

Function escaped(text)
    Dim i, c, out
    out = ""
    For i = 1 To Len(text)
        c = AscW(Mid(text, i, 1))
        If c < 0 Then c = c + 65536
        If c >= 32 And c < 127 Then
            out = out & Mid(text, i, 1)
        ElseIf c = 13 Then
            out = out & "\r"
        ElseIf c = 10 Then
            out = out & "\n"
        Else
            out = out & "\u" & Right("000" & LCase(Hex(c)), 4)
        End If
    Next
    escaped = out
End Function

Sub attempt(statement)
    Dim last
    On Error Resume Next
    Err.Clear
    Execute statement
    WScript.Echo statement & ": " & Hex(Err.Number) & " [" & escaped(Err.Description) & "] source [" & _
        escaped(Err.Source) & "]"
    Err.Clear
    Set last = CreateObject("WbemScripting.SWbemLastError")
    If Err.Number <> 0 Then
        WScript.Echo "  SWbemLastError: " & Hex(Err.Number)
    Else
        WScript.Echo "  SWbemLastError: Operation " & last.Properties_("Operation").Value & ", ParameterInfo " & _
            last.Properties_("ParameterInfo").Value & ", ProviderName " & last.Properties_("ProviderName").Value
    End If
    On Error GoTo 0
End Sub

Set svc = GetObject("winmgmts:{impersonationLevel=impersonate}!\\.\root\cimv2")
attempt "Set o = svc.Get(""Win32_NoSuchClass"")"
attempt "Set o = svc.Get(""Win32_LogicalDisk.DeviceID=""""nonesuch"""""")"
attempt "For Each o In svc.ExecQuery(""SELEC * FROM Win32_LogicalDisk""): Next"
attempt "For Each o In svc.ExecQuery(""SELECT * FROM Win32_NoSuchClass""): Next"
attempt "Set o = GetObject(""winmgmts:\\.\root\nonesuch"")"
Set p = CreateObject("WbemScripting.SWbemObjectPath")
attempt "p.Path = ""Class.A = 5"""
attempt "x = p.Security_.ImpersonationLevel"
Set privs = p.Security_.Privileges
attempt "privs.Add 0"
attempt "Set o = privs.Item(20)"
Set o = svc.Get("Win32_LogicalDisk.DeviceID=""C:""")
attempt "o.Path_.Class = ""Other"""
attempt "x = o.Properties_(""NoSuchProperty"").Value"
