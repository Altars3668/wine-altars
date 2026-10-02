# securitycenterprobe

What Windows Security Center tells WMI: every instance of `AntiVirusProduct`, `AntiSpywareProduct` and
`FirewallProduct` in `root\SecurityCenter2` with all their properties (`productState` also in hex), then whether
the old `root\SecurityCenter` of Windows XP still answers.  Prints only; it changes nothing.

    cscript //nologo securitycenter.vbs

`securitycenter.vbs` also prints the properties and CIM types of every product class in both namespaces, and
`wscscript.vbs` drives `wscAPI.WSCProductList` through IDispatch the way a script would.

`results/*.win.txt` are from winref (Windows 11 build 29671).  WMI names Defender "Windows Defender" while the WSC
product list gives the localized "Microsoft Defender 防病毒", under the same GUID; Windows's own firewall is in the
product list with a GUID of "NULL" and in no WMI class; `root\SecurityCenter` is there with `AntiVirusProduct`,
`AntiSpywareProduct` and `FirewallProduct` and no instances.  A script gets the object and then 0x8002801D from
every call: the type library in wscapi.dll says version 2.0 and is registered as 1.0.

Under Wine the products are the host's ClamAV, as wscapi.dll reports it (altars-up `7f4b3330a77`, `8bf54fc4b20`):
with clamd running and signatures less than a week old, one antivirus and one antispyware product named
`ClamAV`, `productState` 266240 (0x41000), and no firewall product.  On Windows it shows what the undocumented
`productState` is for Defender and any other product installed there, which the Wine side follows.
