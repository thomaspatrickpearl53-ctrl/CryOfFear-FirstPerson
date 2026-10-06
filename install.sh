#!/usr/bin/env bash
# Installs the Cry of Fear first-person mod on Linux / Steam Deck (the game runs
# through Steam's Proton). Same as install.bat, plus the Wine/Proton graphics
# fix for the original engine (tools/cof_glfix.py, made to your own files).
#
#   ./install.sh                         find the game in your Steam libraries
#   ./install.sh "/path/to/Cry of Fear"  or give the folder
#   ./install.sh reset                   also put back the mod's default settings
#   ./install.sh --no-glfix              skip the graphics fix
set -u
. "$(dirname "$0")/linux/common.sh"

RESET=""; GLFIX=1; ARG=""
for a in "$@"; do
  case "$a" in
    reset) RESET=1 ;;
    --no-glfix) GLFIX="" ;;
    *) ARG="$a" ;;
  esac
done
ENGINES_URL="https://github.com/thomaspatrickpearl53-ctrl/CryOfFear-FirstPerson/blob/main/docs/ENGINES.md"

find_game "$ARG" || exit 1
if game_running; then echo "Cry of Fear is running. Close it first, then run this again."; exit 1; fi

CL="$GAME/cryoffear/cl_dlls"
FIRST=""
if [ ! -f "$CL/client_cof.dll" ]; then
  # First install: keep the original client twice - as the forward target and as a backup.
  FIRST=1
  cp -f "$CL/client.dll" "$CL/client.dll.original" && mv -f "$CL/client.dll" "$CL/client_cof.dll" || { echo "Install failed."; exit 1; }
fi
cp -f "$MOD_DIR/bin/client.dll" "$CL/client.dll" || { echo "Install failed."; exit 1; }

# Cry of Fear: Enhanced (Xash3D) brings its own cheats and renderer.
ENGINE=original
[ -f "$GAME/xash.dll" ] && ENGINE=enhanced
if [ "$ENGINE" = original ]; then
  if [ -f "$MOD_DIR/bin/hl.dll" ]; then
    if [ ! -f "$CL/hl_cof.dll" ]; then
      cp -f "$CL/hl.dll" "$CL/hl.dll.original" && mv -f "$CL/hl.dll" "$CL/hl_cof.dll" || { echo "Install failed."; exit 1; }
    fi
    cp -f "$MOD_DIR/bin/hl.dll" "$CL/hl.dll" || { echo "Install failed."; exit 1; }
  fi
elif [ -f "$CL/hl_cof.dll" ]; then
  mv -f "$CL/hl_cof.dll" "$CL/hl.dll" && echo "Restored the original hl.dll so Cry of Fear: Enhanced's cheats work."
fi

# Settings: on first install (or "reset"), use the mod's defaults. Later updates keep yours.
[ -n "$FIRST" ] && RESET=1
[ -f "$GAME/cryoffear/fpbody.cfg" ] || RESET=1
if [ -n "$RESET" ]; then
  [ -f "$GAME/cryoffear/fpbody.cfg" ] && cp -f "$GAME/cryoffear/fpbody.cfg" "$GAME/cryoffear/fpbody.cfg.bak"
  cp -f "$MOD_DIR/config/fpbody.cfg" "$GAME/cryoffear/fpbody.cfg" && echo "Default settings applied."
fi

if [ "$ENGINE" = original ] && [ -n "$GLFIX" ]; then
  PY="$(python_cmd)"
  if [ -n "$PY" ]; then
    "$PY" "$MOD_DIR/tools/cof_glfix.py" "$GAME"
  else
    echo "Python 3 not found: skipped the graphics fix (install python3, then run this again)."
  fi
fi

echo
echo "Installed into \"$GAME\"."
if [ "$ENGINE" = enhanced ]; then
  echo "Engine: Cry of Fear: Enhanced (Xash3D). The F8 menu uses Enhanced's cheats."
else
  echo "Engine: original Cry of Fear. The F8 menu uses this mod's cheats."
fi
echo "Start Cry of Fear from Steam with Proton (Properties > Compatibility). To remove the mod, run ./uninstall.sh."
echo "Engine notes: $ENGINES_URL"
