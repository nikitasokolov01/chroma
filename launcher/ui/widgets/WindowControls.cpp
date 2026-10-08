// SPDX-License-Identifier: GPL-3.0-only
#include "WindowControls.h"

#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVariant>

#include "ui/themes/ClayStyle.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
class CaptionButton final : public QToolButton {
   public:
    enum Role { Minimize, Maximize, Close };
    CaptionButton(Role role, QWidget* parent) : QToolButton(parent), m_role(role)
    {
        setFixedSize(Clay::MinimumTarget, Clay::MinimumTarget);
        // Caption clicks should not steal focus from the editor or leave a
        // keyboard focus ring behind when the window returns from minimized.
        setFocusPolicy(Qt::TabFocus);
        setCursor(Qt::ArrowCursor);
        setAutoRaise(true);
        // Application form styles use larger padded buttons. Keep the compact
        // caption targets at the same accessible size across theme changes.
        setStyleSheet(QStringLiteral("QToolButton { margin: 0; padding: 0; border: none; min-width: 44px; max-width: 44px; "
                                     "min-height: 44px; max-height: 44px; }"));
    }

   protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && hasFocus())
            clearFocus();
        QToolButton::mousePressEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            click();
            event->accept();
            return;
        }
        QToolButton::keyPressEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        const bool active = window()->isActiveWindow();
        const bool hovered = active && ((underMouse() && rect().contains(mapFromGlobal(QCursor::pos()))) ||
                                        property("nativeHover").toBool());
        const auto group = !isEnabled() ? QPalette::Disabled : window()->isActiveWindow() ? QPalette::Active : QPalette::Inactive;
        // QSS makes tool-button backgrounds transparent. Read the established
        // clay colors (or the window palette) instead of that altered palette.
        const auto windowPalette = window()->palette();
        QColor foreground = Clay::enabled() ? (isEnabled() ? Clay::colors().Foreground : Clay::colors().Muted)
                                            : windowPalette.color(group, QPalette::WindowText);
        QColor surface = windowPalette.color(group, QPalette::Window);
        QColor focus = windowPalette.color(group, QPalette::Highlight);
        bool highContrast = false;
#ifdef Q_OS_WIN
        HIGHCONTRASTW settings{};
        settings.cbSize = sizeof(settings);
        highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(settings), &settings, 0) &&
                       (settings.dwFlags & HCF_HIGHCONTRASTON);
        if (highContrast) {
            const auto systemColor = [](int index) {
                const auto color = GetSysColor(index);
                return QColor(GetRValue(color), GetGValue(color), GetBValue(color));
            };
            surface = systemColor(hovered || isDown() ? COLOR_HIGHLIGHT : COLOR_WINDOW);
            foreground = systemColor(!isEnabled() ? COLOR_GRAYTEXT : hovered || isDown() ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
            focus = systemColor(COLOR_HIGHLIGHT);
        }
#endif
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto bounds = QRectF(rect());
        if (highContrast) {
            painter.setPen(QPen(foreground, 1));
            painter.setBrush(surface);
            painter.drawRect(bounds.adjusted(.5, .5, -.5, -.5));
        } else if (m_role == Close && (hovered || isDown()) && isEnabled()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(isDown() ? QColor("#B72339") : QColor("#D93649"));
            painter.drawRect(bounds);
            foreground = Qt::white;
        } else if ((hovered || isDown()) && isEnabled()) {
            QColor hover = foreground;
            hover.setAlphaF(isDown() ? .16 : .08);
            painter.fillRect(bounds, hover);
        }
        if (hasFocus() && active) {
            painter.setPen(QPen(focus, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(bounds.adjusted(3, 3, -3, -3), 3, 3);
        }
        painter.setPen(QPen(foreground, 1.4, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        painter.setBrush(Qt::NoBrush);
        painter.translate((width() - 32) / 2.0, (height() - 32) / 2.0);
        if (m_role == Minimize) {
            painter.drawLine(QPointF(10, 18), QPointF(22, 18));
        } else if (m_role == Close) {
            painter.drawLine(QPointF(11, 11), QPointF(21, 21));
            painter.drawLine(QPointF(21, 11), QPointF(11, 21));
        } else if (property("restore").toBool()) {
            painter.drawPolyline(QPolygonF{ QPointF(13, 11), QPointF(13, 9), QPointF(23, 9), QPointF(23, 19), QPointF(21, 19) });
            painter.drawRect(QRectF(9, 13, 10, 10));
        } else {
            painter.drawRect(QRectF(10, 10, 12, 12));
        }
    }

   private:
    Role m_role;
};
}  // namespace

WindowControls::WindowControls(QWidget* targetWindow, QWidget* parent) : QWidget(parent), m_window(targetWindow)
{
    setObjectName(QStringLiteral("windowControls"));
    setAccessibleName(tr("Window controls"));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_minimize = new CaptionButton(CaptionButton::Minimize, this);
    m_maximize = new CaptionButton(CaptionButton::Maximize, this);
    m_close = new CaptionButton(CaptionButton::Close, this);
    m_minimize->setObjectName(QStringLiteral("windowMinimize"));
    m_maximize->setObjectName(QStringLiteral("windowMaximize"));
    m_close->setObjectName(QStringLiteral("windowClose"));
    for (auto* button : { m_minimize, m_maximize, m_close })
        layout->addWidget(button);
    connect(m_minimize, &QToolButton::clicked, this, [this] {
        if (m_window)
            m_window->showMinimized();
    });
    connect(m_maximize, &QToolButton::clicked, this, [this] {
        if (!m_window)
            return;
        if (m_window->isMaximized() || m_window->isFullScreen())
            m_window->showNormal();
        else
            m_window->showMaximized();
    });
    connect(m_close, &QToolButton::clicked, this, [this] {
        if (m_window)
            m_window->close();
    });
    if (m_window) {
        m_window->installEventFilter(this);
        connect(m_window, &QObject::destroyed, this, [this] {
            m_window.clear();
            updateState();
        });
    }
    updateState();
}

bool WindowControls::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window && (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide ||
                                event->type() == QEvent::WindowStateChange)) {
        for (auto* button : { m_minimize, m_maximize, m_close }) {
            button->setDown(false);
            button->setProperty("nativeHover", false);
            button->setAttribute(Qt::WA_UnderMouse, false);
            button->update();
        }
    }
    if (watched == m_window && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show ||
                                event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange ||
                                event->type() == QEvent::ActivationChange || event->type() == QEvent::LanguageChange ||
                                event->type() == QEvent::Resize))
        updateState();
    return QWidget::eventFilter(watched, event);
}

void WindowControls::updateState()
{
    const bool restore = m_window && (m_window->isMaximized() || m_window->isFullScreen());
    const bool canResize = m_window && (m_window->minimumWidth() < m_window->maximumWidth() ||
                                        m_window->minimumHeight() < m_window->maximumHeight());
    m_minimize->setEnabled(m_window && m_window->windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
    m_maximize->setEnabled(m_window && (restore || (canResize && m_window->windowFlags().testFlag(Qt::WindowMaximizeButtonHint))));
    m_close->setEnabled(m_window && m_window->windowFlags().testFlag(Qt::WindowCloseButtonHint));
    m_maximize->setProperty("restore", restore);
    const QString labels[] = { tr("Minimize"), restore ? tr("Restore") : tr("Maximize"), tr("Close") };
    int i = 0;
    for (auto* button : { m_minimize, m_maximize, m_close }) {
        button->setToolTip(labels[i]);
        button->setAccessibleName(labels[i++]);
        button->setDown(false);
        button->update();
    }
}
