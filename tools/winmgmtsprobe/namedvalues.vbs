' What a WbemScripting.SWbemNamedValueSet does with its values, what an object path's Keys do to the path, how
' DisplayName writes every setting at once, and what Path_ an object has when a query left its keys out (C:'s).
' Prints only; the host name is printed as <host>.
' Usage: cscript //nologo namedvalues.vbs
Option Explicit
Dim host, s, v, c, p, k, svc, o, n

host = CreateObject("WScript.Network").ComputerName

Function scrub(v)
    scrub = Replace(Replace(v, host, "<host>"), LCase(host), "<host>")
End Function

' an expression, or the error it raises
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
    ElseIf IsArray(x) Then
        value = "array"
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

Sub list(what, coll)
    Dim item, line, count
    On Error Resume Next
    Err.Clear
    count = coll.Count
    If Err.Number <> 0 Then count = "error " & Hex(Err.Number)
    Err.Clear
    line = "  " & what & ": Count " & count & ":"
    For Each item In coll
        line = line & " " & item.Name & "=[" & CStr(item.Value) & "] " & TypeName(item.Value)
    Next
    If Err.Number <> 0 Then line = line & " error " & Hex(Err.Number)
    WScript.Echo line
End Sub

WScript.Echo "SWbemNamedValueSet"
Set s = CreateObject("WbemScripting.SWbemNamedValueSet")
list "new", s
Set v = s.Add("B", 1)
WScript.Echo "  Add B, 1 gives " & TypeName(v) & " " & v.Name & " = " & v.Value & " " & TypeName(v.Value)
act "s.Add ""a"", ""x"""
act "s.Add ""C"", True"
act "s.Add ""b"", 2"
list "after Add B, a, C, b", s
WScript.Echo "  Item(A).Value = " & value("s.Item(""A"").Value") & ", Item(A).Name = " & value("s.Item(""A"").Name")
WScript.Echo "  s(a) = " & value("s(""a"")") & ", s(a).Name = " & value("s(""a"").Name")
WScript.Echo "  Item(nonesuch) = " & value("s.Item(""nonesuch"").Value")
act "s.Remove ""nonesuch"""
act "s.Remove ""A"""
list "after Remove A", s
act "s(""B"").Value = 7"
list "after s(B).Value = 7", s
Set v = s("C")
act "s.Remove ""C"""
WScript.Echo "  a value of C kept after Remove C: " & value("v.Name") & " " & value("v.Value")
Set c = s.Clone
act "c.Add ""Z"", 9"
list "the set after Clone and c.Add Z", s
list "the clone", c
act "s.DeleteAll"
list "after DeleteAll", s

WScript.Echo "Keys"
Set p = CreateObject("WbemScripting.SWbemObjectPath")
p.Path = "Class.K=1,S=""s"""
list "Keys of Class.K=1,S=""s""", p.Keys
WScript.Echo "  Keys(K) = " & value("p.Keys(""K"")") & ", Keys.Item(s).Name = " & value("p.Keys.Item(""s"").Name")
act "p.Keys(""K"").Value = 9"
WScript.Echo "  Path = " & value("p.Path")
Set k = p.Keys
act "p.Path = ""Other.Q=""""q"""""""
list "the keys taken before Path changed", k
Set c = p.Keys.Clone
act "c.Add ""X"", 1"
WScript.Echo "  Path after a clone's Add = " & value("p.Path")
act "p.Keys.Add ""L"", CLng(70000)"
act "p.Keys.Add ""Y"", CByte(3)"
act "p.Keys.Add ""T"", True"
act "p.Keys.Add ""D"", 1.5"
act "p.Keys.Add ""E"", CDbl(4)"
act "p.Keys.Add ""Z"", ""1,2"""
act "p.Keys.Add ""N"", Null"
WScript.Echo "  Path = " & value("p.Path")
list "Keys", p.Keys
act "p.Keys.Remove ""nonesuch"""
act "p.Keys.DeleteAll"
WScript.Echo "  Path after DeleteAll = " & value("p.Path") & ", IsClass " & value("p.IsClass")

