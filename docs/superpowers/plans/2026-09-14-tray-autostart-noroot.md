# Tray + Autostart + No-Root Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Spotify/Steam-style system-tray minimize, XDG autostart, and remove the
pkexec root requirement (udev `uaccess`) so launching the app never prompts for a
password.

**Architecture:** The app starts as the normal desktop user (no elevation). A small
`systemtray` QObject wraps `QSystemTrayIcon` (Open/Quit menu, restore on click) and is
wired to `atsx11`'s `closeEvent` so X hides to tray when `minToTray` is on. A small
`autostart` namespace writes/removes `~/.config/autostart/attackshark.desktop`
(`Exec="<app>" --hidden`). Two existing (disabled) checkboxes in `settings.ui` are
re-enabled and wired to QSettings keys `minToTray` and `autostartEnabled`. A udev rule
owned by `uaccess` gives the active user libusb access to the dongle, so `hook.cpp`
works without root — `hook.*`, `dpi.*`, `dpibar.*`, `dpiscale.*` are untouched.

**Tech Stack:** Qt Widgets (Qt6, falls back to Qt5), CMake, C++17, libudev, libusb-1.0.

## Global Constraints

(Verbatum: spec `docs/superpowers/specs/2026-09-14-tray-autostart-noroot-design.md`)

- No changes to DPI logic (`dpibar.*`, `dpiscale.*`), device protocol (`hook.*`), or
  DPI/mouse payload logic (`dpi.*`). Only `main.cpp`, `atsx11.h/.cpp`, `settings.ui`,
  `settings.h/.cpp`, `CMakeLists.txt`, plus new files.
- Follow existing style: anonymous namespaces for `kSettingsOrg`/`kSettingsApp`,
  `QStringLiteral`, no gratuitous comments.
- New app sources go in `PROJECT_SOURCES` (used by both Qt6 and Qt5 branches).
- QSettings keys: `minToTray` (default `true`), `autostartEnabled` (default `false`).
  Org `AttackShark`, app `X11`.
- English UI/hard-coded strings (matching current app text).
- Tray icon: reuse `:/images/assets/mouse.png`.
- If the tray is unavailable (`QSystemTrayIcon::isSystemTrayAvailable() == false`):
  no tray object, X really quits, `--hidden` is ignored, no crash.
- `--hidden` start is only honored when the tray is available.
- udev rule installation touches `/etc` — user must approve the rule content first
  (global install-security rule) before it is installed.
- Git: local commits only. **Never `git push` to `origin/main`.**
- Do not touch untracked leftovers: `REFERENCE.md`,
  `docs/superpowers/plans/2026-09-14-dpi-configuration.md`,
  `docs/superpowers/specs/2026-09-14-dpi-configuration-design.md`,
  `build-qt5-check/`, `.superpowers/`.

---

## File Structure

- Create `autostart.h` / `autostart.cpp` — namespace `autostart`; write/remove
  `~/.config/autostart/attackshark.desktop`; no Qt object.
- Create `systemtray.h` / `systemtray.cpp` — `systemtray` QObject wrapping
  `QSystemTrayIcon` + context menu; signals `restoreRequested`, `quitRequested`.
- Create `tests/autostart_test.cpp` — unit test for the `autostart` namespace.
- Modify `CMakeLists.txt` — add new sources to `PROJECT_SOURCES`; add tabletest
  target for `autostart`.
- Modify `atsx11.h` / `atsx11.cpp` — tray member, `closeEvent` tray logic,
  restore/quit slots.
- Modify `settings.ui` — enable `chkbox_minimizeTray` and `chkbox_autostartup`.
- Modify `settings.h` / `settings.cpp` — read toggles at open; persist on toggle.
- Modify `main.cpp` — run as user, `--hidden` flag, remove elevation code.

---

### Task 1: `autostart` namespace + unit tests

