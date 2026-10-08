// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QGuiApplication>
#include <QTest>

#include "minecraft/skins/SkinTextureDocument.h"
#include "ui/dialogs/skins/SkinCanvas.h"
#include "ui/dialogs/skins/draw/SkinOpenGLWindow.h"

namespace SkinMirrorViewTests {

inline QImage blankSkin()
{
    QImage image(64, 64, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    return image;
}

class Provider final : public SkinProvider {
   public:
    SkinModel* getSelectedSkin() override { return &skin; }
    QHash<QString, QImage> capes() override { return {}; }
    SkinModel skin{ blankSkin() };
};

inline QPoint frontPixelPoint(SkinOpenGLWindow& preview, SkinTextureDocument::Part part, QPoint pixel)
{
    for (int y = 2; y < preview.height(); y += 2)
        for (int x = 2; x < preview.width(); x += 2) {
            const QPoint point(x, y);
            const auto hit = preview.pickAt(point);
            if (hit && hit->part == part && hit->pixel == pixel && hit->face == 0)
                return point;
        }
    return QPoint(-1, -1);
}

inline void canvasMirrorsPairedArmsAndHonorsHiddenParts()
{
    using D = SkinTextureDocument;
    for (auto model : { SkinModel::CLASSIC, SkinModel::SLIM }) {
        D document;
        QVERIFY(document.load(blankSkin(), model));
        document.setMirrorOptions({ false, true, false });
        SkinCanvas canvas(&document);
        canvas.resize(420, 420);
        canvas.setLayerVisibility(true, false);
        canvas.setColor(Qt::red);
        canvas.show();
        canvas.setFocus();
        // The accessible texture cursor starts at (8, 8).
        for (int i = 0; i < 36; ++i)
            QTest::keyClick(&canvas, Qt::Key_Right);
        for (int i = 0; i < 12; ++i)
            QTest::keyClick(&canvas, Qt::Key_Down);
        const QPoint source(44, 20), destination(model == SkinModel::SLIM ? 38 : 39, 52);
        const auto before = document.image();
        QTest::keyClick(&canvas, Qt::Key_Space);
        auto expected = before;
        expected.setPixelColor(source, Qt::red);
        expected.setPixelColor(destination, Qt::red);
        QCOMPARE(document.image(), expected);
        document.undo();
        QCOMPARE(document.image(), before);
        QVERIFY(!document.canUndo());

        canvas.setPartVisible(D::LeftArm, false);
        QTest::keyClick(&canvas, Qt::Key_Space);
        expected = before;
        expected.setPixelColor(source, Qt::red);
        QCOMPARE(document.image(), expected);
        document.undo();
        canvas.setPartVisible(D::LeftArm, true);
        document.setSelection(QRegion(QRect(source, QSize(1, 1))));
        QTest::keyClick(&canvas, Qt::Key_Space);
        QCOMPARE(document.image(), expected);
        document.undo();
        document.clearSelection();
        canvas.setLayerVisibility(false, true);
        QTest::keyClick(&canvas, Qt::Key_Space);
        QCOMPARE(document.image(), before);
        QVERIFY(!document.canUndo());
    }
}

inline void previewMirrorsPickedArmsAndHonorsHiddenParts()
{
    using D = SkinTextureDocument;
    const auto platform = QGuiApplication::platformName();
    if (platform == "offscreen" || platform == "minimal" || !SkinOpenGLWindow::hasOpenGL())
        QSKIP("Native OpenGL mirror painting requires an available window-system context.");

    for (auto model : { SkinModel::CLASSIC, SkinModel::SLIM }) {
        for (auto layer : { D::Base, D::Overlay }) {
            Provider provider;
            D document;
            QVERIFY(document.load(blankSkin(), model));
            document.setMirrorOptions({ false, true, false });
            SkinOpenGLWindow preview(&provider, QColor(225, 225, 235));
            preview.setDocument(&document);
            preview.setEditingEnabled(true);
            preview.setLayersVisible(true, layer == D::Overlay);
            preview.setColor(Qt::red);
            preview.resize(440, 480);
            preview.show();
            QVERIFY(QTest::qWaitForWindowExposed(&preview));
            QTRY_VERIFY(preview.isValid());

            const QPoint source(44, layer == D::Overlay ? 39 : 23);
            const int lastColumn = model == SkinModel::SLIM ? 2 : 3;
            const QPoint destination((layer == D::Overlay ? 52 : 36) + lastColumn, 55);
            const auto point = frontPixelPoint(preview, D::RightArm, source);
            QVERIFY(point.x() >= 0);
            const auto picked = preview.pickAt(point);
            QVERIFY(picked);
            QCOMPARE(picked->layer, layer);
            QCOMPARE(picked->faceRect.width(), model == SkinModel::SLIM ? 3 : 4);
            const auto before = document.image();
            QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, point);
            auto expected = before;
            expected.setPixelColor(source, Qt::red);
            expected.setPixelColor(destination, Qt::red);
            QCOMPARE(document.image(), expected);
            document.undo();
            QCOMPARE(document.image(), before);
            QVERIFY(!document.canUndo());

            preview.setPartLayerVisible(D::LeftArm, layer, false);
            QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, point);
            expected = before;
            expected.setPixelColor(source, Qt::red);
            QCOMPARE(document.image(), expected);
            document.undo();
            preview.setPartLayerVisible(D::LeftArm, layer, true);
            preview.setPartVisible(D::LeftArm, false);
            QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, point);
            QCOMPARE(document.image(), expected);
            document.undo();
            preview.setPartVisible(D::LeftArm, true);
            document.setSelection(QRegion(QRect(source, QSize(1, 1))));
            QTest::mouseClick(&preview, Qt::LeftButton, Qt::NoModifier, point);
            QCOMPARE(document.image(), expected);
            document.undo();
            QCOMPARE(document.image(), before);
            QVERIFY(!document.canUndo());
        }
    }
}

}  // namespace SkinMirrorViewTests
