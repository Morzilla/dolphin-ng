#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# dolphin-ng: download, build and install everything.
#
#   ng/install.sh                 from a clone of the repository
#   curl -fsSL https://raw.githubusercontent.com/Morzilla/dolphin-ng/ng/ng/install.sh | bash
#                                  on its own: clones the repository into ~/.local/src/dolphin-ng first
#
# What it does, in order:
#   1. installs the build dependencies with your package manager (asks for sudo)
#   2. builds the fork and installs it into ~/.local/dolphin-ng (your distribution's Dolphin is left alone)
#   3. adds a "Dolphin NG" entry to the application menu and a dolphin-ng command in ~/.local/bin
#   4. installs the Motto-Neo theme files (Kvantum theme, window decoration, colour scheme)
#
# Options:
#   --no-deps       skip step 1
#   --no-theme      skip step 4
#   --apply-theme   also switch your desktop to the Motto-Neo theme (it changes how every application looks)
#   --prefix DIR    install into DIR instead of ~/.local/dolphin-ng
#   --uninstall     remove what steps 2 and 3 installed
set -euo pipefail

REPO_URL="https://github.com/Morzilla/dolphin-ng.git"
PREFIX="${DOLPHIN_NG_PREFIX:-$HOME/.local/dolphin-ng}"
DEPS=1 THEME=1 APPLY=0 UNINSTALL=0
while [ $# -gt 0 ]; do
    case "$1" in
        --no-deps) DEPS=0 ;;
        --no-theme) THEME=0 ;;
        --apply-theme) APPLY=1 ;;
        --prefix) PREFIX="$2"; shift ;;
        --uninstall) UNINSTALL=1 ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
say() { printf '\n==> %s\n' "$*"; }

DESKTOP="$HOME/.local/share/applications/dolphin-ng.desktop"
COMMAND="$HOME/.local/bin/dolphin-ng"

if [ "$UNINSTALL" = 1 ]; then
    say "removing $PREFIX, the menu entry and the dolphin-ng command"
    rm -rf "$PREFIX" "$DESKTOP" "$COMMAND"
    echo "Done. Settings (dolphinrc, group DolphinNG) and the theme files were left in place."
    exit 0
fi

# ---- where is the source?
if [ -f "$(dirname "${BASH_SOURCE[0]:-/nonexistent}")/../NG.md" ]; then
    ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
else
    ROOT="$HOME/.local/src/dolphin-ng"
    if ! command -v git >/dev/null; then # many desktops do not ship git: install it with the package manager
        say "installing git to download the source (sudo)"
        if command -v dnf >/dev/null; then sudo dnf install -y git
        elif command -v apt-get >/dev/null; then sudo apt-get update && sudo apt-get install -y git
        elif command -v pacman >/dev/null; then sudo pacman -S --needed --noconfirm git
        elif command -v zypper >/dev/null; then sudo zypper --non-interactive install git
        fi
        command -v git >/dev/null || { echo "git is needed to download the source; install it and run this again." >&2; exit 1; }
    fi
    if [ -d "$ROOT/.git" ]; then
        # the installer's own copy: brought to the branch's last commit whatever happened to it (a "pull --ff-only" that
        # failed left the old copy, and the old version was built again)
        say "updating the source in $ROOT"
        if git -C "$ROOT" fetch --depth 1 origin ng && git -C "$ROOT" reset --hard FETCH_HEAD; then
            say "source at $(git -C "$ROOT" log -1 --format='%h (%cd)' --date=format:'%Y-%m-%d %H:%M')"
        else
            say "WARNING: could not update the source; building the copy already there, $(git -C "$ROOT" log -1 --format='%h (%cd)' --date=format:'%Y-%m-%d %H:%M')"
        fi
    else
        say "downloading the source into $ROOT"
        mkdir -p "$(dirname "$ROOT")"
        git clone --depth 1 --branch ng "$REPO_URL" "$ROOT"
    fi
fi