**Files:**
- Create: `autostart.h`
- Create: `autostart.cpp`
- Create: `tests/autostart_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `QString autostart::desktopFilePath()` — path of the XDG autostart file.
  - `bool autostart::isEnabled()` — `true` when the file exists.
  - `bool autostart::setEnabled(bool enabled)` — `true` on success.

- [ ] **Step 1: Write the failing test**

Create `tests/autostart_test.cpp`:

```cpp
#include "../autostart.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QByteArray>
#include <cstdio>

static int g_failures = 0;

#define EXPECT(cond, msg) do { \
    if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
} while (0)

static bool fileContains(const QString &path, const QString &needle)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    return file.readAll().contains(needle.toUtf8());
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::printf("  FAIL: cannot create temp dir\n");
        return 1;
    }
    qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());

    const QString filePath = autostart::desktopFilePath();
    EXPECT(filePath.endsWith(QStringLiteral("/autostart/attackshark.desktop")),
        "paths under XDG_CONFIG_HOME/autostart");

    EXPECT(!autostart::isEnabled(), "no file yet -> disabled");

    EXPECT(autostart::setEnabled(true), "setEnabled(true) succeeds");
    EXPECT(autostart::isEnabled(), "enabled after setEnabled(true)");
    EXPECT(QFileInfo::exists(filePath), "desktop file exists");
    EXPECT(fileContains(filePath, QStringLiteral("Exec=")),
        "desktop file has Exec line");
    EXPECT(fileContains(filePath, QStringLiteral("--hidden")),
        "desktop file Exec passes --hidden");
    EXPECT(fileContains(filePath, QStringLiteral("Type=Application")),
        "desktop file parse header");

    EXPECT(autostart::setEnabled(true), "setEnabled(true) is idempotent");
    EXPECT(autostart::isEnabled(), "still enabled after second enable");

    EXPECT(autostart::setEnabled(false), "setEnabled(false) succeeds");
    EXPECT(!autostart::isEnabled(), "disabled after setEnabled(false)");
    EXPECT(!QFileInfo::exists(filePath), "desktop file removed");

    EXPECT(autostart::setEnabled(false), "setEnabled(false) is idempotent");

    if (g_failures) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll autostart tests PASSED\n");
    return 0;
}
```

Note: the test must set `XDG_CONFIG_HOME` before the first
`QStandardPaths::writableLocation` call (which happens inside
`desktopFilePath()`).

- [ ] **Step 2: Add the test to CMake and run to verify it fails**

In `CMakeLists.txt`, append `autostart.h autostart.cpp` to `PROJECT_SOURCES`
(following the `dpiscale.h dpiscale.cpp` entry), and add at the bottom:

```cmake
add_executable(autostart_tests tests/autostart_test.cpp autostart.cpp)
target_compile_features(autostart_tests PRIVATE cxx_std_17)
target_link_libraries(autostart_tests PRIVATE Qt${QT_VERSION_MAJOR}::Core)
add_test(NAME autostart_tests COMMAND autostart_tests)
```

Run: `cmake --build build --target autostart_tests`
Expected: FAIL — `autostart.h` not found (`autostart.h`/`autostart.cpp` do not exist yet).

- [ ] **Step 3: Write minimal implementation**

Create `autostart.h`:

```cpp
#ifndef AUTOSTART_H
#define AUTOSTART_H

#include <QString>

namespace autostart {

QString desktopFilePath();
bool isEnabled();
bool setEnabled(bool enabled);

} // namespace autostart

#endif // AUTOSTART_H
```

Create `autostart.cpp`:

```cpp
#include "autostart.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace autostart {

QString desktopFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart/attackshark.desktop");
}

bool isEnabled()
{
    return QFileInfo::exists(desktopFilePath());
}

