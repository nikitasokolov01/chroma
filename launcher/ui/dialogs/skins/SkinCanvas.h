// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QWidget>

#include "minecraft/skins/SkinTextureDocument.h"

class SkinCanvas : public QWidget {
    Q_OBJECT
   public:
    enum Tool { Brush, Eraser, Eyedropper, Pan, Bucket, Select, Texture };
    explicit SkinCanvas(SkinTextureDocument* document, QWidget* parent = nullptr);
    void setTool(Tool tool);
    void setColor(QColor color) { m_color = color; }
    void setBrushSize(int size) { m_brushSize = size; }
    void setTextureSettings(SkinTextureDocument::TextureSettings settings);
    void setRegion(SkinTextureDocument::Part part, SkinTextureDocument::Layer layer);
    void setLayerVisibility(bool body, bool outer);
    void setPartVisible(int part, bool visible);
    void setReadOnly(bool readOnly);
    bool isReadOnly() const { return m_readOnly; }
    void setBodyThroughOverlay(bool enabled);
    bool bodyThroughOverlay() const { return m_bodyThroughOverlay || m_shiftHeld; }
    bool copySelection();
    bool beginPaste();
    void cancelPaste();
    bool pastePending() const { return !m_paste.image.isNull(); }
    void setGridVisible(bool visible)
    {
        m_grid = visible;
        update();
    }
    void fitToView();
    void zoomBy(qreal factor);

   signals:
    void toolChanged(SkinCanvas::Tool tool);
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
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void hideEvent(QHideEvent* event) override;

   private:
    QPointF origin() const;
    QPoint pixelAt(QPointF point) const;
    void paintTo(QPoint pixel);
    void paintPixel(QPoint pixel);
    QRegion visibleRegion(SkinTextureDocument::Layer layer) const;
    QRegion editableRegion() const;
    SkinTextureDocument::Layer activeLayer() const;
    void finishInteraction();
    void updateModifiers(Qt::KeyboardModifiers modifiers);
    void selectTo(QPoint pixel);
    void zoomAt(qreal factor, QPointF anchor);
    SkinTextureDocument* m_document;
    SkinTextureDocument::Part m_part = SkinTextureDocument::All;
    SkinTextureDocument::Layer m_layer = SkinTextureDocument::Both;
    Tool m_tool = Brush;
    QColor m_color = Qt::white;
    int m_brushSize = 1;
    SkinTextureDocument::TextureSettings m_textureSettings;
    qreal m_zoom = 5;
    QPointF m_pan;
    QPointF m_dragPosition;
    QPoint m_lastPixel;
    QPoint m_cursor = QPoint(8, 8);
    bool m_painting = false;
    bool m_panning = false;
    bool m_grid = true;
    bool m_fit = true;
    bool m_bodyVisible = true;
    bool m_outerVisible = true;
    bool m_readOnly = false;
    bool m_bodyThroughOverlay = false;
    bool m_shiftHeld = false;
    bool m_selecting = false;
    QPoint m_selectionStart;
    SkinTextureDocument::PixelPatch m_paste;
    unsigned m_visibleParts = 0x3f;
};
