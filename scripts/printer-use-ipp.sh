#!/usr/bin/env bash
# Point a CUPS queue at the printer's own IPP service instead of a rasterising
# driver, so that per-job options survive the trip.
#
# Why this is needed: hpcups renders the job to PCLm and emits no PJL, and it
# drops InputSlot on the floor -- the byte stream it produces is identical with
# and without one.  Nothing downstream can then ask the printer to stop and
# wait for paper, which is what manual duplex is made of.  A driverless queue
# turns the same option into the IPP attribute media-source=manual, which the
# printer does act on: it lights its own "load paper" prompt and holds the job
# until the button is pressed.
#
#   printer-use-ipp.sh <queue> <ip-or-host>     switch the queue to IPP
#   printer-use-ipp.sh --undo <queue> <uri> <ppd>   put back what was there
set -euo pipefail

if [ "${1:-}" = "--undo" ]; then
    [ $# -eq 4 ] || { echo "用法: $0 --undo <队列> <原 device-uri> <原 PPD>" >&2; exit 1; }
    lpadmin -p "$2" -v "$3" -P "$4"
    echo "已还原 $2 -> $3"
    exit 0
fi

[ $# -eq 2 ] || { echo "用法: $0 <队列> <ip 或主机名>" >&2; exit 1; }
queue=$1; host=$2

echo "==> 原配置（还原时要用）"
lpstat -v "$queue" || true
backup="${TMPDIR:-/tmp}/$queue.ppd.bak"
if curl -fsS -o "$backup" "http://localhost:631/printers/$queue.ppd"; then
    echo "    原 PPD 已存到 $backup"
fi

echo "==> 改为 ipp://$host/ipp/print，驱动用 everywhere"
lpadmin -p "$queue" -v "ipp://$host/ipp/print" -m everywhere -E

echo "==> 新配置"
lpstat -v "$queue"
lpoptions -p "$queue" -l | grep -E '^(InputSlot|PageSize)' || true

if ! lpoptions -p "$queue" -l | grep -q '^InputSlot.*Manual'; then
    echo "!! 这台打印机没有 Manual 进纸口，手动双面只能靠屏幕提示" >&2
fi