# ---- 1. build dependencies
# Every dnf/apt transaction is simulated first. If it would remove packages, downgrade them, or upgrade the desktop
# itself (Plasma, KWin, KDE Frameworks, Qt, the display stack) it is not run: you see what would change and decide
# (default: no). On Fedora, installing a -devel package once pulled a whole newer Plasma and removed the packages
# of the running X11 session.
CORE='^(plasma|kwin|kf6-|libkf6|kf5-|qt6|libqt6|qt5|libqt5|libplasma|kdecoration|libkdecorations|libkworkspace|kscreen|powerdevil|sddm|breeze|kde-|xorg|xwayland|mesa|systemd|glibc|dbus)'
safe_pkgs() { # dnf|apt-get, command and packages (without -y)
    local manager="$1" sim changes answer
    shift
    if [ "$manager" = dnf ]; then
        sim=$(LC_ALL=C sudo dnf "$@" --assumeno 2>&1 || true)
        changes=$(awk -v core="$CORE" '
            /^[A-Z][A-Za-z ]*:$/ { sec = $0; next }
            /^$/ { sec = ""; next }
            /^ [^ ]/ && sec != "" {
                if (sec ~ /^(Removing|Downgrading|Replacing)/) print $1 "  " tolower(substr(sec, 1, length(sec) - 1))
                else if (sec ~ /^Upgrading/ && $1 ~ core) print $1 "  upgrading"
            }' <<<"$sim")
    else
        sim=$(LC_ALL=C sudo apt-get -s "$@" 2>&1 || true)
        changes=$(awk -v core="$CORE" '/^Remv / { print $2 "  removing" } /^Inst [^ ]+ \[/ && $2 ~ core { print $2 "  upgrading" }' <<<"$sim")
    fi
    if [ -n "$changes" ]; then
        echo "  !! \"$manager $*\" would change packages of the desktop itself:" >&2
        printf '%s\n' "$changes" | head -n 25 | sed 's/^/       /' >&2
        echo "  It is safer to update the whole system first (then run this again), or to install by hand." >&2
        answer=""
        { read -r -p "  Go on with this anyway? [y/N] " answer </dev/tty; } 2>/dev/null || true
        case "$answer" in y*|Y*|s*|S*) ;; *) echo "  skipped: $manager $*" >&2; return 1 ;; esac
    fi
    if [ "$manager" = dnf ]; then sudo dnf "$@" -y; else sudo apt-get "$@" -y; fi
}

