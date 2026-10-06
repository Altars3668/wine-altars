#!/usr/bin/python3
"""Unit checks for the manualduplex backend's own logic (no CUPS needed).

    python3 -I test/test_backend.py
"""
import importlib.machinery, importlib.util, os, sys

here = os.path.dirname(os.path.abspath(__file__))
loader = importlib.machinery.SourceFileLoader("manualduplex", os.path.join(here, "..", "manualduplex"))
spec = importlib.util.spec_from_loader("manualduplex", loader)
md = importlib.util.module_from_spec(spec)
loader.exec_module(md)

failures = 0
def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print("FAIL %s:\n   got  %r\n   want %r" % (what, got, want))
    else:
        print("ok   %s" % what)

# The option strings cupsd hands a backend, parsed as cupsParseOptions() does.
check("plain", md.parse_options("PageSize=A4 sides=two-sided-long-edge"),
      {"PageSize": "A4", "sides": "two-sided-long-edge"})
check("booleans", md.parse_options("collate nofit-to-page"),
      {"collate": "true", "fit-to-page": "false"})
check("quotes", md.parse_options("job-name='a b' x=\"c\\\"d\" y=e\\ f"),
      {"job-name": "a b", "x": 'c"d', "y": "e f"})
check("collection", md.parse_options(
          "media-col={media-size={x-dimension=21000 y-dimension=29700} media-source=tray-1} copies=2"),
      {"media-col": "{media-size={x-dimension=21000 y-dimension=29700} media-source=tray-1}",
       "copies": "2"})
check("empty value", md.parse_options("date-time-at-creation= time-at-creation=1"),
      {"date-time-at-creation": "", "time-at-creation": "1"})

# ...and written back so lp's parser reads the same thing.
for name, value in [("job-name", "a b"), ("x", 'c"d'), ("media", "A4"),
                    ("media-col", "{media-size={x-dimension=21000 y-dimension=29700}}")]:
    arg = md.option_arg(name, value)
    check("round trip %s" % name, md.parse_options(arg), {name: value})

# Which jobs are two-sided, and which way.
check("sides long", md.duplex_mode({"sides": "two-sided-long-edge"}), "long")
check("sides short", md.duplex_mode({"sides": "two-sided-short-edge"}), "short")
check("Duplex long", md.duplex_mode({"Duplex": "DuplexNoTumble"}), "long")
check("Duplex short", md.duplex_mode({"Duplex": "DuplexTumble"}), "short")
check("one-sided", md.duplex_mode({"sides": "one-sided"}), None)
check("Duplex None beats sides", md.duplex_mode({"Duplex": "None", "sides": "two-sided-long-edge"}), None)
check("nothing", md.duplex_mode({}), None)

sys.exit(1 if failures else 0)
