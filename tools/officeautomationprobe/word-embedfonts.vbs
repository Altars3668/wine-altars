' Whether Word embeds the fonts a document uses: a document in its default font with a line of Chinese, saved with
' EmbedTrueTypeFonts and SaveSubsetFonts, closed and opened again; what Word reports of the embedding settings.
' Usage: cscript word-embedfonts.vbs <output.docx> <progress log>
Option Explicit
Dim fso, progress, destination, word, document, started
If WScript.Arguments.Count <> 2 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
destination = WScript.Arguments(0)
progress = WScript.Arguments(1)
Sub mark(text)
    Dim stream
    Set stream = fso.OpenTextFile(progress, 8, True, -1)
    stream.WriteLine text
    stream.Close
End Sub
Sub check(what)
    If Err.Number <> 0 Then
        mark what & " failed: " & Hex(Err.Number) & " " & Err.Description
        Err.Clear
    End If
End Sub
mark "start"
On Error Resume Next
started = False
Set word = GetObject(, "Word.Application")
If Err.Number <> 0 Then
    Err.Clear
    Set word = CreateObject("Word.Application")
    started = (Err.Number = 0)
End If
If Err.Number <> 0 Then
    mark "activation failed: " & Hex(Err.Number)
    WScript.Quit 3
End If
mark "activated"

Set document = word.Documents.Add()
check "Documents.Add"
document.Content.Text = "Embedded fonts check" & vbCr & ChrW(&H5D4C) & ChrW(&H5165) & ChrW(&H5B57) & ChrW(&H4F53) & _
    ChrW(&H6D4B) & ChrW(&H8BD5) & vbCr
check "text"
mark "font " & document.Content.Font.Name
document.EmbedTrueTypeFonts = True
document.SaveSubsetFonts = True
check "embedding settings"
document.SaveAs2 destination, 16
check "SaveAs2"
mark "saved, embed " & CInt(document.EmbedTrueTypeFonts) & ", subset " & CInt(document.SaveSubsetFonts)
document.Close False
check "Close"
Set document = word.Documents.Open(destination)
check "Open"
mark "opened again, embed " & CInt(document.EmbedTrueTypeFonts) & ", text " & Left(document.Content.Text, 20)
document.Close False
check "Close again"
' Only a Word this script started is quit; one that was already running is left alone.
If started Then
    word.Quit
    check "Quit"
End If
mark "done"
