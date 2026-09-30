// SPDX-License-Identifier: GPL-3.0-only

#include <QApplication>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTest>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>
#include <memory>

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
QPoint pointForPixel(SkinOpenGLWindow& preview, SkinTextureDocument::Part part, QPoint pixel)
{
    for (int y = 2; y < preview.height(); y += 2) {
        for (int x = 2; x < preview.width(); x += 2) {
            const QPoint point(x, y);
            const auto hit = preview.pickAt(point);
            if (hit && hit->part == part && hit->pixel == pixel && hit->face == 0)
                return point;
        }
    }
    return QPoint(-1, -1);
}

void dragMove(SkinOpenGLWindow& preview, QPoint position, Qt::MouseButtons buttons)
{
    QMouseEvent event(QEvent::MouseMove, QPointF(position), QPointF(preview.mapToGlobal(position)), Qt::NoButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(&preview, &event);
}

void canvasDragMove(SkinCanvas& canvas, QPoint position)
{
    QMouseEvent event(QEvent::MouseMove, QPointF(position), QPointF(canvas.mapToGlobal(position)), Qt::NoButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
}

QPoint projectedPixel(const SkinOpenGLWindow& preview, QVector3D world)
{
    // Default camera: (0,-8,48), vertical field of view 45 degrees.
    const float scale = preview.height() / (2.f * qTan(qDegreesToRadians(22.5f)) * (48.f - world.z()));
    return (QPointF(preview.width() / 2.f + world.x() * scale, preview.height() / 2.f - (world.y() + 8) * scale) *
            preview.devicePixelRatioF())
        .toPoint();
}

int differenceNear(const QImage& first, const QImage& second, QPoint point, int radius = 1)
{
    int difference = 0;
    for (int y = point.y() - radius; y <= point.y() + radius; ++y)
        for (int x = point.x() - radius; x <= point.x() + radius; ++x) {
            if (!first.rect().contains(x, y))
                continue;
            const auto a = first.pixelColor(x, y), b = second.pixelColor(x, y);
            difference = qMax(difference, qMax(qAbs(a.red() - b.red()), qMax(qAbs(a.green() - b.green()), qAbs(a.blue() - b.blue()))));
        }
    return difference;
}

QPoint frontTexelPoint(const SkinOpenGLWindow& preview, SkinTextureDocument::Part part, SkinTextureDocument::Layer layer, QPoint pixel)
{
    const auto box = opengl::skinBoxes(false)[int(part) + (layer == SkinTextureDocument::Overlay ? 6 : 0)];
    const auto face = opengl::boxFaces(box.size, box.center, box.uv, box.textureSize)[0];
    const float horizontal = (pixel.x() + .5f - face.uv.x()) / face.uvHorizontal.x();
    const float vertical = (pixel.y() + .5f - face.uv.y()) / face.uvVertical.y();
    const auto physical = projectedPixel(preview, face.origin + horizontal * face.horizontal + vertical * face.vertical);
    return (QPointF(physical) / preview.devicePixelRatioF()).toPoint();
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

    void directPaintingInterpolatesAndUndoesOneDrag()
    {
        using D = SkinTextureDocument;
        PreviewProvider provider;
        D document;
        QVERIFY(document.load(provider.skin.getTexture()));
        SkinOpenGLWindow preview(&provider, QColor(225, 225, 235));
        preview.setDocument(&document);
        preview.setEditingEnabled(true);
        preview.setLayersVisible(true, false);
        preview.resize(440, 480);
        preview.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        QTRY_VERIFY(preview.isValid());
        const auto initial = document.image();
        const auto first = pointForPixel(preview, D::Body, QPoint(21, 25));
        const auto last = pointForPixel(preview, D::Body, QPoint(26, 25));
        QVERIFY(first.x() >= 0 && last.x() >= 0);
        preview.setColor(Qt::green);
        QSignalSpy changes(&document, &D::changed);
        QTest::mousePress(&preview, Qt::LeftButton, Qt::NoModifier, first);
        dragMove(preview, last, Qt::LeftButton);
        QTest::mouseRelease(&preview, Qt::LeftButton, Qt::NoModifier, last);
        for (int x = 21; x <= 26; ++x)
            QCOMPARE(document.image().pixelColor(x, 25), QColor(Qt::green));
        QVERIFY(changes.count() <= 4);  // Coalesced per pointer event, then history notification.
        QVERIFY(document.canUndo());
        document.undo();
        QCOMPARE(document.image(), initial);
        QVERIFY(!document.canUndo());
        document.redo();
        QCOMPARE(document.image().pixelColor(23, 25), QColor(Qt::green));

        // A large stamp at a face edge must not color the adjacent atlas face.
        document.reset();
        preview.setBrushSize(8);
        const auto edge = pointForPixel(preview, D::Body, QPoint(20, 20));
        QVERIFY(edge.x() >= 0);
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, edge);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (document.image().pixel(x, y) != initial.pixel(x, y))
                    QVERIFY(QRect(20, 20, 8, 12).contains(x, y));
    }

    void outerPaintingEraserPickerAndVisibility()
    {
        using D = SkinTextureDocument;
        PreviewProvider provider;
        QImage transparent(64, 64, QImage::Format_ARGB32);
        transparent.fill(Qt::transparent);
        D document;
        QVERIFY(document.load(transparent));
        SkinOpenGLWindow preview(&provider, QColor(225, 225, 235));
        preview.setDocument(&document);
        preview.setEditingEnabled(true);
        preview.setRegion(D::All, D::Overlay);
        preview.resize(440, 480);
        preview.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        QTRY_VERIFY(preview.isValid());
        const auto point = pointForPixel(preview, D::Body, QPoint(23, 41));
        QVERIFY(point.x() >= 0);
        const QColor ink(35, 160, 220, 180);
        preview.setColor(ink);
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(document.image().pixelColor(23, 41), ink);
        preview.setTool(SkinCanvas::Eyedropper);
        QSignalSpy picked(&preview, &SkinOpenGLWindow::colorPicked);
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(picked.count(), 1);
        QCOMPARE(picked.takeFirst().at(0).value<QColor>(), ink);
        preview.setTool(SkinCanvas::Brush);
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::AltModifier, point);
        QCOMPARE(picked.count(), 1);
        preview.setTool(SkinCanvas::Eraser);
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(document.image().pixelColor(23, 41).alpha(), 0);
        document.undo();
        QCOMPARE(document.image().pixelColor(23, 41), ink);
        preview.setPartLayerVisible(D::Body, D::Overlay, false);
        QVERIFY(preview.partLayerVisible(D::Body, D::Base));
        QVERIFY(!preview.partLayerVisible(D::Body, D::Overlay));
        QVERIFY(preview.pickAt(point));
        QCOMPARE(preview.pickAt(point)->layer, D::Base);
        const auto beforeHiddenClick = document.image();
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(document.image(), beforeHiddenClick);
        preview.setRegion(D::All, D::Base);
        const auto basePoint = pointForPixel(preview, D::Body, QPoint(23, 25));
        QVERIFY(basePoint.x() >= 0);
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, basePoint);
        QCOMPARE(document.image(), beforeHiddenClick);  // Base erasing preserves opaque skin.
    }

    void visibleOuterPixelsAgreeWithPickingAndIndependentMasks()
    {
        using D = SkinTextureDocument;
        PreviewProvider provider;
        QImage texture(64, 64, QImage::Format_ARGB32);
        const QColor base(220, 35, 45), outer(25, 200, 80);
        texture.fill(base);
        for (const auto& region : D::uvRegion(D::All, D::Overlay, SkinModel::CLASSIC))
            for (int y = region.top(); y <= region.bottom(); ++y)
                for (int x = region.left(); x <= region.right(); ++x)
                    texture.setPixelColor(x, y, outer);
        D document;
        QVERIFY(document.load(texture));
        SkinOpenGLWindow preview(&provider, QColor(225, 225, 235));
        preview.setDocument(&document);
        preview.setGridVisible(false);
        preview.setRegion(D::All, D::Both);
        preview.resize(440, 480);
        preview.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        QTRY_VERIFY(preview.isValid());
        const auto first = pointForPixel(preview, D::Body, QPoint(23, 41));
        QVERIFY(first.x() >= 0);
        const auto point = first + QPoint(2, 2);
        auto hit = preview.pickAt(point);
        QVERIFY(hit);
        QCOMPARE(hit->layer, D::Overlay);
        const QPoint pixel = (QPointF(point) * preview.devicePixelRatioF()).toPoint();
        QCOMPARE(preview.grabFramebuffer().pixelColor(pixel), outer);
        preview.setPartLayerVisible(D::Body, D::Overlay, false);
        hit = preview.pickAt(point);
        QVERIFY(hit);
        QCOMPARE(hit->layer, D::Base);
        QCOMPARE(preview.grabFramebuffer().pixelColor(pixel), base);
        preview.setPartLayerVisible(D::Body, D::Base, false);
        QVERIFY(!preview.pickAt(point));
        QVERIFY(preview.grabFramebuffer().pixelColor(pixel) != base);
        preview.setPartLayerVisible(D::Body, D::Overlay, true);
        hit = preview.pickAt(point);
        QVERIFY(hit);
        QCOMPARE(hit->layer, D::Overlay);
        QCOMPARE(preview.grabFramebuffer().pixelColor(pixel), outer);
    }

    void transparentOuterReceivesPaintUntilHidden()
    {
        using D = SkinTextureDocument;
        PreviewProvider provider;
        QImage empty(64, 64, QImage::Format_ARGB32);
        empty.fill(Qt::transparent);
        D document;
        QVERIFY(document.load(empty));
        SkinOpenGLWindow preview(&provider, QColor(225, 225, 235));
        preview.setDocument(&document);
        preview.setEditingEnabled(true);
        preview.setRegion(D::All, D::Base);  // A stale 2D hint must not paint under visible Outer.
        preview.resize(440, 480);
        preview.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        QTRY_VERIFY(preview.isValid());
        const auto initial = document.image();
        const auto outerPoint = pointForPixel(preview, D::Body, QPoint(23, 41));
        QVERIFY(outerPoint.x() >= 0);
        preview.setColor(Qt::red);
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, outerPoint);
        QCOMPARE(document.image().pixelColor(23, 41), QColor(Qt::red));
        for (const auto& region : D::uvRegion(D::All, D::Base, document.model()))
            for (int y = region.top(); y <= region.bottom(); ++y)
                for (int x = region.left(); x <= region.right(); ++x)
                    QCOMPARE(document.image().pixel(x, y), initial.pixel(x, y));
        document.undo();
        QCOMPARE(document.image(), initial);
        preview.setPartLayerVisible(D::Body, D::Overlay, false);
        preview.setRegion(D::All, D::Overlay);  // Hidden Outer must not block the visible Base target.
        const auto basePoint = pointForPixel(preview, D::Body, QPoint(23, 25));
        QVERIFY(basePoint.x() >= 0);
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, basePoint);
        QCOMPARE(document.image().pixelColor(23, 25), QColor(Qt::red));
        QCOMPARE(document.image().pixelColor(23, 41), initial.pixelColor(23, 41));
        preview.setPartLayerVisible(D::Body, D::Base, false);
        const auto beforeHiddenClick = document.image();
        QVERIFY(!preview.pickAt(basePoint));
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, basePoint);
        QCOMPARE(document.image(), beforeHiddenClick);
    }

    void gridShowsBothSurfacesAndRespectsPartVisibility()
    {
        using D = SkinTextureDocument;
        PreviewProvider provider;
        QImage skin(64, 64, QImage::Format_ARGB32);
        skin.fill(Qt::transparent);
        for (const auto& region : D::uvRegion(D::All, D::Base, SkinModel::CLASSIC))
            for (int y = region.top(); y <= region.bottom(); ++y)
                for (int x = region.left(); x <= region.right(); ++x)
                    skin.setPixelColor(x, y, QColor(190, 180, 165));
        D document;
        QVERIFY(document.load(skin));
        const auto initial = document.image();
        SkinOpenGLWindow preview(&provider, QColor(225, 225, 235));
        preview.setDocument(&document);
        for (int part = 1; part < 6; ++part)
            preview.setPartVisible(part, false);
        preview.resize(440, 480);
        preview.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        QTRY_VERIFY(preview.isValid());
        QVERIFY(preview.gridVisible());
        const auto withBothGrids = preview.grabFramebuffer();
        preview.setGridVisible(false);
        const auto withoutGrid = preview.grabFramebuffer();
        // These boundaries are separated by several screen pixels: one is
        // on the 8-wide Base head, the other on its transparent 9-wide shell.
        const auto baseLine = projectedPixel(preview, QVector3D(-3, 4.5f, 4));
        const auto outerLine = projectedPixel(preview, QVector3D(-3.375f, 4.5f, 4.5f));
        QVERIFY(differenceNear(withBothGrids, withoutGrid, baseLine) > 15);
        QVERIFY(differenceNear(withBothGrids, withoutGrid, outerLine) > 15);
        preview.setPartLayerVisible(D::Head, D::Overlay, false);
        preview.setGridVisible(true);
        const auto baseGridOnly = preview.grabFramebuffer();
        QVERIFY(differenceNear(baseGridOnly, withoutGrid, baseLine) > 15);
        QCOMPARE(differenceNear(baseGridOnly, withoutGrid, outerLine, 0), 0);
        preview.setPartLayerVisible(D::Head, D::Base, false);
        const auto hiddenWithGrid = preview.grabFramebuffer();
        preview.setGridVisible(false);
        QCOMPARE(preview.grabFramebuffer(), hiddenWithGrid);
        preview.setPartLayerVisible(D::Head, D::Overlay, true);
        const auto emptyShellWithoutGrid = preview.grabFramebuffer();
        preview.setGridVisible(true);
        const auto emptyShellGrid = preview.grabFramebuffer();
        QVERIFY(differenceNear(emptyShellGrid, emptyShellWithoutGrid, outerLine) > 15);
        // A back-facing grid line must not leak through the transparent shell.
        const auto backLine = projectedPixel(preview, QVector3D(-3.375f, 4.5f, -4.5f));
        QCOMPARE(differenceNear(emptyShellGrid, emptyShellWithoutGrid, backLine, 0), 0);
        QCOMPARE(document.image(), initial);
        QVERIFY(!document.canUndo());
    }

    void gridTracksClassicAndSlimArmTexels()
    {
        using D = SkinTextureDocument;
        PreviewProvider provider;
        D document;
        QImage skin(64, 64, QImage::Format_ARGB32);
        skin.fill(QColor(190, 180, 165));
        QVERIFY(document.load(skin));
        SkinOpenGLWindow preview(&provider, QColor(225, 225, 235));
        preview.setDocument(&document);
        preview.setLayersVisible(true, false);
        for (int part = 0; part < 6; ++part)
            preview.setPartVisible(part, part == D::RightArm);
        preview.resize(440, 480);
        preview.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        QTRY_VERIFY(preview.isValid());
        for (const auto model : { SkinModel::CLASSIC, SkinModel::SLIM }) {
            document.setModel(model);
            preview.setGridVisible(false);
            const auto plain = preview.grabFramebuffer();
            preview.setGridVisible(true);
            const auto grid = preview.grabFramebuffer();
            const float x = model == SkinModel::SLIM ? -6.f : -7.f;
            QVERIFY(differenceNear(grid, plain, projectedPixel(preview, QVector3D(x, -5.5f, 2))) > 15);
            QCOMPARE(differenceNear(grid, plain, projectedPixel(preview, QVector3D(x + .5f, -5.5f, 2)), 0), 0);
        }
    }

    void shiftDimsOuterWithoutChangingTextureAndPaintsVisibleBody()
    {
        using D = SkinTextureDocument;
        PreviewProvider provider;
        QImage texture(64, 64, QImage::Format_ARGB32);
        const QColor body(220, 40, 40), outer(30, 60, 230);
        texture.fill(body);
        for (const auto& rect : D::uvRegion(D::All, D::Overlay, SkinModel::CLASSIC))
            for (int y = rect.top(); y <= rect.bottom(); ++y)
                for (int x = rect.left(); x <= rect.right(); ++x)
                    texture.setPixelColor(x, y, outer);
        D document;
        QVERIFY(document.load(texture));
        const auto original = document.image();
        SkinOpenGLWindow preview(&provider, Qt::gray);
        preview.setDocument(&document);
        preview.setEditingEnabled(true);
        preview.setGridVisible(false);
        for (int part = 1; part < 6; ++part)
            preview.setPartVisible(part, false);
        preview.resize(440, 480);
        preview.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        QTRY_VERIFY(preview.isValid());
        const auto point = frontTexelPoint(preview, D::Head, D::Base, QPoint(12, 12));
        const auto sample = (QPointF(point) * preview.devicePixelRatioF()).toPoint();
        const auto normal = preview.grabFramebuffer();
        preview.setBodyThroughOverlay(true);
        QVERIFY(preview.bodyThroughOverlay());
        QVERIFY(preview.pickAt(point));
        QCOMPARE(preview.pickAt(point)->layer, D::Base);
        const auto dimmed = preview.grabFramebuffer();
        QVERIFY(dimmed.pixelColor(sample).red() > normal.pixelColor(sample).red() + 100);
        QVERIFY(dimmed.pixelColor(sample).blue() < normal.pixelColor(sample).blue() - 100);
        QCOMPARE(document.image(), original);
        QVERIFY(!document.canUndo());
        preview.setColor(Qt::green);
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::ShiftModifier, point);
        QCOMPARE(document.image().pixelColor(12, 12), QColor(Qt::green));
        QCOMPARE(document.image().pixelColor(44, 12), outer);
        document.undo();
        QCOMPARE(document.image(), original);
        QVERIFY(!document.canUndo());
        QTest::keyRelease(&preview, Qt::Key_Shift);
        QVERIFY(!preview.bodyThroughOverlay());
        QCOMPARE(preview.pickAt(point)->layer, D::Overlay);
        preview.setPartLayerVisible(D::Head, D::Base, false);
        preview.setBodyThroughOverlay(true);
        QVERIFY(!preview.pickAt(point));
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::ShiftModifier, point);
        QCOMPARE(document.image(), original);
        preview.hide();
        QVERIFY(!preview.bodyThroughOverlay());
    }

    void bucketUsesOneFaceAndOneUndoForEachVisibleLayer()
    {
        using D = SkinTextureDocument;
        PreviewProvider provider;
        QImage texture(64, 64, QImage::Format_ARGB32);
        texture.fill(Qt::transparent);
        D document;
        QVERIFY(document.load(texture));
        const auto original = document.image();
        SkinOpenGLWindow preview(&provider, Qt::gray);
        preview.setDocument(&document);
        preview.setEditingEnabled(true);
        preview.setTool(SkinCanvas::Bucket);
        preview.setColor(QColor(20, 200, 210, 180));
        preview.resize(440, 480);
        preview.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        QTRY_VERIFY(preview.isValid());
        for (const auto layer : { D::Overlay, D::Base }) {
            const bool body = layer == D::Base;
            preview.setBodyThroughOverlay(body);
            const QPoint pixel(body ? 12 : 44, 12);
            const auto point = frontTexelPoint(preview, D::Head, layer, pixel);
            const QRect expectedFace(body ? 8 : 40, 8, 8, 8);
            QTest::mouseClick(&preview, Qt::LeftButton, body ? Qt::ShiftModifier : Qt::NoModifier, point);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) {
                    if (expectedFace.contains(x, y)) {
                        QCOMPARE(document.image().pixelColor(x, y).red(), 20);
                        QCOMPARE(document.image().pixelColor(x, y).alpha(), body ? 255 : 180);
                    } else {
                        QCOMPARE(document.image().pixel(x, y), original.pixel(x, y));
                    }
                }
            QVERIFY(document.canUndo());
            document.undo();
            QCOMPARE(document.image(), original);
            QVERIFY(!document.canUndo());
        }
    }

    void referenceMarqueeCopiesVisiblePixelsAndPasteClipsToSelection()
    {
        using D = SkinTextureDocument;
        PreviewProvider provider;
        D source;
        QVERIFY(source.load(provider.skin.getTexture()));
        const auto original = source.image();
        SkinOpenGLWindow preview(&provider, Qt::gray);
        preview.setDocument(&source);
        preview.setEditingEnabled(true);
        preview.setReadOnly(true);
        preview.setGridVisible(false);
        preview.resize(440, 480);
        preview.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        QTRY_VERIFY(preview.isValid());
        const auto first = frontTexelPoint(preview, D::Head, D::Overlay, QPoint(42, 10));
        const auto last = frontTexelPoint(preview, D::Head, D::Overlay, QPoint(45, 13));
        for (const auto tool : { SkinCanvas::Brush, SkinCanvas::Eraser, SkinCanvas::Bucket }) {
            preview.setTool(tool);
            QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, first);
            QCOMPARE(source.image(), original);
        }
        const auto plain = preview.grabFramebuffer();
        preview.setTool(SkinCanvas::Select);
        QTest::mousePress(&preview, Qt::LeftButton, Qt::NoModifier, first - QPoint(2, 2));
        dragMove(preview, last + QPoint(2, 2), Qt::LeftButton);
        QTest::mouseRelease(&preview, Qt::LeftButton, Qt::NoModifier, last + QPoint(2, 2));
        QVERIFY(source.selection().contains(QPoint(43, 11)));
        QVERIFY(!source.selection().contains(QPoint(11, 11)));
        QVERIFY(source.selection().subtracted(QRect(40, 8, 8, 8)).isEmpty());
        QCOMPARE(source.image(), original);
        QVERIFY(!source.canUndo());
        const auto highlighted = preview.grabFramebuffer();
        const auto selectedPoint = frontTexelPoint(preview, D::Head, D::Overlay, QPoint(43, 11));
        QVERIFY(differenceNear(plain, highlighted, (QPointF(selectedPoint) * preview.devicePixelRatioF()).toPoint()) > 20);
        QTest::keyClick(&preview, Qt::Key_C, Qt::ControlModifier);
        const auto patch = D::readClipboard();
        QVERIFY(!patch.image.isNull());
        QVERIFY(!patch.mask.isEmpty());
        QVERIFY(!preview.beginPaste());
        QSignalSpy picked(&preview, &SkinOpenGLWindow::colorPicked);
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::AltModifier, first);
        QCOMPARE(picked.count(), 1);

        D target;
        QImage blank(64, 64, QImage::Format_ARGB32);
        blank.fill(Qt::white);
        QVERIFY(target.load(blank));
        const auto targetOriginal = target.image();
        preview.setDocument(&target);
        preview.setReadOnly(false);
        preview.setLayersVisible(true, false);
        preview.setTool(SkinCanvas::Pan);  // Placement must also override orbit mode.
        target.setSelection(QRect(12, 12, 2, 2));
        QTest::keyClick(&preview, Qt::Key_V, Qt::ControlModifier);
        QVERIFY(preview.pastePending());
        QTest::keyClick(&preview, Qt::Key_Escape);
        QVERIFY(!preview.pastePending());
        QCOMPARE(target.image(), targetOriginal);
        QVERIFY(preview.beginPaste());
        const auto destination = frontTexelPoint(preview, D::Head, D::Base, QPoint(12, 12));
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, destination);
        QVERIFY(!preview.pastePending());
        QVERIFY(target.image().pixelColor(12, 12) != QColor(Qt::white));
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (!QRect(12, 12, 2, 2).contains(x, y))
                    QCOMPARE(target.image().pixel(x, y), targetOriginal.pixel(x, y));
        target.undo();
        QCOMPARE(target.image(), targetOriginal);
        QVERIFY(!target.canUndo());
    }

    void textureCanvasSupportsBucketMarqueeReadOnlyAndPaste()
    {
        using D = SkinTextureDocument;
        QImage texture(64, 64, QImage::Format_ARGB32);
        texture.fill(QColor(200, 30, 40));
        D document;
        QVERIFY(document.load(texture));
        const auto original = document.image();
        SkinCanvas canvas(&document);
        canvas.resize(536, 536);  // Fit gives 8 screen pixels per texel, origin (12,12).
        canvas.setRegion(D::Head, D::Overlay);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToView();
        auto point = [](QPoint pixel) { return QPoint(12 + pixel.x() * 8 + 4, 12 + pixel.y() * 8 + 4); };
        canvas.setTool(SkinCanvas::Bucket);
        canvas.setColor(Qt::blue);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, point(QPoint(42, 10)));
        QCOMPARE(document.image().pixelColor(42, 10), QColor(Qt::blue));
        const auto allowed = D::uvRegion(D::Head, D::Overlay, SkinModel::CLASSIC);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (!allowed.contains(QPoint(x, y)))
                    QCOMPARE(document.image().pixel(x, y), original.pixel(x, y));
        document.undo();
        QCOMPARE(document.image(), original);
        QVERIFY(!document.canUndo());
        canvas.setReadOnly(true);
        canvas.setTool(SkinCanvas::Select);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, point(QPoint(42, 10)));
        canvasDragMove(canvas, point(QPoint(45, 13)));
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, point(QPoint(45, 13)));
        QCOMPARE(document.selection(), QRegion(QRect(42, 10, 4, 4)));
        QVERIFY(canvas.copySelection());
        QVERIFY(!canvas.beginPaste());
        for (const auto tool : { SkinCanvas::Brush, SkinCanvas::Eraser, SkinCanvas::Bucket }) {
            canvas.setTool(tool);
            QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, point(QPoint(43, 11)));
            QTest::keyClick(&canvas, Qt::Key_Space);
            QCOMPARE(document.image(), original);
        }
        QVERIFY(!document.canUndo());
        canvas.setReadOnly(false);
        canvas.setTool(SkinCanvas::Pan);
        document.setSelection(QRect(46, 14, 1, 1));
        QVERIFY(canvas.beginPaste());
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(!canvas.pastePending());
        QImage patchImage(2, 2, QImage::Format_ARGB32);
        patchImage.fill(Qt::green);
        QVERIFY(D::writeClipboard({ patchImage, QRegion(patchImage.rect()), {} }));
        canvas.setTool(SkinCanvas::Eyedropper);  // Armed placement overrides the selected tool.
        QVERIFY(canvas.beginPaste());
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, point(QPoint(46, 14)));
        QVERIFY(!canvas.pastePending());
        QCOMPARE(document.image().pixelColor(46, 14), QColor(Qt::green));
        QCOMPARE(document.image().pixelColor(47, 14), original.pixelColor(47, 14));
        QCOMPARE(document.image().pixelColor(46, 15), original.pixelColor(46, 15));
        document.undo();
        QCOMPARE(document.image(), original);
        QVERIFY(!document.canUndo());
        document.clearSelection();
        canvas.setTool(SkinCanvas::Brush);
        canvas.setColor(Qt::green);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::ShiftModifier, point(QPoint(12, 12)));
        QCOMPARE(document.image().pixelColor(12, 12), QColor(Qt::green));
        QCOMPARE(document.image().pixelColor(44, 12), original.pixelColor(44, 12));
        QTest::keyRelease(&canvas, Qt::Key_Shift);
        QVERIFY(!canvas.bodyThroughOverlay());
        document.undo();
        QCOMPARE(document.image(), original);
    }

    void rotationZoomAndInterruptedStrokesKeepHistoryConsistent()
    {
        using D = SkinTextureDocument;
        PreviewProvider provider;
        auto document = std::make_unique<D>();
        QVERIFY(document->load(provider.skin.getTexture()));
        SkinOpenGLWindow preview(&provider, QColor(225, 225, 235));
        preview.setDocument(document.get());
        preview.setEditingEnabled(true);
        preview.setLayersVisible(true, false);
        preview.resize(440, 480);
        preview.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        QTRY_VERIFY(preview.isValid());
        const auto initial = document->image();
        const auto frame = preview.grabFramebuffer();
        const QPoint start(220, 240), end(290, 265);
        QTest::mousePress(&preview, Qt::RightButton, Qt::NoModifier, start);
        dragMove(preview, end, Qt::RightButton);
        QTest::mouseRelease(&preview, Qt::RightButton, Qt::NoModifier, end);
        QVERIFY(preview.grabFramebuffer() != frame);
        QCOMPARE(document->image(), initial);
        QVERIFY(!document->canUndo());
        preview.resetView();
        preview.setTool(SkinCanvas::Pan);
        QTest::mousePress(&preview, Qt::LeftButton, Qt::NoModifier, start);
        dragMove(preview, end, Qt::LeftButton);
        QTest::mouseRelease(&preview, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(preview.grabFramebuffer() != frame);
        QCOMPARE(document->image(), initial);
        preview.resetView();
        QWheelEvent wheel(QPointF(start), QPointF(preview.mapToGlobal(start)), {}, QPoint(0, 600), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(&preview, &wheel);
        QVERIFY(wheel.isAccepted());
        QVERIFY(preview.grabFramebuffer() != frame);
        preview.resetView();
        preview.setTool(SkinCanvas::Brush);
        preview.setColor(Qt::blue);
        const auto point = pointForPixel(preview, D::Body, QPoint(23, 25));
        QVERIFY(point.x() >= 0);
        QTest::mousePress(&preview, Qt::LeftButton, Qt::NoModifier, point);
        // Lost release (e.g. a compositor cancels mouse capture) must close history.
        dragMove(preview, point, Qt::NoButton);
        QVERIFY(document->canUndo());
        document->undo();
        QCOMPARE(document->image(), initial);
        QVERIFY(!document->canUndo());
        QTest::mousePress(&preview, Qt::LeftButton, Qt::NoModifier, point);
        preview.hide();
        QVERIFY(document->canUndo());
        document->undo();
        QCOMPARE(document->image(), initial);
        document.reset();  // The preview's guarded document pointer may outlive its owner.
        preview.show();
        QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, start);
        QVERIFY(!preview.grabFramebuffer().isNull());
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
