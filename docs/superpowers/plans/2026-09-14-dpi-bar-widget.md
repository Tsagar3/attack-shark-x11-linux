# DPI Bar Widget (GHub-style) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 6 DPI spinboxes + active-stage combo with a custom GHub-style widget: a horizontal logarithmically-scaled bar with 6 draggable points (one per stage), values always visible above points, click-to-activate stage, double-click inline value editor, and mouse-wheel fine tuning.

**Architecture:** A pure C++ module `dpiscale` (no Qt) provides the log-scale mapping/ordering functions and is unit-tested via CTest. A `DpiBarWidget` (QWidget, paintEvent + mouse/keyboard/wheel events) consumes `dpiscale`, and `atsx11` integrates it by replacing the spinbox UI section (inheriting the same QSettings keys so existing configs load unchanged).

**Tech Stack:** C++17, Qt 6 Widgets (build verified: `Qt6_DIR=/usr/lib/cmake/Qt6`), CMake ≥3.16, CTest.

## Global Constraints

- C++17, Qt 6 Widgets, CMake ≥ 3.16. AUTOUIC/AUTOMOC on.
- DPI range 50–26000, step 50. 6 stages, active stage 1-based (1–6).
- Log scale mapping (exact formula):
  `pos = (ln(dpi) − ln(50)) / (ln(26000) − ln(50))`, clamped to [0,1].
- Defaults when no config: `[800, 1600, 2400, 3200, 5000, 22000]`.
- Colors: accent `#2a82da` (RGB 42,130,218); inactive point `#51494a` (81,73,74); active point ring white; inactive border ~`#8c8c8c`; inactive labels `#aaaaaa`.
- QSettings keys UNCHANGED: `dpiValues` (QVariantList), `activeDpiStage` (int 1-based), org `AttackShark`, app `X11`.
- DO NOT modify: `hook.cpp`, `hook.h`, `dpi.cpp`, `dpi.h`, `settings.cpp`, `settings.h`, `main.cpp`, `settings.ui`. `applySettingsFromUser` is untouched.
- `atsx11.ui` uses absolute positioning (no layouts at window level). Only 3 UI geometry changes: window size, delete DPI grid, move bottom bar.
- Save policy: persist directly via `saveSettings()` (which already calls `settings.sync()`) on every `valueChanged` and `stageActivated`. No debounce flag (spec approved "guardar directo + flush").
- Mouse events use `event->pos()` (compatible Qt5/Qt6; deprecation accepted, no `-Werror`).
- Widget is created programmatically in the `atsx11` constructor (not in `.ui`), avoiding Designer `customwidgets` boilerplate and uic warnings about unknown classes.

## File Structure

- Create `dpiscale.h` / `dpiscale.cpp` — pure math: `dpiToPos`, `posToDpi`, `roundToStep`, `clampOrdered`. No Qt includes.
- Create `dpibar.h` / `dpibar.cpp` — `DpiBarWidget` QWidget (render + interaction), consumes `dpiscale`.
- Create `tests/dpiscale_test.cpp` — CTest executable (pattern: existing `tests/dpi_encoding_test.cpp`).
- Modify `CMakeLists.txt` — add `dpiscale`/`dpibar` sources to the app target; add `dpiscale_tests` CTest target.
- Modify `atsx11.h` / `atsx11.cpp` — instantiate widget, connect signals, rewire load/save/reload/apply/info labels.
- Modify `atsx11.ui` — window 720×440 → 740×520; delete `gridLayoutWidget_dpi` (lines 346–492); move bottom `horizontalLayoutWidget` (0,370,721,58) → (0,450,740,58).
- `README.md` line 44 `- [x] DPI Configuration` must remain — verification only, no edit.

---

### Task 1: `dpiscale` pure mapping module (TDD)

**Files:**
- Create: `dpiscale.h`
- Create: `dpiscale.cpp`
- Create: `tests/dpiscale_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing (pure C++, no Qt).
- Produces: `namespace dpiscale` with:
  - `constexpr int kMinDpi = 50;`
  - `constexpr int kMaxDpi = 26000;`
  - `constexpr int kStep = 50;`
  - `constexpr int kNumStages = 6;`
  - `double dpiToPos(int dpi);` → normalized [0,1], clamps input.
  - `int posToDpi(double pos, bool nearestStep);` → inverse, clamps [50,26000];
    with `nearestStep=true` returns a multiple of 50.
  - `int roundToStep(int dpi);` → nearest multiple of 50, clamped [50,26000].
  - `bool clampOrdered(int dpi[kNumStages]);` → clamps each to [50,26000], then enforces
    strictly ascending left→right by pushing `dpi[i] = min(dpi[i-1] + kStep, kMaxDpi)`
    when `dpi[i] <= dpi[i-1]` (duplicates resolve up by 50; collisions at the ceiling
    may still be equal 26000). Returns `true` if any element changed.

- [x] **Step 1: Write the header (test contract)**

Content of `dpiscale.h`:

```cpp
#ifndef DPISCALE_H
#define DPISCALE_H

