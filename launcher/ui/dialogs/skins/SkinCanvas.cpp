// SPDX-License-Identifier: GPL-3.0-only
#include "SkinCanvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>

SkinCanvas::SkinCanvas(SkinTextureDocument* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    setMinimumSize(256, 256);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(tr("Skin texture canvas"));
    setAccessibleDescription(tr("Arrow keys move the pixel cursor. Space paints. Plus and minus zoom. Middle mouse drags the canvas."));
    connect(document, &SkinTextureDocument::changed, this, qOverload<>(&SkinCanvas::update));
}

void SkinCanvas::setTool(Tool tool)
{
    m_document->endStroke();
    m_painting = m_panning = false;
    const bool changed = m_tool != tool;
    m_tool = tool;
    setCursor(tool == Pan ? Qt::OpenHandCursor : Qt::CrossCursor);
    if (changed)
        emit toolChanged(tool);
}

void SkinCanvas::setRegion(SkinTextureDocument::Part part, SkinTextureDocument::Layer layer)
{
    m_part = part;
    m_layer = layer;
    update();
}

QPointF SkinCanvas::origin() const
{
    return QPointF((width() - 64 * m_zoom) / 2., (height() - 64 * m_zoom) / 2.) + m_pan;
}

QPoint SkinCanvas::pixelAt(QPointF point) const
{
    const auto pixel = (point - origin()) / m_zoom;
    return QPoint(qFloor(pixel.x()), qFloor(pixel.y()));
}

void SkinCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().brush(QPalette::Base));
    painter.translate(origin());
    painter.scale(m_zoom, m_zoom);
    const QRect textureRect(0, 0, 64, 64);
    painter.setClipRect(textureRect);
    for (int y = 0; y < 64; y += 4)
        for (int x = 0; x < 64; x += 4)
            painter.fillRect(QRect(x, y, 4, 4), palette().color(((x + y) / 4) % 2 ? QPalette::Base : QPalette::AlternateBase));
    painter.drawImage(QPoint(0, 0), m_document->image());
    auto active = SkinTextureDocument::uvRegion(m_part, m_layer, m_document->model());
    painter.save();
    painter.setClipRegion(QRegion(textureRect).subtracted(active));
    painter.fillRect(textureRect, QColor(0, 0, 0, 160));
    painter.restore();
    if (m_grid && m_zoom >= 7) {
        painter.setPen(QPen(QColor(100, 100, 100, 100), 0));
        for (int i = 0; i <= 64; ++i) {
            painter.drawLine(i, 0, i, 64);
            painter.drawLine(0, i, 64, i);
        }
    }
    painter.setPen(QPen(palette().color(QPalette::Highlight), 0));
    painter.setBrush(Qt::NoBrush);
    for (const auto& region : active)
        painter.drawRect(region);
    if (hasFocus()) {
        painter.setPen(QPen(Qt::white, 0));
        painter.drawRect(QRectF(m_cursor.x(), m_cursor.y(), 1, 1));
    }
}

void SkinCanvas::fitToView()
{
    m_fit = true;
    m_zoom = qBound(2., (qMin(width(), height()) - 24.) / 64., 24.);
    m_pan = {};
    update();
}

void SkinCanvas::resizeEvent(QResizeEvent*)
{
    if (m_fit)
        fitToView();
}

void SkinCanvas::zoomAt(qreal factor, QPointF anchor)
{
    const QPointF texel = (anchor - origin()) / m_zoom;
    m_zoom = qBound(2., m_zoom * factor, 40.);
    m_pan += anchor - (origin() + texel * m_zoom);
    m_fit = false;
    update();
}

void SkinCanvas::zoomBy(qreal factor)
{
    zoomAt(factor, rect().center());
}

void SkinCanvas::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    if (event->button() == Qt::MiddleButton || (m_tool == Pan && event->button() == Qt::LeftButton)) {
        m_panning = true;
        m_dragPosition = event->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    const auto pixel = pixelAt(event->position());
    if (!m_document->image().rect().contains(pixel))
        return;
    m_cursor = pixel;
    if (m_tool == Eyedropper || event->modifiers().testFlag(Qt::AltModifier)) {
        emit colorPicked(m_document->image().pixelColor(pixel));
    } else {
        m_painting = true;
        m_lastPixel = pixel;
        m_document->beginStroke();
        paintTo(pixel);
    }
    update();
}

