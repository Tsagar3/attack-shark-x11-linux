# Tray + Autostart + No-Root – Design

Date: 2026-09-14

## Context

AttackShark X11 (Qt Widgets, C++, CMake) configures an Attack Shark mouse dongle via
libusb (`hook.cpp`, `sendReport` uses `libusb_open` + `libusb_control_transfer` after
`libusb_detach_kernel_driver`). Today `main.cpp` forces elevation: when the process is
not root it shows a dialog and restarts itself via `pkexec env <GUI env> <app>`. This
produces, on every taskbar launch, a polkit prompt:

> Authentication is needed to run `/usr/bin/env DISPLAY=:0 ... attackshark-x11` as the super user

The user wants:

1. **System tray / minimize-to-tray** like Spotify/Steam: closing the window (X) hides
   the app to the tray and it keeps running; the only real exit is the tray menu. A
   configurable checkbox in the UI (`minToTray`, default ON).
2. **Auto-start on session login** like Spotify/Steam: XDG autostart entry in
   `~/.config/autostart/attackshark.desktop`, controlled by a configurable checkbox in
   the UI. When started by autostart, the app must start silently in the tray (no
   visible window).
3. **No password prompt** when launching from the taskbar: grant the desktop user
   direct access to the Attack Shark USB dongle via a udev rule so the app runs as the
   normal user (no root, no pkexec, no polkit).

All three are coupled: autostart launching an app that demands a password at every
login would be a bad UX, so (3) is a prerequisite for (2).

Decisions taken with the user (multiple-choice clarifications):

- Root fix: **udev rule + app runs as the normal user** (like Piper/OpenRGB/Steam),
  not a polkit passwordless-for-root approach.
- Tray behavior: **X → tray** (Spotify/Steam style) with a UI toggle; quit only from
  the tray menu.
- Autostart method: **XDG `.desktop` in `~/.config/autostart/`** with a UI toggle.
- Autostart window state: **start hidden in the tray**.

## Requirements

### Functional

- FR1: When `minToTray` is enabled and the window close button is pressed, the window
  hides to the system tray and the app keeps running. No exit occurs.
- FR2: When `minToTray` is disabled, pressing X really quits the app (current
  behaviour, settings saved first).
- FR3: A tray icon with a context menu: **Open** (restore/raise window) and **Quit**
  (real exit). Single-click/double-click on the icon restores the window.
- FR4: Real exit (tray Quit) persists settings before terminating.
- FR5: A UI checkbox `Cerrar a la bandeja en vez de salir` wired to a QSettings key
  `minToTray` (default `true`).
- FR6: A UI checkbox `Iniciar con la sesión` wired to a QSettings key
  `autostartEnabled` (default `false`); enabling it creates
  `~/.config/autostart/attackshark.desktop`, disabling it removes it.
- FR7: The generated autostart entry runs the app with a `--hidden` flag so it starts
  in the tray without showing the window.
- FR8: The app runs without root: `main.cpp` no longer forces elevation. Launching
  from the taskbar must never show a polkit password prompt.
- FR9: A udev rule grants the active desktop user access to the Attack Shark dongle
  USB device so libusb writes/`detach_kernel_driver` work unprivileged.

### Non-functional / constraints

- NF1: No changes to DPI logic (`dpibar.*`, `dpiscale.*`), device protocol
  (`hook.*`), or settings sent to the mouse (`dpi.*`, `settings_ui` semantics already
  committed). This spec only touches: `main.cpp`, `atsx11.*`, `settings.ui`,
  `settings.*`, `CMakeLists.txt`, and new files.
- NF2: Follow existing code style (anonymous namespaces, `QStringLiteral`,
  `kSettingsOrg`/`kSettingsApp` constants, no comments unless self-evident).
- NF3: Autostart manager must be unit-testable with a redirected
  `XDG_CONFIG_HOME` (no GUI harness exists; manual verification for tray).
- NF4: If the tray is unavailable (`QSystemTrayIcon::isSystemTrayAvailable() ==
  false`), the app must behave like a normal window (X quits) without crashing, and
  the `--hidden` flag must still show the window.
- NF5: App must not regress when running explicitly with `sudo`/as root:
  `adoptDesktopUserEnvironment()` stays in place.