bool setEnabled(bool enabled)
{
    const QString path = desktopFilePath();

    if (!enabled)
        return !QFileInfo::exists(path) || QFile::remove(path);

    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir))
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    const QString content = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Attack Shark X11\n"
        "Comment=Configuración del mouse Attack Shark\n"
        "Exec=\"%1\" --hidden\n"
        "Terminal=false\n"
        "StartupNotify=false\n"
        "X-GNOME-Autostart-enabled=true\n")
        .arg(QCoreApplication::applicationFilePath());

    file.write(content.toUtf8());
    return file.commit();
}

} // namespace autostart
```

- [ ] **Step 4: Build and run the test to verify it passes**

Run: `cmake --build build --target autostart_tests && ./build/autostart_tests`
Expected: build clean, `All autostart tests PASSED`.

Run: `ctest --test-dir build --output-on-failure`
Expected: all tests pass (preexisting `dpi_encoding_tests`, `dpiscale_tests` +
`autostart_tests`).

- [ ] **Step 5: Commit**

```bash
git add autostart.h autostart.cpp tests/autostart_test.cpp CMakeLists.txt
git commit -m "feat: autostart XDG entry manager with unit tests"
```

---

### Task 2: `systemtray` class

**Files:**
- Create: `systemtray.h`
- Create: `systemtray.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `:/images/assets/mouse.png` (icon resource, already used by `atsx11`).
- Produces:
  - Constructor `systemtray::systemtray(const QIcon &icon, QWidget *window, QObject *parent = nullptr)`
  - Signal `void restoreRequested()`
  - Signal `void quitRequested()`

- [ ] **Step 1: Create the header**

Create `systemtray.h`:

```cpp
#ifndef SYSTEMTRAY_H
#define SYSTEMTRAY_H

#include <QObject>

class QIcon;
class QMenu;
class QSystemTrayIcon;
class QWidget;

class systemtray : public QObject
{
    Q_OBJECT

public:
    explicit systemtray(const QIcon &icon, QWidget *window, QObject *parent = nullptr);

signals:
    void restoreRequested();
    void quitRequested();

private:
    void onActivated(QSystemTrayIcon::ActivationReason reason);

    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
};

#endif // SYSTEMTRAY_H
```

- [ ] **Step 2: Create the implementation**

Create `systemtray.cpp`:

```cpp
#include "systemtray.h"
#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QString>
#include <QSystemTrayIcon>

systemtray::systemtray(const QIcon &icon, QWidget *window, QObject *parent)
    : QObject(parent)
    , m_tray(new QSystemTrayIcon(icon, this))
    , m_menu(new QMenu(window))
{
    QAction *open = m_menu->addAction(QStringLiteral("Open"));
    QAction *quit = m_menu->addAction(QStringLiteral("Quit"));

    connect(open, &QAction::triggered, this, &systemtray::restoreRequested);
    connect(quit, &QAction::triggered, this, &systemtray::quitRequested);
    connect(m_tray, &QSystemTrayIcon::activated, this, &systemtray::onActivated);

    m_tray->setToolTip(QStringLiteral("Attack Shark X11"));
    m_tray->setContextMenu(m_menu);
    m_tray->show();
}

void systemtray::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
        emit restoreRequested();
}
```

- [ ] **Step 3: Add to CMake and build**

In `CMakeLists.txt`, append `systemtray.h systemtray.cpp` to `PROJECT_SOURCES`
(after the `autostart.h autostart.cpp` entry).

Run: `cmake --build build`
Expected: clean build, no warnings; outside of `systemtray.cpp` nothing uses the
class yet, so no behaviour change.

- [ ] **Step 4: Verify preexisting tests still pass**

Run: `ctest --test-dir build --output-on-failure`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add systemtray.h systemtray.cpp CMakeLists.txt
git commit -m "feat: system tray icon with Open/Quit menu"
```

---

### Task 3: Wire tray into `atsx11` (close-to-tray, restore, quit)

**Files:**
- Modify: `atsx11.h`
- Modify: `atsx11.cpp`

**Interfaces:**
- Consumes:
  - `systemtray::restoreRequested()`, `systemtray::quitRequested()` (Task 2)
  - QSettings key `minToTray` (bool, default `true`) — read here, written in Task 4.
- Produces:
  - `atsx11` public behaviour: X hides window to tray when `minToTray` is on;
    tray Open restores; tray Quit really exits (persisting settings first).

- [ ] **Step 1: Declare tray members and slots**

In `atsx11.h`:

- After `class QCloseEvent;` add `class systemtray;`.
- In `private slots:` add:

```cpp
    void onTrayRestoreRequested();
    void onTrayQuitRequested();
