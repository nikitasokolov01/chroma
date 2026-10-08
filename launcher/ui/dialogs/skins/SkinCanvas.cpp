// SPDX-License-Identifier: GPL-3.0-only
#include "SkinCanvas.h"

#include <QHideEvent>
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
    connect(document, &SkinTextureDocument::selectionChanged, this, qOverload<>(&SkinCanvas::update));
}

void SkinCanvas::finishInteraction()
{
    m_document->endStroke();
    m_painting = m_panning = m_selecting = false;
}

void SkinCanvas::setReadOnly(bool readOnly)
{
    finishInteraction();
    m_readOnly = readOnly;
    if (readOnly)
        cancelPaste();
}

void SkinCanvas::setBodyThroughOverlay(bool enabled)
{
    const bool previous = bodyThroughOverlay();
    m_bodyThroughOverlay = enabled;
    if (!enabled)
        m_shiftHeld = false;
    if (previous != bodyThroughOverlay()) {
        finishInteraction();
        update();
    }
}

void SkinCanvas::updateModifiers(Qt::KeyboardModifiers modifiers)
{
    const bool previous = bodyThroughOverlay();
    m_shiftHeld = modifiers.testFlag(Qt::ShiftModifier);
    if (previous != bodyThroughOverlay()) {
        finishInteraction();
        update();
    }
}

SkinTextureDocument::Layer SkinCanvas::activeLayer() const
{
    return bodyThroughOverlay() ? SkinTextureDocument::Base : m_layer;
}

QRegion SkinCanvas::editableRegion() const
{
    return visibleRegion(activeLayer());
}

bool SkinCanvas::copySelection()
{
    return SkinTextureDocument::writeClipboard(m_document->copyPixels(visibleRegion(SkinTextureDocument::Both)));
}

bool SkinCanvas::beginPaste()
{
    finishInteraction();
    if (m_readOnly)
        return false;
    m_paste = SkinTextureDocument::readClipboard();
    update();
    return pastePending();
}

void SkinCanvas::cancelPaste()
{
    m_paste = {};
    update();
}

void SkinCanvas::selectTo(QPoint pixel)
{
    m_document->setSelection(QRegion(QRect(m_selectionStart, pixel).normalized()).intersected(editableRegion()));
}

void SkinCanvas::setTool(Tool tool)
{
    finishInteraction();
    cancelPaste();
    const bool changed = m_tool != tool;
    m_tool = tool;
    setCursor(tool == Pan ? Qt::OpenHandCursor : Qt::CrossCursor);
    if (changed)
        emit toolChanged(tool);
}

void SkinCanvas::setTextureSettings(SkinTextureDocument::TextureSettings settings)
{
    settings.strength = qBound(0, settings.strength, 100);
    if (settings.style == m_textureSettings.style && settings.strength == m_textureSettings.strength)
        return;
    finishInteraction();
    m_textureSettings = settings;
}

void SkinCanvas::setRegion(SkinTextureDocument::Part part, SkinTextureDocument::Layer layer)
{
    finishInteraction();
    m_part = part;
    m_layer = layer;
    update();
}

void SkinCanvas::setLayerVisibility(bool body, bool outer)
{
    finishInteraction();
    m_bodyVisible = body;
    m_outerVisible = outer;
    update();
}

void SkinCanvas::setPartVisible(int part, bool visible)
{
    if (part < SkinTextureDocument::Head || part > SkinTextureDocument::LeftLeg)
        return;
    finishInteraction();
    if (visible)
        m_visibleParts |= 1u << part;
    else
        m_visibleParts &= ~(1u << part);
    update();
}

