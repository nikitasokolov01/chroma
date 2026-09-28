// SPDX-License-Identifier: GPL-3.0-only
#include "ClayWidgets.h"

#include <QApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QHideEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QRadialGradient>
#include <QShowEvent>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QStyleOptionComboBox>
#include <QTimer>
#include <QVariantAnimation>
#include <cmath>

#include "ui/themes/AccentColor.h"
#include "ui/themes/ClayStyle.h"

namespace {
QColor mix(const QColor& first, const QColor& second, qreal amount)
{
    return QColor::fromRgbF(first.redF() * (1 - amount) + second.redF() * amount,
                            first.greenF() * (1 - amount) + second.greenF() * amount,
                            first.blueF() * (1 - amount) + second.blueF() * amount);
}

double contrast(const QColor& first, const QColor& second)
{
    const double a = AccentColor::luminance(first);
    const double b = AccentColor::luminance(second);
    return (qMax(a, b) + 0.05) / (qMin(a, b) + 0.05);
}

QColor accessibleAccent(QColor accent, const QColor& foreground)
{
    // Preserve the saved accent and its chosen foreground. Only adjust the
    // painted copy when either end of the surface gradient needs more contrast.
    const bool lightText = AccentColor::luminance(foreground) > 0.5;
    for (int step = 0; step < 80; ++step) {
        if (contrast(mix(accent, Qt::white, 0.08), foreground) >= 4.5 &&
            contrast(mix(accent, Qt::black, 0.03), foreground) >= 4.5)
            break;
        accent = mix(accent, lightText ? QColor(Qt::black) : QColor(Qt::white), 0.035);
    }
    return accent;
}

QIcon symbolicIcon(const QIcon& original, const QSize& size, qreal pixelRatio, const QColor& color)
{
    QIcon result;
    for (const auto state : { QIcon::Off, QIcon::On }) {
        const QString key = QStringLiteral("chroma-clay-symbolic-%1-%2x%3-%4-%5-%6")
                                .arg(original.cacheKey())
                                .arg(size.width())
                                .arg(size.height())
                                .arg(pixelRatio, 0, 'f', 3)
                                .arg(color.rgba())
                                .arg(state);
        QPixmap tinted;
        if (!QPixmapCache::find(key, &tinted)) {
            // Request the actual screen's DPR and retain its pixmap metadata.
            // Use the original normal glyph as an alpha mask even for disabled
            // buttons; their muted foreground supplies the disabled treatment.
            tinted = original.pixmap(size, pixelRatio, QIcon::Normal, state);
            if (tinted.isNull())
                continue;
            QPainter tintPainter(&tinted);
            tintPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tintPainter.fillRect(tinted.rect(), color);
            tintPainter.end();
            QPixmapCache::insert(key, tinted);
        }
        for (const auto mode : { QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled })
            result.addPixmap(tinted, mode, state);
    }
    return result;
}


void focusOutline(QPainter* painter, const QRectF& rect, qreal radius, const QPalette& palette)
{
    QColor outline = palette.color(QPalette::Highlight);
    // Focus remains visible with both pale and dark custom accents. The
    // palette link is contrast-adjusted for the active dark surface.
    const QColor canvas = Clay::colors().Canvas;
    if (contrast(outline, canvas) < 3)
        outline = Clay::dark() ? palette.color(QPalette::Link) : Clay::Violet;
    if (contrast(outline, canvas) < 3)
        outline = Clay::colors().Foreground;
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(outline, 2));
    painter->drawRoundedRect(rect, radius, radius);
}
}  // namespace

ClayToolButton::ClayToolButton(QWidget* parent) : QToolButton(parent), m_hoverAnimation(new QVariantAnimation(this))
{
    setAttribute(Qt::WA_Hover);
    setProperty("clayButton", true);
    updateAppearance();
    m_hoverAnimation->setDuration(160);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_hover = value.toReal();
        update();
    });
}

