// SPDX-License-Identifier: GPL-3.0-only

#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QWidget>
#include <QWindow>

#include "ui/themes/WindowChrome.h"
#include "ui/widgets/WindowControls.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
class GuardedWindow : public QWidget {
   public:
    bool allowClose = false;
    int closeRequests = 0;

   protected:
    void closeEvent(QCloseEvent* event) override
    {
        ++closeRequests;
        if (allowClose)
            event->accept();
        else
            event->ignore();
    }
};

struct CustomFixture {
    GuardedWindow window;
    QWidget header{ &window };
    QLabel title{ QStringLiteral("Chroma"), &header };
    QLineEdit input{ &header };
    QToolButton action{ &header };
    WindowControls controls{ &window, &header };

    CustomFixture()
    {
        window.setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
        window.resize(640, 420);
        header.setGeometry(0, 0, 640, 64);
        title.setGeometry(20, 16, 100, 32);
        input.setGeometry(150, 16, 160, 32);
        action.setGeometry(330, 16, 44, 32);
        action.setFocusPolicy(Qt::NoFocus);  // Still interactive, never caption.
        controls.setGeometry(484, 10, 140, 44);
    }
};
}  // namespace

class WindowChromeTest : public QObject {
    Q_OBJECT

   private slots:
    void integratedControlsHonorWindowStateAndCloseGuard()
    {
        CustomFixture fixture;
        auto* minimize = fixture.controls.findChild<QToolButton*>(QStringLiteral("windowMinimize"));
        auto* maximize = fixture.controls.findChild<QToolButton*>(QStringLiteral("windowMaximize"));
        auto* close = fixture.controls.findChild<QToolButton*>(QStringLiteral("windowClose"));
        QVERIFY(minimize && maximize && close);
        for (auto* button : { minimize, maximize, close }) {
            QCOMPARE(button->size(), QSize(44, 44));
            QCOMPARE(button->focusPolicy(), Qt::TabFocus);
            QVERIFY(!button->accessibleName().isEmpty());
            QCOMPARE(button->accessibleName(), button->toolTip());
        }
        fixture.window.show();
        QTest::mouseClick(maximize, Qt::LeftButton);
        QTRY_VERIFY(fixture.window.isMaximized());
        QCOMPARE(maximize->accessibleName(), QStringLiteral("Restore"));
        QVERIFY(maximize->property("restore").toBool());
        QTest::keyClick(maximize, Qt::Key_Space);
        QTRY_VERIFY(!fixture.window.isMaximized());
        QCOMPARE(maximize->accessibleName(), QStringLiteral("Maximize"));
        minimize->click();
        QTRY_VERIFY(fixture.window.isMinimized());
        fixture.window.showNormal();
        QTest::keyClick(close, Qt::Key_Return);
        QCOMPARE(fixture.window.closeRequests, 1);
        QVERIFY(fixture.window.isVisible());
        fixture.window.allowClose = true;
        QTest::mouseClick(close, Qt::LeftButton);
        QCOMPARE(fixture.window.closeRequests, 2);
        QVERIFY(!fixture.window.isVisible());
    }

    void integratedControlsSurviveMissingOrDestroyedTarget()
    {
        auto* window = new QWidget;
        WindowControls controls(window);
        delete window;
        for (auto* button : controls.findChildren<QToolButton*>()) {
            QVERIFY(!button->isEnabled());
            button->click();
        }
        WindowControls noTarget(nullptr);
        for (auto* button : noTarget.findChildren<QToolButton*>())
            QVERIFY(!button->isEnabled());
    }

