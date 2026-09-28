// SPDX-License-Identifier: GPL-3.0-only

#include <QApplication>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTest>
#include <QVBoxLayout>

#include "ui/dialogs/skins/draw/SkinOpenGLWindow.h"

namespace {
class PreviewProvider final : public SkinProvider {
   public:
    PreviewProvider()
    {
        QImage texture(64, 64, QImage::Format_RGBA8888);
        texture.fill(QColor(220, 35, 45));
        // Exercise alpha composition over the opaque checkerboard as well.
        for (int y = 0; y < 16; ++y)
            for (int x = 32; x < 64; ++x)
                texture.setPixelColor(x, y, QColor(220, 35, 45, 128));
        skin = SkinModel(texture);
    }
    SkinModel* getSelectedSkin() override { return &skin; }
    QHash<QString, QImage> capes() override { return {}; }
    SkinModel skin;
};

bool hasSkinPixels(const QImage& frame, const QColor& skinColor = QColor(220, 35, 45))
{
    int skinPixels = 0;
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            const auto color = frame.pixelColor(x, y);
            if (color.alpha() != 255)
                return false;
            if (qAbs(color.red() - skinColor.red()) < 45 && qAbs(color.green() - skinColor.green()) < 45 &&
                qAbs(color.blue() - skinColor.blue()) < 45)
                ++skinPixels;
        }
    }
    return skinPixels > frame.width() * frame.height() / 100;
}
}  // namespace

class SkinPreviewTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(shaders);
        const auto platform = QGuiApplication::platformName();
        if (platform == "offscreen" || platform == "minimal" || !SkinOpenGLWindow::hasOpenGL())
            QSKIP("Native OpenGL widget composition requires an available window-system context.");
    }

    void statusBarResizeAndScrollKeepPreviewInItsWidget()
    {
        PreviewProvider provider;
        QMainWindow window;
        auto* central = new QWidget(&window);
        auto* layout = new QVBoxLayout(central);
        auto* marker = new QWidget(central);
        const QColor markerColor(20, 180, 70);
        auto palette = marker->palette();
        palette.setColor(QPalette::Window, markerColor);
        marker->setPalette(palette);
        marker->setAutoFillBackground(true);
        marker->setFixedHeight(42);
        layout->addWidget(marker);
        auto* scroll = new QScrollArea(central);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* body = new QWidget;
        auto* bodyLayout = new QVBoxLayout(body);
        auto* preview = new SkinOpenGLWindow(&provider, QColor(225, 225, 235), body);
        preview->setFixedHeight(260);
        auto* row = new QHBoxLayout;
        row->addWidget(preview, 1);
        auto* sideMarker = new QWidget(body);
        const QColor sideColor(30, 70, 190);
        palette.setColor(QPalette::Window, sideColor);
        sideMarker->setPalette(palette);
        sideMarker->setAutoFillBackground(true);
        sideMarker->setFixedSize(64, 260);
        row->addWidget(sideMarker);
        bodyLayout->addLayout(row);
        bodyLayout->addSpacing(800);
        scroll->setWidget(body);
        layout->addWidget(scroll);
        window.setCentralWidget(central);
        window.statusBar()->showMessage("Status remains enabled while the skin preview is visible");
        window.resize(640, 560);
        QSignalSpy frames(preview, &SkinOpenGLWindow::frameRendered);
        QSignalSpy failures(preview, &SkinOpenGLWindow::renderingFailed);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(!frames.isEmpty());
        QCOMPARE(failures.count(), 0);
        QVERIFY(preview->isValid());
        QVERIFY(preview->defaultFramebufferObject() != 0);
        QVERIFY(!preview->testAttribute(Qt::WA_NativeWindow));
        QVERIFY(!scroll->viewport()->testAttribute(Qt::WA_NativeWindow));

        const auto original = preview->grabFramebuffer();
        QVERIFY(hasSkinPixels(original));

        // Qt only guarantees the current context/FBO/viewport before paintGL.
        // State left by another user of the context must not clip or mask it.
        preview->makeCurrent();
        auto* gl = preview->context()->functions();
        gl->glEnable(GL_SCISSOR_TEST);
        gl->glScissor(0, 0, 1, 1);
        gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl->glDepthMask(GL_FALSE);
        gl->glActiveTexture(GL_TEXTURE1);
        preview->doneCurrent();
        const int framesBeforeReadback = frames.count();
        QCOMPARE(preview->grabFramebuffer(), original);
        QVERIFY(frames.count() > framesBeforeReadback);

        for (const auto size : { QSize(720, 600), QSize(520, 640), QSize(640, 560) }) {
            for (bool statusVisible : { false, true }) {
                window.statusBar()->setVisible(statusVisible);
                window.resize(size);
                QCoreApplication::processEvents();
                scroll->verticalScrollBar()->setValue(80);
                QCoreApplication::processEvents();
                const auto frame = preview->grabFramebuffer();
                QCOMPARE(frame.size(), preview->size() * preview->devicePixelRatioF());
                QVERIFY(hasSkinPixels(frame));
                QVERIFY(!preview->testAttribute(Qt::WA_NativeWindow));

                const auto composed = window.grab().toImage();
                const auto point = marker->mapTo(&window, marker->rect().center());
                const auto pixel = (QPointF(point) * composed.devicePixelRatio()).toPoint();
                QCOMPARE(composed.pixelColor(pixel), markerColor);
                QVERIFY(scroll->viewport()->rect().contains(sideMarker->mapTo(scroll->viewport(), sideMarker->rect().center())));
                const auto sidePoint = sideMarker->mapTo(&window, sideMarker->rect().center());
                const auto sidePixel = (QPointF(sidePoint) * composed.devicePixelRatio()).toPoint();
                QCOMPARE(composed.pixelColor(sidePixel), sideColor);
                scroll->verticalScrollBar()->setValue(0);
            }
        }
        QCOMPARE(failures.count(), 0);
    }

    void reparentingRebuildsContextResources()
    {
        PreviewProvider provider;
        QMainWindow first;
        QMainWindow second;
        auto* preview = new SkinOpenGLWindow(&provider, QColor(225, 225, 235));
        QSignalSpy frames(preview, &SkinOpenGLWindow::frameRendered);
        QSignalSpy failures(preview, &SkinOpenGLWindow::renderingFailed);
        first.setCentralWidget(preview);
        first.resize(480, 360);
        first.show();
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        QTRY_VERIFY(!frames.isEmpty());
        QVERIFY(hasSkinPixels(preview->grabFramebuffer()));
        const QColor editedColor(35, 55, 220);
        QImage editedTexture(64, 64, QImage::Format_RGBA8888);
        editedTexture.fill(editedColor);
        preview->setTexture(editedTexture, SkinModel::SLIM);
        QVERIFY(hasSkinPixels(preview->grabFramebuffer(), editedColor));
        first.takeCentralWidget();
        second.setCentralWidget(preview);
        second.resize(540, 400);
        frames.clear();
        second.show();
        preview->show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QTRY_VERIFY(!frames.isEmpty());
        QCOMPARE(failures.count(), 0);
        QVERIFY(preview->isValid());
        QVERIFY(hasSkinPixels(preview->grabFramebuffer(), editedColor));
    }
};

QTEST_MAIN(SkinPreviewTest)
#include "SkinPreview_test.moc"