void ClayToolButton::updateAppearance()
{
    if (m_updatingAppearance)
        return;
    QString sheet = styleSheet();
    if (!m_menuStyle.isEmpty())
        sheet.remove(m_menuStyle);
    m_menuStyle.clear();
    if (Clay::enabled()) {
        // QToolButton's own hit testing reads this same style subcontrol.
        // Qt mirrors logical right in RTL. Insetting only the painted arrow
        // would leave its menu target behind.
        m_menuStyle = QStringLiteral("\n/* Chroma clay split-menu geometry */\n"
                                     "QToolButton[clayButton=\"true\"]::menu-button { "
                                     "subcontrol-origin: border; subcontrol-position: center right; "
                                     "position: absolute; right: 5px; width: 24px; "
                                     "border: none; background: transparent; }\n");
        sheet += m_menuStyle;
    }
    if (sheet == styleSheet())
        return;
    m_updatingAppearance = true;
    setStyleSheet(sheet);
    m_updatingAppearance = false;
    updateGeometry();
}

void ClayToolButton::changeEvent(QEvent* event)
{
    QToolButton::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::LayoutDirectionChange)
        updateAppearance();
}


QSize ClayToolButton::sizeHint() const
{
    const QSize native = QToolButton::sizeHint();
    return Clay::enabled() ? (native + QSize(2 * Clay::ShadowMargin, 2 * Clay::ShadowMargin))
                                .expandedTo(QSize(Clay::MinimumTarget, 54))
                          : native;
}

QSize ClayToolButton::minimumSizeHint() const
{
    const QSize native = QToolButton::minimumSizeHint();
    return Clay::enabled() ? native.expandedTo(QSize(Clay::MinimumTarget, 54)) : native;
}

void ClayToolButton::animateHover(qreal target)
{
    m_hoverAnimation->stop();
    if (!Clay::enabled() || !Clay::motionAllowed()) {
        m_hover = 0;
        update();
        return;
    }
    m_hoverAnimation->setStartValue(m_hover);
    m_hoverAnimation->setEndValue(target);
    m_hoverAnimation->start();
}

void ClayToolButton::enterEvent(QEnterEvent* event)
{
    QToolButton::enterEvent(event);
    if (isEnabled())
        animateHover(1);
}

void ClayToolButton::leaveEvent(QEvent* event)
{
    QToolButton::leaveEvent(event);
    animateHover(0);
}

