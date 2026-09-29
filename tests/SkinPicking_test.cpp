// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>
#include "ui/dialogs/skins/draw/SkinGeometry.h"

using D = SkinTextureDocument;

class SkinPickingTest : public QObject {
    Q_OBJECT
   private slots:
    void headFacesHaveMinecraftImageOrientation()
    {
        const std::array<QVector3D, 6> origins = { QVector3D(-3, 7, 20),  QVector3D(20, 7, 3),    QVector3D(3, 7, -20),
                                                   QVector3D(-20, 7, -3), QVector3D(-3, -20, -3), QVector3D(-3, 20, 3) };
        const std::array<QVector3D, 6> directions = { QVector3D(0, 0, -1), QVector3D(-1, 0, 0), QVector3D(0, 0, 1),
                                                      QVector3D(1, 0, 0),  QVector3D(0, 1, 0),  QVector3D(0, -1, 0) };
        const std::array<QPoint, 6> expected = { QPoint(9, 9), QPoint(17, 9), QPoint(25, 9), QPoint(1, 9), QPoint(17, 1), QPoint(9, 7) };
        for (int face = 0; face < 6; ++face) {
            const auto hit = opengl::pickSkin(origins[face], directions[face], false, 1, 0, D::Head, D::Base);
            QVERIFY(hit);
            QCOMPARE(hit->face, face);
            QCOMPARE(hit->pixel, expected[face]);
        }
    }

    void everyPartAndLayerUsesItsOwnAtlasRegion()
    {
        for (const bool slim : { false, true }) {
            for (const auto& box : opengl::skinBoxes(slim)) {
                const auto faces = opengl::boxFaces(box.size, box.center, box.uv, box.textureSize);
                for (int i = 0; i < 6; ++i) {
                    const auto& face = faces[i];
                    const auto normal = QVector3D::crossProduct(face.horizontal, face.vertical).normalized();
                    const auto center = face.origin + .37f * face.horizontal + .61f * face.vertical;
                    const auto hit =
                        opengl::pickSkin(center + normal * 30, -normal, slim, 1u << box.part, 1u << box.part, box.part, box.layer);
                    QVERIFY(hit);
                    QCOMPARE(hit->part, box.part);
                    QCOMPARE(hit->layer, box.layer);
                    QCOMPARE(hit->face, i);
                    QCOMPARE(hit->faceRect, face.pixels);
                    QVERIFY(D::uvRegion(box.part, box.layer, slim ? SkinModel::SLIM : SkinModel::CLASSIC).contains(hit->pixel));
                }
            }
        }
    }

    void slimArmsHaveThreeTexelsAndCorrectReach()
    {
        const QVector3D front(0, 0, -1);
        for (const auto part : { D::RightArm, D::LeftArm }) {
            const float sign = part == D::RightArm ? -1.f : 1.f;
            const auto classic = opengl::pickSkin(QVector3D(sign * 7.5f, -6, 30), front, false, 0x3f, 0, part, D::Base);
            QVERIFY(classic);
            const auto slim = opengl::pickSkin(QVector3D(sign * 7.5f, -6, 30), front, true, 0x3f, 0, part, D::Base);
            QVERIFY(!slim);
            const auto slimCenter = opengl::pickSkin(QVector3D(sign * 5.5f, -6, 30), front, true, 0x3f, 0, part, D::Base);
            QVERIFY(slimCenter);
            QCOMPARE(slimCenter->faceRect.width(), 3);
            QCOMPARE(classic->faceRect.width(), 4);
            QCOMPARE(slimCenter->pixel, part == D::RightArm ? QPoint(45, 26) : QPoint(37, 58));
        }
    }

    void visibilityTransparencyAndDepthAreRespected()
    {
        QImage image(64, 64, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        const QVector3D origin(0, 4, 30), direction(0, 0, -1);
        auto hit = opengl::pickSkin(origin, direction, false, 1, 1, D::All, D::Both, image);
        QVERIFY(hit);
        QCOMPARE(hit->layer, D::Base);
        hit = opengl::pickSkin(origin, direction, false, 1, 1, D::All, D::Overlay, image);
        QVERIFY(hit);
        QCOMPARE(hit->layer, D::Overlay);
        const auto outerPixel = hit->pixel;
        image.setPixelColor(outerPixel, Qt::blue);
        hit = opengl::pickSkin(origin, direction, false, 1, 1, D::All, D::Both, image);
        QVERIFY(hit);
        QCOMPARE(hit->layer, D::Overlay);
        QCOMPARE(hit->pixel, outerPixel);
        image.setPixelColor(outerPixel, QColor(0, 0, 255, 25));
        hit = opengl::pickSkin(origin, direction, false, 1, 1, D::All, D::Both, image);
        QVERIFY(hit);
        QCOMPARE(hit->layer, D::Base);
        image.setPixelColor(outerPixel, QColor(0, 0, 255, 26));
        hit = opengl::pickSkin(origin, direction, false, 1, 1, D::All, D::Both, image);
        QVERIFY(hit);
        QCOMPARE(hit->layer, D::Overlay);
        QVERIFY(!opengl::pickSkin(origin, direction, false, 0, 1, D::All, D::Base, image));
        QVERIFY(!opengl::pickSkin(origin, direction, false, 1, 0, D::All, D::Overlay, image));
        QVERIFY(!opengl::pickSkin(QVector3D(30, -6, 0), QVector3D(-1, 0, 0), false, 0x3f, 0, D::RightArm, D::Base));
        QVERIFY(opengl::pickSkin(QVector3D(30, -6, 0), QVector3D(-1, 0, 0), false, 1u << D::RightArm, 0, D::RightArm, D::Base));
        // A nearer part's Base blocks a far Outer target and vice versa.
        const QVector3D side(20, -6, 0), across(-1, 0, 0);
        QVERIFY(!opengl::pickSkin(side, across, false, 1u << D::LeftArm, 1u << D::RightArm, D::All, D::Overlay));
        QVERIFY(!opengl::pickSkin(side, across, false, 1u << D::RightArm, 1u << D::LeftArm, D::All, D::Base));
        image.fill(Qt::transparent);
        QVERIFY(opengl::pickSkin(side, across, false, 1u << D::RightArm, 1u << D::LeftArm, D::All, D::Base, image));
        // Selecting Base still reaches under that same part's opaque shell.
        image.fill(Qt::blue);
        hit = opengl::pickSkin(origin, direction, false, 1, 1, D::Head, D::Base, image);
        QVERIFY(hit);
        QCOMPARE(hit->layer, D::Base);
        QVERIFY(!opengl::pickSkin(QVector3D(40, 40, 40), direction, false, 0x3f, 0x3f));
        QVERIFY(!opengl::pickSkin(origin, {}, false, 0x3f, 0x3f));
    }
};

QTEST_GUILESS_MAIN(SkinPickingTest)
#include "SkinPicking_test.moc"