namespace dpiscale {

constexpr int kMinDpi = 50;
constexpr int kMaxDpi = 26000;
constexpr int kStep = 50;
constexpr int kNumStages = 6;

double dpiToPos(int dpi);
int posToDpi(double pos, bool nearestStep);
int roundToStep(int dpi);
bool clampOrdered(int dpi[kNumStages]);

} // namespace dpiscale

#endif // DPISCALE_H
```

- [x] **Step 2: Write the failing test**

Content of `tests/dpiscale_test.cpp`:

```cpp
#include "../dpiscale.h"
#include <cmath>
#include <cstdio>

static int g_failures = 0;

#define EXPECT(cond, msg) do { \
    if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
} while (0)

#define EXPECT_CLOSE(a, b, tol, msg) do { \
    const double _a = (a), _b = (b); \
    if (std::abs(_a - _b) > (tol)) { \
        std::printf("  FAIL: %s (%g vs %g)\n", msg, _a, _b); \
        ++g_failures; \
    } \
} while (0)

int main()
{
    std::printf("--- dpiToPos ---\n");
    {
        EXPECT(dpiscale::dpiToPos(50) == 0.0, "50 -> 0.0");
        EXPECT(dpiscale::dpiToPos(26000) == 1.0, "26000 -> 1.0");
        const double p1 = dpiscale::dpiToPos(400);
        const double p2 = dpiscale::dpiToPos(1000);
        const double p3 = dpiscale::dpiToPos(12000);
        EXPECT(p1 > 0.0 && p1 < p2 && p2 < 0.5 && p3 < 1.0,
            "log scale: defaults well-spaced in (0,1)");
        EXPECT(dpiscale::dpiToPos(0) == 0.0, "below-min clamps to 0");
        EXPECT(dpiscale::dpiToPos(30000) == 1.0, "above-max clamps to 1");
    }

    std::printf("--- posToDpi ---\n");
    {
        EXPECT(dpiscale::posToDpi(0.0, false) == 50, "0 -> 50");
        EXPECT(dpiscale::posToDpi(1.0, false) == 26000, "1 -> 26000");
        EXPECT(dpiscale::posToDpi(-0.5, false) == 50, "negative clamps to 50");
        EXPECT(dpiscale::posToDpi(2.0, false) == 26000, ">1 clamps to 26000");

        const int sample[6] = {800, 1600, 2400, 3200, 5000, 22000};
        for (int v : sample) {
            const double pos = dpiscale::dpiToPos(v);
            EXPECT_CLOSE(dpiscale::posToDpi(pos, false), v, 1.0,
                "log round-trip within 1 DPI");
        }
        EXPECT(dpiscale::posToDpi(0.5, true) % 50 == 0,
            "nearestStep returns a multiple of 50");
        EXPECT(dpiscale::posToDpi(0.123456789, true) % 50 == 0,
            "nearestStep at arbitrary pos is a multiple of 50");
    }

    std::printf("--- roundToStep ---\n");
    {
        EXPECT(dpiscale::roundToStep(999) == 1000, "999 -> 1000");
        EXPECT(dpiscale::roundToStep(12551) == 12550, "12551 -> 12550");
        EXPECT(dpiscale::roundToStep(50) == 50, "50 -> 50");
        EXPECT(dpiscale::roundToStep(26000) == 26000, "26000 -> 26000");
        EXPECT(dpiscale::roundToStep(49) == 50, "49 -> 50");
        EXPECT(dpiscale::roundToStep(26010) == 26000, "26010 -> 26000");
    }

    std::printf("--- clampOrdered ---\n");
    {
        int a[6] = {800, 1600, 2400, 3200, 5000, 22000};
        EXPECT(!dpiscale::clampOrdered(a), "already ordered -> false");

        int b[6] = {800, 800, 2400, 3200, 5000, 22000};
        EXPECT(dpiscale::clampOrdered(b), "duplicate -> changed");
        EXPECT(b[1] == 850, "duplicate resolved up by one step (850)");

        int c[6] = {0, 1000, 99999, 3200, 5000, 22000};
        EXPECT(dpiscale::clampOrdered(c), "out-of-range -> changed");
        EXPECT(c[0] == 50 && c[2] == 26000, "out-of-range values clamped");

        int d[6] = {5000, 4000, 3000, 2000, 1000, 800};
        EXPECT(dpiscale::clampOrdered(d), "descending -> changed");
        for (int i = 1; i < 6; ++i)
            EXPECT(d[i] > d[i - 1], "forced strictly ascending");

        int e[6] = {26000, 26000, 26000, 26000, 26000, 26000};
        dpiscale::clampOrdered(e);
        EXPECT(e[5] == 26000, "ceiling keeps max (duplicates allowed at max)");
    }

    if (g_failures) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll dpiscale tests PASSED\n");
    return 0;
}
```

- [x] **Step 3: Wire the test target into CMake, with an empty implementation (must FAIL to link)**

Append to the end of `CMakeLists.txt` (after the existing `dpi_encoding_tests` block lines 97–100):

```cmake
add_executable(dpiscale_tests tests/dpiscale_test.cpp dpiscale.cpp)
target_compile_features(dpiscale_tests PRIVATE cxx_std_17)
add_test(NAME dpiscale_tests COMMAND dpiscale_tests)
```

Create `dpiscale.cpp` with the include and namespace but **no function definitions** (empty bodies would still define the symbols and link):

```cpp
#include "dpiscale.h"