    void explicitCustomInstallIsScopedAndIdempotent()
    {
        CustomFixture fixture;
        const auto flags = fixture.window.windowFlags();
        const auto handle = fixture.window.internalWinId();
        const auto geometry = fixture.window.geometry();
        QVERIFY(!WindowChrome::installCustom(nullptr, &fixture.header, &fixture.controls));
        QVERIFY(!WindowChrome::installCustom(&fixture.header, &fixture.title, &fixture.controls));
        const bool supported = WindowChrome::installCustom(&fixture.window, &fixture.header, &fixture.controls);
        QCOMPARE(WindowChrome::installCustom(&fixture.window, &fixture.header, &fixture.controls), supported);
        QCoreApplication::processEvents();
        QCOMPARE(fixture.window.internalWinId(), handle);
        QCOMPARE(fixture.window.windowFlags(), flags);
        QCOMPARE(fixture.window.geometry(), geometry);
        QVERIFY(!fixture.window.isVisible());
        QCOMPARE(fixture.window.findChildren<QObject*>(QStringLiteral("customWindowChrome"), Qt::FindDirectChildrenOnly).size(),
                 supported ? 1 : 0);
        QWidget secondary;
        WindowChrome::install(&secondary);
        QVERIFY(!secondary.property("customWindowChromeEnabled").toBool());
        QVERIFY(!secondary.findChild<QObject*>(QStringLiteral("customWindowChrome")));
    }

    void headerDoubleClickExcludesInteractiveChildren()
    {
        CustomFixture fixture;
        if (!WindowChrome::installCustom(&fixture.window, &fixture.header, &fixture.controls))
            QSKIP("Integrated Windows caption behavior is unavailable on this platform.");
        fixture.window.show();
        QTest::mouseDClick(&fixture.title, Qt::LeftButton);
        QTRY_VERIFY(fixture.window.isMaximized());
        QTest::mouseDClick(&fixture.title, Qt::LeftButton);
        QTRY_VERIFY(!fixture.window.isMaximized());
        QTest::mouseDClick(&fixture.input, Qt::LeftButton);
        QVERIFY(!fixture.window.isMaximized());
        QTest::mouseDClick(&fixture.action, Qt::LeftButton);
        QVERIFY(!fixture.window.isMaximized());
        const auto before = fixture.window.windowState();
        QTest::mouseDClick(&fixture.controls, Qt::LeftButton, Qt::NoModifier, QPoint(34, 16));
        QCOMPARE(fixture.window.windowState(), before);
    }

    void nativeCustomFrameHitTestsAndWorkArea()
    {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Native Windows hit tests require the Windows platform plugin.");
        CustomFixture fixture;
        QVERIFY(WindowChrome::installCustom(&fixture.window, &fixture.header, &fixture.controls));
        fixture.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
        QCoreApplication::processEvents();
        const auto handle = reinterpret_cast<HWND>(fixture.window.winId());
        const auto clientTopLeft = [handle] {
            POINT point{ 0, 0 };
            ClientToScreen(handle, &point);
            return QPoint(point.x, point.y);
        };
        RECT bounds{};
        QVERIFY(GetWindowRect(handle, &bounds));
        RECT normalClient{};
        QVERIFY(GetClientRect(handle, &normalClient));
        const QPoint clientOrigin = clientTopLeft();
        // Only equally sized resize borders remain. A native caption would
        // make the top margin larger than the bottom margin.
        const int bottomBorder = bounds.bottom - clientOrigin.y() - normalClient.bottom;
        QVERIFY(bottomBorder > 0);
        QCOMPARE(clientOrigin.y() - bounds.top, bottomBorder);
        QCOMPARE(clientOrigin.x() - bounds.left, bounds.right - clientOrigin.x() - normalClient.right);
        const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
        // Measuring an equal top/bottom border is not sufficient: leaving the
        // caption style enabled still lets DWM paint native button fragments
        // into that border. Windows itself must consider the caption hidden.
        QCOMPARE(style & WS_CAPTION, LONG_PTR(0));
        TITLEBARINFO titleBar{};
        titleBar.cbSize = sizeof(titleBar);
        QVERIFY(GetTitleBarInfo(handle, &titleBar));
        QVERIFY(titleBar.rgstate[0] & STATE_SYSTEM_INVISIBLE);
        QVERIFY(style & WS_THICKFRAME);
        QVERIFY(style & WS_SYSMENU);
        QVERIFY(style & WS_MINIMIZEBOX);
        QVERIFY(style & WS_MAXIMIZEBOX);
        const auto hit = [handle](POINT point) {
            return SendMessageW(handle, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y));
        };
        const auto widgetHit = [&](QWidget* widget, QPoint point) {
            const auto local = widget->mapTo(&fixture.window, point);
            POINT native{ qRound(local.x() * fixture.window.devicePixelRatioF()), qRound(local.y() * fixture.window.devicePixelRatioF()) };
            ClientToScreen(handle, &native);
            return hit(native);
        };
        QCOMPARE(hit(POINT{ bounds.left + 1, bounds.top + 1 }), LRESULT(HTTOPLEFT));
        QCOMPARE(hit(POINT{ bounds.right - 2, bounds.bottom - 2 }), LRESULT(HTBOTTOMRIGHT));
        QCOMPARE(widgetHit(&fixture.title, fixture.title.rect().center()), LRESULT(HTCAPTION));
        QCOMPARE(widgetHit(&fixture.input, fixture.input.rect().center()), LRESULT(HTCLIENT));
        QCOMPARE(widgetHit(&fixture.action, fixture.action.rect().center()), LRESULT(HTCLIENT));
        auto* maximize = fixture.controls.findChild<QToolButton*>(QStringLiteral("windowMaximize"));
        auto* close = fixture.controls.findChild<QToolButton*>(QStringLiteral("windowClose"));
        QCOMPARE(widgetHit(maximize, maximize->rect().center()), LRESULT(HTMAXBUTTON));
        QCOMPARE(widgetHit(close, close->rect().center()), LRESULT(HTCLIENT));

