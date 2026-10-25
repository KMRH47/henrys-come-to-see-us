#!/bin/sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
PAYLOAD="$HERE/game"
APP_ID=1771300
OWN_DIRS='Mods/m_carbine m4_reload'
STEAM_BIN=Bin/Win64MasterMasterSteamPGO
OVERRIDE='"dinput8"="native,builtin"'
GAME=${M_CARBINE_GAME_DIR:-}

die() { echo "Error: $*"; exit 1; }

exe_dir() {
    for d in "$1"/Bin/*/; do
        if [ -f "${d}KingdomCome.exe" ]; then basename "$d"; return 0; fi
    done
    return 1
}

game_running() {
    for f in /proc/[0-9]*/cmdline; do
        case $({ tr '\0' ' ' < "$f"; } 2>/dev/null || true) in *KingdomCome.exe*) return 0;; esac
    done
    return 1
}

find_game() {
    if [ -n "$GAME" ]; then
        exe_dir "$GAME" > /dev/null || die "no Bin/.../KingdomCome.exe under $GAME"
        return
    fi
    for root in "$HOME/.steam/steam" "$HOME/.local/share/Steam" "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam" "$HOME/snap/steam/common/.local/share/Steam"; do
        vdf="$root/steamapps/libraryfolders.vdf"
        [ -f "$vdf" ] || continue
        while IFS= read -r library; do
            if [ -n "$library" ] && exe_dir "$library/steamapps/common/KingdomComeDeliverance2" > /dev/null; then
                GAME="$library/steamapps/common/KingdomComeDeliverance2"
                return
            fi
        done <<EOF
$(sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' "$vdf")
EOF
    done
    while :; do
        if command -v zenity > /dev/null 2>&1; then
            picked=$(zenity --file-selection --directory --title="Pick the KingdomComeDeliverance2 game folder" 2>/dev/null || true)
        else
            printf 'Paste the KingdomComeDeliverance2 game folder (the one holding Bin and Mods): '
            IFS= read -r picked || picked=""
        fi
        [ -n "$picked" ] || die "no game folder picked."
        if exe_dir "$picked" > /dev/null; then GAME=$picked; return; fi
        echo "That folder has no Bin/.../KingdomCome.exe."
    done
}

note() { echo "$1" >> "$RECORD"; }

noted() { sed -n "s/^$1 //p" "$RECORD"; }

has_note() { [ -n "$(sed -n "/^$1\$/p" "$RECORD")" ]; }

make_dir() {
    d=$1
    while [ ! -e "$d" ]; do
        note "created ${d#"$GAME"/}"
        d=$(dirname "$d")
    done
    mkdir -p "$1"
}

add_override() {
    [ -f "$REG" ] || return 0
    tmp=$(mktemp)
    if awk -v add="$OVERRIDE" '
        { line[NR] = $0 }
        /^"dinput8"=/ { found = 1 }
        index($0, "[Software\\\\Wine\\\\DllOverrides]") == 1 && !section { section = NR }
        END {
            if (found) exit 1
            at = section
            if (section && line[section + 1] ~ /^#time=/) at = section + 1
            for (i = 1; i <= NR; i++) { print line[i]; if (i == at) print add }
            if (!section) { print ""; print "[Software\\\\Wine\\\\DllOverrides]"; print add }
        }' "$REG" > "$tmp"; then
        cat "$tmp" > "$REG"
        note override
    fi
    rm -f -- "$tmp"
}

remove_override() {
    [ -f "$REG" ] || return 0
    tmp=$(mktemp)
    awk -v drop="$OVERRIDE" '$0 == drop && !done { done = 1; next } { print }' "$REG" > "$tmp"
    cat "$tmp" > "$REG"
    rm -f -- "$tmp"
}

install_mod() {
    exe=$(exe_dir "$GAME")
    : > "$RECORD"
    for d in $OWN_DIRS; do rm -rf -- "${GAME:?}/$d"; done
    while IFS= read -r rel; do
        case $rel in
            "$STEAM_BIN"/*) placed="Bin/$exe/${rel#"$STEAM_BIN"/}" ;;
            *) placed=$rel ;;
        esac
        target="$GAME/$placed"
        if [ "${placed##*/}" = dinput8.dll ] && [ -e "$target" ]; then
            note "kept $placed"
            echo "Kept your $placed (another ASI loader works too)"
            continue
        fi
        make_dir "$(dirname "$target")"
        cp "$PAYLOAD/$rel" "$target"
        note "installed $placed"
    done <<EOF
$(cd "$PAYLOAD" && find . -type f | sed 's|^\./||' | sort)
EOF
    add_override
}

restore() {
    noted installed | while IFS= read -r p; do rm -f -- "$GAME/$p"; done
    noted created | awk '{ print length($0) "\t" $0 }' | sort -rn | cut -f2- | while IFS= read -r p; do
        rmdir -- "$GAME/$p" 2>/dev/null || true
    done
    if has_note override; then remove_override; fi
    rm -f -- "$RECORD"
}

if game_running; then die "close Kingdom Come: Deliverance II first."; fi
find_game
GAME=$(cd "$GAME" && pwd)
RECORD="$GAME/m_carbine_install.txt"
REG="$(dirname "$(dirname "$GAME")")/compatdata/$APP_ID/pfx/user.reg"
echo "Game folder: $GAME"
if [ -f "$RECORD" ]; then
    printf "Henry's Come to See Us! is installed. Update it (u) or remove it (r)? "
    IFS= read -r answer || answer=""
    case $answer in
        r|R) restore; echo "Henry's Come to See Us! removed."; exit 0 ;;
        u|U) restore ;;
        *) die "nothing changed." ;;
    esac
fi
set +e
(set -e; install_mod)
status=$?
set -e
if [ "$status" -ne 0 ]; then
    echo "Install failed. Removing what it copied."
    restore
    exit 1
fi
echo "Henry's Come to See Us! installed. Run install_linux.sh again to update or remove it."
if [ ! -f "$REG" ]; then
    echo "Start the game once from Steam, then run install_linux.sh again and pick u, so Proton loads the plugin."
fi