namespace dpiscale {

// Function bodies intentionally omitted for the RED step.

} // namespace dpiscale
```

Run: `cmake -S . -B build && cmake --build build -j$(nproc)`
Expected: build FAILS — `undefined reference to dpiscale::dpiToPos(int)` etc. (functions declared but not defined).

- [x] **Step 4: Implement `dpiscale.cpp`**

Replace the bodies:

```cpp
#include "dpiscale.h"
#include <algorithm>
#include <cmath>

namespace dpiscale {

double dpiToPos(int dpi)
{
    const int v = std::clamp(dpi, kMinDpi, kMaxDpi);
    return (std::log(static_cast<double>(v)) - std::log(static_cast<double>(kMinDpi)))
         / (std::log(static_cast<double>(kMaxDpi)) - std::log(static_cast<double>(kMinDpi)));
}

int posToDpi(double pos, bool nearestStep)
{
    const double c = std::clamp(pos, 0.0, 1.0);
    const double raw = std::exp(c * (std::log(static_cast<double>(kMaxDpi))
                                     - std::log(static_cast<double>(kMinDpi)))
                                + std::log(static_cast<double>(kMinDpi)));
    int v = static_cast<int>(std::llround(raw));
    v = std::clamp(v, kMinDpi, kMaxDpi);
    return nearestStep ? roundToStep(v) : v;
}

int roundToStep(int dpi)
{
    const double stepped = std::round(static_cast<double>(dpi) / kStep) * kStep;
    return std::clamp(static_cast<int>(std::llround(stepped)), kMinDpi, kMaxDpi);
}

bool clampOrdered(int dpi[kNumStages])
{
    bool changed = false;
    for (int i = 0; i < kNumStages; ++i) {
        const int v = std::clamp(dpi[i], kMinDpi, kMaxDpi);
        if (v != dpi[i]) {
            dpi[i] = v;
            changed = true;
        }
    }
    for (int i = 1; i < kNumStages; ++i) {
        if (dpi[i] <= dpi[i - 1]) {
            const int v = std::min(dpi[i - 1] + kStep, kMaxDpi);
            if (v != dpi[i]) {
                dpi[i] = v;
                changed = true;
            }
        }
    }
    return changed;
}

} // namespace dpiscale
```

- [x] **Step 5: Run the dpiscale tests — must now PASS**

Run: `cmake --build build -j$(nproc) && ./build/dpiscale_tests`
Expected: `All dpiscale tests PASSED`.

- [x] **Step 6: Run existing regression test**

Run: `./build/dpi_encoding_tests`
Expected: `All DPI encoding tests PASSED`.

- [x] **Step 7: Commit**

```bash
git add dpiscale.h dpiscale.cpp tests/dpiscale_test.cpp CMakeLists.txt
git commit -m "feat: add dpiscale logarithmic DPI bar mapping with unit tests"
```

---

### Task 2: `DpiBarWidget` (render + interaction)

**Files:**
- Create: `dpibar.h`
- Create: `dpibar.cpp`
- Modify: `CMakeLists.txt` (add sources to the Qt 6 app target)

**Interfaces:**
- Consumes: `dpiscale` (Task 1): `kNumStages`, `kMinDpi`, `kMaxDpi`, `dpiToPos`, `posToDpi`, `roundToStep`, `clampOrdered`.
- Produces (used by Task 3):
  - `class DpiBarWidget : public QWidget` (Q_OBJECT)
  - `void setValues(const int values[6]);` — orders/clamps, repaints; emits `valueChanged` per stage ONLY if any value changed.
  - `void setActiveStage(int stage);` — clamps 1–6, repaints; emits `stageActivated` only on change.
  - `int activeStage() const;`
  - `const int *values() const;`
  - `void valueChanged(int stage, int dpi);` (signal)
  - `void stageActivated(int stage);` (signal)

Layout contract (widget is 700×150 in the final UI): track baseline at `height() - 48`, points on that line, values above points, `50`/`26000` labels below the track.

- [x] **Step 1: Write the header**

Content of `dpibar.h`:

```cpp
#ifndef DPIBAR_H
#define DPIBAR_H

#include <QWidget>

class QLineEdit;

class DpiBarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DpiBarWidget(QWidget *parent = nullptr);

    void setValues(const int values[6]);
    void setActiveStage(int stage);
    int activeStage() const { return m_activeStage; }
    const int *values() const { return m_values; }

signals:
    void valueChanged(int stage, int dpi);
    void stageActivated(int stage);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QRect trackRect() const;
    QPoint pointCenter(int stage) const;
    int hitTestPoint(const QPoint &p) const;
    void stepValue(int stage, int delta);
    void openEditor(int stage);
    void closeEditor();
    void applyEditedValue();

    int m_values[6] = {800, 1600, 2400, 3200, 5000, 22000};
    int m_activeStage = 1;
    int m_dragStage = -1;
    int m_dragStartValue = 0;
    QLineEdit *m_editor = nullptr;
    int m_editStage = 0;
};

