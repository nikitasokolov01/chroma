// SPDX-License-Identifier: GPL-3.0-only
#include "SkinColorWheel.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>
#include <cmath>

SkinColorWheel::SkinColorWheel(QWidget* parent) : QWidget(parent)
{
    setObjectName("skinColorWheel");
    setAccessibleName(tr("Brush color wheel"));
    setAccessibleDescription(
        tr("Drag the ring for hue and the square for saturation and brightness. "
           "Left and right change hue; up and down change brightness. Hold Shift to change saturation."));
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    setMinimumSize(144, 144);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

qreal SkinColorWheel::radius() const
{
    return qMin(width(), height()) / 2. - 7;
}

QRectF SkinColorWheel::colorSquare() const
{
    const qreal half = (radius() - 21) / qSqrt(2.);
    return QRectF(QPointF(width() / 2. - half, height() / 2. - half), QSizeF(half * 2, half * 2));
}

void SkinColorWheel::setColor(const QColor& value)
{
    if (!value.isValid())
        return;
    // Keep the chosen hue while moving through gray, black, or white.
    if (value.hsvHueF() >= 0)
        m_hue = value.hsvHueF();
    m_saturation = value.hsvSaturationF();
    m_value = value.valueF();
    m_alpha = value.alphaF();
    update();
}

void SkinColorWheel::paintEvent(QPaintEvent*)
{
    const qreal dpr = devicePixelRatioF();
    const QSize pixels(qRound(width() * dpr), qRound(height() * dpr));
    const QPointF center(width() / 2., height() / 2.);
    const qreal outer = radius();
    const qreal inner = outer - 16;
    if (m_ring.size() != pixels || m_ring.devicePixelRatio() != dpr) {
        m_ring = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
        m_ring.setDevicePixelRatio(dpr);
        m_ring.fill(Qt::transparent);
        for (int y = 0; y < pixels.height(); ++y) {
            auto* row = reinterpret_cast<QRgb*>(m_ring.scanLine(y));
            for (int x = 0; x < pixels.width(); ++x) {
                const QPointF delta = QPointF((x + .5) / dpr, (y + .5) / dpr) - center;
                const qreal distance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
                const qreal coverage = qBound(0., qMin(outer - distance, distance - inner) * dpr + .5, 1.);
                if (coverage <= 0)
                    continue;
                qreal hue = qAtan2(-delta.y(), delta.x()) / (2 * M_PI);
                if (hue < 0)
                    hue += 1;
                row[x] = qPremultiply(QColor::fromHsvF(hue, 1, 1, coverage).rgba());
            }
        }
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.drawImage(QPoint(), m_ring);
    const auto square = colorSquare();
    QLinearGradient saturation(square.topLeft(), square.topRight());
    saturation.setColorAt(0, Qt::white);
    saturation.setColorAt(1, QColor::fromHsvF(m_hue, 1, 1));
    painter.fillRect(square, saturation);
    QLinearGradient value(square.topLeft(), square.bottomLeft());
    value.setColorAt(0, QColor(0, 0, 0, 0));
    value.setColorAt(1, Qt::black);
    painter.fillRect(square, value);
    const QPointF huePoint = center + QPointF(qCos(m_hue * 2 * M_PI), -qSin(m_hue * 2 * M_PI)) * (outer - 8);
    const QPointF svPoint(square.left() + m_saturation * square.width(), square.top() + (1 - m_value) * square.height());
    for (auto point : { huePoint, svPoint }) {
        painter.setPen(QPen(Qt::black, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(point, 4.5, 4.5);
        painter.setPen(QPen(Qt::white, 1.5));
        painter.drawEllipse(point, 4.5, 4.5);
    }
    if (hasFocus()) {
        painter.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DashLine));
        painter.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 8, 8);
    }
}

void SkinColorWheel::notifyColor()
{
    update();
    emit colorChanged(color());
}

void SkinColorWheel::choose(QPointF position)
{
    if (m_drag == Hue) {
        const auto delta = position - QPointF(width() / 2., height() / 2.);
        m_hue = qAtan2(-delta.y(), delta.x()) / (2 * M_PI);
        if (m_hue < 0)
            m_hue += 1;
    } else if (m_drag == SaturationValue) {
        const auto square = colorSquare();
        m_saturation = qBound(0., (position.x() - square.left()) / square.width(), 1.);
        m_value = 1 - qBound(0., (position.y() - square.top()) / square.height(), 1.);
    }
    if (m_drag != None)
        notifyColor();
}

void SkinColorWheel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_drag = None;
    setFocus(Qt::MouseFocusReason);
    const auto delta = event->position() - QPointF(width() / 2., height() / 2.);
    const auto distance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
    if (distance >= radius() - 20 && distance <= radius() + 4)
        m_drag = Hue;
    else if (colorSquare().contains(event->position()))
        m_drag = SaturationValue;
    choose(event->position());
}

void SkinColorWheel::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons().testFlag(Qt::LeftButton))
        choose(event->position());
    else
        m_drag = None;
}

void SkinColorWheel::mouseReleaseEvent(QMouseEvent*)
{
    m_drag = None;
}

void SkinColorWheel::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
        case Qt::Key_Left:
            m_hue = std::fmod(m_hue + 359. / 360., 1.);
            break;
        case Qt::Key_Right:
            m_hue = std::fmod(m_hue + 1. / 360., 1.);
            break;
        case Qt::Key_Up:
        case Qt::Key_Down: {
            auto& value = event->modifiers().testFlag(Qt::ShiftModifier) ? m_saturation : m_value;
            value = qBound(0., value + (event->key() == Qt::Key_Up ? .01 : -.01), 1.);
            break;
        }
        default:
            QWidget::keyPressEvent(event);
            return;
    }
    notifyColor();
    event->accept();
}
