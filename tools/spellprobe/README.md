# spellprobe

What the Windows spell checking API (`MsSpellCheckingFacility.dll`, `CLSID_SpellCheckerFactory`) says: the languages
it has and how it takes their tags, a checker's tag, id, name and options, the errors it finds in a set of English
texts with `Check` and `ComprehensiveCheck`, its suggestions, what adding, ignoring, removing and autocorrecting words
does and whether a second checker sees it, how many change events each gives, what the user's word lists then hold,
and what registering a user dictionary does.

    spellprobe.exe [language]       en-US by default

Adding and autocorrecting write the current user's lists, `%APPDATA%\Microsoft\Spelling\<language>\default.exc` and
`.acl` (and `neutral\default.dic`, which the probe does not keep: what it adds it removes again); the probe reads the
language's two before and writes them back as they were, and prints only the lines with its own words.

`results/spellprobe.win.txt` is Windows 11 build 29671 (winref, batch 42; the user's lists were as before afterwards,
batch 43):

- Languages: en-CA, en-LR, en-PH, en-US, zh-Latn-CN-x-ext; a tag in any case, nothing else (no `en`, no `en_US`); an
  empty one is E_INVALIDARG (and the answer cleared), NULL E_POINTER; a checker keeps the tag as it was given.
- Id `MsSpell`, the name in the user's language, no options (an unknown one is E_INVALIDARG, value cleared).
- Words in capitals, with digits, URLs and mail addresses are not checked; apostrophes (`'` and `’`) and hyphens join
  a word, only the wrong parts of a compound are errors; a word repeating the one before -- spaces only between them,
  any case, the one before right -- is to be deleted; a comprehensive check replaces a word that has one suggestion.
- Added words are in `neutral\default.dic` for every language, removed ones (dictionary words too) in `default.exc`,
  corrections in `default.acl` as `word|correction`, UTF-16 with a byte order mark; ignored words are the checker's
  own.  A correction is for the word as written.  Adding gives one change event, removing and correcting two.
  A correction to two words is accepted and then every check fails with E_INVALIDARG -- a Windows bug.

`results/spellprobe.wine.txt` is altars-up `604ce6dbfa9` with the host's Hunspell (en_US, en_GB): the same rules;
what differs is the dictionaries' -- other languages, other suggestions (so a comprehensive check replaces other
words), no `café` or `naïve` in Hunspell's en_US -- and three things Windows does a moment late or not at all: it
keeps a changed correction and a re-added word as they were until later, and applies a registered dictionary later.
The documentation (docs/office365-under-wine.md, "Windows 拼写检查 API") has the rest.

Build: `x86_64-w64-mingw32-gcc -O2 -Wall -municode -o spellprobe.exe spellprobe.c -lole32 -luuid`
