# wintrustprobe

`WinVerifyTrust` (generic verify v2, no UI, no revocation) on files, with what decides it for a signature whose
certificate has expired: the signing certificate's validity, the time the signer was verified as of, and the
signer's unauthenticated attributes (`1.2.840.113549.1.9.6` is a legacy countersignature, `1.3.6.1.4.1.311.3.3.1`
an RFC 3161 timestamp).

    wintrustprobe.exe <file>...

Under Wine (2026-10-01) a signature with no legacy countersignature is verified as of the file's creation time
(wintrust has done so since 2007), and RFC 3161 timestamps are not read at all: a file whose certificate expired
passes when its creation time falls in the certificate's validity, and a timestamped one is not verified as of its
timestamp.  Windows verifies as of the timestamp, or the current time.

Build: `x86_64-w64-mingw32-gcc -O2 -Wall -municode -o wintrustprobe.exe wintrustprobe.c -lwintrust -lcrypt32`