if [ "$DEPS" = 1 ]; then
    say "installing the build dependencies (sudo)"
    if command -v dnf >/dev/null; then
        # A partly updated system can end up with mixed KDE Frameworks versions: update first if in doubt.
        safe_pkgs dnf install git cmake gcc-c++ extra-cmake-modules 'dnf-command(builddep)' poppler-utils qt6-qtmultimedia-devel ImageMagick || true
        # ffmpeg-free conflicts with the RPM Fusion ffmpeg many people have
        command -v ffmpeg >/dev/null || safe_pkgs dnf install ffmpeg-free || true
        safe_pkgs dnf builddep dolphin || { echo "Dolphin's build dependencies were not installed." >&2; exit 1; }
        [ "$THEME" = 1 ] && safe_pkgs dnf install kvantum || true
    elif command -v apt-get >/dev/null; then
        sudo apt-get update
        safe_pkgs apt-get install git cmake g++ extra-cmake-modules poppler-utils qt6-multimedia-dev imagemagick ffmpeg || true
        # Dolphin's build dependencies by name (from Debian's dolphin package), so the source repositories (deb-src),
        # off by default, are not needed; apt build-dep only as a fallback
        safe_pkgs apt-get install gettext qt6-base-dev qt6-base-private-dev qt6-multimedia-dev libbaloowidgets-dev \
            libkf6baloo-dev libkf6bookmarks-dev libkf6codecs-dev libkf6colorscheme-dev libkf6completion-dev \
            libkf6config-dev libkf6coreaddons-dev libkf6crash-dev libkf6dbusaddons-dev libkf6filemetadata-dev \
            libkf6guiaddons-dev libkf6i18n-dev libkf6iconthemes-dev libkf6kcmutils-dev libkf6kio-dev libkf6newstuff-dev \
            libkf6notifications-dev libkf6parts-dev libkf6solid-dev libkf6textwidgets-dev libkf6widgetsaddons-dev \
            libkf6windowsystem-dev \
        || safe_pkgs apt-get build-dep dolphin || {
            echo "apt could not install Dolphin's build dependencies (see above). Your distribution may be too old:" >&2
            echo "Dolphin 26.08 needs KDE Frameworks 6.23 or newer." >&2
            exit 1
        }
        # optional parts of Dolphin: built without them if missing
        for optional in libkf6doctools-dev libkf6userfeedback-dev libpackagekitqt6-dev; do
            safe_pkgs apt-get install "$optional" || true
        done
        [ "$THEME" = 1 ] && safe_pkgs apt-get install qt6-style-kvantum || true
    elif command -v pacman >/dev/null; then
        sudo pacman -S --needed --noconfirm git cmake extra-cmake-modules base-devel poppler qt6-multimedia qt6-multimedia-ffmpeg imagemagick ffmpeg \
            kio kparts knewstuff kcmutils kdbusaddons kiconthemes ktextwidgets kcrash kbookmarks kcompletion \
            kwindowsystem kdoctools baloo baloo-widgets kfilemetadata kuserfeedback phonon-qt6 plasma-activities
        [ "$THEME" = 1 ] && sudo pacman -S --needed --noconfirm kvantum || true
    elif command -v zypper >/dev/null; then
        sudo zypper --non-interactive install git cmake gcc-c++ extra-cmake-modules poppler-tools qt6-multimedia-devel ImageMagick ffmpeg || true
        # what Dolphin's CMakeLists asks for, as cmake() capabilities: no source repositories needed
        sudo zypper --non-interactive install 'cmake(Qt6Core)' 'cmake(Qt6Concurrent)' 'cmake(Qt6Widgets)' 'cmake(Qt6Gui)' \
            'cmake(Qt6DBus)' 'cmake(Qt6GuiPrivate)' 'cmake(Qt6Multimedia)' 'cmake(Qt6MultimediaWidgets)' \
            'cmake(KF6KCMUtils)' 'cmake(KF6NewStuff)' 'cmake(KF6CoreAddons)' 'cmake(KF6I18n)' 'cmake(KF6DBusAddons)' \
            'cmake(KF6Bookmarks)' 'cmake(KF6Config)' 'cmake(KF6KIO)' 'cmake(KF6Parts)' 'cmake(KF6Solid)' \
            'cmake(KF6IconThemes)' 'cmake(KF6Completion)' 'cmake(KF6TextWidgets)' 'cmake(KF6Notifications)' \
            'cmake(KF6Crash)' 'cmake(KF6WindowSystem)' 'cmake(KF6WidgetsAddons)' 'cmake(KF6Codecs)' 'cmake(KF6GuiAddons)' \
            'cmake(KF6ColorScheme)' 'cmake(KF6FileMetaData)' 'cmake(KF6Baloo)' 'cmake(KF6BalooWidgets)' || {
            echo "zypper could not install Dolphin's build dependencies (see above)." >&2
            exit 1
        }
        # optional parts of Dolphin: built without them if missing
        for optional in 'cmake(KF6DocTools)' 'cmake(KF6UserFeedback)' 'cmake(PackageKitQt6)'; do
            sudo zypper --non-interactive install "$optional" || true
        done
        [ "$THEME" = 1 ] && sudo zypper --non-interactive install kvantum-qt6 || true
    else
        echo "I do not know this distribution's package manager. Install Dolphin's build dependencies yourself, then run this again with --no-deps." >&2
        exit 1
    fi
fi