void ClayToolButton::paintEvent(QPaintEvent* event)
{
    if (!Clay::enabled()) {
        QToolButton::paintEvent(event);
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QStyleOptionToolButton option;
    initStyleOption(&option);
    const bool primary = property("role").toString() == QStringLiteral("primary");
    const bool pressed = isDown() || isChecked();
    const bool animate = isEnabled() && Clay::motionAllowed();
    const qreal hover = animate ? m_hover : 0;
    QRectF surface = QRectF(rect()).adjusted(Clay::ShadowMargin, Clay::ShadowMargin,
                                           -Clay::ShadowMargin, -Clay::ShadowMargin);
    painter.save();
    if (isDown() && animate) {
        painter.translate(surface.center());
        painter.scale(0.92, 0.92);
        painter.translate(-surface.center());
    } else {
        painter.translate(0, -2.5 * hover);
    }

    QColor foreground = primary ? palette().color(QPalette::HighlightedText) : Clay::colors().Foreground;
    QColor color = primary ? accessibleAccent(palette().color(QPalette::Highlight), foreground) : Clay::colors().Surface;
    if (!primary && isChecked())
        color = mix(Clay::colors().Canvas, palette().color(QPalette::Highlight), 0.13);
    if (!isEnabled()) {
        color = mix(Clay::colors().Canvas, color, 0.25);
        foreground = Clay::colors().Muted;
    }
    Clay::drawSurface(&painter, surface, color, Clay::Radius::Control, pressed, hover);

    // Ask the active Qt style to render the actual icon, text, mnemonic, and
    // tool-button layout. Inherited event handling keeps QAction and popup
    // behavior unchanged, including the split menu's native hit rectangle.
    const QRect menuRect = style()->subControlRect(QStyle::CC_ToolButton, &option, QStyle::SC_ToolButtonMenu, this);
    option.rect = surface.adjusted(8, 0, -8, 0).toAlignedRect();
    const bool hasMenu = option.features.testFlag(QStyleOptionToolButton::HasMenu);
    const bool splitMenu = popupMode() == QToolButton::MenuButtonPopup;
    QRect arrowRect;
    if (hasMenu) {
        const int indicator = qMax(12, style()->pixelMetric(QStyle::PM_MenuButtonIndicator, &option, this));
        arrowRect = splitMenu ? menuRect : QRect(option.rect.right() - indicator + 1, option.rect.top(), indicator, option.rect.height());
        if (layoutDirection() == Qt::RightToLeft) {
            if (!splitMenu)
                arrowRect.moveLeft(option.rect.left());
            option.rect.setLeft(arrowRect.right() + 4);
        } else {
            option.rect.setRight(arrowRect.left() - 4);
        }
    }
    for (const auto group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled }) {
        option.palette.setColor(group, QPalette::ButtonText, foreground);
        option.palette.setColor(group, QPalette::Text, foreground);
        option.palette.setColor(group, QPalette::WindowText, foreground);
    }
    if (property("claySymbolic").toBool() && !option.icon.isNull())
        option.icon = symbolicIcon(option.icon, option.iconSize, devicePixelRatioF(), foreground);
    option.state &= ~(QStyle::State_MouseOver | QStyle::State_Sunken | QStyle::State_HasFocus);
    style()->drawControl(QStyle::CE_ToolButtonLabel, &option, &painter, this);
    if (hasMenu) {
        QStyleOption arrowOption;
        arrowOption.initFrom(this);
        arrowOption.palette = option.palette;
        arrowOption.rect = QRect(arrowRect.center() - QPoint(4, 4), QSize(9, 9));
        style()->drawPrimitive(QStyle::PE_IndicatorArrowDown, &arrowOption, &painter, this);
        if (splitMenu) {
            QColor separator = foreground;
            separator.setAlpha(50);
            painter.setPen(QPen(separator, 1));
            const qreal x = layoutDirection() == Qt::RightToLeft ? arrowRect.right() : arrowRect.left();
            painter.drawLine(QPointF(x, surface.top() + 12), QPointF(x, surface.bottom() - 12));
        }
    }
    painter.restore();

    if (hasFocus())
        focusOutline(&painter, QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), Clay::Radius::Control + 3, palette());
}

ClayLineEdit::ClayLineEdit(QWidget* parent) : QLineEdit(parent)
{
    setProperty("clayInput", true);
    updateAppearance();
}

void ClayLineEdit::updateAppearance()
{
    if (m_updatingAppearance || m_clayAppearance == Clay::enabled())
        return;
    m_updatingAppearance = true;
    m_clayAppearance = Clay::enabled();
    setStyleSheet(m_clayAppearance
                      ? QStringLiteral("QLineEdit[clayInput=\"true\"] { background: transparent; border: 5px solid transparent; "
                                       "border-radius: 20px; padding: 10px 18px; }")
                      : QString());
    m_updatingAppearance = false;
    updateGeometry();
}

QSize ClayLineEdit::sizeHint() const
{
    const QSize native = QLineEdit::sizeHint();
    return Clay::enabled() ? native.expandedTo(QSize(Clay::MinimumTarget, 64)) : native;
}

void ClayLineEdit::changeEvent(QEvent* event)
{
    QLineEdit::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        updateAppearance();
}

