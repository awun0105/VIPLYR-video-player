#pragma once
#include <QSlider>

// A horizontal slider that jumps to the clicked position and reports the value
// under the mouse while hovering. It can mark values (chapter starts) and a range
// (an A-B loop).
class JumpSlider : public QSlider {
    Q_OBJECT
public:
    explicit JumpSlider(const QString &filledStyle, QWidget *parent = nullptr);

    void setMarks(const QVector<int> &values);
    // Highlights from..to; `to` < 0 marks `from` alone; `from` < 0 shows nothing.
    void setHighlight(int from, int to);

signals:
    void hoverValueChanged(int value);
    void hoverLeft();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int valueAt(int x) const;
    int xFor(int value) const;

    QVector<int> marks_;
    int highlightFrom_ = -1;
    int highlightTo_ = -1;
};
