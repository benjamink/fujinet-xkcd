#!/usr/bin/env bash
# Build an OFS test disk (volume NIO) holding the FujiNet NIO drivers and
# tools from the workspace's wb13 artifact set, xkcd, and install scripts for
# Workbench 1.3 and 2.04.
#
# Prerequisites:
#   source "$NIO_WORKSPACE/scripts/env.sh"
#   (cd "$NIO_WORKSPACE" && scripts/amiga-artifacts wb13)
#   make amiga
#
# Usage: tools/make-nio-adf.sh [output.adf]   (default r2r/amiga/xkcd-nio.adf)
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/r2r/amiga/xkcd-nio.adf}

if [ -z "${NIO_WORKSPACE:-}" ]; then
    echo "Set NIO_WORKSPACE (source scripts/env.sh in the workspace)" >&2
    exit 1
fi
nio=$NIO_WORKSPACE/build/amiga-artifacts/wb13/NIO
install_dir=$NIO_WORKSPACE/configs/amiga/install

if command -v xdftool >/dev/null 2>&1; then
    xdftool=(xdftool)
else
    xdftool=(uvx --from amitools xdftool)
fi

nio_files=(
    fujinet-nio.device fujinet-disk.device fujinet-serial.device
    fujinet-load-resident fujinet-mount fujinet-nio-exchange
    fapp fboot fdrive fhost fin fls fmount fumount fout
    config-nio config-nio.info
)

for f in "${nio_files[@]}"; do
    [ -f "$nio/$f" ] || { echo "missing $nio/$f: run scripts/amiga-artifacts wb13" >&2; exit 1; }
done
[ -f "$root/r2r/amiga/xkcd" ] || { echo "missing r2r/amiga/xkcd: run make amiga" >&2; exit 1; }

args=("$out" create + format NIO ofs)
for f in "${nio_files[@]}"; do
    args+=(+ write "$nio/$f" "$f")
done
args+=(+ write "$install_dir/Install-FujiNet-WB13" Install-FujiNet-WB13)
args+=(+ write "$install_dir/MountList-FujiNet-WB13" MountList-FujiNet-WB13)
args+=(+ write "$root/amiga/Install-FujiNet-WB204" Install-FujiNet-WB204)
args+=(+ write "$root/amiga/Run-xkcd" Run-xkcd)
args+=(+ write "$root/r2r/amiga/xkcd" xkcd)
args+=(+ write "$root/amiga/icons/xkcd.info" xkcd.info)
args+=(+ write "$root/amiga/ReadMe.txt" ReadMe)
args+=(+ write "$root/amiga/icons/ReadMe.info" ReadMe.info)
args+=(+ write "$root/amiga/icons/Disk.info" Disk.info)

mkdir -p "$(dirname "$out")"
rm -f "$out"
"${xdftool[@]}" "${args[@]}"
"${xdftool[@]}" "$out" list
