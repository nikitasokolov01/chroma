// SPDX-License-Identifier: GPL-3.0-only
#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QTest>
#include <QVBoxLayout>
#include <QWindow>

#include "ui/widgets/OpenGLComposition.h"

namespace {

class SurfaceEvents : public QObject {
   public:
    int windowIdsChanged = 0;
    int surfacesCreated = 0;
    int surfacesDestroyed = 0;

   protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::WinIdChange)
            ++windowIdsChanged;
        if (event->type() == QEvent::PlatformSurface) {
            const auto type = static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
            if (type == QPlatformSurfaceEvent::SurfaceCreated)
                ++surfacesCreated;
            else if (type == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
                ++surfacesDestroyed;
        }
        return false;
    }
};

class Preview final : public QOpenGLWidget {
   public:
    using QOpenGLWidget::QOpenGLWidget;

   protected:
    void paintGL() override
    {
        auto* functions = context()->functions();
        functions->glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
        functions->glClear(GL_COLOR_BUFFER_BIT);
    }
};

}  // namespace

class OpenGLCompositionTest : public QObject {
    Q_OBJECT

   private slots:
    void preparationStaysInvisibleAndIdempotent()
    {
        QMainWindow window;
        const bool prepared = OpenGLComposition::prepare(&window);
        auto* anchor = window.findChild<QOpenGLWidget*>("openGLCompositionAnchor");
        QCOMPARE(anchor != nullptr, prepared);
        QVERIFY(!window.testAttribute(Qt::WA_WState_Created));
        QVERIFY(!window.isVisible());
        if (!prepared)
            return;
        QVERIFY(anchor->isHidden());
        QCOMPARE(anchor->size(), QSize(0, 0));
        QCOMPARE(anchor->focusPolicy(), Qt::NoFocus);
        QVERIFY(!anchor->context());
        QVERIFY(OpenGLComposition::prepare(&window));
        QCOMPARE(window.findChildren<QOpenGLWidget*>("openGLCompositionAnchor").size(), 1);
    }

    void firstPreviewKeepsTheNativeWindow()
    {
        QMainWindow window;
        if (!OpenGLComposition::prepare(&window))
            QSKIP("A native platform with OpenGL is required for surface lifecycle checks.");

        window.resize(560, 420);
        auto* contents = new QWidget(&window);
        auto* layout = new QVBoxLayout(contents);
        auto* label = new QLabel("Launcher content", contents);
        layout->addWidget(label);
        window.setCentralWidget(contents);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* anchor = window.findChild<QOpenGLWidget*>("openGLCompositionAnchor");
        QVERIFY(anchor);
        QVERIFY(anchor->isHidden());
        QVERIFY(!anchor->context());

        QPointer<QWindow> nativeWindow = window.windowHandle();
        QVERIFY(nativeWindow);
        QCOMPARE(nativeWindow->surfaceType(), QSurface::OpenGLSurface);
        const WId nativeId = window.internalWinId();
        QVERIFY(nativeId);
        SurfaceEvents events;
        window.installEventFilter(&events);
        nativeWindow->installEventFilter(&events);

        for (int open = 0; open < 2; ++open) {
            // Match an editor created as a dialog subtree, then adopted by the
            // already-visible inline host. No preview has been shown yet.
            auto* page = new QWidget;
            auto* pageLayout = new QVBoxLayout(page);
            auto* preview = new Preview(page);
            pageLayout->addWidget(preview);
            page->setParent(contents, Qt::Widget);
            layout->addWidget(page, 1);
            page->show();
            QTRY_VERIFY(preview->isValid());
            const auto frame = preview->grabFramebuffer();
            QVERIFY(!frame.isNull());
            const auto center = frame.pixelColor(frame.width() / 2, frame.height() / 2);
            QVERIFY(qAbs(center.red() - 64) <= 1);
            QVERIFY(qAbs(center.green() - 128) <= 1);
            QVERIFY(qAbs(center.blue() - 191) <= 1);
            QCOMPARE(window.windowHandle(), nativeWindow.data());
            QCOMPARE(window.internalWinId(), nativeId);
            QCOMPARE(events.windowIdsChanged, 0);
            QCOMPARE(events.surfacesCreated, 0);
            QCOMPARE(events.surfacesDestroyed, 0);
            QVERIFY(label->isVisible());
            delete page;
            QCoreApplication::processEvents();
            QCOMPARE(window.internalWinId(), nativeId);
            QVERIFY(anchor->isHidden());
            QVERIFY(!anchor->context());
        }
    }
};

QTEST_MAIN(OpenGLCompositionTest)
#include "OpenGLComposition_test.moc"
