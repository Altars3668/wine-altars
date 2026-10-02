# fileattrprobe

What Windows keeps of a file's attributes.

- `archiveprobe.c`: a new file's attributes, each attribute set on its own (`FILE_ATTRIBUTE_NORMAL`, hidden, read-only
  after hidden, temporary, 0) and what `GetFileAttributes`, `FindFirstFile` and `FileBasicInformation` say then; which
  operations give a file whose archive attribute was cleared it back -- writing (still open, flushed, closed), opening
  for writing only, making it larger or smaller, reading, setting its times, renaming, copying (the copy and the
  original), writing over, truncating, writing a stream, a hard link; a directory's archive attribute; and what
  `GetVolumePathName` says to a drive given in lower case.  It works in a directory of its own under the current one.

      archiveprobe.exe

`results/archive.win.txt` is Windows 11 build 29671 (winref, batch 38):

- A file with no attribute says `FILE_ATTRIBUTE_NORMAL` (0x80) everywhere; hidden, read-only and temporary stand on
  their own (0x2, 0x1, 0x100).
- The archive attribute comes back when the file is made larger or smaller (at once), renamed, written over or
  truncated, given a stream or a hard link, and when it is written to -- once it is flushed or closed.  Opening it for
  writing only, reading it and setting its times do not.  A copy has it, the original stays as it was.
- A directory has it only when it is set (0x30), and keeps it.
- `GetVolumePathName` keeps the drive as given: `c:\windows` is on `c:\`.  (A bare `c:` is the current directory on
  C:, so it says `C:\` there.)

`results/archive.wine.txt` is altars-up `b6be22fee26`, run from a directory on C:: the same, but for two lines.  Wine
gives the file the archive attribute as soon as it is written to, not when it is flushed or closed; and it has no
streams, so `a.txt:s` is a file of its own and `a.txt` is left as it was.  Before, Wine said 0x20 for a file whatever
was set, kept nothing but hidden and system, did not let a directory have the archive attribute, left a hidden file
hidden when it was made read-only, and gave the drive in capitals.

Build: `x86_64-w64-mingw32-gcc -O2 -Wall -o archiveprobe.exe archiveprobe.c -lntdll`
