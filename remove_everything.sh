#!/usr/bin/env bash
# Completely removes the first-person mod (Linux): original client.dll / hl.dll
# back, the graphics fix undone, the mod's settings, logs, generated models and
# copied maps deleted, and its lines removed from config.cfg. Saves are kept.
#   ./remove_everything.sh ["/path/to/Cry of Fear"]      (FP_YES=1 skips the question)
set -u
. "$(dirname "$0")/linux/common.sh"
find_game "${1:-}" || exit 1
if game_running; then echo "Cry of Fear is running. Close it first, then run this again."; exit 1; fi
CF="$GAME/cryoffear"; CL="$CF/cl_dlls"

echo "This removes the first-person mod and ALL of its settings from:"
echo "  $GAME"
echo "Your saves and the game's own settings stay."
if [ -z "${FP_YES:-}" ]; then
  read -r -p "Continue? [y/N] " yn
  case "$yn" in [yY]*) ;; *) exit 0 ;; esac
fi

# 1. The game's original DLLs
if [ -f "$CL/client_cof.dll" ]; then mv -f "$CL/client_cof.dll" "$CL/client.dll"
elif [ -f "$CL/client.dll.original" ]; then cp -f "$CL/client.dll.original" "$CL/client.dll"; fi
[ -f "$CL/client_cof.dll.beforeglfix" ] && mv -f "$CL/client_cof.dll.beforeglfix" "$CL/client.dll.beforeglfix"
rm -f "$CL/client.dll.original"
if [ -f "$CL/hl_cof.dll" ]; then mv -f "$CL/hl_cof.dll" "$CL/hl.dll"
elif [ -f "$CL/hl.dll.original" ]; then cp -f "$CL/hl.dll.original" "$CL/hl.dll"; fi
rm -f "$CL/hl.dll.original"
PY="$(python_cmd)"
[ -n "$PY" ] && "$PY" "$MOD_DIR/tools/cof_glfix.py" "$GAME" --undo
echo " - original client.dll, hl.dll and engine files restored"

# 2. Files the mod created
rm -f "$CF/fpbody.cfg" "$CF/fpbody.cfg.bak" "$CF/fpbody.log" "$CF/fpcheats.cfg" "$CF/fpmaps_games.txt"
rm -rf "$CF/models/fpbody"
if [ -f "$CF/fpmaps_installed.txt" ]; then
  while IFS= read -r f; do
    f="${f%$'\r'}"; f="${f//\\//}"
    [ -n "$f" ] && rm -f "$CF/$f"
  done < "$CF/fpmaps_installed.txt"
  rm -f "$CF/fpmaps_installed.txt"
  echo " - maps copied in from other games removed"
fi
echo " - mod settings, log and generated models deleted"

# 3. The mod's lines in the game's config files
for c in "$GAME/config.cfg" "$CF/config.cfg"; do
  [ -f "$c" ] && sed -i -E '/^(cl_fp|cl_pp|fp_unlock_cheats)/Id; /pp_toggle|ssao_toggle|fp_menu/d' "$c"
done
echo " - mod settings and binds removed from config.cfg"
echo
echo "Done. Cry of Fear is back to the original."
