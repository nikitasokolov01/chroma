// SPDX-License-Identifier: GPL-3.0-only
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include "minecraft/skins/SkinExtrasLibrary.h"
#include "ui/dialogs/skins/SkinExtrasPanel.h"
#include "ui/dialogs/skins/draw/SkinGeometry.h"

using D = SkinTextureDocument;

class SkinExtrasTest : public QObject {
    Q_OBJECT
   private:
    QImage texture(QColor color = Qt::cyan) const
    {
        QImage image(64, 64, QImage::Format_ARGB32);
        image.fill(color);
        return image;
    }
   private slots:
    void defaultDirectoryWorksWithoutLauncherApplication()
    {
        D document;
        QVERIFY(document.load(texture()));
        SkinExtrasPanel panel(&document);
        QVERIFY(panel.findChild<QListWidget*>("skinExtraInventory"));
        QVERIFY(!document.isDirty());
    }

    void namedSectionsPersistWithoutChangingSource()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        D source;
        QVERIFY(source.load(texture()));
        source.setSelection(QRegion(QRect(40, 8, 5, 4)));
        const auto patch = source.copyPixels(D::uvRegion(D::Head, D::Overlay, source.model()));
        SkinExtrasLibrary library(dir.path());
        const auto id = library.save("Helmet / blue", patch, source.model());
        QVERIFY(!id.isEmpty());
        QVERIFY(!source.isDirty());
        QVERIFY(!source.canUndo());
        SkinExtrasLibrary reopened(dir.path());
        const auto saved = reopened.load(id);
        QVERIFY(saved);
        QCOMPARE(saved->name, QString("Helmet / blue"));
        QCOMPARE(saved->pixels.image, patch.image);
        QCOMPARE(saved->pixels.mask, patch.mask);
        QCOMPARE(saved->pixels.origin, QPoint(40, 8));
        QCOMPARE(reopened.entries().size(), 1);
        const auto second = reopened.save("Helmet / blue", patch, source.model());
        QVERIFY(!second.isEmpty() && second != id);
        QVERIFY(reopened.remove(id));
        QCOMPARE(reopened.entries().size(), 1);
        QVERIFY(reopened.load(second));
    }

    void rejectsCorruptEntriesAndUnsafeIdentifiers()
    {
        QTemporaryDir dir;
        D source;
        QVERIFY(source.load(texture()));
        SkinExtrasLibrary library(dir.path());
        const auto patch = source.copyPixels(D::uvRegion(D::Head, D::Overlay, source.model()));
        QString error;
        QVERIFY(library.save("", patch, source.model(), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(library.save(QString(81, 'a'), patch, source.model()).isEmpty());
        QVERIFY(!library.load("../accounts"));
        QVERIFY(!library.remove("../accounts"));
        const auto id = library.save("Helmet", patch, source.model());
        QVERIFY(!id.isEmpty());
        QFile file(dir.filePath(id + ".skinextra"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("{\"format\":1,\"name\":\"Bad\",\"model\":0,\"pixels\":\"invalid\"}");
        file.close();
        QVERIFY(!library.load(id));
        QString warning;
        QVERIFY(library.entries(&warning).isEmpty());
        QVERIFY(!warning.isEmpty());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QByteArray(32769, ' '));
        file.close();
        QVERIFY(!library.load(id));
    }

    void wholeOuterLayerLeavesBodyUntouchedAndUndoesOnce()
    {
        D source;
        D target;
        QVERIFY(source.load(texture(QColor(80, 160, 240, 100))));
        QVERIFY(target.load(texture(Qt::red)));
        const auto before = target.image();
        SkinExtrasLibrary::Entry extra{ "", "Jelly", source.model(), source.copyPixels(D::uvRegion(D::All, D::Overlay, source.model())) };
        const auto patch = SkinExtrasLibrary::forModel(extra, target.model());
        target.pastePixels(patch, patch.origin, D::uvRegion(D::All, D::Both, target.model()));
        QCOMPARE(target.image().pixelColor(8, 8), before.pixelColor(8, 8));
        QCOMPARE(target.image().pixelColor(40, 8), QColor(80, 160, 240, 100));
        QVERIFY(target.canUndo());
        target.undo();
        QCOMPARE(target.image(), before);
        QVERIFY(!target.canUndo());
        target.redo();
        QCOMPARE(target.image().pixelColor(40, 8), QColor(80, 160, 240, 100));
    }

    void remapsArmFacesBetweenClassicAndSlim()
    {
        for (const auto sourceModel : { SkinModel::CLASSIC, SkinModel::SLIM }) {
            const auto targetModel = sourceModel == SkinModel::CLASSIC ? SkinModel::SLIM : SkinModel::CLASSIC;
            QImage image = texture(Qt::transparent);
            const auto boxes = opengl::skinBoxes(sourceModel == SkinModel::SLIM);
            const auto& arm = boxes[8];
            const auto faces = opengl::boxFaces(arm.size, arm.center, arm.uv, arm.textureSize);
            for (int face = 0; face < 6; ++face)
                for (int y = faces[face].pixels.top(); y <= faces[face].pixels.bottom(); ++y)
                    for (int x = faces[face].pixels.left(); x <= faces[face].pixels.right(); ++x)
                        image.setPixelColor(x, y, QColor(30 * face, 100, 200, 180));
            D source;
            QVERIFY(source.load(image, sourceModel));
            SkinExtrasLibrary::Entry extra{ "", "Arm", sourceModel, source.copyPixels(D::uvRegion(D::RightArm, D::Overlay, sourceModel)) };
            const auto patch = SkinExtrasLibrary::forModel(extra, targetModel);
            const auto targetBoxes = opengl::skinBoxes(targetModel == SkinModel::SLIM);
            const auto& targetArm = targetBoxes[8];
            const auto targetFaces = opengl::boxFaces(targetArm.size, targetArm.center, targetArm.uv, targetArm.textureSize);
            QCOMPARE(patch.mask.translated(patch.origin), D::uvRegion(D::RightArm, D::Overlay, targetModel));
            for (int face = 0; face < 6; ++face)
                for (int y = targetFaces[face].pixels.top(); y <= targetFaces[face].pixels.bottom(); ++y)
                    for (int x = targetFaces[face].pixels.left(); x <= targetFaces[face].pixels.right(); ++x)
                        QCOMPARE(patch.image.pixelColor(QPoint(x, y) - patch.origin), QColor(30 * face, 100, 200, 180));
        }
    }

    void referenceInventoryMergesPixelsAndHonorsSelection()
    {
        QTemporaryDir dir;
        D edited;
        D reference;
        QVERIFY(edited.load(texture(Qt::red)));
        auto referenceImage = texture(Qt::transparent);
        referenceImage.setPixelColor(40, 8, Qt::blue);
        referenceImage.setPixelColor(41, 8, Qt::green);
        QVERIFY(reference.load(referenceImage));
        const auto referenceBefore = reference.image();
        SkinExtrasPanel panel(&edited, nullptr, dir.path());
        panel.setReferenceDocument(&reference);
        panel.findChild<QComboBox*>("skinExtraSource")->setCurrentIndex(1);
        auto* part = panel.findChild<QComboBox*>("skinExtraPart");
        part->setCurrentIndex(part->findData(int(D::Head)));
        panel.findChild<QLineEdit*>("skinExtraName")->setText("Helmet");
        panel.findChild<QPushButton*>("skinExtraSave")->click();
        QCOMPARE(panel.findChild<QListWidget*>("skinExtraInventory")->count(), 1);
        QVERIFY(!panel.findChild<QToolButton*>("skinExtraSaveToggle")->isChecked());
        QVERIFY(!edited.isDirty());
        QVERIFY(!reference.isDirty());
        edited.setSelection(QRegion(QRect(40, 8, 1, 1)));
        panel.findChild<QPushButton*>("skinExtraApply")->click();
        QCOMPARE(edited.image().pixelColor(40, 8), QColor(Qt::blue));
        QCOMPARE(edited.image().pixelColor(41, 8), QColor(Qt::red));
        QCOMPARE(edited.image().pixelColor(8, 8), QColor(Qt::red));
        edited.undo();
        edited.clearSelection();
        panel.findChild<QPushButton*>("skinExtraApply")->click();
        QCOMPARE(edited.image().pixelColor(41, 8), QColor(Qt::green));
        QCOMPARE(edited.image().pixelColor(42, 8), QColor(Qt::red));
        panel.findChild<QCheckBox*>("skinExtraReplaceTransparent")->setChecked(true);
        panel.findChild<QPushButton*>("skinExtraApply")->click();
        QCOMPARE(edited.image().pixelColor(42, 8).alpha(), 0);
        QCOMPARE(reference.image(), referenceBefore);
        SkinExtrasPanel reopened(&edited, nullptr, dir.path());
        QCOMPARE(reopened.findChild<QListWidget*>("skinExtraInventory")->count(), 1);
        QVERIFY(!reopened.findChild<QToolButton*>("skinExtraSaveToggle")->isChecked());
    }
};

QTEST_MAIN(SkinExtrasTest)
#include "SkinExtras_test.moc"
