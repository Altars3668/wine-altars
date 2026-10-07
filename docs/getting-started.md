# Getting Microsoft 365 running

From nothing to Word, Excel and PowerPoint signed in with your own Microsoft account.
(中文：[getting-started.zh-CN.md](getting-started.zh-CN.md).)

Time: 40–90 minutes, most of it Microsoft's installer downloading about 2–3 GB.
Disk: about 10 GB free while installing, roughly 6 GB afterwards.

What is verified and what is not, so you know what to trust:

* `office-setup.sh prefix`, `webview2`, `signin`, `status` and the generated Office configuration
  were run on a clean prefix with a wine-altars build (11.19 plus 540 of the 578 patches).
* The Office installation (`setup.exe /configure`) and the sign-in are the procedure recorded in the
  notes ([`office365-under-wine.md`](office365-under-wine.md)), done there by hand on the same Wine.
  The scripted wrapper around them is newer than the notes. If it fails, the log paths below tell
  you where, and the manual commands are in the script — it is short on purpose.

## 0. What you need

* A **Microsoft 365 subscription** on a personal Microsoft account. Family and Personal were
  measured. Microsoft 365 Apps for business or enterprise (`O365ProPlusRetail`) installs the same
  way but was not tried, and neither were work or school accounts. Without a subscription Office does
  not license itself, here as on Windows.
* x86-64 Linux with a graphical session (X11, or Wayland with Xwayland) and a working OpenGL driver.
  A virtual display without a GPU is fine for installing and bad for looking at the result.
* `curl`, `git`, and the usual Wine runtime libraries. Installing your distribution's `wine` package
  is the easiest way to get the libraries; you will not use that Wine.

## 1. Wine