        SendMessageW(handle, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
        QTRY_VERIFY(fixture.window.isMaximized());
        QCoreApplication::processEvents();
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        QVERIFY(GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor));
        RECT client{};
        QVERIFY(GetClientRect(handle, &client));
        QCOMPARE(clientTopLeft(), QPoint(monitor.rcWork.left, monitor.rcWork.top));
        QCOMPARE(QSize(client.right, client.bottom), QSize(monitor.rcWork.right - monitor.rcWork.left, monitor.rcWork.bottom - monitor.rcWork.top));
        QVERIFY(GetWindowRect(handle, &bounds));
        // The outer maximized frame must also use the work area. Clipping a
        // full-monitor frame at the taskbar gives Qt an asymmetric margin and
        // changes the window's minimum size when it is restored.
        QCOMPARE(clientTopLeft().y() - bounds.top, bounds.bottom - clientTopLeft().y() - client.bottom);
        QCOMPARE(clientTopLeft().x() - bounds.left, bounds.right - clientTopLeft().x() - client.right);
        SendMessageW(handle, WM_SYSCOMMAND, SC_RESTORE, 0);
        QTRY_VERIFY(!fixture.window.isMaximized());
        SendMessageW(handle, WM_SYSCOMMAND, SC_CLOSE, 0);
        QTRY_COMPARE(fixture.window.closeRequests, 1);
        QVERIFY(fixture.window.isVisible());
#else
        QSKIP("Native Windows hit tests require Windows.");
#endif
    }

    void nativeMaximizeClickUsesCustomButton()
    {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Native caption clicks require the Windows platform plugin.");
        CustomFixture fixture;
        QVERIFY(WindowChrome::installCustom(&fixture.window, &fixture.header, &fixture.controls));
        fixture.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
        auto* maximize = fixture.controls.findChild<QToolButton*>(QStringLiteral("windowMaximize"));
        QVERIFY(maximize);
        const auto handle = reinterpret_cast<HWND>(fixture.window.winId());
        QSignalSpy clicks(maximize, &QToolButton::clicked);
        const auto nativeClick = [&] {
            const QPoint point = maximize->mapTo(&fixture.window, maximize->rect().center());
            POINT native{ qRound(point.x() * fixture.window.devicePixelRatioF()),
                          qRound(point.y() * fixture.window.devicePixelRatioF()) };
            ClientToScreen(handle, &native);
            const LPARAM position = MAKELPARAM(native.x, native.y);
            QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, position), LRESULT(HTMAXBUTTON));
            // QTest::mouseClick bypasses the Windows non-client input path.
            // A native press starts outside the client area, then capture
            // routes its release back as a client-coordinate message.
            QVERIFY(PostMessageW(handle, WM_NCLBUTTONDOWN, HTMAXBUTTON, position));
            ScreenToClient(handle, &native);
            QVERIFY(PostMessageW(handle, WM_LBUTTONUP, 0, MAKELPARAM(native.x, native.y)));
            // Bound failures in an unhandled native caption tracking loop.
            // This arrives only after the complete press/release sequence.
            QVERIFY(PostMessageW(handle, WM_CANCELMODE, 0, 0));
        };
        const QRect normalGeometry = fixture.window.geometry();
        for (int cycle = 0; cycle < 3; ++cycle) {
            nativeClick();
            QTRY_VERIFY_WITH_TIMEOUT(fixture.window.isMaximized(), 2000);
            QTRY_COMPARE(clicks.count(), cycle * 2 + 1);
            QVERIFY(!maximize->isDown());
            nativeClick();
            QTRY_VERIFY_WITH_TIMEOUT(!fixture.window.isMaximized(), 2000);
            QTRY_COMPARE(clicks.count(), cycle * 2 + 2);
            QTRY_COMPARE(fixture.window.geometry(), normalGeometry);
            QVERIFY(!maximize->isDown());
        }
        fixture.window.showFullScreen();
        QTRY_VERIFY(fixture.window.isFullScreen());
        nativeClick();
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.window.isFullScreen(), 2000);
        QTRY_COMPARE(clicks.count(), 7);
        QTRY_COMPARE(fixture.window.geometry(), normalGeometry);
