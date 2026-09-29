' word-cjkfonts.vbs <output.pdf> <progress log>
' Which fonts Word draws Chinese text with when a document names the Chinese fonts Windows has and
' Office does not bring (SimSun, NSimSun, SimHei, KaiTi, FangSong) next to those Office installs
' (Microsoft YaHei, DengXian): one paragraph per font, the font's name followed by the same Chinese
' sentence, exported to PDF.  pdffonts on the PDF names the fonts that were used, and a rendered page
' shows whether any line fell back to boxes.
Dim fso, progress, destination, word, document, started, names, i, para, text

Sub mark(text)
    Dim file
    Set file = fso.OpenTextFile(progress, 8, True)
    file.WriteLine text
    file.Close
End Sub

Sub check(what)
    If Err.Number <> 0 Then
        mark what & " failed: " & Hex(Err.Number) & " " & Err.Description
        Err.Clear
    End If
End Sub

Set fso = CreateObject("Scripting.FileSystemObject")
If WScript.Arguments.Count <> 2 Then WScript.Quit 2
destination = WScript.Arguments(0)
progress = WScript.Arguments(1)
mark "start"
On Error Resume Next
started = False
Set word = CreateObject("Word.Application")
started = (Err.Number = 0)
If Err.Number <> 0 Then
    mark "activation failed: " & Hex(Err.Number)
    WScript.Quit 3
End If
mark "activated"

' SimSun, NSimSun, SimHei, KaiTi, FangSong by their Chinese names, then by their English ones, then
' the fonts Office installs
names = Array(ChrW(&H5B8B) & ChrW(&H4F53), ChrW(&H65B0) & ChrW(&H5B8B) & ChrW(&H4F53), ChrW(&H9ED1) & ChrW(&H4F53), _
              ChrW(&H6977) & ChrW(&H4F53), ChrW(&H4EFF) & ChrW(&H5B8B), "SimSun", "SimHei", _
              ChrW(&H5FAE) & ChrW(&H8F6F) & ChrW(&H96C5) & ChrW(&H9ED1), ChrW(&H7B49) & ChrW(&H7EBF), "STSong")

Set document = word.Documents.Add()
check "Documents.Add"
text = ""
For i = 0 To UBound(names)
    text = text & names(i) & ": " & ChrW(&H4E2D) & ChrW(&H6587) & ChrW(&H5B57) & ChrW(&H4F53) & ChrW(&H6D4B) & _
        ChrW(&H8BD5) & ChrW(&H3002) & " ABC 123" & vbCr
Next
document.Content.Text = text
check "text"
For i = 0 To UBound(names)
    Set para = document.Paragraphs(i + 1)
    para.Range.Font.Name = names(i)
    para.Range.Font.NameFarEast = names(i)
    check "paragraph " & i
Next
mark "paragraphs " & document.Paragraphs.Count
mark "document made"

document.ExportAsFixedFormat destination, 17
check "ExportAsFixedFormat"
mark "exported"

document.Close False
check "Close"
If started Then
    word.Quit
    check "Quit"
End If
mark "done"
