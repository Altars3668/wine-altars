' wscAPI.WSCProductList through IDispatch, the way a script uses it: what the type information calls the
' objects, and every property of every product for the firewall, antivirus and antispyware lists.  Prints only.
' Usage: cscript //nologo wscscript.vbs
Option Explicit
Dim list, product, provider, i, n

Sub show(name)
    Dim value
    Err.Clear
    value = Eval("product." & name)
    If Err.Number <> 0 Then
        WScript.Echo "    " & name & ": " & Hex(Err.Number) & " " & Err.Description
    Else
        WScript.Echo "    " & name & " = " & value
    End If
End Sub

On Error Resume Next
For Each provider In Array(1, 4, 8)
    Err.Clear
    Set list = CreateObject("wscAPI.WSCProductList")
    If Err.Number <> 0 Then
        WScript.Echo "CreateObject: " & Hex(Err.Number) & " " & Err.Description
        WScript.Quit 1
    End If
    WScript.Echo "provider " & provider & ": TypeName " & TypeName(list)
    n = list.Count
    WScript.Echo "  Count before Initialize: " & Hex(Err.Number) & " " & Err.Description
    Err.Clear
    list.Initialize provider
    WScript.Echo "  Initialize " & Hex(Err.Number)
    Err.Clear
    n = list.Count
    WScript.Echo "  Count " & n & " (" & Hex(Err.Number) & ")"
    For i = 0 To n - 1
        Err.Clear
        Set product = list.Item(i)
        WScript.Echo "  Item(" & i & ") " & Hex(Err.Number) & ", TypeName " & TypeName(product)
        show "ProductName"
        show "ProductState"
        show "SignatureStatus"
        show "RemediationPath"
        show "ProductStateTimestamp"
        show "ProductGuid"
        show "ProductIsDefault"
        show "AntivirusScanSubstatus"
        show "AntivirusSettingsSubstatus"
        show "AntivirusProtectionUpdateSubstatus"
        show "FirewallDomainProfileSubstatus"
        show "FirewallPrivateProfileSubstatus"
        show "FirewallPublicProfileSubstatus"
        show "AntivirusDaysUntilExpired"
        Err.Clear
        Set product = list(i)
        WScript.Echo "  list(" & i & ") as the default member: " & Hex(Err.Number)
    Next
Next
