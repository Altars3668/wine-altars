#!/bin/bash
# 在私有前缀串行运行一个探针，保留实际 PE 退出码；绝不退回真实桌面。
set -euo pipefail
[[ $# -ge 1 ]] || { printf 'usage: %s probe.exe [arguments...]\n' "$0" >&2; exit 2; }
R=$(dirname "$(dirname "$(dirname "$(realpath "$0")")")")
B="$R/wine-src-up/build-wow64"
wine=${PROBE_WINE:-$B/wine}
server=${PROBE_WINESERVER:-$B/server/wineserver}
display=${PROBE_DISPLAY:-:77}
case "$display" in :77|:77.0|:2|:2.0) ;; *) printf 'unsupported_test_display=%s\n' "$display" >&2; exit 2;; esac
timeout 5 env DISPLAY="$display" XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}" xdpyinfo >/dev/null 2>&1 || exit 2
exe=$(realpath "$1")
shift
[[ -f "$exe" && -x "$wine" && -x "$server" ]] || exit 2
# 复用目录必须是专用探针目录，不可传入日常前缀或其他会话目录。
D=${PROBE_WORKDIR:-$(mktemp -d "${TMPDIR:-/tmp}/wine-prefixupdate.XXXXXX")}
mkdir -p "$D"
D=$(realpath "$D")
exec 9>"$D/runner.lock"
flock -n 9 || { printf 'probe_directory_busy=1\n' >&2; exit 2; }
mkdir -p "$D/home" "$D/tmp" "$D/xdg-data" "$D/xdg-config" "$D/xdg-cache"
E=(env -i HOME="$D/home" USER="${USER:-user}" PATH=/usr/bin:/bin
   WINEPREFIX="$D/pfx" DISPLAY="$display" XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}"
   WAYLAND_DISPLAY=wine-altars-no-wayland XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
   WINEDLLOVERRIDES=mscoree= WINEDEBUG="${PROBE_DEBUG:--all}"
   TMPDIR="$D/tmp" TMP="$D/tmp" TEMP="$D/tmp"
   XDG_DATA_HOME="$D/xdg-data" XDG_CONFIG_HOME="$D/xdg-config" XDG_CACHE_HOME="$D/xdg-cache")
cleanup()
{
    saved=$?
    trap - EXIT
    set +e
    timeout --kill-after=2s 5s "${E[@]}" "$server" -k >"$D/cleanup.log" 2>&1
    kill_rc=$?
    timeout --kill-after=2s 10s "${E[@]}" "$server" -w >>"$D/cleanup.log" 2>&1
    wait_rc=$?
    # -k 的 1 也可能只是没有运行中的 server；-w 成功且无错误日志才接受。
    if [[ $saved == 0 && ( $kill_rc -gt 1 || $wait_rc != 0 || ( $kill_rc == 1 && -s "$D/cleanup.log" ) ) ]]; then saved=1; fi
    printf 'PE-EXIT %s\nCLEANUP-EXIT %s %s\nRUNNER-EXIT %s\n' "${pe_rc:-not_run}" "$kill_rc" "$wait_rc" "$saved"
    # 留下证据和可复用的私有前缀，不自动删除传入目录。
    printf 'probe_workdir=%s\n' "$D" >&2
    exit "$saved"
}
trap cleanup EXIT
# kill_lock_owner 没有发现运行中的 server 时返回 0，-k 因而退出 1。
# 只接受无错误日志的这一情况，实际失败和超时仍然停止；-w 必须成功。
set +e
timeout --kill-after=2s 5s "${E[@]}" "$server" -k >"$D/start-cleanup.log" 2>&1
start_kill_rc=$?
set -e
[[ $start_kill_rc -le 1 && ( $start_kill_rc != 1 || ! -s "$D/start-cleanup.log" ) ]] || exit "$start_kill_rc"
timeout --kill-after=2s 10s "${E[@]}" "$server" -w >>"$D/start-cleanup.log" 2>&1
timeout --kill-after=2s 90s "${E[@]}" "$wine" wineboot -u >"$D/wineboot.log" 2>&1 </dev/null
set +e
timeout --kill-after=5s 120s env -C "$D" "${E[@]}" "$wine" "$exe" "$@" </dev/null
pe_rc=$?
set -e
exit "$pe_rc"
