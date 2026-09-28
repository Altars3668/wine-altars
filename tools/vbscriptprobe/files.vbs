Option Explicit
Dim fso, out, r, bmp, sh
Set fso = CreateObject("Scripting.FileSystemObject")
Set out = fso.CreateTextFile(WScript.Arguments(0), True, True)
bmp = fso.BuildPath(fso.GetSpecialFolder(2), "wa-probe.bmp")
Sub show(what)
    If Err.Number = 0 Then
        If IsObject(r) Then
            out.WriteLine what & vbTab & "ok" & vbTab & TypeName(r)
        Else
            out.WriteLine what & vbTab & "ok" & vbTab & TypeName(r) & vbTab & r
        End If
    Else
        out.WriteLine what & vbTab & Err.Number & vbTab & Err.Source
    End If
    Err.Clear
End Sub
On Error Resume Next
' a tiny 1x1 bitmap to load
Dim s, i, st : s = Array(&H42,&H4D,&H3A,0,0,0,0,0,0,0,&H36,0,0,0,&H28,0,0,0,1,0,0,0,1,0,0,0,1,0,&H18,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,&HFF,0,0,0)
Set st = CreateObject("ADODB.Stream") : st.Type = 2 : st.Charset = "iso-8859-1" : st.Open
For i = 0 To UBound(s) : st.WriteText Chr(s(i)) : Next
st.SaveToFile bmp, 2 : st.Close
Err.Clear
r = Empty : Call NoSuchSub() : show "Call NoSuchSub()"
r = Empty : NoSuchSub 1 : show "NoSuchSub 1"
r = Empty : r = NoSuchFunc(1) : show "r = NoSuchFunc(1)"
r = Empty : r = NoSuchVar : show "r = NoSuchVar"
r = Empty : Execute "Call NoSuchSub()" : show "Execute ""Call NoSuchSub()"""
r = Empty : Set r = LoadPicture("") : show "Set r = LoadPicture("""")"
r = Empty : Set r = LoadPicture("x") : show "Set r = LoadPicture(""x"")"
r = Empty : Set r = LoadPicture(WScript.ScriptFullName) : show "Set r = LoadPicture(WScript.ScriptFullName)"
r = Empty : Set r = LoadPicture(bmp) : show "Set r = LoadPicture(bmp)"
r = Empty : r = LoadPicture(bmp).Width : show "r = LoadPicture(bmp).Width"
r = Empty : r = TypeName(LoadPicture(bmp)) : show "r = TypeName(LoadPicture(bmp))"
r = Empty : r = LoadPicture(bmp).Type : show "r = LoadPicture(bmp).Type"
r = Empty : r = TypeName(LoadPicture("")) : show "r = TypeName(LoadPicture(""""))"
r = Empty : Set r = fso.GetFile("C:\nodir\x.txt") : show "GetFile(C:\nodir\x.txt)"
r = Empty : Set r = fso.GetFile("C:\Windows\nofile.txt") : show "GetFile(C:\Windows\nofile.txt)"
r = Empty : Set r = fso.GetFile("Q:\x.txt") : show "GetFile(Q:\x.txt)"
r = Empty : Set r = fso.GetFile("") : show "GetFile("""")"
r = Empty : Set r = fso.GetFile("C:\Windows") : show "GetFile(C:\Windows)"
r = Empty : Set r = fso.GetFolder("C:\nodir") : show "GetFolder(C:\nodir)"
r = Empty : Set r = fso.GetFolder("Q:\") : show "GetFolder(Q:\)"
r = Empty : Set r = fso.GetFolder("C:\Windows\notepad.exe") : show "GetFolder(C:\Windows\notepad.exe)"
r = Empty : Set r = fso.GetFolder("") : show "GetFolder("""")"
r = Empty : Set r = fso.GetDrive("Q:") : show "GetDrive(Q:)"
r = Empty : Set r = fso.GetDrive("") : show "GetDrive("""")"
r = Empty : Set r = fso.OpenTextFile("C:\nodir\x.txt") : show "OpenTextFile(C:\nodir\x.txt)"
r = Empty : Set r = fso.OpenTextFile("C:\Windows\nofile.txt") : show "OpenTextFile(C:\Windows\nofile.txt)"
r = Empty : fso.DeleteFile "C:\Windows\nofile.txt" : show "DeleteFile(C:\Windows\nofile.txt)"
r = Empty : fso.DeleteFolder "C:\nodir" : show "DeleteFolder(C:\nodir)"
r = Empty : fso.CopyFile "C:\Windows\nofile.txt", "C:\Windows\x.txt" : show "CopyFile(C:\Windows\nofile.txt)"
r = Empty : fso.CreateFolder "C:\Windows" : show "CreateFolder(C:\Windows)"
r = Empty : r = fso.GetFileVersion("C:\Windows\nofile.dll") : show "GetFileVersion(nofile)"
fso.DeleteFile bmp
out.Close