```

- In `private:` members add:

```cpp
    systemtray *m_tray = nullptr;
    bool m_quitting = false;
```

- [ ] **Step 2: Create the tray in the constructor**

In `atsx11.cpp` constructor, after
`setWindowIcon(QIcon(QStringLiteral(":/images/assets/mouse.png")));` add:

```cpp
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = new systemtray(
            QIcon(QStringLiteral(":/images/assets/mouse.png")), this, this);
        connect(m_tray, &systemtray::restoreRequested,
                this, &atsx11::onTrayRestoreRequested);
        connect(m_tray, &systemtray::quitRequested,
                this, &atsx11::onTrayQuitRequested);
    }
```

Add includes at the top of `atsx11.cpp`:

```cpp
#include "systemtray.h"
#include <QSystemTrayIcon>
```

- [ ] **Step 3: Implement close-to-tray in `closeEvent`**

Replace the current `atsx11::closeEvent` body:

```cpp
void atsx11::closeEvent(QCloseEvent *event)
{
    if (m_tray && !m_quitting) {
        QSettings qSettings(kSettingsOrg, kSettingsApp);
        if (qSettings.value(QStringLiteral("minToTray"), true).toBool()) {
            event->ignore();
            hide();
            return;
        }
    }

    saveSettings();
    QMainWindow::closeEvent(event);
}
```

- [ ] **Step 4: Add the restore/quit slots**

At the end of `atsx11.cpp` (after `reloadSettingsUi`) add:

```cpp
void atsx11::onTrayRestoreRequested()
{
    show();
    raise();
    activateWindow();
}

void atsx11::onTrayQuitRequested()
{
    m_quitting = true;
    close();
}
```

- [ ] **Step 5: Build**

Run: `cmake --build build`
Expected: clean build, no warnings.

- [ ] **Step 6: Verify preexisting tests still pass**

Run: `ctest --test-dir build --output-on-failure`
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add atsx11.h atsx11.cpp
git commit -m "feat: close-to-tray and tray Open/Quit in main window"
```

---

### Task 4: Re-enable and wire the settings toggles

**Files:**
- Modify: `settings.ui`
- Modify: `settings.h`
- Modify: `settings.cpp`

**Interfaces:**
- Consumes: `autostart::setEnabled` / `autostart::desktopFilePath` (Task 1).
- Produces:
  - QSettings keys `minToTray` (default `true`) and `autostartEnabled` (default
    `false`) written immediately when the corresponding checkbox is toggled.

- [ ] **Step 1: Enable the two placeholder checkboxes**

In `settings.ui`, for widget `chkbox_minimizeTray` change:

```xml
   <property name="enabled">
    <bool>false</bool>
   </property>
```

to:

```xml
   <property name="enabled">
    <bool>true</bool>
   </property>
```

Same change for `chkbox_autostartup`. No other geometry changes.

- [ ] **Step 2: Declare the toggle slots**

In `settings.h` `private slots:` add:

```cpp
    void on_chkbox_minimizeTray_toggled(bool checked);
    void on_chkbox_autostartup_toggled(bool checked);
```

- [ ] **Step 3: Read the toggles when the dialog opens**

In `settings.cpp` constructor, after the existing `QSettings qSettings(...)` /
`allDevices` reads and before the `chbox_alldevices` block, add:

