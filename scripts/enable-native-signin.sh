#!/usr/bin/env bash
# Turn on the sign-in path Office already ships with.
#
# Two feature gates decide how Office signs a user in, and both default the
# wrong way for this prefix:
#
#   IsWebView2ForOneAuthEnabled   off -> Office never tries WebView2, falls back
#                                 to mshtml, and the provider's sign-in page is
#                                 a script-built single-page app that the
#                                 bundled engine leaves as an empty <div>.
#   TestGate.DisableBrokerForOneAuth
#                                 off -> sign-in is routed through the broker,
#                                 which cannot produce the RPS tickets Office
#                                 wants, and every failure is reported as a
#                                 broker error instead of falling back.
#
# Neither touches licence or entitlement state: they select which of Office's
# own sign-in implementations runs.  The names must carry their full prefix --
# "TestGate.DisableBrokerForOneAuth" without it is silently ignored.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The caller usually resolved which wine runs this prefix; don't second-guess it.
WINE="${WINE:-${DIST:-$ROOT/dist-cx}/bin/wine}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-altars-office}"

KEY='HKCU\Software\Microsoft\Office\16.0\Common\ExperimentConfigs\ExternalFeatureOverrides\word'

for gate in \
    'Microsoft.Office.Identity.FG.IsWebView2ForOneAuthEnabled' \
    'Microsoft.Office.Identity.TestGate.DisableBrokerForOneAuth'
do
    WINEDEBUG=-all "$WINE" reg add "$KEY" /v "$gate" /t REG_SZ /d true /f /reg:64 >/dev/null 2>&1
    echo "enabled $gate"
done

echo
echo "Verify from Office's own telemetry after the next start: the gate should"
echo 'read "V" : true with "S" : 4 (4 = came from an override, 1 = the default).'
echo "Office should then load WebView2Loader.dll and the sign-in window should"
echo "be Chrome_WidgetWin_1, not Internet Explorer_Server."