- NF6: Git: local commits only. **Never `git push`** to `origin/main`.

## Architecture

### Components

```
main.cpp           → parse args; run as user; construct app; position window
atsx11.*           → wire SystemTray: closeEvent, restore, quit, settings
systemtray.h/.cpp  → NEW: QSystemTrayIcon wrapper (Open/Quit menu)
autostart.h/.cpp   → NEW: writes/removes ~/.config/autostart/attackshark.desktop
settings.ui        → new "General" group with 2 checkboxes
settings.cpp/.h    → persist minToTray + autostartEnabled; drive AutostartManager
```

New files are kept small and single-purpose. `SystemTray` only owns the tray icon and
menu, emitting `restoreRequested()` and `quitRequested()`; it does not know about the
main window. `AutostartManager` only knows about the `.desktop` file path and
enable/disable; it does not know about QSettings.

### Data flow

- Startup: `main` (non-root) → `atsx11` ctor → `SystemTray` (if available) →
  `loadSettings()` reads `minToTray`, `autostartEnabled` → `--hidden` given → start
  hidden (tray only), else show window.
- Close (user X): `closeEvent` → if `minToTray && !quitting` → `ignore()+hide()`; else
  → `saveSettings()` + accept.
- Tray Open: `restoreRequested()` → `show(), raise(), activateWindow()`, cancel any
  minimized state.
- Tray Quit: `quitRequested()` → set `m_quitting = true` → `close()` → closeEvent
  accepts → `saveSettings()` already ran.
- Settings dialog OK: persists `devicePath`, `allDevices` (existing), plus
  `minToTray` and `autostartEnabled`; calls `AutostartManager::setEnabled(checked)`.
  Remove `.desktop` if unchecked (no-op if absent).
- Autostart session login: KDE/DE runs `attackshark.desktop` → `Exec="<app>" --hidden`
  → app starts, tray only.

## File-by-file design

### `main.cpp`
- Remove the `geteuid() != 0` elevation gate and `requestElevatedRestart()`,
  `startElevated()`, `guiSessionEnvironment()` (dead code that produced the polkit
  prompts). `desktopUsername()`/`adoptDesktopUserEnvironment()` remain (only act when
  euid == 0).
- Accept `--hidden` argument: `main.cpp` computes
  `const bool hidden = args.contains("--hidden") && QSystemTrayIcon::isSystemTrayAvailable();`
  and calls `w.show()` only when `hidden` is false. The `atsx11` ctor creates the tray
  when available, so a hidden start is always recoverable from the tray icon.
- Keep: dark theme, `applyDarkTheme`.

### `systemtray.h` / `systemtray.cpp` (NEW)
- Constructor: `QSystemTrayIcon(QIcon(":/images/assets/mouse.png"))`; context menu
  with `Abrir` and `Salir`; `setToolTip("Attack Shark X11")`.
- Signals: `restoreRequested()`, `quitRequested()`.
- Slots: `activate(QSystemTrayIcon::ActivationReason)` → emit `restoreRequested()` on
  `Trigger` and `DoubleClick`.
- Guard: member only created when `QSystemTrayIcon::isSystemTrayAvailable()`.
- Ownership: `SystemTray` owns the `QMenu` and `QSystemTrayIcon` (parented to it).

### `autostart.h` / `autostart.cpp` (NEW)
- `QString desktopFilePath()` → `QStandardPaths::writableLocation(GenericConfigLocation)`
  + `/autostart/attackshark.desktop` (create dirs as needed).
- `bool isEnabled()` → file exists.
- `bool setEnabled(bool)` → write the desktop file (QSaveFile for atomic replace) or
  remove it; returns success.
- Desktop file content:
  ```
  [Desktop Entry]
  Type=Application
  Name=Attack Shark X11
  Comment=Configuración del mouse Attack Shark
  Exec="<appPath>" --hidden
  Terminal=false
  StartupNotify=false
  X-GNOME-Autostart-enabled=true
  ```
  `appPath` = `QCoreApplication::applicationFilePath()`; quote it.

