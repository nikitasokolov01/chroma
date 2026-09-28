// SPDX-License-Identifier: GPL-3.0-only

#include <QApplication>
#include <QPointer>
#include <QTest>
#include <QWidget>

#include "ui/themes/WindowChrome.h"

class WindowChromeTest : public QObject {
    Q_OBJECT

   private slots:
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
