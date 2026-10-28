#!/usr/bin/env bash
# Rebuild the bits that embed this Hyprland's commit hash.
#
# `ninja install` and `cmake --install` install the compositor and headers,
# then stop. Plugins and screen-shadow-omit compare that hash at load time
# and refuse to run when it moved. Package builds set DESTDIR and skip this.
#
# Runs as the owner of this tree when install was invoked with sudo.

set -euo pipefail

if [[ -n "${DESTDIR:-}" ]]; then
    exit 0
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "$(id -u)" -eq 0 ]]; then
    owner="${SUDO_USER:-$(stat -c %U "$ROOT")}"
    if [[ -z "$owner" || "$owner" == root ]]; then
        echo "after-install: not rebuilding plugins as root" >&2
        exit 0
    fi
    exec runuser -u "$owner" -- "$0"
fi

fail=0

omit="${SCREEN_SHADOW_DIR:-$HOME/Engineering/screen-shadow}/scripts/ensure-omit-plugin.sh"
plugins="${HYPR_PATCHED_PLUGINS_DIR:-$HOME/Engineering/hyprland-patched-plugins}/sync.sh"
polkit="$HOME/.config/ags/scripts/build-hypr-polkit.sh"

if [[ -x "$omit" ]]; then
    echo "==> screen-shadow-omit"
    "$omit" --ensure || fail=1
else
    echo "after-install: missing $omit" >&2
    fail=1
fi

if [[ -x "$plugins" ]]; then
    echo "==> community plugins"
    # Do not fetch. A clean checkout would be reset to origin mid-install.
    "$plugins" --ensure --no-fetch || fail=1
else
    echo "after-install: missing $plugins" >&2
    fail=1
fi

if [[ -x "$polkit" ]]; then
    echo "==> hyprpolkitagent"
    "$polkit" --ensure || fail=1
fi

exit "$fail"