```cpp
    const bool minToTray = qSettings.value(QStringLiteral("minToTray"), true).toBool();
    const bool autostartEnabled =
        qSettings.value(QStringLiteral("autostartEnabled"), false).toBool();

    ui->chkbox_minimizeTray->blockSignals(true);
    ui->chkbox_minimizeTray->setChecked(minToTray);
    ui->chkbox_minimizeTray->blockSignals(false);

    ui->chkbox_autostartup->blockSignals(true);
    ui->chkbox_autostartup->setChecked(autostartEnabled);
    ui->chkbox_autostartup->blockSignals(false);
```

Add `#include "autostart.h"` to the includes in `settings.cpp`.

- [ ] **Step 4: Persist on toggle**

At the end of `settings.cpp` add:

```cpp
void settings::on_chkbox_minimizeTray_toggled(bool checked)
{
    QSettings qSettings(kSettingsOrg, kSettingsApp);
    qSettings.setValue(QStringLiteral("minToTray"), checked);
    qSettings.sync();
}

void settings::on_chkbox_autostartup_toggled(bool checked)
{
    if (!autostart::setEnabled(checked)) {
        QMessageBox::warning(this, QStringLiteral("Error"),
            QStringLiteral("Failed to update autostart entry: %1")
                .arg(autostart::desktopFilePath()));
        return;
    }

    QSettings qSettings(kSettingsOrg, kSettingsApp);
    qSettings.setValue(QStringLiteral("autostartEnabled"), checked);
    qSettings.sync();
}
```

Qt auto-connect calls these via the object names `chkbox_minimizeTray` /
`chkbox_autostartup`; the `toggled(bool)` signature matches.

- [ ] **Step 5: Build**

Run: `cmake --build build`
Expected: clean build, no warnings.

- [ ] **Step 6: Verify tests still pass**

Run: `ctest --test-dir build --output-on-failure`
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add settings.ui settings.h settings.cpp
git commit -m "feat: wire minimize-to-tray and autostart toggles to settings"
```

---

### Task 5: Run as user; `--hidden` flag; remove elevation code

**Files:**
- Modify: `main.cpp`

**Interfaces:**
- Consumes: `QSystemTrayIcon::isSystemTrayAvailable()` for the `--hidden` guard.
- Produces:
  - App always runs with the invoking user's privileges (no pkexec).
  - CLI arg `--hidden`: start without showing the window (only when a tray is
    available).

- [ ] **Step 1: Trim includes**

In `main.cpp` remove:

```cpp
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
```

Add:

```cpp
#include <QSystemTrayIcon>
```

Keep `#include <unistd.h>` and `#include <pwd.h>` (used by
`adoptDesktopUserEnvironment`).

- [ ] **Step 2: Delete the elevation machinery**

Remove entirely from `main.cpp` the functions `startElevated()` and
`requestElevatedRestart()`. `desktopUsername()` and `adoptDesktopUserEnvironment()`
remain (they are inert when `geteuid() != 0`).

- [ ] **Step 3: Remove the root gate and add `--hidden`**

Replace the body of `main()`:

```cpp
int main(int argc, char *argv[])
{
    adoptDesktopUserEnvironment();

    QApplication a(argc, argv);
    applyDarkTheme(a);

    atsx11 w;

    const QStringList args = QCoreApplication::arguments();
    const bool startHidden = args.contains(QStringLiteral("--hidden"))
        && QSystemTrayIcon::isSystemTrayAvailable();
    if (!startHidden)
        w.show();

    return a.exec();
}
```

- [ ] **Step 4: Build**

Run: `cmake --build build`
Expected: clean build, no warnings.

- [ ] **Step 5: Run the app as the normal user**

Run: `./build/attackshark-x11 --hidden &` then
`./build/attackshark-x11` in a second instance window; confirm:
- No pkexec/polkit prompt is shown.
- `ps -o euid= -C attackshark-x11` shows your UID (not 0).
- With `--hidden`, no window appears (tray icon present).
Close the test instances (via the tray menu Quit, or `pkill attackshark-x11`).

- [ ] **Step 6: Verify tests still pass**

