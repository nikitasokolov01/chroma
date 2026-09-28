// SPDX-License-Identifier: GPL-3.0-only
#include "ClayStyle.h"

#include <QApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QtGlobal>

#include "Application.h"
#include "settings/SettingsObject.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
QColor mix(const QColor& first, const QColor& second, qreal amount)
{
    return QColor::fromRgbF(first.redF() * (1 - amount) + second.redF() * amount, first.greenF() * (1 - amount) + second.greenF() * amount,
                            first.blueF() * (1 - amount) + second.blueF() * amount);
}

// Filled translucent layers form a continuous falloff. Thin stroked rings
// accumulate into sharp double outlines, especially on saturated surfaces.
void outerLight(QPainter* painter, const QRectF& rect, qreal radius, QColor color, QPointF offset, int spread)
{
    painter->setPen(Qt::NoPen);
    for (int step = spread; step >= 1; --step) {
        QColor layer = color;
        layer.setAlphaF(color.alphaF() * (1.0 - qreal(step) / (spread + 1)));
        painter->setBrush(layer);
        const qreal growth = step * 0.5;
        painter->drawRoundedRect(rect.translated(offset).adjusted(-growth, -growth, growth, growth), radius + growth, radius + growth);
    }
}

// Clip shifted silhouettes to the surface to produce directional inner light.
// Only the narrow upper-left or lower-right crescent receives this shading;
// the flat center remains clear so label contrast is unchanged.
void innerLight(QPainter* painter, const QRectF& rect, qreal radius, QColor color, QPointF offset, int spread)
{
    for (int step = spread; step >= 1; --step) {
        QColor layer = color;
        layer.setAlphaF(color.alphaF() * (1.0 - qreal(step) / (spread + 1)));
        QPainterPath edge;
        edge.setFillRule(Qt::OddEvenFill);
        edge.addRect(rect.adjusted(-8, -8, 8, 8));
        edge.addRoundedRect(rect.translated(offset * (qreal(step) / spread)), radius, radius);
        painter->fillPath(edge, layer);
    }
}
}  // namespace

namespace Clay {

const Colors& colors(bool dark)
{
    static const Colors lightColors{ QColor("#F4F1FA"), QColor("#FAF8FF"), QColor("#EFEBF5"), QColor("#332F3A"), QColor("#635F69") };
    static const Colors darkColors{ QColor("#191622"), QColor("#292333"), QColor("#211C2B"), QColor("#F4EFFA"), QColor("#BEB4CB") };
    return dark ? darkColors : lightColors;
}

const Colors& colors()
{
    return colors(dark());
}

bool dark()
{
    const auto* application = APPLICATION_DYN;
    return application && application->settings() && application->settings()->get("ApplicationTheme").toString() == "chroma-dark";
}

bool enabled()
{
    const auto* application = APPLICATION_DYN;
    if (!application || !application->settings())
        return false;
    const auto theme = application->settings()->get("ApplicationTheme").toString();
    return theme == "chroma" || theme == "chroma-dark";
}

QFont headingFont(int pixelSize)
{
    QFont font = QApplication::font();
    font.setFamilies({ QStringLiteral("Nunito"), QStringLiteral("Segoe UI"), QStringLiteral("sans-serif") });
    font.setWeight(QFont::ExtraBold);
    if (pixelSize > 0)
        font.setPixelSize(pixelSize);
    return font;
}

QString formStyleSheet(const QString& selector)
{
    // Qt's min-height is the content height: 28 + 2 * (6 padding + 2 border)
    // gives a 44px target. Keep border widths fixed when focus changes.
    return QStringLiteral(R"(
        %1 QPushButton, %1 QToolButton {
            border: 2px solid palette(light); border-radius: 20px;
            padding: 6px 12px; min-height: 28px;
        }
        %1 QPushButton:hover, %1 QToolButton:hover { border-color: palette(link); }
        %1 QLineEdit, %1 QAbstractSpinBox, %1 QComboBox {
            background: palette(alternate-base); color: palette(text);
            border: 2px solid palette(light); border-radius: 20px;
            padding: 6px 12px; min-height: 28px;
        }
        %1 QPushButton:focus, %1 QToolButton:focus, %1 QLineEdit:focus,
        %1 QAbstractSpinBox:focus, %1 QComboBox:focus {
            border: 2px solid palette(link);
        }
        %1 QLineEdit:focus, %1 QAbstractSpinBox:focus, %1 QComboBox:focus {
            background: palette(base);
        }
        %1 QAbstractSpinBox QLineEdit, %1 QComboBox QLineEdit {
            background: transparent; border: none; border-radius: 0;
            padding: 0; min-height: 0;
        }
        %1 QGroupBox { border-radius: 24px; margin-top: 18px; padding: 12px; }
        %1 QTabWidget::pane { border-radius: 20px; }
    )")
        .arg(selector);
}

QString scrollBarStyleSheet(const QString& selector)
{
    return QStringLiteral(R"(
        %1 QScrollBar:vertical { width: 10px; background: transparent; margin: 2px; }
        %1 QScrollBar:horizontal { height: 10px; background: transparent; margin: 2px; }
        %1 QScrollBar::handle { background: palette(mid); border-radius: 3px; }
        %1 QScrollBar::handle:vertical { min-height: 36px; }
        %1 QScrollBar::handle:horizontal { min-width: 36px; }
        %1 QScrollBar::handle:hover, %1 QScrollBar::handle:pressed { background: palette(link); }
        %1 QScrollBar::add-line, %1 QScrollBar::sub-line { width: 0; height: 0; }
        %1 QScrollBar::add-page, %1 QScrollBar::sub-page { background: transparent; }
    )")
        .arg(selector);
}

QString menuStyleSheet()
{
    // QMenu keeps its native popup window, scrolling, shortcut labels, check
    // marks, submenus, keyboard navigation and screen-edge placement.
    return QStringLiteral(R"(
        QMenu {
            background: palette(base); color: palette(text);
            border: 1px solid palette(mid); border-radius: %1px;
            padding: %2px; menu-scrollable: 1;
        }
        QMenu::item { padding: 9px 28px; border: 1px solid transparent; border-radius: 10px; }
        QMenu::item:selected { background: palette(highlight); color: palette(highlighted-text); }
        QMenu::item:disabled { color: palette(placeholder-text); }
        QMenu::separator { height: 1px; background: palette(mid); margin: 5px 10px; }
        QMenu::icon { margin-left: 8px; }
        QComboBox QAbstractItemView {
            background: palette(base); color: palette(text); border: 1px solid palette(mid);
            padding: 4px; selection-background-color: palette(highlight);
            selection-color: palette(highlighted-text); outline: 0;
        }
        QComboBox QAbstractItemView::item { min-height: 28px; padding: 4px 8px; }
    )")
        .arg(Radius::Popup)
        .arg(Space::Small);
}

bool motionAllowed()
{
    if (qEnvironmentVariableIntValue("CHROMA_REDUCED_MOTION") == 1 || !QApplication::isEffectEnabled(Qt::UI_AnimateCombo))
        return false;
#ifdef Q_OS_WIN
    BOOL animate = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) && !animate)
        return false;
