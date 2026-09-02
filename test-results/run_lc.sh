#!/bin/bash
# Run a flatpak-builder build of LibreCAD headless on Qt's VNC platform, with an
# isolated configuration directory (never touches ~/.var/app/org.librecad.librecad).
# Usage: run_lc.sh <variant: baseline|fix> <vnc-port> <config-dir> [extra librecad args]
set -u
VAR="$1"; PORT="$2"; CONF="$3"; shift 3
FB=~/Documents/LibreCAD-fork/flatpak
mkdir -p "$CONF/config" "$CONF/data" "$FB/.run"
cd "$FB/.run"
exec flatpak run --app-path="$FB/$VAR/build/files" --share=network --nofilesystem=home \
  --filesystem="$CONF" \
  --env=XDG_CONFIG_HOME="$CONF/config" --env=XDG_DATA_HOME="$CONF/data" \
  --env=QT_QPA_PLATFORM="vnc:port=$PORT:size=${LC_SIZE:-1800x1400}" \
  --env=LC_ALL=C.UTF-8 --env=LANG=C \
  --command="${LC_COMMAND:-librecad}" org.kde.Platform//5.15-25.08 "$@"