void SkinCanvas::paintTo(QPoint pixel)
{
    // Interpolate strokes so fast pointer movement cannot leave holes.
    const int steps = qMax(qAbs(pixel.x() - m_lastPixel.x()), qAbs(pixel.y() - m_lastPixel.y()));
    for (int i = 0; i <= steps; ++i) {
        const qreal amount = steps ? qreal(i) / steps : 0;
        const QPoint point(qRound(m_lastPixel.x() + (pixel.x() - m_lastPixel.x()) * amount),
                           qRound(m_lastPixel.y() + (pixel.y() - m_lastPixel.y()) * amount));
        m_document->paintPixel(point, m_color, m_brushSize, m_part, m_layer, m_tool == Eraser);
    }
    m_lastPixel = pixel;
}

void SkinCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & (Qt::LeftButton | Qt::MiddleButton))) {
        m_painting = m_panning = false;
        m_document->endStroke();
        setCursor(m_tool == Pan ? Qt::OpenHandCursor : Qt::CrossCursor);
    }
    if (m_panning) {
        m_pan += event->position() - m_dragPosition;
        m_dragPosition = event->position();
        m_fit = false;
        update();
        return;
    }
    const auto pixel = pixelAt(event->position());
    if (m_document->image().rect().contains(pixel)) {
        m_cursor = pixel;
        emit pixelHovered(pixel, m_document->image().pixelColor(pixel));
    }
    if (m_painting)
        paintTo(QPoint(qBound(0, pixel.x(), 63), qBound(0, pixel.y(), 63)));
    update();
}

void SkinCanvas::mouseReleaseEvent(QMouseEvent*)
{
    m_panning = m_painting = false;
    m_document->endStroke();
    setCursor(m_tool == Pan ? Qt::OpenHandCursor : Qt::CrossCursor);
}

void SkinCanvas::wheelEvent(QWheelEvent* event)
{
    zoomAt(qPow(1.15, event->angleDelta().y() / 120.), event->position());
    event->accept();
}

void SkinCanvas::focusOutEvent(QFocusEvent* event)
{
    m_painting = m_panning = false;
    m_document->endStroke();
    QWidget::focusOutEvent(event);
}

void SkinCanvas::keyPressEvent(QKeyEvent* event)
{
    // Keep single-key tools local to the canvas, including when the editor is
    // reparented into the inline workspace. Text inputs keep their own keys.
    if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        switch (event->key()) {
            case Qt::Key_B:
                setTool(Brush);
                event->accept();
                return;
            case Qt::Key_E:
                setTool(Eraser);
                event->accept();
                return;
            case Qt::Key_I:
                setTool(Eyedropper);
                event->accept();
                return;
            case Qt::Key_H:
                setTool(Pan);
                event->accept();
                return;
            default:
                break;
        }
    }
    switch (event->key()) {
        case Qt::Key_Left:
            m_cursor.rx() = qMax(0, m_cursor.x() - 1);
            break;
        case Qt::Key_Right:
            m_cursor.rx() = qMin(63, m_cursor.x() + 1);
            break;
        case Qt::Key_Up:
            m_cursor.ry() = qMax(0, m_cursor.y() - 1);
            break;
        case Qt::Key_Down:
            m_cursor.ry() = qMin(63, m_cursor.y() + 1);
            break;
        case Qt::Key_Space:
            if (m_tool == Eyedropper)
                emit colorPicked(m_document->image().pixelColor(m_cursor));
            else if (m_tool != Pan)
                m_document->paintPixel(m_cursor, m_color, m_brushSize, m_part, m_layer, m_tool == Eraser);
            break;
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            zoomBy(1.25);
            break;
        case Qt::Key_Minus:
            zoomBy(0.8);
            break;
        case Qt::Key_0:
            fitToView();
            break;
        default:
            QWidget::keyPressEvent(event);
            return;
    }
    emit pixelHovered(m_cursor, m_document->image().pixelColor(m_cursor));
    update();
    event->accept();
}
