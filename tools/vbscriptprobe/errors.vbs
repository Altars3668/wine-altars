' vbserrors.vbs <output>: the description VBScript gives each error number it knows
Option Explicit
Dim fso, out, n, unknown
Set fso = CreateObject("Scripting.FileSystemObject")
Set out = fso.CreateTextFile(WScript.Arguments(0), True, True)
On Error Resume Next
Err.Raise 65000
unknown = Err.Description
Err.Clear
For n = 1 To 65535
    Err.Raise n
    If Err.Description <> unknown Then out.WriteLine n & vbTab & Err.Description
    Err.Clear
Next
out.WriteLine "unknown" & vbTab & unknown
out.Close
