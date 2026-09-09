#!/bin/bash
# Run a pixi/CMake build of LibreCAD master headless on Qt's VNC platform, with
# an isolated configuration directory (never touches ~/.config/LibreCAD).
# Usage: run_lc_master.sh <build-variant: hidden|base> <vnc-port> <config-dir> [extra librecad args]
#   LC_SRC     source tree that owns build/<variant>/install (default: src-hidden)
#   LC_PIXI    pixi manifest whose environment provides Qt (default: src-hidden/pixi.toml)
#   LC_SIZE    VNC screen size (default 1800x1400)
#   LC_COMMAND binary under install/bin to run (default librecad)
set -u
VAR="$1"; PORT="$2"; CONF="$3"; shift 3
FORK=~/Documents/LibreCAD-fork
SRC="${LC_SRC:-$FORK/src-hidden}"
case "$VAR" in
  base) SRC="${LC_SRC:-$FORK/src-master-base}" ;;
esac
INST="$SRC/build/$VAR/install"
[ -x "$INST/bin/${LC_COMMAND:-librecad}" ] || { echo "no existe $INST/bin/${LC_COMMAND:-librecad}" >&2; exit 1; }
export PATH="$HOME/.pixi/bin:$PATH"
set +u   # conda activation scripts reference unset variables
eval "$(pixi shell-hook --manifest-path "${LC_PIXI:-$FORK/src-hidden/pixi.toml}" 2>/dev/null)"
set -u
mkdir -p "$CONF/config" "$CONF/data" "$CONF/home" "$CONF/cwd"
export HOME="$CONF/home" XDG_CONFIG_HOME="$CONF/config" XDG_DATA_HOME="$CONF/data"
export QT_QPA_PLATFORM="vnc:port=$PORT:size=${LC_SIZE:-1800x1400}" LC_ALL=C.UTF-8 LANG=C
unset QT_QPA_PLATFORMTHEME QT_STYLE_OVERRIDE
cd "$CONF/cwd"
exec "$INST/bin/${LC_COMMAND:-librecad}" "$@"
