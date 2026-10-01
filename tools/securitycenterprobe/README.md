# securitycenterprobe

What Windows Security Center tells WMI: every instance of `AntiVirusProduct`, `AntiSpywareProduct` and
`FirewallProduct` in `root\SecurityCenter2` with all their properties (`productState` also in hex), then whether
the old `root\SecurityCenter` of Windows XP still answers.  Prints only; it changes nothing.

    cscript //nologo securitycenter.vbs

Under Wine the products are the host's ClamAV, as wscapi.dll reports it (altars-up `7f4b3330a77`, `8bf54fc4b20`):
with clamd running and signatures less than a week old, one antivirus and one antispyware product named
`ClamAV`, `productState` 266240 (0x41000), and no firewall product.  On Windows it shows what the undocumented
`productState` is for Defender and any other product installed there, which the Wine side follows.