QRegion SkinCanvas::visibleRegion(SkinTextureDocument::Layer layer) const
{
    QRegion region;
    for (int part = SkinTextureDocument::Head; part <= SkinTextureDocument::LeftLeg; ++part) {
        if (!(m_visibleParts & (1u << part)) || (m_part != SkinTextureDocument::All && m_part != part))
            continue;
        const auto bodyPart = static_cast<SkinTextureDocument::Part>(part);
        if (m_bodyVisible && layer != SkinTextureDocument::Overlay)
            region += SkinTextureDocument::uvRegion(bodyPart, SkinTextureDocument::Base, m_document->model());
        if (m_outerVisible && layer != SkinTextureDocument::Base)
            region += SkinTextureDocument::uvRegion(bodyPart, SkinTextureDocument::Overlay, m_document->model());
    }
    return region;
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
    const auto visible = visibleRegion(SkinTextureDocument::Both);
    const auto active = editableRegion();
    painter.save();
    painter.setClipRegion(visible, Qt::IntersectClip);
    painter.drawImage(QPoint(0, 0), m_document->image());
    painter.restore();
    painter.save();
    painter.setClipRegion(QRegion(textureRect).subtracted(active));
    painter.fillRect(textureRect, QColor(0, 0, 0, 160));
    painter.restore();
    if (m_grid && m_zoom >= 7) {
        painter.save();
        painter.setClipRegion(visible, Qt::IntersectClip);
        painter.setPen(QPen(QColor(100, 100, 100, 100), 0));
        for (int i = 0; i <= 64; ++i) {
            painter.drawLine(i, 0, i, 64);
            painter.drawLine(0, i, 64, i);
        }
        painter.restore();
    }
    painter.setPen(QPen(palette().color(QPalette::Highlight), 0));
    painter.setBrush(Qt::NoBrush);
    for (const auto& region : active)
        painter.drawRect(region);
    const auto selection = m_document->selection().intersected(visible);
    for (const auto& region : selection) {
        painter.fillRect(region, QColor(40, 180, 255, 75));
        painter.setPen(QPen(QColor(100, 220, 255), 0));
        painter.drawRect(region);
    }
    if (pastePending()) {
        painter.save();
        painter.setClipRegion(active, Qt::IntersectClip);
        if (m_document->hasSelection())
            painter.setClipRegion(m_document->selection(), Qt::IntersectClip);
        painter.setClipRegion(m_paste.mask.translated(m_cursor), Qt::IntersectClip);
        painter.setOpacity(.65);
        painter.drawImage(m_cursor, m_paste.image);
        painter.restore();
    }
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
    updateModifiers(event->modifiers());
    if (event->button() == Qt::MiddleButton || (m_tool == Pan && !pastePending() && event->button() == Qt::LeftButton)) {
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
    if (event->modifiers().testFlag(Qt::AltModifier)) {
        emit colorPicked(m_document->image().pixelColor(pixel));
    } else if (pastePending() && !m_readOnly) {
        m_document->pastePixels(m_paste, pixel, editableRegion());
        cancelPaste();
    } else if (m_tool == Eyedropper) {
        emit colorPicked(m_document->image().pixelColor(pixel));
    } else if (m_tool == Select) {
        m_selecting = true;
        m_selectionStart = pixel;
        selectTo(pixel);
    } else if (!m_readOnly && m_tool == Bucket) {
        m_document->floodFill(pixel, m_color, editableRegion());
    } else if (!m_readOnly && (m_tool == Brush || m_tool == Eraser || m_tool == Texture)) {
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
        paintPixel(point);
    }
    m_lastPixel = pixel;
}

void SkinCanvas::paintPixel(QPoint pixel)
{
    if (m_readOnly)
        return;
    if (m_tool == Texture)
        m_document->texturePixel(pixel, m_brushSize, m_part, activeLayer(), m_textureSettings, editableRegion());
    else
        m_document->paintPixel(pixel, m_color, m_brushSize, m_part, activeLayer(), m_tool == Eraser, editableRegion());
}

void SkinCanvas::mouseMoveEvent(QMouseEvent* event)
{
    updateModifiers(event->modifiers());
    if (!(event->buttons() & (Qt::LeftButton | Qt::MiddleButton))) {
        finishInteraction();
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
    if (m_selecting)
        selectTo(pixel);
    update();
}

void SkinCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_selecting && event->button() == Qt::LeftButton)
        selectTo(pixelAt(event->position()));
    finishInteraction();
    setCursor(m_tool == Pan ? Qt::OpenHandCursor : Qt::CrossCursor);
}

void SkinCanvas::wheelEvent(QWheelEvent* event)
{
    zoomAt(qPow(1.15, event->angleDelta().y() / 120.), event->position());
    event->accept();
}

void SkinCanvas::focusOutEvent(QFocusEvent* event)
{
    finishInteraction();
    m_shiftHeld = m_bodyThroughOverlay = false;
    update();
    QWidget::focusOutEvent(event);
}

void SkinCanvas::hideEvent(QHideEvent* event)
{
    finishInteraction();
    m_shiftHeld = m_bodyThroughOverlay = false;
    cancelPaste();
    QWidget::hideEvent(event);
}

void SkinCanvas::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Shift && !event->isAutoRepeat()) {
        finishInteraction();
        m_shiftHeld = m_bodyThroughOverlay = false;
        update();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void SkinCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Copy)) {
        copySelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        beginPaste();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Shift) {
        updateModifiers(event->modifiers() | Qt::ShiftModifier);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        finishInteraction();
        if (pastePending())
            cancelPaste();
        else
            m_document->clearSelection();
        event->accept();
        return;
    }
    // Keep single-key tools local to the canvas, including when the editor is
    // reparented into the inline workspace. Text inputs keep their own keys.
    if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        switch (event->key()) {
            case Qt::Key_B:
                setTool(Brush);
                event->accept();
                return;
            case Qt::Key_T:
                setTool(Texture);
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
            case Qt::Key_G:
                setTool(Bucket);
                event->accept();
                return;
            case Qt::Key_M:
                setTool(Select);
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
            if (pastePending() && !m_readOnly) {
                m_document->pastePixels(m_paste, m_cursor, editableRegion());
                cancelPaste();
            } else if (m_tool == Eyedropper)
                emit colorPicked(m_document->image().pixelColor(m_cursor));
            else if (m_tool == Select)
                m_document->setSelection(QRegion(QRect(m_cursor, QSize(1, 1))).intersected(editableRegion()));
            else if (!m_readOnly && m_tool == Bucket)
                m_document->floodFill(m_cursor, m_color, editableRegion());
            else if (!m_readOnly && (m_tool == Brush || m_tool == Eraser || m_tool == Texture)) {
                m_document->beginStroke();
                paintPixel(m_cursor);
                m_document->endStroke();
            }
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