#else
        QSKIP("Native caption clicks require Windows.");
#endif
    }

    void nativeMaximizePressCanBeCanceled()
    {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Native caption clicks require the Windows platform plugin.");
        CustomFixture fixture;
        QVERIFY(WindowChrome::installCustom(&fixture.window, &fixture.header, &fixture.controls));
        fixture.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
        auto* maximize = fixture.controls.findChild<QToolButton*>(QStringLiteral("windowMaximize"));
        QVERIFY(maximize);
        const auto handle = reinterpret_cast<HWND>(fixture.window.winId());
        const QPoint center = maximize->mapTo(&fixture.window, maximize->rect().center());
        POINT client{ qRound(center.x() * fixture.window.devicePixelRatioF()),
                      qRound(center.y() * fixture.window.devicePixelRatioF()) };
        POINT screen = client;
        ClientToScreen(handle, &screen);
        const LPARAM clientPosition = MAKELPARAM(client.x, client.y);
        const LPARAM screenPosition = MAKELPARAM(screen.x, screen.y);
        const LPARAM outside = MAKELPARAM(10, 120);
        QSignalSpy clicks(maximize, &QToolButton::clicked);
        const auto post = [handle](UINT message, WPARAM parameter, LPARAM position) {
            QVERIFY(PostMessageW(handle, message, parameter, position));
        };
        const auto verifyCanceled = [&] {
            post(WM_CANCELMODE, 0, 0);
            QCoreApplication::processEvents();
            QVERIFY(!fixture.window.isMaximized());
            QVERIFY(!maximize->isDown());
            QVERIFY(!maximize->property("nativeHover").toBool());
            QVERIFY(GetCapture() != handle);
            QCOMPARE(clicks.count(), 0);
        };
        post(WM_NCLBUTTONDOWN, HTMAXBUTTON, screenPosition);
        post(WM_MOUSEMOVE, MK_LBUTTON, outside);
        post(WM_LBUTTONUP, 0, outside);
        verifyCanceled();

        for (UINT cancel : { UINT(WM_CANCELMODE), UINT(WM_CAPTURECHANGED), UINT(WM_ACTIVATE) }) {
            post(WM_NCLBUTTONDOWN, HTMAXBUTTON, screenPosition);
            post(cancel, 0, 0);
            post(WM_LBUTTONUP, 0, clientPosition);
            verifyCanceled();
        }
        // Moving off and back on during a held press still activates once.
        post(WM_NCLBUTTONDOWN, HTMAXBUTTON, screenPosition);
        post(WM_MOUSEMOVE, MK_LBUTTON, outside);
        post(WM_MOUSEMOVE, MK_LBUTTON, clientPosition);
        post(WM_LBUTTONUP, 0, clientPosition);
        post(WM_CANCELMODE, 0, 0);
        QTRY_VERIFY_WITH_TIMEOUT(fixture.window.isMaximized(), 2000);
        QCOMPARE(clicks.count(), 1);
        QVERIFY(!maximize->isDown());
        QVERIFY(GetCapture() != handle);

        // Accept a non-client release too (e.g. accessibility input) without
        // handing it to Windows for a duplicate maximize/restore command.
        POINT maximizedScreen = client;
        ClientToScreen(handle, &maximizedScreen);
        const LPARAM maximizedPosition = MAKELPARAM(maximizedScreen.x, maximizedScreen.y);
        post(WM_NCLBUTTONDOWN, HTMAXBUTTON, maximizedPosition);
        post(WM_NCLBUTTONUP, HTMAXBUTTON, maximizedPosition);
        post(WM_CANCELMODE, 0, 0);
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.window.isMaximized(), 2000);
        QCOMPARE(clicks.count(), 2);