Run: `ctest --test-dir build --output-on-failure`
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add main.cpp
git commit -m "feat: run as normal user, start hidden via --hidden, drop pkexec elevation"
```

---

### Task 6: Install udev rule and end-to-end verification (with user)

**Files:**
- Create (system file): `/etc/udev/rules.d/98-attackshark.rules`

**Interfaces:**
- Consumes: the rebuilt user-mode app from Task 5.
- Produces: user `uaccess` (ACL) on the Attack Shark dongle's USB device so
  `libusb_open` + `libusb_detach_kernel_driver` + `libusb_control_transfer` in
  `hook.cpp` work with the user's UID.

- [ ] **Step 1: Confirm the dongle identity**

Run: `lsusb`
Expected: an entry for the Attack Shark dongle. Confirm `idVendor:idProduct`
(expected `1d57:fa60`).

- [ ] **Step 2: Review the rule with the user (security gate)**

Show the user the rule below and get their explicit OK before installing (global
install-security rule). The rule only matches the dongle's vendor/product and grants
a session ACL — no world-write, no daemons, no network.

```udev
# Attack Shark dongle - allow the active desktop user to configure via libusb
SUBSYSTEM=="usb", ATTRS{idVendor}=="1d57", ATTRS{idProduct}=="fa60", TAG+="uaccess", MODE="0664", GROUP="users"
```

- [ ] **Step 3: Install and apply the rule**

Write the rule to `/tmp/98-attackshark.rules`, then have the user run (or run via a
`pkexec` GUI prompt shown to the user):

```bash
sudo install -m 644 /tmp/98-attackshark.rules /etc/udev/rules.d/98-attackshark.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

If the ACL does not appear after `trigger`, ask the user to unplug/replug the dongle.

- [ ] **Step 4: Verify unprivileged device access**

Run (as the user, no `sudo`): open the app, select a device in Settings, click
**Apply**. Expected: "Successfully applied" with no password prompt.
If it still fails, inspect `ls -l /dev/bus/usb/*/*` and confirm the dongle got the
`uaccess` ACL (see `getfacl` on the node) and was replugged.

- [ ] **Step 5: Manual GUI verification with the user**

Walk through, confirming each item:

1. Launch from taskbar: **no password prompt**.
2. X on the window → app hides to tray; tray icon shows menus.
3. Tray **Open** restores the window; tray **Quit** exits (verify with
   `pgrep attackshark-x11` → gone).
4. Settings → enable **Minimize to System tray** stays on; with it off, X quits.
5. Settings → enable **Auto Startup** → `~/.config/autostart/attackshark.desktop`
   exists and contains `Exec="..." --hidden`.
6. Run `~/.config/autostart/attackshark.desktop` (via `gtk-launch`/KDE runner,
   or `dbus-launch`/`setsid` to simulate login): app starts in tray, no window, no
   password prompt.
7. Settings → disable **Auto Startup** → the `.desktop` file is gone.

- [ ] **Step 6: Re-run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: all pass.

- [ ] **Step 7: Commit (if verification uncovered fixes, commit those first)**

No code changes in this task normally; skip the commit if nothing changed. If fixes
were needed, commit them with an appropriate message.

---

## Self-Review Notes

- Spec coverage: FR1–FR4 → Task 3; FR5–FR6 → Task 4; FR7–FR8 → Tasks 5 + 6; FR9 →
  Task 6; NF1 → Global Constraints; NF2 → constraints + pattern-in-code; NF3 → Task 1;
  NF4 → Tasks 3/5 guards; NF5 → main keeps `adoptDesktopUserEnvironment`; NF6 →
  Global Constraints.
- Placeholder scan: no TBD/TODO; every code step has full content.
- Type consistency: `autostart::desktopFilePath/isEnabled/setEnabled` and
  `systemtray(restoreRequested/quitRequested)`, `m_tray`, `m_quitting`,
  `onTrayRestoreRequested`, `onTrayQuitRequested`, `on_chkbox_minimizeTray_toggled`,
  `on_chkbox_autostartup_toggled` are defined once and referenced consistently.