# Patches on `patched`

This branch tracks [hyprwm/Hyprland](https://github.com/hyprwm/Hyprland) `main` with a small set of personal fixes on top.

| Commit | Summary |
|--------|---------|
| `input: software touchpad rotation when libinput cannot rotate` | Compositor-side `input:rotation` for touchpads without libinput hardware rotation (Synaptics PS/2). |
| `monitor: update logical size on soft transform changes` | Keeps `m_size` in sync when only monitor transform changes via `applyMonitorRuleSoft`. |
| `lock: cover session until a lock surface maps` | `session_lock_xray` used to keep drawing the live workspace when hyprlock failed to commit a frame, so lockdead looked like a window on the desktop. |
| `monitor: drop output listeners on shutdown disconnect` | `onDisconnect` used to return early while shutting down, so aquamarine could still emit `commit` into a CMonitor whose coordinator/scheduler were already gone. |

## Updating from upstream

**Automated:** GitHub Actions rebases `patched` onto [hyprwm/Hyprland](https://github.com/hyprwm/Hyprland) `main` every Monday. If your patches conflict, the workflow fails and GitHub emails you (with default notification settings).

**Manual:**

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

**Local rebuild** (topgrade post-step or by hand):

```bash
~/.local/share/pkgbuilds/hyprland-patched/update.sh
```

The local `hyprland-patched` PKGBUILD pulls this branch directly — no `.patch` files.