#else
        QSKIP("Native caption clicks require Windows.");
#endif
    }

    void pointerMinimizePreservesEditorFocus()
    {
        CustomFixture fixture;
        WindowChrome::installCustom(&fixture.window, &fixture.header, &fixture.controls);
        auto* minimize = fixture.controls.findChild<QToolButton*>(QStringLiteral("windowMinimize"));
        auto* maximize = fixture.controls.findChild<QToolButton*>(QStringLiteral("windowMaximize"));
        QVERIFY(minimize && maximize);
        fixture.window.show();
        fixture.window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&fixture.window));
        fixture.input.setFocus();
        QTRY_VERIFY(fixture.input.hasFocus());
        QTest::mouseClick(minimize, Qt::LeftButton);
        QTRY_VERIFY(fixture.window.isMinimized());
        QVERIFY(!minimize->isDown());
        QVERIFY(!minimize->property("nativeHover").toBool());
        fixture.window.showNormal();
        fixture.window.activateWindow();
        QTRY_VERIFY(fixture.input.hasFocus());
        QVERIFY(!minimize->hasFocus());
        QVERIFY(!minimize->isDown());
        QVERIFY(!minimize->property("nativeHover").toBool());
        // Tab still reaches each caption control, with Space/Enter support.
        QTest::keyClick(&fixture.input, Qt::Key_Tab);
        QTRY_VERIFY(minimize->hasFocus());
        QTest::keyClick(minimize, Qt::Key_Tab);
        QTRY_VERIFY(maximize->hasFocus());
        QTest::keyClick(maximize, Qt::Key_Space);
        QTRY_VERIFY(fixture.window.isMaximized());
        QTest::keyClick(maximize, Qt::Key_Return);
        QTRY_VERIFY(!fixture.window.isMaximized());
        // A later pointer click must also clear a previously keyboard-focused
        // caption button, so its focus ring cannot persist after restoring.
        minimize->setFocus(Qt::TabFocusReason);
        QTest::mouseClick(minimize, Qt::LeftButton);
        QTRY_VERIFY(fixture.window.isMinimized());
        fixture.window.showNormal();
        fixture.window.activateWindow();
        QTRY_VERIFY(!minimize->hasFocus());
        QVERIFY(!minimize->isDown());
    }

    void nativeCaptionCannotReturnDuringWindowChanges()
    {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Native caption behavior requires the Windows platform plugin.");
        CustomFixture fixture;
        QVERIFY(WindowChrome::installCustom(&fixture.window, &fixture.header, &fixture.controls));
        fixture.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
        const auto handle = reinterpret_cast<HWND>(fixture.window.winId());
        const auto flags = fixture.window.windowFlags();
        const auto geometry = fixture.window.geometry();
        const auto hasNativeCaption = [handle] { return (GetWindowLongPtrW(handle, GWL_STYLE) & WS_CAPTION) != 0; };
        QTRY_VERIFY(!hasNativeCaption());

        // Exercise the same style rewrite used by window frameworks. It must
        // be corrected before drawing, rather than in a later queued update.
        SetWindowLongPtrW(handle, GWL_STYLE, GetWindowLongPtrW(handle, GWL_STYLE) | WS_CAPTION);
        QVERIFY(!hasNativeCaption());
        QVERIFY(SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED));
        QCoreApplication::processEvents();
        QCOMPARE(fixture.window.windowFlags(), flags);
        QCOMPARE(fixture.window.geometry(), geometry);

        fixture.window.showMaximized();
        QTRY_VERIFY(fixture.window.isMaximized());
        QVERIFY(!hasNativeCaption());
        fixture.window.showNormal();
        QTRY_VERIFY(!fixture.window.isMaximized());
        QVERIFY(!hasNativeCaption());
        fixture.window.showMinimized();
        QTRY_VERIFY(fixture.window.isMinimized());
        QVERIFY(!hasNativeCaption());
        fixture.window.showNormal();
        QTRY_VERIFY(!fixture.window.isMinimized());
        QVERIFY(!hasNativeCaption());
        QTRY_COMPARE(fixture.window.geometry(), geometry);

        for (const auto color : { QColor("#202020"), QColor("#F4F1FA") }) {
            auto palette = fixture.window.palette();
            palette.setColor(QPalette::Window, color);
            fixture.window.setPalette(palette);
            QCoreApplication::processEvents();
            QVERIFY(!hasNativeCaption());
            TITLEBARINFO titleBar{};
            titleBar.cbSize = sizeof(titleBar);
            QVERIFY(GetTitleBarInfo(handle, &titleBar));
            QVERIFY(titleBar.rgstate[0] & STATE_SYSTEM_INVISIBLE);
        }
