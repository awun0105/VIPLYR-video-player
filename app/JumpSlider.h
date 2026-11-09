#pragma once
#include <QSlider>

// A horizontal slider that jumps to the clicked position and reports the value
// under the mouse while hovering.
class JumpSlider : public QSlider {
    Q_OBJECT
public:
    explicit JumpSlider(const QString &filledStyle, QWidget *parent = nullptr);

signals:
    void hoverValueChanged(int value);
    void hoverLeft();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int valueAt(int x) const;
};
