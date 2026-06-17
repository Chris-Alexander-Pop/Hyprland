# Patches on `patched`

This branch tracks [hyprwm/Hyprland](https://github.com/hyprwm/Hyprland) `main` with a small set of personal fixes on top.

| Commit | Summary |
|--------|---------|
| `input: software touchpad rotation when libinput cannot rotate` | Compositor-side `input:rotation` for touchpads without libinput hardware rotation (Synaptics PS/2). |
| `monitor: update logical size on soft transform changes` | Keeps `m_size` in sync when only monitor transform changes via `applyMonitorRuleSoft`. |

## Updating from upstream

```bash
git fetch upstream
git checkout patched
git rebase upstream/main
# fix conflicts if any, then:
git push --force-with-lease origin patched
```

Or from the PKGBUILD directory:

```bash
~/.local/share/pkgbuilds/hyprland-patched/rebase-fork.sh
```

## Building (Arch)

```bash
~/.local/share/pkgbuilds/hyprland-patched/update.sh
```

The local `hyprland-patched` PKGBUILD pulls this branch directly — no `.patch` files.