#else
        QSKIP("Native caption behavior requires Windows.");
#endif
    }

    void customClientGeometrySurvivesResizeAndRestore()
    {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Native Windows geometry requires the Windows platform plugin.");
        CustomFixture fixture;
        fixture.window.setMinimumSize(680, 640);
        QVERIFY(WindowChrome::installCustom(&fixture.window, &fixture.header, &fixture.controls));
        fixture.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
        const auto handle = reinterpret_cast<HWND>(fixture.window.winId());
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (const QSize requested : { QSize(680, 640), QSize(960, 720), QSize(680, 640) }) {
                fixture.window.resize(requested);
                QCoreApplication::processEvents();
                QTRY_COMPARE(fixture.window.size(), requested);
                QCOMPARE(fixture.window.windowHandle()->size(), requested);
                RECT client{};
                QVERIFY(GetClientRect(handle, &client));
                QCOMPARE(QSize(client.right, client.bottom), requested * fixture.window.devicePixelRatioF());
            }
            const QRect normalGeometry = fixture.window.geometry();
            const QByteArray savedGeometry = fixture.window.saveGeometry();
            fixture.window.showMaximized();
            QTRY_VERIFY(fixture.window.isMaximized());
            fixture.window.showNormal();
            QTRY_VERIFY(!fixture.window.isMaximized());
            QCoreApplication::processEvents();
            QTRY_COMPARE(fixture.window.geometry(), normalGeometry);
            fixture.window.showMinimized();
            QTRY_VERIFY(fixture.window.isMinimized());
            fixture.window.showNormal();
            QTRY_VERIFY(!fixture.window.isMinimized());
            QCoreApplication::processEvents();
            QTRY_COMPARE(fixture.window.geometry(), normalGeometry);
            fixture.window.resize(900, 700);
            QVERIFY(fixture.window.restoreGeometry(savedGeometry));
            QTRY_COMPARE(fixture.window.geometry(), normalGeometry);
        }
