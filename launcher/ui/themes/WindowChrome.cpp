// SPDX-License-Identifier: GPL-3.0-only
#include "WindowChrome.h"

#include <QWidget>

#ifdef Q_OS_WIN
#include <QAbstractButton>
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QPointer>
#include <QTimer>
#include <QVariant>
#include <QWindow>
#include <QtMath>

#include "ClayStyle.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>

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

UINT windowDpi(HWND handle, qreal scale)
{
    using Function = UINT(WINAPI*)(HWND);
    static const auto function = reinterpret_cast<Function>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    return function ? function(handle) : UINT(qRound(96 * scale));
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

class CustomWindowChrome final : public QObject, public QAbstractNativeEventFilter {
   public:
    CustomWindowChrome(QWidget* window, QWidget* dragRegion, QWidget* controls) : QObject(window), m_window(window)
    {
        setObjectName(QStringLiteral("customWindowChrome"));
        configure(dragRegion, controls);
        qApp->installEventFilter(this);
        if (QGuiApplication::platformName() == QStringLiteral("windows"))
            qApp->installNativeEventFilter(this);
        scheduleFrameUpdate();
    }

    ~CustomWindowChrome() override
    {
        cancelNativePress();
        if (qApp) {
            qApp->removeEventFilter(this);
            qApp->removeNativeEventFilter(this);
        }
    }

    void configure(QWidget* dragRegion, QWidget* controls)
    {
        cancelNativePress();
        m_dragRegion = dragRegion;
        m_controls = controls;
        m_maximize = controls->findChild<QAbstractButton*>(QStringLiteral("windowMaximize"));
        controls->show();
        scheduleFrameUpdate();
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!m_window)
            return false;
        if (watched == m_window) {
            switch (event->type()) {
                case QEvent::Show:
                    updateNativeFrame();
                    break;
                case QEvent::WinIdChange:
                case QEvent::WindowStateChange:
                    cancelNativePress();
                    scheduleFrameUpdate();
                    break;
                case QEvent::WindowDeactivate:
                case QEvent::Hide:
                    cancelNativePress();
                    setNativeHover(false);
                    break;
                default:
                    break;
            }
        }
        if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonDblClick)
            return false;
        const auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget || widget->window() != m_window)
            return false;
        auto* mouse = static_cast<QMouseEvent*>(event);
        const auto point = m_window->mapFromGlobal(mouse->globalPosition().toPoint());
        if (mouse->button() != Qt::LeftButton || !isCaption(point))
            return false;
        if (event->type() == QEvent::MouseButtonDblClick) {
            if (m_maximize && m_maximize->isEnabled())
                m_maximize->click();
            mouse->accept();
            return true;
        }
        // Native caption hit testing normally handles mouse input. This also
        // covers Qt-delivered input without manually moving the window, so
        // the OS retains drag-to-snap and drag-from-maximized behavior.
        if (auto* handle = m_window->windowHandle(); handle && handle->startSystemMove()) {
            mouse->accept();
            return true;
        }
        return false;
    }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override
    {
        if (eventType != QByteArrayLiteral("windows_generic_MSG"))
            return false;
        if (!m_window || !m_dragRegion || !m_controls) {
            cancelNativePress();
            return false;
        }
        const auto* event = static_cast<MSG*>(message);
        const auto handle = reinterpret_cast<HWND>(m_window->internalWinId());
        if (!event || !handle || event->hwnd != handle)
            return false;
        // Qt 6.5 delivers queued mouse/key input only before DispatchMessage,
        // with no result pointer. Consume custom input there; returning true
        // prevents a second delivery. Other messages are handled in the HWND
        // window proc, where a native return value is available.
        qintptr inputResult = 0;
        if (!result) {
            switch (event->message) {
                case WM_NCLBUTTONDOWN:
                case WM_NCLBUTTONDBLCLK:
                case WM_NCLBUTTONUP:
                case WM_LBUTTONUP:
                case WM_MOUSEMOVE:
                case WM_NCMOUSEMOVE:
                case WM_NCMOUSELEAVE:
                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                    result = &inputResult;
                    break;
                default:
                    return false;
            }
        }
        switch (event->message) {
            case WM_GETMINMAXINFO: {
                if (m_window->isFullScreen() || !event->lParam)
                    break;
                MONITORINFO monitor{};
                monitor.cbSize = sizeof(monitor);
                if (!GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor))
                    break;
                // Without WS_CAPTION, Windows' default maximized bounds can
                // cover the taskbar. Set the work-area bounds before sizing,
                // rather than clipping a taskbar-sized strip in NCCALCSIZE.
                // Qt 6.5 otherwise mistakes that strip for a title bar and
                // carries its height into the restored minimum window size.
                RECT frame{};
                const DWORD style = DWORD(GetWindowLongPtrW(handle, GWL_STYLE)) & ~DWORD(WS_CAPTION | WS_MINIMIZE | WS_MAXIMIZE);
                const DWORD exStyle = DWORD(GetWindowLongPtrW(handle, GWL_EXSTYLE));
                using AdjustFunction = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
                static const auto adjustForDpi =
                    reinterpret_cast<AdjustFunction>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
                const bool adjusted = adjustForDpi
                                          ? adjustForDpi(&frame, style, FALSE, exStyle, windowDpi(handle, m_window->devicePixelRatioF()))
                                          : AdjustWindowRectEx(&frame, style, FALSE, exStyle);
                if (!adjusted)
                    break;
                auto* limits = reinterpret_cast<MINMAXINFO*>(event->lParam);
                limits->ptMaxPosition = { monitor.rcWork.left - monitor.rcMonitor.left + frame.left,
                                          monitor.rcWork.top - monitor.rcMonitor.top + frame.top };
                limits->ptMaxSize = { monitor.rcWork.right - monitor.rcWork.left + frame.right - frame.left,
                                      monitor.rcWork.bottom - monitor.rcWork.top + frame.bottom - frame.top };
                // Qt must still apply the widget's minimum/maximum tracking
                // sizes; leave the message unhandled after setting our bounds.
                break;
            }
            case WM_STYLECHANGING:
                if (static_cast<int>(event->wParam) == GWL_STYLE && event->lParam) {
                    // Qt can restore its normal window styles after changing
                    // state or flags. Keep the native caption disabled before
                    // Windows paints it; resizing the client area alone leaves
                    // the native buttons visible in the top resize border.
                    // https://learn.microsoft.com/windows/win32/winmsg/wm-stylechanging
                    auto* styles = reinterpret_cast<STYLESTRUCT*>(event->lParam);
                    styles->styleNew &= ~DWORD(WS_CAPTION);
                    *result = 0;
                    return true;
                }
                break;
            case WM_NCCALCSIZE: {
                // An iconic window has a small legacy minimized frame, not
                // the normal client geometry. Let Windows size that frame;
                // overriding it corrupts the placement restored by Qt.
                if (IsIconic(handle))
                    return false;
                // Keep WS_THICKFRAME and all system-command styles. The native
                // caption style is removed in updateNativeFrame; preserve the
                // system resize borders so Qt also measures the correct frame.
                // https://learn.microsoft.com/windows/win32/dwm/customframe
                auto* rectangle = event->wParam ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(event->lParam)->rgrc[0]
                                               : reinterpret_cast<RECT*>(event->lParam);
                if (!rectangle)
                    return false;
                const RECT bounds = *rectangle;
                if (!m_window->isFullScreen()) {
                    DefWindowProcW(handle, event->message, event->wParam, event->lParam);
                    // Qt 6.5 calculates left/right/bottom frame margins from
                    // the native window style, then measures only the top.
                    // Removing those borders too leaves stale 8px margins and
                    // causes resize, minimum-size and saved-geometry drift.
                    // The top gets the same resize border as the bottom, with
                    // no native title bar above our own header.
                    rectangle->top = bounds.top + qMax<LONG>(0, bounds.bottom - rectangle->bottom);
                }
                if (IsZoomed(handle) && !m_window->isFullScreen()) {
                    MONITORINFO monitor{};
                    monitor.cbSize = sizeof(monitor);
                    if (GetMonitorInfoW(MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST), &monitor)) {
                        // Windows maximizes the resize frame beyond the work
                        // area. Keep our client content inside it, including
                        // taskbars on any edge and negative monitor origins.
                        rectangle->left = qMax(bounds.left, monitor.rcWork.left);
                        rectangle->top = qMax(bounds.top, monitor.rcWork.top);
                        rectangle->right = qMin(bounds.right, monitor.rcWork.right);
                        rectangle->bottom = qMin(bounds.bottom, monitor.rcWork.bottom);
                    }
                }
                *result = 0;
                return true;
            }
            case WM_NCHITTEST: {
                POINT screen{ GET_X_LPARAM(event->lParam), GET_Y_LPARAM(event->lParam) };
                const auto resize = resizeHit(handle, screen);
                if (resize != HTCLIENT) {
                    *result = resize;
                    return true;
                }
                POINT client = screen;
                ScreenToClient(handle, &client);
                const qreal scale = m_window->devicePixelRatioF();
                const QPoint point(qFloor(client.x / scale), qFloor(client.y / scale));
                const bool maximize = inWidget(m_maximize, point) && m_maximize->isEnabled();
                setNativeHover(maximize);
                // Keep the documented hit result for Windows 11 Snap Layouts.
                // Our non-client press/release handlers activate the custom
                // button: DefWindowProc cannot click a caption we removed.
                // https://learn.microsoft.com/windows/apps/desktop/modernize/ui/apply-snap-layout-menu
                *result = maximize ? HTMAXBUTTON : isCaption(point) ? HTCAPTION : HTCLIENT;
                return true;
            }
            case WM_NCLBUTTONDOWN:
            case WM_NCLBUTTONDBLCLK:
                if (event->wParam == HTMAXBUTTON) {
                    if (maximizeAt(handle, event->lParam, true)) {
                        if (m_maximize->hasFocus())
                            m_maximize->clearFocus();
                        SetCapture(handle);
                        m_nativePress = true;
                        m_maximize->setDown(true);
                        setNativeHover(true);
                    }
                    *result = 0;
                    return true;
                }
                break;
            case WM_LBUTTONUP:
            case WM_NCLBUTTONUP:
                if (m_nativePress) {
                    const bool activate = maximizeAt(handle, event->lParam, event->message == WM_NCLBUTTONUP);
                    cancelNativePress();
                    setNativeHover(false);
                    if (activate && m_maximize)
                        m_maximize->click();
                    *result = 0;
                    return true;
                }
                // Never let a canceled custom press reach the native caption
                // implementation or trigger a second action on release.
                if (event->message == WM_NCLBUTTONUP && event->wParam == HTMAXBUTTON) {
                    *result = 0;
                    return true;
                }
                break;
            case WM_MOUSEMOVE:
                if (m_nativePress) {
                    updateNativePress(maximizeAt(handle, event->lParam, false));
                    *result = 0;
                    return true;
                }
                break;
            case WM_NCMOUSEMOVE: {
                if (m_nativePress)
                    updateNativePress(maximizeAt(handle, event->lParam, true));
                TRACKMOUSEEVENT tracking{ sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, handle, 0 };
                TrackMouseEvent(&tracking);
                // Keep hover messages available to Windows for Snap Layouts.
                break;
            }
            case WM_NCMOUSELEAVE:
                setNativeHover(false);
                break;
            case WM_CANCELMODE:
            case WM_CAPTURECHANGED:
                cancelNativePress();
                setNativeHover(false);
                break;
            case WM_ACTIVATE:
                if (LOWORD(event->wParam) == WA_INACTIVE) {
                    cancelNativePress();
                    setNativeHover(false);
                }
                break;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                if (m_nativePress && event->wParam == VK_ESCAPE) {
                    cancelNativePress();
                    setNativeHover(false);
                    *result = 0;
                    return true;
                }
                break;
            case WM_SETTINGCHANGE:
            case WM_THEMECHANGED:
                if (m_controls)
                    for (auto* button : m_controls->findChildren<QAbstractButton*>())
                        button->update();
                scheduleFrameUpdate();
                break;
            case WM_DPICHANGED:
                // Qt applies Windows' suggested rectangle and updates its
                // device-pixel ratio. Reapply the DWM frame after that work.
                scheduleFrameUpdate();
                break;
            default:
                break;
        }
        return false;
    }

   private:
    bool maximizeAt(HWND handle, LPARAM position, bool screenCoordinates) const
    {
        POINT point{ GET_X_LPARAM(position), GET_Y_LPARAM(position) };
        if (screenCoordinates)
            ScreenToClient(handle, &point);
        const qreal scale = m_window->devicePixelRatioF();
        return inWidget(m_maximize, QPoint(qFloor(point.x / scale), qFloor(point.y / scale))) && m_maximize->isEnabled();
    }

    void updateNativePress(bool inside)
    {
        if (m_maximize)
            m_maximize->setDown(inside);
        setNativeHover(inside);
    }

    void cancelNativePress()
    {
        if (!m_nativePress)
            return;
        m_nativePress = false;
        if (m_maximize)
            m_maximize->setDown(false);
        // ReleaseCapture synchronously sends WM_CAPTURECHANGED. Clear the
        // press first, and never release capture now owned by another window.
        if (m_window && GetCapture() == reinterpret_cast<HWND>(m_window->internalWinId()))
            ReleaseCapture();
    }

    bool inWidget(const QWidget* widget, QPoint point) const
    {
        return widget && widget->isVisibleTo(m_window) && widget->rect().contains(widget->mapFrom(m_window, point));
    }

    bool isCaption(QPoint point) const
    {
        if (!m_window || !inWidget(m_dragRegion, point) || inWidget(m_controls, point) || m_window->isFullScreen())
            return false;
        for (auto* child = m_window->childAt(point); child && child != m_dragRegion; child = child->parentWidget()) {
            if (qobject_cast<QAbstractButton*>(child) || child->focusPolicy() != Qt::NoFocus ||
                child->cursor().shape() == Qt::PointingHandCursor || child->property("windowChromeInteractive").toBool())
                return false;
            if (const auto* label = qobject_cast<QLabel*>(child);
                label && ((label->textInteractionFlags() & (Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard)) ||
                          label->openExternalLinks() || label->text().contains(QStringLiteral("<a "), Qt::CaseInsensitive)))
                return false;
        }
        return true;
    }

    LRESULT resizeHit(HWND handle, POINT point) const
    {
        if (IsZoomed(handle) || m_window->isFullScreen())
            return HTCLIENT;
        RECT bounds{};
        if (!GetWindowRect(handle, &bounds) || !PtInRect(&bounds, point))
            return HTCLIENT;
        using MetricFunction = int(WINAPI*)(int, UINT);
        static const auto metricFunction =
            reinterpret_cast<MetricFunction>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
        const UINT dpi = windowDpi(handle, m_window->devicePixelRatioF());
        const auto metric = [dpi](int index) { return metricFunction ? metricFunction(index, dpi) : GetSystemMetrics(index); };
        const int xBorder = qMax(1, metric(SM_CXSIZEFRAME) + metric(SM_CXPADDEDBORDER));
        const int yBorder = qMax(1, metric(SM_CYSIZEFRAME) + metric(SM_CXPADDEDBORDER));
        const bool horizontal = m_window->minimumWidth() < m_window->maximumWidth();
        const bool vertical = m_window->minimumHeight() < m_window->maximumHeight();
        const bool left = horizontal && point.x < bounds.left + xBorder;
        const bool right = horizontal && point.x >= bounds.right - xBorder;
        const bool top = vertical && point.y < bounds.top + yBorder;
        const bool bottom = vertical && point.y >= bounds.bottom - yBorder;
        if (top)
            return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
        if (bottom)
            return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
        return left ? HTLEFT : right ? HTRIGHT : HTCLIENT;
    }

    void setNativeHover(bool hovered)
    {
        if (m_maximize && m_maximize->property("nativeHover").toBool() != hovered) {
            m_maximize->setProperty("nativeHover", hovered);
            m_maximize->update();
        }
    }

    void scheduleFrameUpdate()
    {
        if (m_updatePending)
            return;
        m_updatePending = true;
        QTimer::singleShot(0, this, [this] {
            m_updatePending = false;
            updateNativeFrame();
        });
    }

    void updateNativeFrame()
    {
        if (!m_window || !m_window->isWindow() || QGuiApplication::platformName() != QStringLiteral("windows"))
            return;
        const auto handle = reinterpret_cast<HWND>(m_window->internalWinId());
        if (!handle)
            return;
        // There is no visible client frame to update while minimized. Forcing
        // WM_NCCALCSIZE here can overwrite Qt's cached normal frame margins.
        // The restore/show event reapplies the frame when it becomes visible.
        if (IsIconic(handle))
            return;
        const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
        if (style & WS_CAPTION)
            SetWindowLongPtrW(handle, GWL_STYLE, style & ~LONG_PTR(WS_CAPTION));
        const MARGINS margins{ 1, 1, 1, 1 };
        DwmExtendFrameIntoClientArea(handle, &margins);
        // No move/resize or activation here: restoring saved geometry and Qt's
        // handling of monitor DPI and min/max size constraints remain intact.
        SetWindowPos(handle, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    QPointer<QWidget> m_window;
    QPointer<QWidget> m_dragRegion;
    QPointer<QWidget> m_controls;
    QPointer<QAbstractButton> m_maximize;
    bool m_nativePress = false;
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

bool WindowChrome::installCustom(QWidget* window, QWidget* dragRegion, QWidget* controls)
{
#ifdef Q_OS_WIN
    const auto platform = QGuiApplication::platformName();
    if (window && window->isWindow() && dragRegion && controls && dragRegion->window() == window && controls->window() == window &&
        (platform == QStringLiteral("windows") || platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal"))) {
        install(window);
        if (auto* existing = window->findChild<QObject*>(QStringLiteral("customWindowChrome"), Qt::FindDirectChildrenOnly))
            static_cast<CustomWindowChrome*>(existing)->configure(dragRegion, controls);
        else
            new CustomWindowChrome(window, dragRegion, controls);
        window->setProperty("customWindowChromeEnabled", true);
        return true;
    }
#else
    Q_UNUSED(window)
    Q_UNUSED(dragRegion)
#endif
    if (controls)
        controls->hide();
    return false;
}
