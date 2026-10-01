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
