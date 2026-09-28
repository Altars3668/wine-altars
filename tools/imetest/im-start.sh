#!/bin/bash
# im-start.sh <fcitx|ibus> <display> [state directory]
#
# Starts an input method for one X display only, apart from the desktop's own: its own D-Bus session, and its own
# configuration, data and cache under the state directory (default: a new one under $TMPDIR), with pinyin as the input
# method and no cloud lookups.  The XIM server shows up as @server=fcitx or @server=ibus in the display's XIM_SERVERS
# root property; point Wine at it with XMODIFIERS=@im=fcitx or @im=ibus.  fcitx offers only the over-the-spot and root
# styles, ibus also the on-the-spot one (preedit callbacks), which is the one Wine asks for.  Stop it by killing the
# processes whose environment holds this XDG_CONFIG_HOME.
#
# XAUTHORITY is passed on, for an Xwayland display.  The input method's own candidate window is drawn on the display,
# so the display needs no window manager, but see README.md for what a missing one changes.
set -eu
im=$1; display=$2
state=${3:-$(mktemp -d -p "${TMPDIR:-/tmp}" "im-$im-XXXXXX")}
mkdir -p "$state/config" "$state/data" "$state/cache" "$state/run"
chmod 700 "$state/run"
echo "state in $state" >&2
common=(env -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS DISPLAY="$display" XAUTHORITY="${XAUTHORITY:-}"
        XDG_CONFIG_HOME="$state/config" XDG_DATA_HOME="$state/data" XDG_CACHE_HOME="$state/cache"
        XDG_RUNTIME_DIR="$state/run")
case $im in
fcitx)
    mkdir -p "$state/config/fcitx5/conf"
    printf '%s\n' '[Groups/0]' 'Name=Default' 'Default Layout=us' 'DefaultIM=pinyin' '' '[Groups/0/Items/0]' \
        'Name=keyboard-us' 'Layout=' '' '[Groups/0/Items/1]' 'Name=pinyin' 'Layout=' '' '[GroupOrder]' '0=Default' \
        > "$state/config/fcitx5/profile"
    echo 'CloudPinyinEnabled=False' > "$state/config/fcitx5/conf/pinyin.conf"
    exec "${common[@]}" dbus-run-session -- fcitx5 \
        --disable=cloudpinyin,wayland,waylandim,notificationitem,kimpanel,dbusfrontend,ibusfrontend,notifications \
        > "$state/fcitx5.log" 2>&1
    ;;
ibus)
    # the daemon's default socket lives under XDG_CACHE_HOME, and a long path there overflows sun_path
    address="unix:abstract=wine-altars-ibus-$$"
    exec "${common[@]}" IBUS_ADDRESS="$address" dbus-run-session -- bash -c '
        gsettings set org.freedesktop.ibus.general preload-engines "[\"libpinyin\"]"
        gsettings set org.freedesktop.ibus.general engines-order "[\"libpinyin\"]"
        ibus-daemon --panel=/usr/libexec/ibus-ui-gtk3 --address="$IBUS_ADDRESS" &
        sleep 5
        ibus engine libpinyin
        /usr/libexec/ibus-x11 &
        wait' > "$state/ibus.log" 2>&1
    ;;
*)
    echo "usage: $0 <fcitx|ibus> <display> [state directory]" >&2
    exit 2
    ;;
esac