#endif // DPIBAR_H
```

- [x] **Step 2: Write the implementation**

Content of `dpibar.cpp`:

```cpp
#include "dpibar.h"
#include "dpiscale.h"

#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kMargin = 40;      // horizontal padding of track inside widget
constexpr int kTrackHeight = 8;
constexpr int kPointRadius = 7;  // 14px diameter points
constexpr int kHitRadius = 8;    // hit-test tolerance around point center
constexpr int kLabelAbove = 34;  // px from point center up to its value label
constexpr int kEditorWidth = 100;
constexpr int kEditorHeight = 24;

const QColor kAccent(42, 130, 218);        // #2a82da
const QColor kInactive(81, 73, 74);        // #51494a
const QColor kInactiveBorder(140, 140, 140);
const QColor kLabelGray(170, 170, 170);
const QColor kTrackDark(40, 40, 40);
const QColor kTrackLight(70, 70, 70);
} // namespace

DpiBarWidget::DpiBarWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(500, 120);
}

QRect DpiBarWidget::trackRect() const
{
    const int y = height() - kTrackHeight - 42;
    return QRect(kMargin, y, width() - 2 * kMargin, kTrackHeight);
}

QPoint DpiBarWidget::pointCenter(int stage) const
{
    const QRect t = trackRect();
    const double pos = dpiscale::dpiToPos(m_values[stage]);
    const int x = t.left() + static_cast<int>(std::llround(pos * t.width()));
    return QPoint(x, t.center().y());
}

int DpiBarWidget::hitTestPoint(const QPoint &p) const
{
    const int hitSq = kHitRadius * kHitRadius;
    int best = -1;
    int bestSq = hitSq;
    for (int i = 0; i < dpiscale::kNumStages; ++i) {
        const QPoint c = pointCenter(i);
        const int dx = c.x() - p.x();
        const int dy = c.y() - p.y();
        const int d = dx * dx + dy * dy;
        if (d <= bestSq) {
            bestSq = d;
            best = i;
        }
    }
    return best;
}

void DpiBarWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect track = trackRect();
    const int bandY = track.center().y();

    // Track base
    QLinearGradient grad(track.left(), 0, track.right(), 0);
    grad.setColorAt(0.0, kTrackDark);
    grad.setColorAt(1.0, kTrackLight);
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawRoundedRect(track, kTrackHeight / 2, kTrackHeight / 2);

    // Accent fill between leftmost point and the active point
    const int xLeft = pointCenter(0).x();
    const int xActive = pointCenter(m_activeStage - 1).x();
    if (xActive > xLeft) {
        p.setBrush(kAccent);
        p.drawRoundedRect(QRect(xLeft, track.top(), xActive - xLeft, track.height()),
                          kTrackHeight / 2, kTrackHeight / 2);
    }

    // Points + value labels
    for (int i = 0; i < dpiscale::kNumStages; ++i) {
        const QPoint c = pointCenter(i);
        const bool active = (i + 1 == m_activeStage);

        QFont f = p.font();
        f.setPixelSize(active ? 12 : 10);
        f.setBold(active);
        p.setFont(f);

        const QString text = QString::number(m_values[i]);
        const QFontMetrics fm(f);
        const int tw = fm.horizontalAdvance(text);
        const QRect lr(c.x() - tw / 2 - 3, bandY - kLabelAbove, tw + 6, fm.height());
        p.setPen(active ? kAccent.lighter(155) : kLabelGray);
        p.drawText(lr, Qt::AlignCenter, text);

        p.setPen(active ? Qt::white : kInactiveBorder);
        p.setBrush(active ? kAccent : kInactive);
        p.drawEllipse(c, kPointRadius, kPointRadius);
    }

    // Range extremes
    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(kLabelGray);
    p.drawText(QRect(track.left() - 34, bandY + 14, 60, 14),
               Qt::AlignCenter, QStringLiteral("50"));
    p.drawText(QRect(track.right() - 26, bandY + 14, 60, 14),
               Qt::AlignCenter, QStringLiteral("26000"));
}

void DpiBarWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const int stage = hitTestPoint(event->pos());
    if (stage < 0)
        return;
    m_dragStage = stage;
    m_dragStartValue = m_values[stage];
    setActiveStage(stage + 1);
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void DpiBarWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragStage < 0)
        return;
    const QRect t = trackRect();
    const double pos = (event->pos().x() - t.left()) / static_cast<double>(t.width());
    m_values[m_dragStage] = dpiscale::posToDpi(pos, true);
    dpiscale::clampOrdered(m_values);
    update();
    event->accept();
}

void DpiBarWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragStage < 0)
        return;
    const int stage = m_dragStage;
    m_dragStage = -1;
    unsetCursor();
    update();
    if (m_values[stage] != m_dragStartValue)
        emit valueChanged(stage + 1, m_values[stage]);
    event->accept();
}

void DpiBarWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const int stage = hitTestPoint(event->pos());
    if (stage >= 0)
        openEditor(stage);
}

