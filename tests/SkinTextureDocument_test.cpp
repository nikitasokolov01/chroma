// SPDX-License-Identifier: GPL-3.0-only
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "minecraft/skins/SkinApplyTask.h"
#include "minecraft/skins/SkinTextureDocument.h"

using Document = SkinTextureDocument;

class SkinTextureDocumentTest : public QObject {
    Q_OBJECT
   private:
    QImage texture() const
    {
        QImage image(64, 64, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        return SkinModel::normalizeTexture(image);
    }

    int pixels(const QRegion& region) const
    {
        int count = 0;
        for (const auto& rect : region)
            count += rect.width() * rect.height();
        return count;
    }

   private slots:
    void normalizationHandlesPixelFormatsAndOpaqueEdges()
    {
        for (const auto format : { QImage::Format_RGB888, QImage::Format_RGBA8888, QImage::Format_ARGB32 }) {
            QImage image(64, 64, format);
            image.fill(QColor(70, 80, 90));
            // QColor fill on RGB888 composites translucent colors. Set pixels
            // explicitly so the fixture's RGB values are unambiguous.
            for (const QPoint point : { QPoint(8, 8), QPoint(31, 15), QPoint(63, 31), QPoint(47, 63) })
                image.setPixelColor(point, QColor(70, 80, 90, 0));
            QCOMPARE(image.pixelColor(8, 8).red(), 70);
            auto normalized = SkinModel::normalizeTexture(image);
            QCOMPARE(normalized.format(), QImage::Format_ARGB32);
            QCOMPARE(normalized.pixelColor(31, 15).alpha(), 255);
            QCOMPARE(normalized.pixelColor(63, 31).alpha(), 255);
            QCOMPARE(normalized.pixelColor(47, 63).alpha(), 255);
            QCOMPARE(normalized.pixelColor(8, 8).red(), 70);
        }
        QVERIFY(SkinModel::normalizeTexture(QImage(128, 128, QImage::Format_ARGB32)).isNull());
        QCOMPARE(texture().pixelColor(63, 15).alpha(), 0);
    }

    void legacyConversionMirrorsOppositeLimbs()
    {
        QImage image(64, 32, QImage::Format_RGB888);
        image.fill(Qt::white);
        image.setPixelColor(4, 20, Qt::red);
        image.setPixelColor(7, 20, Qt::blue);
        image.setPixelColor(44, 20, Qt::green);
        auto normalized = SkinModel::normalizeTexture(image);
        QCOMPARE(normalized.size(), QSize(64, 64));
        QCOMPARE(normalized.pixelColor(23, 52), QColor(Qt::red));
        QCOMPARE(normalized.pixelColor(20, 52), QColor(Qt::blue));
        QCOMPARE(normalized.pixelColor(39, 52), QColor(Qt::green));
        QCOMPARE(normalized.pixelColor(63, 31).alpha(), 255);
        QCOMPARE(normalized.pixelColor(63, 15).alpha(), 0);
    }

    void validatesContentBeforeImport()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto good = dir.filePath("skin.png");
        QVERIFY(texture().save(good, "PNG"));
        QCOMPARE(Document::readPng(good).size(), QSize(64, 64));
        const auto wrongSize = dir.filePath("large.png");
        QVERIFY(QImage(1024, 1024, QImage::Format_RGB32).save(wrongSize, "PNG"));
        QString error;
        QVERIFY(Document::readPng(wrongSize, &error).isNull());
        QVERIFY(!error.isEmpty());
        const auto disguised = dir.filePath("not-png.png");
        QVERIFY(texture().save(disguised, "BMP"));
        QVERIFY(Document::readPng(disguised).isNull());
        QFile truncated(dir.filePath("truncated.png"));
        QVERIFY(truncated.open(QIODevice::WriteOnly));
        truncated.write("\x89PNG\r\n\x1a\n", 8);
        truncated.close();
        QVERIFY(Document::readPng(truncated.fileName()).isNull());
    }

    void uvMasksMatchGeometry()
    {
        QCOMPARE(pixels(Document::uvRegion(Document::Head, Document::Base, SkinModel::CLASSIC)), 384);
        QCOMPARE(pixels(Document::uvRegion(Document::Body, Document::Base, SkinModel::CLASSIC)), 352);
        QCOMPARE(pixels(Document::uvRegion(Document::RightArm, Document::Base, SkinModel::CLASSIC)), 224);
        QCOMPARE(pixels(Document::uvRegion(Document::RightArm, Document::Base, SkinModel::SLIM)), 192);
        for (auto model : { SkinModel::CLASSIC, SkinModel::SLIM }) {
            auto base = Document::uvRegion(Document::All, Document::Base, model);
            auto overlay = Document::uvRegion(Document::All, Document::Overlay, model);
            QVERIFY(base.intersected(overlay).isEmpty());
            QVERIFY(base.subtracted(QRegion(0, 0, 64, 64)).isEmpty());
            QCOMPARE(pixels(base), model == SkinModel::SLIM ? 1568 : 1632);
        }
        QVERIFY(Document::uvRegion(Document::RightArm, Document::Base, SkinModel::CLASSIC).contains(QPoint(54, 20)));
        QVERIFY(!Document::uvRegion(Document::RightArm, Document::Base, SkinModel::SLIM).contains(QPoint(54, 20)));
    }