# ---- 2. build and install
say "building (this takes a few minutes)"
DOLPHIN_NG_PREFIX="$PREFIX" "$ROOT/ng/build.sh"

# ---- 3. menu entry and command
say "adding the menu entry and the dolphin-ng command"
mkdir -p "$(dirname "$DESKTOP")" "$(dirname "$COMMAND")"
cat > "$COMMAND" <<LAUNCH
#!/bin/sh
DOLPHIN_NG_PREFIX="$PREFIX" exec "$ROOT/ng/dolphin-ng" "\$@"
LAUNCH
chmod +x "$COMMAND"
# speech to text for the "Transcribe" quick action (needs "ng-transcribe --setup" once)
ln -sf "$ROOT/ng/ng-transcribe" "$(dirname "$COMMAND")/ng-transcribe"
# quick actions run through it: they show in Plasma's notifications, also with Dolphin NG closed
ln -sf "$ROOT/ng/ng-job" "$(dirname "$COMMAND")/ng-job"
cat > "$DESKTOP" <<ENTRY
[Desktop Entry]
Type=Application
Name=Dolphin NG
GenericName=File Manager
Comment=Experimental fork of Dolphin with a different file list
Exec=$COMMAND %u
Icon=dolphin-ng
Terminal=false
Categories=Qt;KDE;System;FileTools;FileManager;
MimeType=inode/directory;
StartupWMClass=dolphin
ENTRY
install -Dm644 "$ROOT/ng/icons/dolphin-ng.svg" "$HOME/.local/share/icons/hicolor/scalable/apps/dolphin-ng.svg"
if [ ! -e "$HOME/.local/share/kxmlgui5/dolphin/dolphinui.rc" ]; then
    mkdir -p "$HOME/.local/share/kxmlgui5/dolphin"
    cp "$ROOT/ng/dolphinui.rc.example" "$HOME/.local/share/kxmlgui5/dolphin/dolphinui.rc" # toolbar with New Folder, Up and Root
fi

# ---- 4. theme
if [ "$THEME" = 1 ]; then
    say "installing the Motto-Neo theme files"
    mkdir -p "$HOME/.config/Kvantum" "$HOME/.local/share/aurorae/themes" "$HOME/.local/share/color-schemes"
    cp -r "$ROOT/ng/theme/kvantum/Motto-Neo" "$HOME/.config/Kvantum/"
    cp -r "$ROOT/ng/theme/aurorae/Motto-Neo-Aurorae" "$HOME/.local/share/aurorae/themes/"
    cp "$ROOT/ng/theme/color-schemes/MottoNeo.colors" "$HOME/.local/share/color-schemes/"
    if [ "$APPLY" = 1 ]; then
        say "switching the desktop to Motto-Neo"
        command -v kvantummanager >/dev/null && kvantummanager --set Motto-Neo || echo "kvantummanager not found: choose Motto-Neo in Kvantum Manager yourself."
        command -v kwriteconfig6 >/dev/null && kwriteconfig6 --file kdeglobals --group KDE --key widgetStyle kvantum-dark || true
        command -v plasma-apply-colorscheme >/dev/null && plasma-apply-colorscheme MottoNeo || true
        if command -v kwriteconfig6 >/dev/null; then
            kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key library org.kde.kwin.aurorae.v2
            kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key theme __aurorae__svg__Motto-Neo-Aurorae
            dbus-send --session --type=method_call --dest=org.kde.KWin /KWin org.kde.KWin.reconfigure 2>/dev/null || true
        fi
    fi
fi

say "done"
echo "Start it from the application menu (Dolphin NG) or with: dolphin-ng"
case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *) echo "(~/.local/bin is not in your PATH: use $COMMAND)" ;; esac
[ "$THEME" = 1 ] && [ "$APPLY" = 0 ] && echo "The theme files are installed but not active: run this again with --apply-theme, or pick Motto-Neo in Kvantum Manager, in the colour schemes and in the window decorations."
echo "Settings: Configure Dolphin > NG."