void DpiBarWidget::wheelEvent(QWheelEvent *event)
{
    const int deltaY = event->angleDelta().y();
    if (deltaY == 0) {
        event->ignore();
        return;
    }
    const int dir = (deltaY > 0) ? dpiscale::kStep : -dpiscale::kStep;

    int stage = hitTestPoint(event->pos());
    if (stage < 0) {
        // On the track but not on a point: adjust the active stage.
        const QRect band = trackRect().adjusted(-40, -16, 40, 16);
        if (!band.contains(event->pos())) {
            event->ignore();
            return;
        }
        stage = m_activeStage - 1;
    }
    stepValue(stage, dir);
    event->accept();
}

void DpiBarWidget::keyPressEvent(QKeyEvent *event)
{
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    int delta = 0;
    switch (event->key()) {
    case Qt::Key_W:
    case Qt::Key_Up:
    case Qt::Key_Right:
        delta = shift ? 250 : 50;
        break;
    case Qt::Key_S:
    case Qt::Key_Down:
    case Qt::Key_Left:
        delta = shift ? -250 : -50;
        break;
    case Qt::Key_Tab:
        setActiveStage(m_activeStage < dpiscale::kNumStages ? m_activeStage + 1 : 1);
        event->accept();
        return;
    case Qt::Key_Backtab:
        setActiveStage(m_activeStage > 1 ? m_activeStage - 1 : dpiscale::kNumStages);
        event->accept();
        return;
    case Qt::Key_F2:
        if (m_editor == nullptr)
            openEditor(m_activeStage - 1);
        event->accept();
        return;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    stepValue(m_activeStage - 1, delta);
    event->accept();
}

bool DpiBarWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_editor && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            closeEditor();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void DpiBarWidget::stepValue(int stage, int delta)
{
    const int next = dpiscale::roundToStep(m_values[stage] + delta);
    if (next == m_values[stage])
        return;
    m_values[stage] = next;
    dpiscale::clampOrdered(m_values);
    update();
    emit valueChanged(stage + 1, m_values[stage]);
}

void DpiBarWidget::openEditor(int stage)
{
    closeEditor();
    m_editStage = stage;

    m_editor = new QLineEdit(this);
    m_editor->setText(QString::number(m_values[stage]));
    m_editor->selectAll();
    m_editor->setAlignment(Qt::AlignCenter);
    m_editor->setFixedSize(kEditorWidth, kEditorHeight);

    const QPoint c = pointCenter(stage);
    const int x = std::clamp(c.x() - kEditorWidth / 2, 0, width() - kEditorWidth);
    const int y = std::clamp(c.y() - kEditorHeight / 2, 4, height() - kEditorHeight - 4);
    m_editor->move(x, y);

    m_editor->installEventFilter(this);
    connect(m_editor, &QLineEdit::editingFinished,
            this, &DpiBarWidget::applyEditedValue);
    m_editor->show();
    m_editor->setFocus();
    update();
}

void DpiBarWidget::closeEditor()
{
    if (!m_editor)
        return;
    m_editor->disconnect(this);
    m_editor->removeEventFilter(this);
    m_editor->deleteLater();
    m_editor = nullptr;
    setFocus();
    update();
}

void DpiBarWidget::applyEditedValue()
{
    if (!m_editor)
        return;
    bool ok = false;
    const int raw = m_editor->text().toInt(&ok);
    closeEditor();
    if (!ok)
        return;
    const int val = dpiscale::roundToStep(raw);
    m_values[m_editStage] = val;
    dpiscale::clampOrdered(m_values);
    update();
    emit valueChanged(m_editStage + 1, m_values[m_editStage]);
}

void DpiBarWidget::setValues(const int *values)
{
    bool changed = false;
    for (int i = 0; i < dpiscale::kNumStages; ++i) {
        if (m_values[i] != values[i]) {
            m_values[i] = values[i];
            changed = true;
        }
    }
    if (dpiscale::clampOrdered(m_values))
        changed = true;
    if (changed) {
        update();
        for (int i = 0; i < dpiscale::kNumStages; ++i)
            emit valueChanged(i + 1, m_values[i]);
    }
}

void DpiBarWidget::setActiveStage(int stage)
{
    const int s = std::clamp(stage, 1, dpiscale::kNumStages);
    if (m_activeStage == s)
        return;
    m_activeStage = s;
    update();
    emit stageActivated(s);
}
```

Note: `setValues(const int *values)` (array parameter decays; matches the `const int values[6]` in the header declaration).

- [x] **Step 3: Add sources to the app target in `CMakeLists.txt`**

In the Qt 6 `qt_add_executable(attackshark-x11 ...)` block, after the `dpi.h dpi.cpp` line (lines 30–34), add:

```cmake
        dpi.h dpi.cpp
        dpiscale.h dpiscale.cpp
        dpibar.h dpibar.cpp
```

- [x] **Step 4: Configure and build — must compile cleanly**

Run: `cmake -S . -B build && cmake --build build -j$(nproc)`
Expected: build succeeds with no errors. The app binary is not run yet (it will crash-free only after Task 3 wiring is done; but it still compiles because the widget is not yet instantiated).

- [x] **Step 5: Commit**

```bash
git add dpibar.h dpibar.cpp CMakeLists.txt
git commit -m "feat: add DpiBarWidget GHub-style DPI bar (render + interaction)"
```

---

### Task 3: Integrate into the main window

**Files:**
- Modify: `atsx11.h`
- Modify: `atsx11.cpp`
- Modify: `atsx11.ui`

**Interfaces:**
- Consumes: `DpiBarWidget` (Task 2) and `dpiscale::kNumStages` (Task 1).
- Produces: nothing new; keeps `applySettingsFromUser(path, color, prate, angleSnap, keyResp, sleep, deepSleep, ripple, dpi[6], activeStage)` call signature and QSettings keys `dpiValues`/`activeDpiStage` identical.

- [x] **Step 1: Edit `atsx11.ui` — resize window to 740×520**

In three places (the `geometry` property inside the `QMainWindow` `atsx11`, `minimumSize`, and `maximumSize`), change `720`→`740` and `440`→`520`:

1. `geometry` (lines ~11–19):
   `<width>720</width>` → `<width>740</width>`; `<height>440</height>` → `<height>520</height>`
2. `minimumSize` (lines ~22–27):
   `<width>720</width>` → `<width>740</width>`; `<height>440</height>` → `<height>520</height>`
3. `maximumSize` (lines ~28–33):
   `<width>720</width>` → `<width>740</width>`; `<height>440</height>` → `<height>520</height>`

- [x] **Step 2: Edit `atsx11.ui` — delete the DPI grid widget**

Delete the entire block (original lines 346–492), which starts with:

```xml
   <widget class="QWidget" name="gridLayoutWidget_dpi">
    <property name="geometry">
     <rect>
      <x>220</x>
      <y>282</y>
      <width>491</width>
      <height>80</height>
     </rect>
    </property>
    <layout class="QGridLayout" name="gridLayout_dpi">
```

and ends with:

```xml
     </layout>
    </widget>
   </widget>
```

just before the `<widget class="QWidget" name="gridLayoutWidget_6">` line (original line 493). The block to remove contains exactly: `lbl_dpiStage`, `cbox_dpiStage`, `horizontalSpacer_dpi`, `lbl_dpi1..6`, `spn_dpi1..6`, and `spn_dpi6`'s closing tag as above. After deletion, `gridLayoutWidget_6` must be the next widget after `gridLayoutWidget_5`.

Verification of the edit: `grep -c "spn_dpi" atsx11.ui` → `0`.

- [x] **Step 3: Edit `atsx11.ui` — move the bottom bar down**

The widget `<widget class="QWidget" name="horizontalLayoutWidget">` currently has (original lines 2696–2704):

```xml
   <widget class="QWidget" name="horizontalLayoutWidget">
    <property name="geometry">
      <rect>
       <x>0</x>
       <y>370</y>
       <width>721</width>
      <height>58</height>
     </rect>
    </property>
```

Change to:

```xml
   <widget class="QWidget" name="horizontalLayoutWidget">
    <property name="geometry">
      <rect>
       <x>0</x>
       <y>450</y>
       <width>740</width>
      <height>58</height>
     </rect>
    </property>
```

Do NOT touch `verticalLayoutWidget` (the Apply button stays at `(390, 210, 321, 61)`).

- [x] **Step 4: Edit `atsx11.h`**

Add `#include "dpibar.h"` and the member + slots:

```cpp
#include <QMainWindow>
#include <QFutureWatcher>
#include "dpibar.h"

class QCloseEvent;
class settings;
```

Add two private slots after `void reloadSettingsUi();`:

```cpp
    void onDpiValueChanged(int stage, int dpi);
    void onDpiStageActivated(int stage);
```

Add the member after `settings *m_settings = nullptr;`:

```cpp
    DpiBarWidget *m_dpiBar = nullptr;
```

- [x] **Step 5: Edit `atsx11.cpp` — constructor: instantiate widget + connect**

In `atsx11::atsx11()`, insert before `loadSettings();`:

```cpp
    m_dpiBar = new DpiBarWidget(this);
    m_dpiBar->setGeometry(20, 278, 700, 150);
```

and after `loadSettings();`:

```cpp
    connect(m_dpiBar, &DpiBarWidget::valueChanged,
            this, &atsx11::onDpiValueChanged);
    connect(m_dpiBar, &DpiBarWidget::stageActivated,
            this, &atsx11::onDpiStageActivated);
```

- [x] **Step 6: Edit `atsx11.cpp` — `updateInfoLabels()`**

Replace lines 56–62 (the spinbox DPI block):

```cpp
    const int stage = ui->cbox_dpiStage->currentIndex();
    QSpinBox *dpiSpinboxes[6] = {
        ui->spn_dpi1, ui->spn_dpi2, ui->spn_dpi3,
        ui->spn_dpi4, ui->spn_dpi5, ui->spn_dpi6
    };
    ui->lbl_activeDpiValue->setText(
        QString::number(dpiSpinboxes[stage]->value()) + QStringLiteral(" DPI"));
```

with:

```cpp
    const int active = m_dpiBar->activeStage();
    ui->lbl_activeDpiValue->setText(
        QString::number(m_dpiBar->values()[active - 1]) + QStringLiteral(" DPI"));
```

- [x] **Step 7: Edit `atsx11.cpp` — `loadSettings()` DPI block**

Replace lines 120–137 (the DPI load using spinboxes/combo):

```cpp
    // Load DPI
    const QVariantList dpiList = qSettings.value(
        QStringLiteral("dpiValues"),
        QVariantList{800, 1600, 2400, 3200, 5000, 22000}).toList();
    const int dpiDefaults[6] = {800, 1600, 2400, 3200, 5000, 22000};
    QSpinBox *dpiSpinboxes[6] = {
        ui->spn_dpi1, ui->spn_dpi2, ui->spn_dpi3,
        ui->spn_dpi4, ui->spn_dpi5, ui->spn_dpi6
    };
    for (int i = 0; i < 6; ++i) {
        const int val = (i < dpiList.size())
            ? dpiList[i].toInt() : dpiDefaults[i];
        dpiSpinboxes[i]->setValue(std::clamp(val, 50, 26000));
    }
    const int activeStage = qSettings.value(
        QStringLiteral("activeDpiStage"), 1).toInt();
    ui->cbox_dpiStage->setCurrentIndex(
        std::clamp(activeStage, 1, 6) - 1);
```

with:

```cpp
    // Load DPI
    const QVariantList dpiList = qSettings.value(
        QStringLiteral("dpiValues"),
        QVariantList{800, 1600, 2400, 3200, 5000, 22000}).toList();
    const int dpiDefaults[6] = {800, 1600, 2400, 3200, 5000, 22000};
    int dpi[6] = {};
    for (int i = 0; i < 6; ++i)
        dpi[i] = (i < dpiList.size()) ? dpiList[i].toInt() : dpiDefaults[i];
    m_dpiBar->setValues(dpi);
    const int activeStage = qSettings.value(
        QStringLiteral("activeDpiStage"), 1).toInt();
    m_dpiBar->setActiveStage(std::clamp(activeStage, 1, 6));
```

- [x] **Step 8: Edit `atsx11.cpp` — `saveSettings()` DPI block**

Replace lines 157–163:

```cpp
    QVariantList dpiList;
    dpiList << ui->spn_dpi1->value() << ui->spn_dpi2->value()
            << ui->spn_dpi3->value() << ui->spn_dpi4->value()
            << ui->spn_dpi5->value() << ui->spn_dpi6->value();
    qSettings.setValue(QStringLiteral("dpiValues"), dpiList);
    qSettings.setValue(QStringLiteral("activeDpiStage"),
        ui->cbox_dpiStage->currentIndex() + 1);
```

with:

```cpp
    QVariantList dpiList;
    for (int i = 0; i < 6; ++i)
        dpiList << m_dpiBar->values()[i];
    qSettings.setValue(QStringLiteral("dpiValues"), dpiList);
    qSettings.setValue(QStringLiteral("activeDpiStage"),
        m_dpiBar->activeStage());
```

- [x] **Step 9: Edit `atsx11.cpp` — `reloadSettingsUi()` DPI block**

Apply the identical replacement as Step 7 (the block is duplicated verbatim at original lines 291–308).

- [x] **Step 10: Edit `atsx11.cpp` — `on_btn_apply_clicked()` DPI block**

Replace lines 184–188:

```cpp
    int dpi[6] = {
        ui->spn_dpi1->value(), ui->spn_dpi2->value(), ui->spn_dpi3->value(),
        ui->spn_dpi4->value(), ui->spn_dpi5->value(), ui->spn_dpi6->value()
    };
    int activeDpiStage = ui->cbox_dpiStage->currentIndex() + 1;
```

with:

```cpp
    int dpi[6];
    for (int i = 0; i < 6; ++i)
        dpi[i] = m_dpiBar->values()[i];
    const int activeDpiStage = m_dpiBar->activeStage();
```

- [x] **Step 11: Add the two new slots to `atsx11.cpp`**

Place after `on_btn_apply_clicked()`:

```cpp
void atsx11::onDpiValueChanged(int, int)
{
    updateInfoLabels();
    saveSettings();
}

void atsx11::onDpiStageActivated(int)
{
    updateInfoLabels();
    saveSettings();
}
```

- [x] **Step 12: Build**

Run: `cmake -S . -B build && cmake --build build -j$(nproc)`
Expected: build succeeds (AUTOUIC regenerates `ui_atsx11.h` without the deleted widgets; no references to `spn_dpi*`/`cbox_dpiStage` remain in `atsx11.cpp`).

Double-check no stale references:
`grep -rn "spn_dpi\|cbox_dpiStage" atsx11.cpp atsx11.h` → no output.

- [x] **Step 13: Regression + smoke test**

Run: `./build/dpiscale_tests` and `./build/dpi_encoding_tests` — both PASS.

Manual smoke (requires root + the X11 mouse dongle, e.g. via `sudo` if your environment allows; otherwise skip and note it):
- Launch the app, confirm the DPI bar renders at the bottom-center area below the mouse image with 6 points.
- Drag a point → value follows the cursor, snaps to steps of 50, neighbors don't cross.
- Click a point → becomes active (accent ring); top info label updates to that DPI.
- Double-click a point → inline editor; type `1600` + Enter → applied.
- Wheel over a point → ±50; wheel over the track → active stage changes by ±50.
- Close + reopen → values and active stage persist.
- Apply with the dongle connected → mouse sensitivity follows the active DPI.

- [x] **Step 14: Commit**

```bash
git add atsx11.ui atsx11.h atsx11.cpp
git commit -m "feat: integrate DPI bar widget, remove spinbox grid"
```

---

### Task 4: Final verification and docs check

**Files:**
- Modify: none expected (verification task; commit only if a change is needed)
- Verify: `README.md`, `CMakeLists.txt`, all tests

- [x] **Step 1: Clean rebuild**

Run: `rm -rf build && cmake -S . -B build && cmake --build build -j$(nproc)`
(If `rm -rf` is blocked by tool permissions, run `cmake -S . -B build --fresh && cmake --build build -j$(nproc)` instead.)
Expected: clean configure + build with no errors/warnings beyond pre-existing ones.

- [x] **Step 2: Run full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: 2/2 tests pass — `dpiscale_tests` and `dpi_encoding_tests`.

- [x] **Step 3: Verify README**

Run: `grep -n "DPI Configuration" README.md` — line 44 must still read `- [x] DPI Configuration`. Do not edit.

- [x] **Step 4: Verify git state**

Run: `git status --short`
Expected: working tree clean (all changes committed in Tasks 1–3). If any file is dirty (e.g. regenerated lockfile), inspect before deciding; commit only meaningful changes.

- [x] **Step 5: Commit only if there are changes**

```bash
git add -A
git commit -m "chore: final DPI bar verification"   # only if Step 4 showed pending files
```

If the tree is clean, no commit is needed (matches project precedent: the previous plan's Task 4 was a verified no-op).

---

## Execution Record (2026-09-14)

**Method:** superpowers:subagent-driven-development (subagent drivers per task, task review after each, whole-branch review at the end). Implemented directly on `main` (user consent from previous plan).

**Plan corrections made during execution (all reviewed and ruled by the controller):**
- `1f45011` docs: fix `dpiToPos` test sample `2000 → 1000` (log midpoint of [50,26000] is ~1140, so `dpiToPos(2000) ≈ 0.59` violated the strict `p2 < 0.5` assertion).
- `48f05e8` docs: fix drag-release-commit semantics (per-move `valueChanged` contradicted the spec's "release-commit only") and add the Tab/Backtab/F2 keyboard paths mandated by the spec.

**Commits (on `main`, ordered):**

| SHA | Subject |
|---|---|
| `c7a3c58` | feat: add dpiscale logarithmic DPI bar mapping with unit tests |
| `6c21cd3` | feat: add GHub-style DpiBarWidget for DPI stage editing |
| `986ef4d` | fix: commit drag on release and add Tab/Backtab/F2 keyboard paths |
| `b5a2814` | feat: integrate DpiBarWidget into atsx11 window (replace DPI spinboxes) |
| `565ea54` | fix: grab mouse during drag, share DPI sources across Qt branches, per-stage setValues emission |

**Reviews:**
- Task 1: Approved (spec ✅, verbatim; no issues).
- Task 2: 2 Important plan-mandated defects found (per-move drag emit, missing Tab/Backtab/F2) → plan corrected + fixes re-reviewed → Approved.
- Task 3: Approved (verified `state.currentDpi` adaptation to the real QSettings pattern end-to-end).
- Task 4: PASS (clean rebuild, 2/2 tests, README line 44 intact, tracked tree clean).
- Whole-branch: 2 Important (phantom drag without `grabMouse()`, Qt5 target link regression) + 1 Minor included → single fix wave `565ea54` → re-review: all findings ADDRESSED, no new breakage.
- Deferred minors ruled ACCEPT: trailing newlines (repo style), track baseline `height()-50` vs layout-text `-48`, drag also moves the just-activated stage, 1-based active index, direct-save policy (1 write/drag with release-commit), `setValues` not rounding legacy off-grid values, hardcoded `std::clamp(1, 6)` (matches plan/style), no GUI test harness (no display env).

**Automated verification (final tree, HEAD `565ea54`):**
- `cmake -S . -B build && cmake --build build -j$(nproc)` — clean, no warnings (GCC 16.2.1, Qt 6.11.2).
- `ctest --test-dir build --output-on-failure` — 2/2 passed (`dpiscale_tests`, `dpi_encoding_tests`).
- `grep -c "spn_dpi\|cbox_dpiStage\|lbl_dpiStage" atsx11.ui→ 0` — no stale UI references.

**Manual hardware verification (2026-09-14, user-confirmed):** the app was launched as root via the built-in pkexec elevation flow on the desktop session and the user confirmed the interface **works correctly** with the new GHub-style DPI bar (6 draggable points, log scale, no crash).

**Not touched:** `hook.cpp`, `hook.h`, `dpi.cpp`, `dpi.h`, `settings.cpp`, `settings.h`, `main.cpp`, `settings.ui`, `applySettingsFromUser`.