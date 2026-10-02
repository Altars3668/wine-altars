# finalpathprobe

What a program learns about where a path really is: `GetVolumePathName`, `GetFinalPathNameByHandle` with each
volume name format (normalized and opened), and the file's name from `NtQueryObject` and `FileNameInformation`.

    finalpathprobe.exe <path>...

Under Wine the user's Documents, Desktop and so on are symbolic links to the host's home directory.  Before
altars-up `6cd0a144199` the normalized final path of `C:\users\<user>\Documents` came out as
`\\?\C:\home\<user>\Documents`: the drive it was opened on, followed by the host's real path, which is on Z:.
Outlook builds the folder of a new PST from it ("the path is not valid").  Now it is `\\?\C:\users\<user>\Documents`,
each component in the case it has on disk.

Build: `x86_64-w64-mingw32-gcc -O2 -Wall -municode -o finalpathprobe.exe finalpathprobe.c -lntdll`

Since altars-up `b6be22fee26` the name a file was opened by also comes back in the case it has on disk, as on Windows
11 (`results/case.*.txt`, Windows from winref's batch 36, the user's name and the volume's GUID taken out): opened as
`casedir\mixedcase.txt` or `CASEDIR\MIXEDCASE.TXT`, every name -- final, opened, `NtQueryObject`, `FileNameInformation`
-- ends in `CaseDir\MixedCase.txt`; one opened by short names keeps them, as they are stored (`\PROGRA~1\COMMON~1`).
What still differs: `NtQueryObject` names the volume `\Device\HarddiskVolumeN` on Windows and `\??\X:` under Wine;
Wine's own `C:\windows\system32` is in lower case on disk; and Windows' short names (`PROGRA~1`) are not Wine's, which
are made from a hash of the long name.

## trailingprobe

What every file function says to a file's name with a backslash after it (`f.txt\`), to a file used as a directory
(`f.txt\x`) and to a new name with one (`n.txt\`), with a file's and a directory's name as they are and a directory's
with a backslash as the controls: `CreateFile` with each disposition, `NtCreateFile` with each disposition and
directory option and relative to a directory handle, `NtQueryAttributesFile`, attributes, find, copy, move, delete and
the directory calls.  It works in a directory of its own under the current one and puts it back after each call.

    trailingprobe.exe

`results/trailing.win.txt` is Windows 11 build 29671 (batch 36): a file's name with a backslash after it is no
directory's, `STATUS_NOT_A_DIRECTORY` (`ERROR_DIRECTORY`, 267) for everything, even `CREATE_NEW`, except creating a
directory by it (`STATUS_OBJECT_NAME_COLLISION`); a new name with one creates nothing but a directory; a file used as
a directory is a path not found.  It also shows that a file superseded with only read access is emptied all the same.
`results/trailing.wine.txt` is altars-up `b6be22fee26`: the same, line for line.  Wine said
`STATUS_OBJECT_NAME_INVALID` (`ERROR_INVALID_NAME`, 123), as Windows before 11 did, and left the superseded file as it
was.

Build: `x86_64-w64-mingw32-gcc -O2 -Wall -o trailingprobe.exe trailingprobe.c -lntdll`
