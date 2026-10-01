' What Windows Security Center tells WMI: every instance of the product classes in root\SecurityCenter2, with
' all their properties, and whether the old root\SecurityCenter of Windows XP still answers.  Prints only.
' Usage: cscript //nologo securitycenter.vbs
Option Explicit
Dim ns, cls, svc, items, item, prop, line

Sub show(namespace, classes)
    Dim c
    On Error Resume Next
    Set svc = GetObject("winmgmts:{impersonationLevel=impersonate}!\\.\" & namespace)
    If Err.Number <> 0 Then
        WScript.Echo namespace & ": " & Hex(Err.Number) & " " & Err.Description
        Err.Clear
        Exit Sub
    End If
    For Each c In classes
        Set items = svc.ExecQuery("SELECT * FROM " & c)
        WScript.Echo namespace & " " & c & ": " & items.Count & " instances"
        If Err.Number <> 0 Then
            WScript.Echo "  " & Hex(Err.Number) & " " & Err.Description
            Err.Clear
        Else
            For Each item In items
                For Each prop In item.Properties_
                    If IsNull(prop.Value) Then
                        line = "(null)"
                    ElseIf prop.Name = "productState" Then
                        line = prop.Value & " (0x" & Hex(prop.Value) & ")"
                    Else
                        line = """" & prop.Value & """"
                    End If
                    WScript.Echo "  " & prop.Name & " = " & line
                Next
                WScript.Echo "  --"
            Next
        End If
    Next
End Sub

show "root\SecurityCenter2", Array("AntiVirusProduct", "AntiSpywareProduct", "FirewallProduct")
show "root\SecurityCenter", Array("AntiVirusProduct", "FirewallProduct")
