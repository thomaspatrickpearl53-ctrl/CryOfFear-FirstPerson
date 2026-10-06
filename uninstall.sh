#!/usr/bin/env bash
# Removes the first-person mod and restores the game's own client.dll / hl.dll.
# Keeps your mod settings (cryoffear/fpbody.cfg) and the graphics fix; to remove
# those too, use ./remove_everything.sh.
#   ./uninstall.sh ["/path/to/Cry of Fear"]
set -u
. "$(dirname "$0")/linux/common.sh"
find_game "${1:-}" || exit 1
if game_running; then echo "Cry of Fear is running. Close it first, then run this again."; exit 1; fi

CL="$GAME/cryoffear/cl_dlls"
if [ ! -f "$CL/client_cof.dll" ] && [ ! -f "$CL/hl_cof.dll" ]; then
  echo "The mod isn't installed in \"$GAME\"."
  exit 0
fi
[ -f "$CL/client_cof.dll" ] && { mv -f "$CL/client_cof.dll" "$CL/client.dll" || { echo "Couldn't restore client.dll."; exit 1; }; }
[ -f "$CL/hl_cof.dll" ] && { mv -f "$CL/hl_cof.dll" "$CL/hl.dll" || { echo "Couldn't restore hl.dll."; exit 1; }; }
# The graphics fix's backup of the client follows it back to its own name.
[ -f "$CL/client_cof.dll.beforeglfix" ] && mv -f "$CL/client_cof.dll.beforeglfix" "$CL/client.dll.beforeglfix"
echo "Original Cry of Fear restored. Your mod settings stay in cryoffear/fpbody.cfg in case you reinstall."
