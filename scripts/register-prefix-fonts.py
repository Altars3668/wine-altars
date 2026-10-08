#!/usr/bin/env python3
"""Register the fonts in the prefix's Fonts directory so DirectWrite can see them.

Office draws its entire UI through Direct2D's DrawTextLayout. Wine's DirectWrite
builds its system font collection *only* from
`HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts` -- see
`factory_create_system_fontset` -> `create_system_path_list` -> `open_fonts_key`
in dlls/dwrite/main.c. It never scans `C:\\windows\\Fonts`.

GDI does scan that directory, so fonts dropped into it work for ordinary Win32
programs and the discrepancy stays invisible -- until something asks
DirectWrite. `import-office.sh` unfolds `root/vfs/Fonts/private` into
`C:\\windows\\Fonts`, several hundred files, none of them registered. The result
is a DirectWrite that can see only the host's Linux fonts, cannot match even
Tahoma, and fails every DrawTextLayout, which Office renders as a blank window
with nothing in any log except one WARN per failed run.

Only the value's *data* matters to DirectWrite (it collects paths and parses the
files itself); the value name is for GDI's benefit, so it is built the way
Windows builds it.

Registering every file in the directory is not the right answer: with all 422
of them present Wine's dwrite spends over a million localizedstrings calls
building its collection and Word stops making progress at 56 loaded modules.
So the default is a curated set -- the families Office actually asks for,
including the zh-CN ones -- and --all is there for when something is missing.

    scripts/register-prefix-fonts.py [--all] [--dry-run]
"""
import io, os, re, subprocess, sys, tempfile

PREFIX = os.environ.get("WINEPREFIX", os.path.expanduser("~/.wine-c2r-up"))
FONTDIR = os.path.join(PREFIX, "drive_c", "windows", "Fonts")
KEY = r"HKEY_LOCAL_MACHINE\Software\Microsoft\Windows NT\CurrentVersion\Fonts"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WINE = os.environ.get("WINE", os.path.join(ROOT, "dist-up", "bin", "wine"))


def names_of(path):
    """Full font names inside a file, in order; [] if it cannot be read."""
    try:
        from fontTools.ttLib import TTFont, TTCollection
    except ImportError:
        sys.exit("needs fontTools (pip install fonttools)")
    try:
        fonts = TTCollection(path).fonts if path.lower().endswith(".ttc") else [TTFont(path, fontNumber=0)]
    except Exception:
        return []
    out = []
    for f in fonts:
        best = None
        try:
            for rec in f["name"].names:
                if rec.nameID != 4:          # full font name
                    continue
                try: value = rec.toUnicode()
                except Exception: continue
                # prefer the English/Windows record, fall back to any
                if rec.platformID == 3 and rec.langID == 0x409:
                    best = value; break
                best = best or value
        except Exception:
            pass
        if best:
            out.append(best)
        try: f.close()
        except Exception: pass
    return out


# What Office reaches for: DirectWrite's own last resort first, then the UI and
# document families, then the CJK ones this installation is localised in.
ESSENTIAL = (
    "tahoma", "tahomabd",
    "segoeui", "segoeuib", "segoeuii", "segoeuil", "segoeuisl", "segoeuiz", "seguisb", "seguili",
    "arial", "arialbd", "ariali", "arialbi",
    "times", "timesbd", "timesi", "timesbi",
    "cour", "courbd", "couri", "courbi",
    "calibri", "calibrib", "calibrii", "calibriz", "calibril", "calibrili",
    "cambria", "cambriab", "cambriai", "cambriaz",
    "consola", "consolab", "consolai", "consolaz",
    "verdana", "verdanab", "verdanai", "verdanaz",
    "georgia", "georgiab", "georgiai", "georgiaz",
    "trebuc", "trebucbd", "trebucit", "trebucbi",
    "wingding", "webdings", "symbol", "marlett", "sylfaen",
    "msyh", "msyhbd", "msyhl", "simsun", "simsunb", "simhei", "simkai", "simfang",
    "msgothic", "msmincho", "malgun", "dengxian", "deng", "dengb", "dengl",
)


def clean():
    """Drop every entry that points into C:\\windows\\Fonts, leaving the host's."""
    hive = os.path.join(PREFIX, "system.reg")
    s = io.open(hive, encoding="utf-8", errors="replace").read()
    m = re.search(r"\[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion\\\\Fonts\][^\[]*", s)
    if not m:
        print("no Fonts key"); return
    names = []
    for line in m.group(0).split("\n"):
        line = line.strip()
        if not line.startswith('"') or "=" not in line: continue
        name, _, data = line.partition("=")
        if "windows" in data.lower() and "fonts" in data.lower():
            names.append(name.strip('"'))
    print("removing %d entries that point into C:\\windows\\Fonts" % len(names))
    if not names: return
    lines = ["Windows Registry Editor Version 5.00", "", "[%s]" % KEY]
    lines += ['"%s"=-' % n for n in names]
    with tempfile.NamedTemporaryFile("w", suffix=".reg", delete=False,
                                     encoding="utf-16-le", newline="\r\n") as fh:
        fh.write("\ufeff" + "\r\n".join(lines) + "\r\n")
        path = fh.name
    subprocess.run([WINE, "regedit", "/S", path],
                   env=dict(os.environ, WINEPREFIX=PREFIX, WINEDEBUG="-all"), check=False)
    os.unlink(path)


def main():
    dry = "--dry-run" in sys.argv
    everything = "--all" in sys.argv
    if "--clean" in sys.argv:
        clean(); return
    if not os.path.isdir(FONTDIR):
        sys.exit("no font directory at %s" % FONTDIR)

    files = sorted(f for f in os.listdir(FONTDIR)
                   if f.lower().endswith((".ttf", ".ttc", ".otf")))
    if not everything:
        files = [f for f in files if os.path.splitext(f)[0].lower() in ESSENTIAL]
    lines = ["Windows Registry Editor Version 5.00", "", "[%s]" % KEY]
    added = skipped = 0
    for f in files:
        names = names_of(os.path.join(FONTDIR, f))
        if not names:
            skipped += 1
            continue
        suffix = "(OpenType)" if f.lower().endswith(".otf") else "(TrueType)"
        label = " & ".join(names) + " " + suffix
        label = label.replace("\\", "\\\\").replace('"', '\\"')
        lines.append('"%s"="C:\\\\windows\\\\Fonts\\\\%s"' % (label, f))
        added += 1

    print("%d font files considered%s, %d registrable, %d unreadable"
          % (len(files), "" if everything else " (curated set; --all for every file)", added, skipped))
    if dry:
        for l in lines[3:8]:
            print("   ", l[:110])
        return

    with tempfile.NamedTemporaryFile("w", suffix=".reg", delete=False,
                                     encoding="utf-16-le", newline="\r\n") as fh:
        fh.write("\ufeff" + "\r\n".join(lines) + "\r\n")
        regfile = fh.name
    env = dict(os.environ, WINEPREFIX=PREFIX, WINEDEBUG="-all")
    subprocess.run([WINE, "regedit", "/S", regfile], env=env, check=False)
    os.unlink(regfile)
    print("registered; DirectWrite rebuilds its collection from this key on next start")


if __name__ == "__main__":
    main()