void ClayLineEdit::paintEvent(QPaintEvent* event)
{
    if (!Clay::enabled()) {
        QLineEdit::paintEvent(event);
        return;
    }
    {
        QPainter painter(this);
        Clay::drawSurface(&painter, QRectF(rect()).adjusted(5, 5, -5, -5), hasFocus() ? Clay::colors().Surface : Clay::colors().Input,
                          Clay::Radius::Control, !hasFocus());
    }
    QLineEdit::paintEvent(event);
    if (hasFocus()) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        focusOutline(&painter, QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), Clay::Radius::Control + 3, palette());
    }
}

ClayComboBox::ClayComboBox(QWidget* parent) : QComboBox(parent)
{
    setProperty("clayCombo", true);
    updateAppearance();
}

void ClayComboBox::updateAppearance()
{
    if (m_updatingAppearance || m_clayAppearance == Clay::enabled())
        return;
    m_updatingAppearance = true;
    m_clayAppearance = Clay::enabled();
    setStyleSheet(m_clayAppearance
                      ? QStringLiteral("QComboBox[clayCombo=\"true\"] { background: transparent; border: 5px solid transparent; "
                                       "border-radius: 20px; padding: 10px 16px; } "
                                       "QComboBox[clayCombo=\"true\"]::drop-down { background: transparent; border: none; width: 28px; } "
                                       "QComboBox[clayCombo=\"true\"]::down-arrow { image: none; }")
                      : QString());
    m_updatingAppearance = false;
    updateGeometry();
}

QSize ClayComboBox::sizeHint() const
{
    const QSize native = QComboBox::sizeHint();
    return Clay::enabled() ? native.expandedTo(QSize(Clay::MinimumTarget, 54)) : native;
}

QSize ClayComboBox::minimumSizeHint() const
{
    const QSize native = QComboBox::minimumSizeHint();
    return Clay::enabled() ? native.expandedTo(QSize(Clay::MinimumTarget, 54)) : native;
}

void ClayComboBox::changeEvent(QEvent* event)
{
    QComboBox::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        updateAppearance();
}

void ClayComboBox::paintEvent(QPaintEvent* event)
{
    if (!Clay::enabled()) {
        QComboBox::paintEvent(event);
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QStyleOptionComboBox option;
    initStyleOption(&option);
    option.frame = false;
    const QRectF surface = QRectF(rect()).adjusted(Clay::ShadowMargin, Clay::ShadowMargin,
                                                 -Clay::ShadowMargin, -Clay::ShadowMargin);
    const bool pressed = option.state.testFlag(QStyle::State_On) || option.state.testFlag(QStyle::State_Sunken);
    Clay::drawSurface(&painter, surface, Clay::colors().Surface, Clay::Radius::Control, pressed);

    const QColor foreground = isEnabled() ? Clay::colors().Foreground : Clay::colors().Muted;
    for (const auto group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled }) {
        option.palette.setColor(group, QPalette::ButtonText, foreground);
        option.palette.setColor(group, QPalette::Text, foreground);
        option.palette.setColor(group, QPalette::WindowText, foreground);
    }
    // Keep the original control geometry for native label layout and popup
    // anchoring. Only replace the frame and arrow painting.
    const QRect arrowRect = style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, this);
    option.state &= ~(QStyle::State_MouseOver | QStyle::State_Sunken | QStyle::State_HasFocus);
    style()->drawControl(QStyle::CE_ComboBoxLabel, &option, &painter, this);

    const QPointF center = arrowRect.center();
    QPainterPath chevron;
    chevron.moveTo(center + QPointF(-4, -2));
    chevron.lineTo(center + QPointF(0, 2));
    chevron.lineTo(center + QPointF(4, -2));
    painter.setPen(QPen(foreground, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(chevron);
    if (hasFocus())
        focusOutline(&painter, QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), Clay::Radius::Control + 3, palette());
}


ClayPanel::ClayPanel(QWidget* parent) : QFrame(parent)
{
    setProperty("clayPanel", true);
    setAutoFillBackground(false);
    updateAppearance();
}

