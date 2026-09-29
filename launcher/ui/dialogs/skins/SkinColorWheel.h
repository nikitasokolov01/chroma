// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QColor>
#include <QImage>
#include <QWidget>

// An inline HSV picker. The hue ring and saturation/value square stay visible
// together; alpha and exact hexadecimal entry are supplied by the editor.
class SkinColorWheel : public QWidget {
    Q_OBJECT
   public:
    explicit SkinColorWheel(QWidget* parent = nullptr);
    QColor color() const { return QColor::fromHsvF(m_hue, m_saturation, m_value, m_alpha); }
    void setColor(const QColor& color);
    QSize sizeHint() const override { return { 188, 188 }; }

   signals:
    void colorChanged(QColor color);

   protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

   private:
    QRectF colorSquare() const;
    qreal radius() const;
    void choose(QPointF position);
    void notifyColor();
    enum Drag { None, Hue, SaturationValue };
    Drag m_drag = None;
    qreal m_hue = 0;
    qreal m_saturation = 0;
    qreal m_value = 1;
    qreal m_alpha = 1;
    QImage m_ring;
};
