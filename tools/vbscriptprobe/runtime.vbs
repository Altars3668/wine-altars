' vbsrt.vbs <output>: the number, source and description of errors scripts commonly run into
Option Explicit
Dim fso, out, x, o, a(2), d
Set fso = CreateObject("Scripting.FileSystemObject")
Set out = fso.CreateTextFile(WScript.Arguments(0), True, True)
Sub show(what)
    out.WriteLine what & vbTab & Err.Number & vbTab & Err.Source & vbTab & Err.Description
    Err.Clear
End Sub
On Error Resume Next
x = 1 / 0 : show "1/0"
x = 1 \ 0 : show "1\0"
x = 5 Mod 0 : show "5 mod 0"
x = CInt(40000) : show "CInt(40000)"
x = a(5) : show "a(5)"
x = "a" * 2 : show """a""*2"
o.Foo : show "o.Foo on Empty"
Set o = Nothing : o.Foo : show "Nothing.Foo"
Set o = CreateObject("Scripting.Dictionary") : o.Nope : show "dict.Nope"
o.Add "k", 1 : o.Add "k", 2 : show "dict dup key"
x = o.Item("k")(1) : show "index a number"
Set o = CreateObject("No.Such.Class") : show "CreateObject missing"
Execute "y = 1" : show "Execute undefined var"
Execute "Call NoSuchSub()" : show "undefined sub"
Execute "x = (" : show "syntax error"
Execute "If x Then" : show "missing End If"
x = CDate("not a date") : show "CDate bad"
x = Mid("abc", 0, 1) : show "Mid 0"
x = Chr(-1) : show "Chr -1"
x = Null + Len(Null) : x = CLng(Null) : show "CLng Null"
Set d = CreateObject("Scripting.FileSystemObject").GetFile("Z:\no\such\file.txt") : show "GetFile missing"
x = Array(1, 2)
ReDim Preserve x(1, 1) : show "ReDim Preserve dims"
out.Close
