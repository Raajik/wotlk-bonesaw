#!/usr/bin/env bash
# Install the Bonesaw Linux launcher for the current user.
#
#   ./install.sh [CLIENT_DIR]
#
# Puts `bonesaw` on PATH, adds a desktop entry with the Bonesaw mark, and
# remembers where the client lives. Everything lands under ~/.local, so no
# root and nothing outside your home directory.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$HOME/.local/bin"
APP_DIR="$HOME/.local/share/applications"
ICON_ROOT="$HOME/.local/share/icons/hicolor"
ICO="$HERE/../assets/bonesaw.ico"

mkdir -p "$BIN_DIR" "$APP_DIR"
install -m 755 "$HERE/bonesaw" "$BIN_DIR/bonesaw"
echo "installed $BIN_DIR/bonesaw"

# The mark is committed as a Windows .ico; split it into the PNG sizes a
# freedesktop icon theme expects. Skipped without ImageMagick -- the launcher
# works fine, the menu entry just falls back to a generic icon.
if command -v magick >/dev/null 2>&1; then
    for i in 0 1 2 3 4 5 6 7; do
        size=$(magick identify -format "%w" "$ICO[$i]" 2>/dev/null) || continue
        dir="$ICON_ROOT/${size}x${size}/apps"
        mkdir -p "$dir"
        magick "$ICO[$i]" -alpha on "$dir/bonesaw.png" 2>/dev/null || true
    done
    command -v gtk-update-icon-cache >/dev/null 2>&1 && \
        gtk-update-icon-cache -f -t "$ICON_ROOT" >/dev/null 2>&1 || true
    echo "installed icons under $ICON_ROOT"
else
    echo "note: ImageMagick not found, skipping icons" >&2
fi

# StartupWMClass lets the bar match the running game window to this entry.
cat > "$APP_DIR/bonesaw.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Bonesaw
Comment=WotLK 3.3.5a private realm
Exec=$BIN_DIR/bonesaw
Icon=bonesaw
Terminal=false
Categories=Game;RolePlaying;
StartupWMClass=wow.exe
EOF
echo "installed $APP_DIR/bonesaw.desktop"
command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database "$APP_DIR" >/dev/null 2>&1 || true

if [[ -n "${1:-}" ]]; then
    "$BIN_DIR/bonesaw" --set-dir "$1"
fi

missing=()
for t in wine wtype secret-tool; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
if (( ${#missing[@]} )); then
    echo
    echo "missing: ${missing[*]}"
    echo "  Arch:            sudo pacman -S wine wtype libsecret"
    echo "  Debian/Ubuntu:   sudo apt install wine wtype libsecret-tools"
    echo "  Fedora:          sudo dnf install wine wtype libsecret"
    echo "(wtype and secret-tool are only needed for auto-login)"
fi

case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *) echo; echo "note: $BIN_DIR is not on your PATH" ;;
esac

echo
echo "Done. Run 'bonesaw' to play, or 'bonesaw --set-login' to set up auto-login."