#else
        QSKIP("Native Windows geometry requires Windows.");
#endif
    }

    void queuedCustomUpdateEndsWithItsOwner()
    {
        auto* window = new QWidget;
        auto* header = new QWidget(window);
        auto* controls = new WindowControls(window, header);
        WindowChrome::installCustom(window, header, controls);
        delete window;
        QCoreApplication::processEvents();
    }

    void installingDoesNotCreateOrReconfigureWindows()
    {
        QWidget window;
        window.setGeometry(120, 160, 900, 640);
        window.setMinimumSize(680, 480);
        window.setMaximumSize(1600, 1200);
        const auto geometry = window.geometry();
        const auto flags = window.windowFlags();
        const auto handle = window.internalWinId();

        WindowChrome::install(nullptr);
        WindowChrome::install(&window);
        WindowChrome::install(&window);
        QCoreApplication::processEvents();

        QCOMPARE(window.internalWinId(), handle);
        QCOMPARE(window.windowFlags(), flags);
        QCOMPARE(window.geometry(), geometry);
        QCOMPARE(window.minimumSize(), QSize(680, 480));
        QCOMPARE(window.maximumSize(), QSize(1600, 1200));
        QVERIFY(!window.isVisible());
    }

    void inlineWidgetsRemainNonNative()
    {
        QWidget window;
        QWidget page(&window);
        page.setGeometry(12, 24, 320, 240);
        const auto geometry = page.geometry();
        const auto flags = page.windowFlags();
        const auto handle = page.internalWinId();
        WindowChrome::install(&page);

        auto palette = page.palette();
        palette.setColor(QPalette::Window, QColor("#191622"));
        page.setPalette(palette);
        QCoreApplication::processEvents();

        QVERIFY(!page.isWindow());
        QCOMPARE(page.parentWidget(), &window);
        QCOMPARE(page.internalWinId(), handle);
        QCOMPARE(page.windowFlags(), flags);
        QCOMPARE(page.geometry(), geometry);
        QCOMPARE(window.internalWinId(), WId(0));
    }

    void nativeControlsAndGeometrySurvivePaletteChanges()
    {
        QWidget window;
        window.setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
        window.setGeometry(100, 100, 900, 640);
        const auto handle = window.winId();
        const auto flags = window.windowFlags();
        const auto geometry = window.geometry();
        WindowChrome::install(&window);
        QCoreApplication::processEvents();

        for (const auto& color : { QColor("#191622"), QColor("#F4F1FA"), QColor(Qt::white) }) {
            auto palette = window.palette();
            palette.setColor(QPalette::Window, color);
            window.setPalette(palette);
            QCoreApplication::processEvents();
            QCOMPARE(window.internalWinId(), handle);
            QCOMPARE(window.windowFlags(), flags);
            QCOMPARE(window.geometry(), geometry);
            QVERIFY(!window.windowFlags().testFlag(Qt::FramelessWindowHint));
            QVERIFY(window.windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
            QVERIFY(window.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
            QVERIFY(window.windowFlags().testFlag(Qt::WindowCloseButtonHint));
        }
    }

    void queuedUpdateEndsWithItsOwner()
    {
        // Destroy a window before the coalesced theme update can run. This also
        // exercises native event-filter teardown under sanitizers.
        QPointer<QWidget> window = new QWidget;
        WindowChrome::install(window);
        delete window;
        QVERIFY(window.isNull());
        QCoreApplication::processEvents();
    }
};

QTEST_MAIN(WindowChromeTest)
#include "WindowChrome_test.moc"
