# Patches on `patched`

This branch tracks [hyprwm/Hyprland](https://github.com/hyprwm/Hyprland) `main` with a small set of personal fixes on top.

| Commit | Summary |
|--------|---------|
| `input: software touchpad rotation when libinput cannot rotate` | Compositor-side `input:rotation` for touchpads without libinput hardware rotation (Synaptics PS/2). |
| `monitor: update logical size on soft transform changes` | Keeps `m_size` in sync when only monitor transform changes via `applyMonitorRuleSoft`. |
| `lock: cover session until a lock surface maps` | `session_lock_xray` used to keep drawing the live workspace when hyprlock failed to commit a frame, so lockdead looked like a window on the desktop. |
| `monitor: drop output listeners on shutdown disconnect` | `onDisconnect` used to return early while shutting down, so aquamarine could still emit `commit` into a CMonitor whose coordinator/scheduler were already gone. |
| `input: hold client pointer over a named layer` | Optional. `misc:layer_hold_pointer` keeps the last client's pointer enter while the cursor is over `misc:layer_hold_pointer_namespace`. Layer clicks stay on the layer. `misc:layer_hold_freeze` pins motion on the surface below and restores the cursor only when the flag is unset (Super unlatch). Hide does not drop freeze. Clicks go to the window under the cursor at the real position, then motion is put back on the pin so the meeting tab keeps pointer attention. Window focus does not follow those clicks. |
| `dmabuf: keep renderer formats on hybrid render/scanout` | `quirks:skip_non_kms_dmabuf_formats` must not intersect the composition tranche with KMS formats when the render GPU is not the scanout GPU (NVIDIA render, Intel eDP). Scanout tranches still advertise the output's KMS device. |

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