    void strokeUndoRedoAndClipping()
    {
        Document document;
        QVERIFY(document.load(texture()));
        const auto original = document.image();
        document.beginStroke();
        document.paintPixel({ 8, 8 }, Qt::red, 1, Document::Head, Document::Base);
        document.paintPixel({ 9, 8 }, Qt::blue, 1, Document::Head, Document::Base);
        document.paintPixel({ 4, 20 }, Qt::red, 8, Document::Head, Document::Base);
        document.endStroke();
        QCOMPARE(document.image().pixelColor(4, 20), original.pixelColor(4, 20));
        QVERIFY(document.isDirty());
        document.undo();
        QCOMPARE(document.image(), original);
        QVERIFY(!document.canUndo());
        QVERIFY(!document.isDirty());
        document.redo();
        QCOMPARE(document.image().pixelColor(8, 8), QColor(Qt::red));
        QCOMPARE(document.image().pixelColor(9, 8), QColor(Qt::blue));
        document.undo();
        document.paintPixel({ 10, 8 }, Qt::green, 1, Document::Head, Document::Base);
        QVERIFY(!document.canRedo());
    }

    void eraserProtectsBaseLayer()
    {
        Document document;
        QVERIFY(document.load(texture()));
        document.paintPixel({ 8, 8 }, QColor(20, 30, 40, 50), 1, Document::Head, Document::Base);
        QCOMPARE(document.image().pixelColor(8, 8).alpha(), 255);
        document.paintPixel({ 40, 8 }, Qt::red, 1, Document::Head, Document::Overlay);
        document.paintPixel({ 40, 8 }, Qt::blue, 1, Document::Head, Document::Both, true);
        QCOMPARE(document.image().pixelColor(40, 8).alpha(), 0);
        document.paintPixel({ 8, 8 }, Qt::blue, 1, Document::Head, Document::Both, true);
        QCOMPARE(document.image().pixelColor(8, 8), QColor(20, 30, 40));
    }

    void importIsUndoableAndResetKeepsOriginal()
    {
        QTemporaryDir dir;
        Document document;
        QVERIFY(document.load(texture(), SkinModel::SLIM));
        auto imported = texture();
        imported.setPixelColor(8, 8, Qt::red);
        const auto path = dir.filePath("import.png");
        QVERIFY(imported.save(path, "PNG"));
        QVERIFY(document.importPng(path));
        QVERIFY(document.isDirty());
        QCOMPARE(document.model(), SkinModel::SLIM);
        document.undo();
        QCOMPARE(document.image(), texture());
        document.redo();
        QCOMPARE(document.image(), imported);
        document.setModel(SkinModel::CLASSIC);
        document.undo();
        QCOMPARE(document.model(), SkinModel::SLIM);
        document.reset();
        QCOMPARE(document.image(), texture());
        document.undo();
        QCOMPARE(document.image(), imported);
    }

    void pngRoundTripAndSavedState()
    {
        QTemporaryDir dir;
        Document document;
        QVERIFY(document.load(texture()));
        document.paintPixel({ 40, 8 }, QColor(10, 20, 30, 128), 1, Document::Head, Document::Overlay);
        const auto path = dir.filePath("export.png");
        QVERIFY(document.exportPng(path));
        QCOMPARE(Document::readPng(path), document.image());
        document.markSaved();
        QVERIFY(!document.isDirty());
        document.undo();
        QVERIFY(document.isDirty());
        document.redo();
        QVERIFY(!document.isDirty());
        QString error;
        QVERIFY(!document.exportPng(dir.filePath("missing/skin.png"), &error));
        QVERIFY(!error.isEmpty());
    }

    void historyIsBounded()
    {
        Document document;
        QVERIFY(document.load(texture()));
        for (int i = 0; i < 150; ++i)
            document.paintPixel({ 8, 8 }, i % 2 ? Qt::red : Qt::blue, 1, Document::Head, Document::Base);
        int steps = 0;
        while (document.canUndo()) {
            document.undo();
            ++steps;
        }
        QCOMPARE(steps, 100);
    }

    void applyRejectsMissingOrOfflineAccountWithoutNetworking()
    {
        SkinApplyTask missing(nullptr, "unused.png", SkinModel::CLASSIC);
        QSignalSpy failed(&missing, &Task::failed);
        missing.start();
        QCOMPARE(failed.count(), 1);
        QVERIFY(!missing.wasSuccessful());
        SkinApplyTask offline(MinecraftAccount::createOffline("SkinTest"), "unused.png", SkinModel::SLIM);
        offline.start();
        QVERIFY(offline.isFinished());
        QVERIFY(!offline.wasSuccessful());
        QVERIFY(!offline.failReason().isEmpty());
    }
};

QTEST_GUILESS_MAIN(SkinTextureDocumentTest)
#include "SkinTextureDocument_test.moc"