#endif
    return true;
}

void drawSurface(QPainter* painter, const QRectF& rect, const QColor& color, qreal radius, bool pressed, qreal lift)
{
    if (!painter || rect.isEmpty())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    radius = qMin(radius, qMin(rect.width(), rect.height()) / 2);
    QPainterPath shape;
    shape.addRoundedRect(rect, radius, radius);

    // Four lighting layers: ambient occlusion, upper-left light, a colored
    // inner rim, and the opposite inner highlight. All fit the 5px gutter.
    const bool darkSurface = dark();
    const QColor rim = darkSurface ? QColor(181, 163, 214) : QColor(Qt::white);
    if (!pressed) {
        outerLight(painter, rect, radius,
                   darkSurface ? QColor(0, 0, 0, qRound(25 + 4 * lift)) : QColor(121, 101, 155, qRound(13 + 4 * lift)), QPointF(1.0, 1.5),
                   6);
        outerLight(painter, rect, radius, darkSurface ? QColor(181, 163, 214, 4) : QColor(255, 255, 255, 18), QPointF(-0.8, -0.8), 6);
    }

    QLinearGradient fill(rect.topLeft(), rect.bottomRight());
    fill.setColorAt(0, mix(color, rim, pressed ? 0.01 : darkSurface ? 0.025 : 0.08));
    fill.setColorAt(1, mix(color, Qt::black, pressed ? 0.01 : 0.03));
    painter->fillPath(shape, fill);
    painter->setClipPath(shape, Qt::IntersectClip);
    if (pressed) {
        innerLight(painter, rect, radius, darkSurface ? QColor(0, 0, 0, 44) : QColor(115, 97, 148, 30), QPointF(3.5, 3.5), 6);
        innerLight(painter, rect, radius, darkSurface ? QColor(181, 163, 214, 15) : QColor(255, 255, 255, 36), QPointF(-3.5, -3.5), 6);
        innerLight(painter, rect, radius, darkSurface ? QColor(0, 0, 0, 12) : QColor(115, 97, 148, 8), QPointF(1, 1), 3);
        innerLight(painter, rect, radius, darkSurface ? QColor(181, 163, 214, 5) : QColor(255, 255, 255, 10), QPointF(-1, -1), 3);
    } else {
        innerLight(painter, rect, radius, darkSurface ? QColor(0, 0, 0, 25) : QColor(91, 61, 129, 12), QPointF(-2.5, -2.5), 5);
        innerLight(painter, rect, radius, darkSurface ? QColor(181, 163, 214, 12) : QColor(255, 255, 255, 22), QPointF(2.5, 2.5), 5);
    }
    painter->restore();
}

}  // namespace Clay
