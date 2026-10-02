# privprobe

What Windows says of privileges: their LUIDs, their display names, and what an administrator's token has.

- `privnames.c`: `LookupPrivilegeDisplayName` for every privilege name (and a few that are none): the text, its
  language and the lengths asked for and given; with a short buffer, no name, an empty system name and the ANSI call.
  Non-ASCII text is printed as `\uXXXX`.
- `tokenprivs.c`: the LUID `LookupPrivilegeValue` gives each name and the name `LookupPrivilegeName` gives each LUID up
  to 40, this process's token's privileges with their attributes and its elevation type, and what
  `AdjustTokenPrivileges` says to enabling `SeCreateSymbolicLinkPrivilege`.

`results/*.win.txt` is Windows 11 build 29671 (winref, batches 31 and 32; the token is the ssh session's, an elevated
administrator's with every privilege enabled):

- LUIDs run from 2 to 36, `SeDelegateSessionUserImpersonatePrivilege`; a name that is none gives
  `ERROR_NO_SUCH_PRIVILEGE` and a zero LUID.
- `LookupPrivilegeDisplayName` asked for its size gives the length with the terminator and
  `ERROR_INSUFFICIENT_BUFFER`, the language already given; then the length without.  The ANSI call asks for the wide
  length and gives the length in bytes of the code page's text.  The texts are in the user's language (0x804 there).
- An administrator has 24 privileges: 5, 8 to 15, 17 to 20, 22 to 25, 28 to 30 and 33 to 36.

`results/*.wine.txt` is altars-up `1929d3edcbb`, the display names under `LANG=zh_CN.UTF-8`: the same, the texts
included; Wine's administrator also has `SeTcbPrivilege`, and enables `SeLoadDriverPrivilege`, which it uses.

Build: `x86_64-w64-mingw32-gcc -O2 -Wall -o <probe>.exe <probe>.c -ladvapi32`