Either take the build from the [latest release](https://github.com/Altars3668/wine-altars/releases/latest)
(Ubuntu 24.04 build, needs glibc 2.39 or newer):

```sh
curl -LO https://github.com/Altars3668/wine-altars/releases/latest/download/wine-altars-linux-x86_64.tar.xz
sudo tar -C /opt -xf wine-altars-linux-x86_64.tar.xz
export PATH=/opt/wine-altars/bin:$PATH
wine --version
```

or build it ([`building.md`](building.md)). Use *this* Wine for everything below: a prefix made by
a different Wine loads other copies of the system DLLs, and Office then fails without saying why.
If another Wine is installed, make sure `which wine` prints `/opt/wine-altars/bin/wine`, or set
`WINE=/opt/wine-altars/bin/wine` for every script.

## 2. The scripts

```sh
git clone https://github.com/Altars3668/wine-altars.git
cd wine-altars
export WINEPREFIX=$HOME/.wine-office       # the default; any empty directory works
scripts/office-setup.sh status
```

`status` prints what is in place and what is missing; run it whenever you are unsure. Every script
under `scripts/` reads `WINE` (the wine binary) and `WINEPREFIX` from the environment; export them
once in the shell you work in.

## 3. The four steps

`scripts/office-setup.sh all` runs these in order. Run them one by one the first time.

### `prefix` — a 64-bit prefix in Windows 11 mode

Creates `$WINEPREFIX`, sets Windows 11 (Office expects the prefix to report it) and installs Wine
Gecko 2.47.4 from Wine's own download site. Mono and .NET are not installed: Office does not need
them (Excel's Power Query and VSTO add-ins do; that is outside this quick start). Setup steps run
with Wine's menu builder switched off so they leave nothing on your desktop.

### `webview2` — Microsoft's embedded browser

Office's sign-in window is a WebView2 browser. This downloads Microsoft's bootstrapper and runs it
silently (`/silent /install`), then applies the two workarounds `winetricks webview2` applies for
Wine bugs 53925 and 58921 (the update service must not start automatically; the renderer process
must be told to see Windows 7). The bootstrapper is a 32-bit program, which is why the Wine build
has a 32-bit half. Takes a few minutes.

### `install` — Office itself

Downloads Microsoft's Office installer (`setup.exe`, 7 MB, from `officecdn.microsoft.com`), writes
a configuration, and runs `setup.exe /configure`. Environment variables choose what is installed:

| variable | default | |
|---|---|---|
| `OFFICE_PRODUCT` | `O365HomePremRetail` | **Must match your subscription.** `O365HomePremRetail` is Microsoft 365 Family/Personal. The wrong product installs fine and then cannot be licensed |
| `OFFICE_LANG` | `en-us` | `zh-cn`, `de-de`, … |
| `OFFICE_APPS` | `word,excel,powerpoint` | add `outlook`, `onenote`, `access`, `publisher` at your own risk — not tested |
| `OFFICE_CHANNEL` | `Current` | |

```sh
OFFICE_LANG=zh-cn scripts/office-setup.sh install        # Chinese interface
```

The generated configuration is printed before it runs and kept in `~/.cache/wine-altars/`;
`DRY_RUN=1` stops after printing it. `setup.exe` itself prints nothing; the script reports the
size of `Program Files\Microsoft Office` once a minute, and Microsoft's installer log is written
to `$WINEPREFIX/drive_c/c2rlog/`. Office updates are switched off in the configuration, so Office
never changes under you; updating in place was not tested.

Running `install` means you accept Microsoft's licence terms: the configuration contains
`AcceptEULA="TRUE"`. Read them first if you have not.

### `signin` — let Office use its own sign-in window

Writes two per-user registry values for each Office app
(`HKCU\Software\Microsoft\Office\16.0\Common\ExperimentConfigs\ExternalFeatureOverrides\<app>`):

```
Microsoft.Office.Identity.FG.IsWebView2ForOneAuthEnabled      = true
Microsoft.Office.Identity.TestGate.DisableBrokerForOneAuth    = true
```

Both are Office's own override mechanism; they choose *which* sign-in implementation runs and
touch no licence or entitlement state. The first makes Office use WebView2 for its sign-in page
(by default it falls back to the old browser engine, which renders Microsoft's page as an empty
element). The second tells it not to route sign-in through the Windows account broker, which Wine
cannot fully provide. The names must carry the full `Microsoft.Office.Identity.` prefix — a short
name is silently ignored.

## 4. First start, and signing in

```sh
scripts/office-setup.sh launch word
```

A cold start takes tens of seconds. Word shows a dialog, *Sign in to set up Office* (or Microsoft's
equivalent in your language). **Use the mouse.** Office's own buttons answer "done" to the
accessibility "press" request without doing anything — which makes scripted clicks look like
Office is broken — but a real click works. The same door is *File → Account → Sign in*.

A window titled *Sign in*, about 450×520 pixels, opens. Everything in it is Microsoft's page:

1. enter the email address of the account that holds the subscription;
2. password, and the second factor if the account has one;
3. if Microsoft asks anything else (a consent screen, an offer to remember the account), answer
   it as you would on Windows;
4. the window closes; Word shows its Start screen.

Check *File → Account*: the product should be listed as activated, and the account is yours. Close
Word normally (*File → Exit*) — an unclean exit makes Office offer Safe Mode next time. Sign-in is
stored in the prefix (sealed with Wine's own DPAPI keys) and survives restarts; you do it once per
prefix.

If something goes wrong, the thing to read is Office's own diagnostic log, a tab-separated text
file in `$WINEPREFIX/drive_c/users/<you>/AppData/Local/Temp/Diagnostics/WINWORD/`. It names the
failing step in plain words (`FullValidation`, `Entitlement`, `OneAuth…`). **It also contains your
account identifiers; redact it before sharing.**

## 5. Day to day

* Start apps with `scripts/office-setup.sh launch word|excel|powerpoint`, or run
  `$WINEPREFIX/drive_c/Program Files/Microsoft Office/root/Office16/WINWORD.EXE` with your Wine.
  Office is single-instance: a second launch hands the request to the instance already running and
  exits. If an instance is stuck on a display you are not looking at, clicking again does nothing;
  `wineserver -k` (with the same `WINEPREFIX`) clears it.
* Menu entries are not created; make a launcher that runs the command above.
* After installing a different Wine build, the prefix updates itself the next time it starts
  (a minute or so). Do not start Office while that is running.
* Printing goes through CUPS and Wine's PostScript driver. For printers without a duplexer there is
  a manual-duplex path (patches 0030, 0042 and 0060 in the series; verified on captured print
  output, not yet on paper). `tools/cups-manual-duplex` configures a CUPS queue for it; its README
  says exactly what it changes.

## 6. When it does not work

| what you see | likely cause and what to do |
|---|---|
| The sign-in window is blank, or opens and closes at once | The gates are not set or the WebView2 runtime is missing: `scripts/office-setup.sh status`, then `signin` / `webview2`. Or the Wine in use is not this build (the broker patch is missing) |
| Office says *your account can't be accessed* and offers no sign-in form | Sign-in data copied from another machine: `…/AppData/Local/Microsoft/OneAuth` and `IdentityCache` are sealed with that machine's keys. Move both directories aside and start Word again. Never copy them between machines |
| You sign in and Office says the subscription does not include this product | The installed product does not match the subscription. Start over in a new prefix with the right `OFFICE_PRODUCT` |
| A dialog says the product's licence cannot be verified and has only *OK* | Office is not licensed yet. Pressing *OK* **quits Office**, as it does on Windows. Start it again and sign in from the first-run dialog or *File → Account*. While that dialog is up it holds the keyboard, so do not type into the document behind it |
| Word offers *Safe Mode* when it starts | The last exit was not clean. Answer *No* (the *Yes/No* buttons are an ordinary Windows message box) |
| Clicking an icon does nothing | A previous instance is hidden or stuck: `wineserver -k`, then start again |
| `setup.exe` exits with a number other than 0 | Read the last `Error` line in `$WINEPREFIX/drive_c/c2rlog/*.log`. Disk space (10 GB) and a Wine that is not this build are the usual causes. Run `install` once more; if it fails the same way, start from a fresh prefix |
| PowerPoint stops with *not enough memory or system resources* | PowerPoint loaded Wine's small `riched20` instead of the one Office brought. `wine reg add 'HKCU\Software\Wine\DllOverrides' /v riched20 /t REG_SZ /d native,builtin /f`. This was measured on an Office copied from Windows, not on a fresh install; if it helps you, the notes would like to know |
| WebView2 windows are solid black | You are on a display without a GPU (Xvfb). Use a real X11 or Xwayland session |
| Clicks, typing or scripted input do not reach Office | Scripts that press buttons through accessibility APIs do not work on Office's own controls; use real input (a mouse, or `xdotool` on X11) |

## 7. Starting over

Close Office, then `wineserver -k`, then remove the prefix: `rm -rf "$WINEPREFIX"`. Nothing else
was changed on your system apart from the downloads kept in `~/.cache/wine-altars/` and
`~/.cache/wine/` (Wine's own add-on cache), which are safe to delete.

## 8. Where to read more

* [`office365-under-wine.md`](office365-under-wine.md) — the lab notebook: every measurement, in order,
  including the wrong turns. Long. Search it by symptom.
* [`method.md`](method.md) — the instruments, and the traps already hit.
* [`../patches/altars-up/README.md`](../patches/altars-up/README.md) — the patch series.
