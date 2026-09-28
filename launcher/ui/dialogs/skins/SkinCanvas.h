// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QWidget>

#include "minecraft/skins/SkinTextureDocument.h"

class SkinCanvas : public QWidget {
    Q_OBJECT
   public:
    enum Tool { Brush, Eraser, Eyedropper, Pan };
    explicit SkinCanvas(SkinTextureDocument* document, QWidget* parent = nullptr);
    void setTool(Tool tool);
    void setColor(QColor color) { m_color = color; }
    void setBrushSize(int size) { m_brushSize = size; }
    void setRegion(SkinTextureDocument::Part part, SkinTextureDocument::Layer layer);
    void setGridVisible(bool visible)
    {
        m_grid = visible;
        update();
    }
    void fitToView();
    void zoomBy(qreal factor);

   signals:
    void colorPicked(QColor color);
    void pixelHovered(QPoint pixel, QColor color);

   protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

   private:
    QPointF origin() const;
    QPoint pixelAt(QPointF point) const;
    void paintTo(QPoint pixel);
    void zoomAt(qreal factor, QPointF anchor);
    SkinTextureDocument* m_document;
    SkinTextureDocument::Part m_part = SkinTextureDocument::All;
    SkinTextureDocument::Layer m_layer = SkinTextureDocument::Both;
    Tool m_tool = Brush;
    QColor m_color = Qt::white;
    int m_brushSize = 1;
    qreal m_zoom = 5;
    QPointF m_pan;
    QPointF m_dragPosition;
    QPoint m_lastPixel;
    QPoint m_cursor = QPoint(8, 8);
    bool m_painting = false;
    bool m_panning = false;
    bool m_grid = true;
    bool m_fit = true;
};
