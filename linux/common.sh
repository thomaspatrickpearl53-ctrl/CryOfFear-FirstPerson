# Shared by install.sh / uninstall.sh / remove_everything.sh (Linux, Steam Deck).
# Cry of Fear runs through Steam's Proton (or Wine); the mod's DLLs run inside it.

MOD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

steam_roots() {
  local r
  for r in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.steam/root" \
           "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam" \
           "$HOME/.var/app/com.valvesoftware.Steam/data/Steam" \
           "$HOME/snap/steam/common/.local/share/Steam"; do
    [ -d "$r/steamapps" ] && echo "$r"
  done
}

steam_libraries() {
  local root vdf
  steam_roots | while read -r root; do
    echo "$root"
    vdf="$root/steamapps/libraryfolders.vdf"
    [ -f "$vdf" ] && sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)".*/\1/p' "$vdf"
  done | awk '!seen[$0]++'
}

is_game() { [ -f "$1/cryoffear/cl_dlls/client.dll" ]; }

# Sets GAME from: the argument, this folder's parent (mod folder inside the game),
# or the Steam libraries. Asks if none of those work.
find_game() {
  GAME=""
  if [ -n "$1" ] && is_game "$1"; then GAME="$(cd "$1" && pwd)"; return 0; fi
  if is_game "$MOD_DIR/.."; then GAME="$(cd "$MOD_DIR/.." && pwd)"; return 0; fi
  local lib
  while read -r lib; do
    if is_game "$lib/steamapps/common/Cry of Fear"; then GAME="$lib/steamapps/common/Cry of Fear"; return 0; fi
  done < <(steam_libraries)
  echo "Couldn't find Cry of Fear."
  read -r -p "Paste the path of your Cry of Fear folder (the one with cof.exe): " GAME
  is_game "$GAME" || { echo "\"$GAME\" doesn't look like a Cry of Fear folder."; return 1; }
}

game_running() {
  pgrep -fi 'cof\.exe|coflaunchapp\.exe' >/dev/null 2>&1
}

python_cmd() {
  local p
  for p in python3 python; do
    if command -v "$p" >/dev/null 2>&1 && "$p" -c 'import hashlib' >/dev/null 2>&1; then
      command -v "$p"; return 0
    fi
  done
}