void ClayPanel::updateAppearance()
{
    if (m_updatingAppearance || m_clayAppearance == Clay::enabled())
        return;
    m_updatingAppearance = true;
    m_clayAppearance = Clay::enabled();
    setStyleSheet(m_clayAppearance
                      ? QStringLiteral("QFrame[clayPanel=\"true\"] { background: transparent; border: none; }")
                      : QString());
    m_updatingAppearance = false;
}

void ClayPanel::changeEvent(QEvent* event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        updateAppearance();
}

void ClayPanel::paintEvent(QPaintEvent* event)
{
    if (!Clay::enabled()) {
        QFrame::paintEvent(event);
        return;
    }
    QColor tint = Clay::dark() ? property("clayDarkTint").value<QColor>() : QColor();
    if (!tint.isValid())
        tint = property("clayTint").value<QColor>();
    if (!tint.isValid())
        tint = Clay::colors().Surface;
    const qreal radius = property("clayRadius").isValid() ? qMax<qreal>(20, property("clayRadius").toReal()) : Clay::Radius::Card;
    QPainter painter(this);
    Clay::drawSurface(&painter, QRectF(rect()).adjusted(5, 5, -5, -5), tint, radius);
}

ClayCanvas::ClayCanvas(QWidget* parent) : QWidget(parent), m_driftTimer(new QTimer(this))
{
    setAutoFillBackground(false);
    m_driftTimer->setInterval(100);
    m_driftTimer->setTimerType(Qt::CoarseTimer);
    connect(m_driftTimer, &QTimer::timeout, this, [this] {
        if (!isVisible() || !Clay::enabled() || !Clay::motionAllowed()) {
            m_driftTimer->stop();
            update();
            return;
        }
        update();
    });
}

void ClayCanvas::updateAnimation()
{
    const bool animate = isVisible() && Clay::enabled() && Clay::motionAllowed();
    if (animate && !m_driftTimer->isActive()) {
        m_elapsed.restart();
        m_driftTimer->start();
    } else if (!animate) {
        m_driftTimer->stop();
    }
    update();
}

void ClayCanvas::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateAnimation();
}

void ClayCanvas::hideEvent(QHideEvent* event)
{
    m_driftTimer->stop();
    QWidget::hideEvent(event);
}

void ClayCanvas::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::EnabledChange)
        updateAnimation();
}

void ClayCanvas::paintEvent(QPaintEvent* event)
{
    if (!Clay::enabled()) {
        QWidget::paintEvent(event);
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Clay::colors().Canvas);
    const qreal seconds = m_driftTimer->isActive() && m_elapsed.isValid() ? m_elapsed.elapsed() / 1000.0 : 0;
    const qreal radius = qBound<qreal>(180, qMax(width(), height()) * 0.52, 650);
    const auto blob = [&](QPointF center, QColor color, qreal phase, qreal duration) {
        if (m_driftTimer->isActive())
            center += QPointF(std::sin(seconds * 6.28318530718 / duration + phase) * 12,
                              std::cos(seconds * 6.28318530718 / duration + phase) * 16);
        QRadialGradient gradient(center, radius);
        const bool dark = Clay::dark();
        if (dark)
            color = mix(color, Clay::colors().Canvas, 0.2);
        color.setAlpha(dark ? 18 : 27);
        gradient.setColorAt(0, color);
        color.setAlpha(dark ? 8 : 12);
        gradient.setColorAt(0.5, color);
        color.setAlpha(0);
        gradient.setColorAt(1, color);
        painter.fillRect(rect(), gradient);
    };
    blob(QPointF(width() * 0.05, height() * 0.12), Clay::Violet, 0, 12);
    blob(QPointF(width() * 0.92, height() * 0.40), Clay::Pink, 2, 10);
    blob(QPointF(width() * 0.35, height() * 0.95), Clay::Blue, 4, 8);
}
