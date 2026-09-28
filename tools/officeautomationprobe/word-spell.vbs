' Whether Word's proofing works: the speller Word has for US English, a sentence in US English with three words spelt
' wrong, which words Word marks, what it suggests for one, and CheckSpelling of a word spelt right and one spelt wrong.
' Usage: cscript word-spell.vbs <progress log>
Option Explicit
Dim fso, progress, word, document, started, errors, e, suggestions, s, language, dictionary
If WScript.Arguments.Count <> 1 Then WScript.Quit 2
Set fso = CreateObject("Scripting.FileSystemObject")
progress = WScript.Arguments(0)
Sub mark(text)
    Dim stream
    Set stream = fso.OpenTextFile(progress, 8, True)
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

Set language = word.Languages(1033)
check "Languages(1033)"
mark "US English: " & language.Name & ", spelling dictionary type " & language.SpellingDictionaryType
check "SpellingDictionaryType"
Set dictionary = language.ActiveSpellingDictionary
check "ActiveSpellingDictionary"
If Not dictionary Is Nothing Then mark "speller: " & dictionary.Path & "\" & dictionary.Name
check "speller"

Set document = word.Documents.Add()
check "Documents.Add"
document.Content.Text = "Thiss sentense has three mispeled words."
document.Content.LanguageID = 1033
document.Content.NoProofing = False
check "document text"
mark "document language " & document.Content.LanguageID & ", no proofing " & CInt(document.Content.NoProofing)

mark "CheckSpelling(misspelled): " & CInt(word.CheckSpelling("misspelled"))
check "CheckSpelling(misspelled)"
mark "CheckSpelling(mispeled): " & CInt(word.CheckSpelling("mispeled"))
check "CheckSpelling(mispeled)"

Set errors = document.SpellingErrors
check "SpellingErrors"
mark "spelling errors: " & errors.Count
For Each e In errors
    mark "  " & e.Text
Next
Set suggestions = word.GetSpellingSuggestions("mispeled")
check "GetSpellingSuggestions"
mark "suggestions for mispeled: " & suggestions.Count
For Each s In suggestions
    mark "  " & s.Name
Next

document.Close False
check "Close"
' Only a Word this script started is quit; one that was already running is left alone.
If started Then
    word.Quit
    check "Quit"
End If
mark "done"
