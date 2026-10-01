' Outlook's PST engine under load, with made-up data only: in the default store of the running Outlook, which is to be
' started first with a profile holding a PST and no mail account ("outlook.exe /PIM <profile>"), a folder gets
' <count> post items whose subject and body follow from their number, which are then all read back and checked.
' Post items, as a new mail item is saved to Drafts whatever folder it was made in.
' Each step is logged with its result or its error; nothing is sent, and no account's mail is touched.
' Usage: cscript outlook-pst.vbs <progress log> <count>
Option Explicit
Dim fso, progress, app, ns, store, root, folder, item, items, count, i, bad, total, body, t0
If WScript.Arguments.Count < 2 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
progress = WScript.Arguments(0)
count = CLng(WScript.Arguments(1))
Sub mark(text)
    Dim stream
    Set stream = fso.OpenTextFile(progress, 8, True, -1)
    stream.WriteLine text
    stream.Close
End Sub
' logs the step with what it found, or its error
Sub result(what, found)
    If Err.Number <> 0 Then
        mark "FAIL " & what & ": " & Hex(Err.Number) & " " & Err.Description
        Err.Clear
    Else
        mark "ok   " & what & ": " & found
    End If
End Sub
' the body of item n: about 4 KB of text that differs from item to item
Function make_body(n)
    Dim s, k
    s = ""
    For k = 1 To 64
        s = s & "Line " & k & " of item " & n & ": " & String(40, Chr(65 + (n + k) Mod 26)) & vbCrLf
    Next
    make_body = s
End Function

mark "start"
On Error Resume Next
Set app = GetObject(, "Outlook.Application")
If Err.Number <> 0 Then
    mark "no running Outlook: " & Hex(Err.Number)
    WScript.Quit 3
End If
Set ns = app.GetNamespace("MAPI")
Set store = ns.DefaultStore
result "store", store.DisplayName & " type " & store.ExchangeStoreType & " " & store.FilePath
If store.ExchangeStoreType <> 3 Then
    ' olNotExchange: a PST.  An OST would be an account's mail.
    mark "the default store is not a PST, stopping"
    WScript.Quit 4
End If
Set root = store.GetRootFolder()
result "root folder", root.Name

Err.Clear
Set folder = root.Folders("WineAltarsTest")
If Err.Number = 0 Then
    folder.Delete
    result "old test folder deleted", ""
End If
Err.Clear
Set folder = root.Folders.Add("WineAltarsTest")
result "test folder", folder.FolderPath

t0 = Timer
For i = 1 To count
    ' olPostItem
    Set item = folder.Items.Add(6)
    item.Subject = "WineAltars test item " & i
    item.Body = make_body(i)
    item.Save
    If Err.Number <> 0 Then
        result "item " & i, ""
        Exit For
    End If
    If i Mod 200 = 0 Then mark "     saved " & i & " in " & Round(Timer - t0, 1) & " s"
Next
result "saved", count & " in " & Round(Timer - t0, 1) & " s"

t0 = Timer
Set items = folder.Items
total = items.Count
bad = 0
For Each item In items
    i = CLng(Mid(item.Subject, Len("WineAltars test item ") + 1))
    body = item.Body
    ' Outlook may end the body with its own line break
    If Left(body, Len(make_body(i))) <> make_body(i) Then
        bad = bad + 1
        If bad <= 5 Then mark "     item " & i & " reads back wrong, length " & Len(body)
    End If
Next
result "read back", total & " items, " & bad & " wrong, in " & Round(Timer - t0, 1) & " s"
mark "done"