WScript.Echo "DisplayName"
Set p = CreateObject("WbemScripting.SWbemObjectPath")
p.Path = "\\srv\root\cimv2:Class.K=1"
act "p.Locale = ""ms_409"""
act "p.Authority = ""kerberos:srv"""
act "p.Security_.ImpersonationLevel = 4"
act "p.Security_.AuthenticationLevel = 2"
WScript.Echo "  " & value("p.DisplayName")
act "p.Security_.Privileges.Add 7"
act "p.Security_.Privileges.AddAsString ""SeShutdownPrivilege"", False"
WScript.Echo "  " & value("p.DisplayName")
WScript.Echo "  Privileges.Count = " & value("p.Security_.Privileges.Count")
act "p.Locale = """""
act "p.Authority = """""
WScript.Echo "  " & value("p.DisplayName")
act "p.DisplayName = ""winmgmts:{impersonationLevel=impersonate,authenticationLevel=pktIntegrity,authority=ntlmdomain:dom,(Debug,!Shutdown)}[locale=ms_407]!\\srv\root:C.K=2"""
WScript.Echo "  " & value("p.DisplayName")
WScript.Echo "  Locale " & value("p.Locale") & ", Authority " & value("p.Authority") & ", levels " & _
    value("p.Security_.ImpersonationLevel") & " " & value("p.Security_.AuthenticationLevel") & ", privileges " & _
    value("p.Security_.Privileges.Count")
act "p.DisplayName = ""WinMgmts:\\srv\root:C.K=3"""
WScript.Echo "  " & value("p.DisplayName")
act "p.DisplayName = ""\\srv\root:C.K=4"""
WScript.Echo "  " & value("p.DisplayName")
act "p.DisplayName = ""winmgmts:{impersonationLevel=bogus}!\\srv\root:C.K=5"""
WScript.Echo "  " & value("p.DisplayName")
act "p.DisplayName = ""winmgmts:"""
WScript.Echo "  " & value("p.DisplayName") & ", Path " & value("p.Path")

WScript.Echo "monikers"
act "Set svc = GetObject(""winmgmts:{impersonationLevel=impersonate}[locale=ms_409]!\\.\root\cimv2"")"
act "Set svc = GetObject(""winmgmts:[locale=ms_409]!\\.\root\cimv2"")"
act "Set svc = GetObject(""winmgmts:{impersonationLevel=impersonate}!\\.\root\cimv2"")"

WScript.Echo "objects"
For Each o In svc.ExecQuery("SELECT Size FROM Win32_LogicalDisk WHERE DeviceID = 'C:'")
    For Each n In Array("Path", "RelPath", "Server", "Namespace", "Class", "IsClass", "IsSingleton", "DisplayName", _
                        "Keys.Count", "Locale", "Authority", "Security_.ImpersonationLevel", _
                        "Security_.AuthenticationLevel")
        WScript.Echo "  SELECT Size: Path_." & n & " = " & value("o.Path_." & n)
    Next
Next
Set o = svc.Get("Win32_LogicalDisk.DeviceID=""C:""")
For Each n In Array("Locale", "Authority", "Security_.ImpersonationLevel", "Security_.AuthenticationLevel", _
                    "Security_.Privileges.Count")
    WScript.Echo "  Get: Path_." & n & " = " & value("o.Path_." & n)
Next
WScript.Echo "  Path_ Is Path_: " & value("o.Path_ Is o.Path_")
act "o.Path_.Path = ""Win32_LogicalDisk.DeviceID=""""X:"""""""
act "o.Path_.Class = ""Other"""
act "o.Path_.Keys.Add ""X"", 1"
WScript.Echo "  Path_.Path after = " & value("o.Path_.Path")
