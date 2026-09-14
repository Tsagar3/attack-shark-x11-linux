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
    QLineEdit *m_editor = nullptr;
    int m_editStage = 0;
};

#endif // DPIBAR_H