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
    grabMouse();
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
    releaseMouse();
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

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint wheelPos = event->position().toPoint();
#else
    const QPoint wheelPos = event->pos();
#endif

    int stage = hitTestPoint(wheelPos);
    if (stage < 0) {
        // On the track but not on a point: adjust the active stage.
        const QRect band = trackRect().adjusted(-40, -16, 40, 16);
        if (!band.contains(wheelPos)) {
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
    int old[dpiscale::kNumStages];
    for (int i = 0; i < dpiscale::kNumStages; ++i)
        old[i] = m_values[i];
    for (int i = 0; i < dpiscale::kNumStages; ++i)
        m_values[i] = values[i];
    dpiscale::clampOrdered(m_values);
    for (int i = 0; i < dpiscale::kNumStages; ++i) {
        if (m_values[i] != old[i])
            emit valueChanged(i + 1, m_values[i]);
    }
    update();
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