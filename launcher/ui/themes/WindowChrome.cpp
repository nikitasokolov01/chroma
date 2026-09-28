// SPDX-License-Identifier: GPL-3.0-only
#include "WindowChrome.h"

#include <QWidget>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QEvent>
#include <QPalette>
#include <QTimer>

#include "ClayStyle.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dwmapi.h>

namespace {

// These documented attributes are supported from Windows 11 build 22000.
// Use their stable values so older Windows SDKs can still build the launcher.
// Unsupported attributes simply leave the operating system's frame intact.
// https://learn.microsoft.com/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute
constexpr DWORD ImmersiveDarkMode = 20;
constexpr DWORD BorderColor = 34;
constexpr DWORD CaptionColor = 35;
constexpr DWORD TextColor = 36;
constexpr COLORREF DefaultColor = 0xFFFFFFFF;

COLORREF nativeColor(const QColor& color)
{
    return RGB(color.red(), color.green(), color.blue());
}

bool highContrastEnabled()
{
    HIGHCONTRASTW settings{};
    settings.cbSize = sizeof(settings);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(settings), &settings, 0) && (settings.dwFlags & HCF_HIGHCONTRASTON);
}

class NativeWindowChrome final : public QObject, public QAbstractNativeEventFilter {
   public:
    explicit NativeWindowChrome(QWidget* window) : QObject(window), m_window(window)
    {
        setObjectName(QStringLiteral("nativeWindowChrome"));
        window->installEventFilter(this);
        qApp->installNativeEventFilter(this);
        scheduleUpdate();
    }

    ~NativeWindowChrome() override
    {
        if (qApp)
            qApp->removeNativeEventFilter(this);
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_window) {
            switch (event->type()) {
                case QEvent::Show:
                case QEvent::WinIdChange:
                case QEvent::PaletteChange:
                case QEvent::StyleChange:
                case QEvent::ThemeChange:
                case QEvent::ActivationChange:
                case QEvent::WindowStateChange:
                    scheduleUpdate();
                    break;
                default:
                    break;
            }
        }
        return QObject::eventFilter(watched, event);
    }

    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override
    {
        const auto* event = static_cast<MSG*>(message);
        // Qt does not necessarily change the palette when Windows toggles high
        // contrast. Restore system colors even if the selected theme is fixed.
        if (event && event->hwnd == m_handle && (event->message == WM_SETTINGCHANGE || event->message == WM_THEMECHANGED))
            scheduleUpdate();
        return false;
    }

   private:
    void scheduleUpdate()
    {
        if (m_updatePending)
            return;
        m_updatePending = true;
        QTimer::singleShot(0, this, [this] {
            m_updatePending = false;
            updateColors();
        });
    }

    void updateColors()
    {
        // internalWinId never creates a native handle. In particular, theme
        // changes must not turn an inline page or a hidden widget into a window.
        const auto handle = reinterpret_cast<HWND>(m_window->internalWinId());
        if (!m_window->isWindow() || !handle)
            return;
        if (m_handle != handle) {
            m_handle = handle;
            m_customApplied = false;
            m_originalDarkMode = FALSE;
            DwmGetWindowAttribute(handle, ImmersiveDarkMode, &m_originalDarkMode, sizeof(m_originalDarkMode));
        }

        const bool custom = Clay::enabled() && !highContrastEnabled();
        if (!custom && !m_customApplied)
            return;

        const auto palette = m_window->palette();
        const auto group = m_window->isActiveWindow() ? QPalette::Active : QPalette::Inactive;
        const QColor background = palette.color(group, QPalette::Window);
        const BOOL dark = custom ? background.lightnessF() < 0.5 : m_originalDarkMode;
        const COLORREF caption = custom ? nativeColor(background) : DefaultColor;
        const COLORREF text = custom ? nativeColor(palette.color(group, QPalette::WindowText)) : DefaultColor;
        const COLORREF border = custom ? nativeColor(palette.color(group, QPalette::Mid)) : DefaultColor;
        DwmSetWindowAttribute(handle, ImmersiveDarkMode, &dark, sizeof(dark));
        DwmSetWindowAttribute(handle, CaptionColor, &caption, sizeof(caption));
        DwmSetWindowAttribute(handle, TextColor, &text, sizeof(text));
        DwmSetWindowAttribute(handle, BorderColor, &border, sizeof(border));
        m_customApplied = custom;
    }

    QWidget* m_window;
    HWND m_handle = nullptr;
    BOOL m_originalDarkMode = FALSE;
    bool m_customApplied = false;
    bool m_updatePending = false;
};

}  // namespace
#endif

void WindowChrome::install(QWidget* window)
{
#ifdef Q_OS_WIN
    if (window && QGuiApplication::platformName() == QStringLiteral("windows") &&
        !window->findChild<QObject*>(QStringLiteral("nativeWindowChrome"), Qt::FindDirectChildrenOnly))
        new NativeWindowChrome(window);
#else
    Q_UNUSED(window)
#endif
}
