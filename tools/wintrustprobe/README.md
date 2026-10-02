# wintrustprobe

`WinVerifyTrust` (generic verify v2, no UI, no revocation) on files, with what decides it for a signature whose
certificate has expired: each signer WinVerifyTrust keeps, with the time it verified it as of, and its countersigners;
the signing certificate's validity; and the signer's unauthenticated attributes (`1.2.840.113549.1.9.6` is a legacy
countersignature, `1.3.6.1.4.1.311.3.3.1` an RFC 3161 timestamp).  For an RFC 3161 token it also prints what crypt32
makes of it: `TIMESTAMP_INFO` decoded from the token's content, and `CryptVerifyTimeStampSignature` on the signer's
encrypted hash, on wrong data, on no data, on a cut token and on the TSTInfo itself.

    wintrustprobe.exe <file>...

`results/wintrustprobe.win.txt` is Windows 11 build 29671 (winref, batches 16, 18 and 20) on files of the Office prefix:
VSTOInstaller.exe (RFC 3161 only, certificate expired 2024), msdia100.dll (legacy only), api-ms-win-crt-runtime and
System.Security.Cryptography.ProtectedData (legacy, and RFC 3161 in a nested signature), the Windows Community Toolkit's
notifications DLL (a DigiCert RFC 3161 timestamp with no accuracy and no TSA name), and VSTOInstaller.exe with its
timestamp cut out of the signature, as uploaded and with its creation time moved inside the certificate's validity.
Windows verifies each signer as of its timestamp to the millisecond, keeps each timestamp as a countersigner of type
0x10, and verifies a signature with no timestamp as of the call: CERT_E_EXPIRED, whatever the file's creation time.
`results/wintrustprobe.wine.txt` is altars-up `c2a255dc096`: the same, but for the toolkit DLL, whose root Windows got
from its automatic root update (docs/office365-under-wine.md, “签名与时间戳”).

Build: `x86_64-w64-mingw32-gcc -O2 -Wall -municode -o wintrustprobe.exe wintrustprobe.c -lwintrust -lcrypt32`

## tstinfoprobe

What `CryptDecodeObjectEx` makes of RFC 3161 TSTInfo structures, handcrafted to have each optional part on its own
(accuracy in every form, ordering, a nonce, a TSA name, extensions) and times with no fraction of a second and with one
to six digits of it; whether `TIMESTAMP_INFO` encodes back; and what `TIMESTAMP_REQUEST` encodes to and a rejected
`TIMESTAMP_RESPONSE` decodes to.  `tstinfo-vectors.h` holds the structures, which
`gen-tstinfo-vectors.py` writes; crypt32's encode test has most of them.

`results/tstinfoprobe.win.txt` (batch 19): Windows reserves room for the accuracy and leaves `pvAccuracy` NULL whatever
it holds; serial number and nonce are little-endian; the TSA name is the GeneralName's encoding; the time keeps
milliseconds and drops further digits.  `results/tstinfoprobe.wine.txt` is the same since altars-up `9dd82e22dc6`.

Build: `x86_64-w64-mingw32-gcc -O2 -Wall -o tstinfoprobe.exe tstinfoprobe.c -lcrypt32`

## trustscan

`WinVerifyTrust` on every .exe and .dll under the directories given, one line a file, and the tally.
`results/trustscan.wine.txt` is the Office prefix before and after the timestamp and policy work.

Build: `x86_64-w64-mingw32-gcc -O2 -Wall -municode -o trustscan.exe trustscan.c -lwintrust`