### `atsx11.h` / `atsx11.cpp`
- Members: `QScopedPointer<SystemTray> m_tray;` and `bool m_quitting = false;`.
- Ctor: create tray if available; connect `restoreRequested`/`quitRequested`.
- `closeEvent(QCloseEvent*)`:
  ```
  if (m_tray && minToTray_enabled && !m_quitting) { event->ignore(); hide(); return; }
  saveSettings();
  QMainWindow::closeEvent(event);
  ```
- New `--hidden` is handled in `main.cpp` (see above); `atsx11` does not parse
  arguments.

### `settings.ui`
- Add a group at the bottom: label "General" with two `QCheckBox`:
  `chbox_minToTray` `"Cerrar a la bandeja en vez de salir"`, `chbox_autostart`
  `"Iniciar con la sesión"`.

### `settings.cpp` / `settings.h`
- Constructor: read `minToTray` (default true), `autostartEnabled` (default false),
  set checkbox states (`blockSignals` pattern as existing).
- `on_buttonBox_accepted`: persist `minToTray`, `autostartEnabled`; call
  `AutostartManager::setEnabled(chbox_autostart->isChecked())`; on failure show a
  `QMessageBox::warning`.

### `CMakeLists.txt`
- Append `systemtray.h systemtray.cpp autostart.h autostart.cpp` to `PROJECT_SOURCES`
  (the list used by both the Qt6 and Qt5 branches, same as `dpibar`/`dpiscale`). No
  other CMake change needed (AUTOMOC covers the new QObjects).

### udev rule (install once, at verification time)
File `/etc/udev/rules.d/98-attackshark.rules`:
```
# Attack Shark dongle - allow the active desktop user to configure via libusb
SUBSYSTEM=="usb", ATTRS{idVendor}=="1d57", ATTRS{idProduct}=="fa60", TAG+="uaccess", MODE="0664", GROUP="users"
```
Install + apply:
```
sudo install -m 644 /tmp/98-attackshark.rules /etc/udev/rules.d/98-attackshark.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```
Verify `lsusb` vid/pid and `ls -l /dev/bus/usb/*/*` before/after; replug the dongle if
the ACL does not appear. Actual VID/PID to be confirmed from `lsusb` before install
(expected `1d57:fa60`).

## Error handling

- Tray unavailable → no tray object; window behaves normally; `--hidden` is ignored;
  no crash.
- `AutostartManager::setEnabled` failure → `QMessageBox::warning` in settings dialog
  with the path that failed.
- Device write fails as non-root (rule missing/dongle unplugged) → existing error
  message surfaces the apply error code; verification step ensures the user has a
  writeup of the udev rule to fix it.

## Out of scope

- DPI bar, polling rate, profiles, battery, protocol payloads (`hook.*`, `dpi.*`,
  `dpibar.*`, `dpiscale.*`).
- Packaging/deploy (`packaging/`).
- Icon redesign; reuse `:/images/assets/mouse.png`.
- `tray` in `packaging/` or installers.

## Testing strategy

- Unit (existing `ctest` infra, `tests/`):
  - `AutostartManager`: in a temp `XDG_CONFIG_HOME`, `setEnabled(true)` creates a
    parseable `.desktop` containing `Exec` and `--hidden`; `setEnabled(false)` removes
    it; `isEnabled()` reflects state; idempotent.
  - No new tests needed for `SystemTray` (no GUI harness; rule ACCEPT in prior plan).
- Build verification: clean build with current toolchain; run `ctest`; run the app
  from the taskbar as the user → no polkit prompt.
- Manual verification with user:
  1. Launch from taskbar: no password prompt.
  2. Settings → apply profile → mouse responds (libusb works unprivileged).
  3. X → window hides to tray; tray icon present and shows app menu.
  4. Tray Open restores window; tray Quit exits the process.
  5. Toggle `minToTray` off → X quits.
  6. Enable autostart checkbox → `~/.config/autostart/attackshark.desktop` exists and
     references `--hidden`; login again or run the desktop file manually → app starts
     in the tray without a window and without any password prompt.
  7. Disable autostart → file removed.
- Security note (per global install rules): the udev rule is reviewed with the user
  before installing; it only matches the Attack Shark vendor/product pair and grants
  `uaccess` (session ACL), no broad permissions.

## Approvals

- Design sections approved by user on 2026-09-14 (X → tray; XDG autostart; udev +
  run-as-user; start hidden in tray; local commits only, never push).