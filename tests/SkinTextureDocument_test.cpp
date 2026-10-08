// SPDX-License-Identifier: GPL-3.0-only
#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <limits>

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
    void textureStylesKeepExistingColorsAndStableStrokeHistory_data()
    {
        QTest::addColumn<int>("style");
        QTest::newRow("fine") << int(Document::Fine);
        QTest::newRow("fabric") << int(Document::Fabric);
        QTest::newRow("hair") << int(Document::Hair);
    }

    void textureStylesKeepExistingColorsAndStableStrokeHistory()
    {
        QFETCH(int, style);
        QImage source(64, 64, QImage::Format_ARGB32);
        source.fill(QColor(100, 160, 200, 137));
        source.setPixelColor(40, 8, QColor(12, 24, 48, 0));
        Document document;
        QVERIFY(document.load(source));
        const auto before = document.image();
        const QRegion allowed(40, 8, 8, 8);
        const Document::TextureSettings settings{ static_cast<Document::TextureStyle>(style), 20 };
        QSignalSpy changes(&document, &Document::changed);
        document.beginStroke();
        document.texturePixel({ 43, 11 }, 8, Document::Head, Document::Overlay, settings, allowed);
        const auto firstStamp = document.image();
        QVERIFY(firstStamp != before);
        QCOMPARE(changes.count(), 1);
        // Duplicate begin/event delivery cannot reset the sampled baseline or
        // progressively brighten/darken a pixel during the same drag.
        document.beginStroke();
        for (int repeat = 0; repeat < 20; ++repeat)
            document.texturePixel({ 43, 11 }, 8, Document::Head, Document::Overlay, settings, allowed);
        QCOMPARE(document.image(), firstStamp);
        QCOMPARE(changes.count(), 1);
        document.endStroke();
        bool lighter = false, darker = false;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const auto original = before.pixelColor(x, y), result = document.image().pixelColor(x, y);
                if (!allowed.contains(QPoint(x, y)) || !original.alpha()) {
                    QCOMPARE(result, original);
                    continue;
                }
                QCOMPARE(result.alpha(), original.alpha());
                QVERIFY(qAbs(result.hsvHueF() - original.hsvHueF()) < .02);
                QVERIFY(qAbs(result.hsvSaturationF() - original.hsvSaturationF()) < .02);
                QVERIFY(qAbs(result.red() - original.red()) <= 13);
                QVERIFY(qAbs(result.green() - original.green()) <= 13);
                QVERIFY(qAbs(result.blue() - original.blue()) <= 13);
                lighter |= result.value() > original.value();
                darker |= result.value() < original.value();
            }
        }
        QVERIFY(lighter && darker);
        document.undo();
        QCOMPARE(document.image(), before);
        QVERIFY(!document.canUndo());
        document.redo();
        QCOMPARE(document.image(), firstStamp);
    }

    void texturePatternsAreDistinctAndHairFollowsStrands()
    {
        QImage source(64, 64, QImage::Format_ARGB32);
        source.fill(QColor(128, 128, 128));
        QVector<QImage> results;
        for (auto style : { Document::Fine, Document::Fabric, Document::Hair }) {
            Document document;
            QVERIFY(document.load(source));
            document.texturePixel({ 11, 11 }, 8, Document::Head, Document::Base, { style, 60 });
            results.append(document.image());
        }
        QVERIFY(results[0] != results[1]);
        QVERIFY(results[0] != results[2]);
        QVERIFY(results[1] != results[2]);
        int horizontalVariation = 0, verticalVariation = 0;
        for (int y = 8; y < 15; ++y)
            for (int x = 8; x < 15; ++x) {
                const auto value = results[2].pixelColor(x, y).value();
                horizontalVariation += qAbs(value - results[2].pixelColor(x + 1, y).value());
                verticalVariation += qAbs(value - results[2].pixelColor(x, y + 1).value());
            }
        QVERIFY(horizontalVariation > verticalVariation * 3);
    }

    void textureMirrorsRespectMasksAndExistingPaint()
    {
        for (auto model : { SkinModel::CLASSIC, SkinModel::SLIM }) {
            QImage source(64, 64, QImage::Format_ARGB32);
            source.fill(QColor(120, 80, 40, 137));
            Document document;
            QVERIFY(document.load(source, model));
            document.setMirrorOptions({ false, true, false });
            const auto before = document.image();
            const QPoint pixel(44, 36), mirror(model == SkinModel::SLIM ? 54 : 55, 52);
            const Document::TextureSettings settings{ Document::Fabric, 50 };
            document.texturePixel(pixel, 1, Document::RightArm, Document::Overlay, settings);
            const auto color = document.image().pixelColor(pixel);
            QVERIFY(color != before.pixelColor(pixel));
            QCOMPARE(color.alpha(), 137);
            auto expected = before;
            expected.setPixelColor(pixel, color);
            expected.setPixelColor(mirror, color);
            QCOMPARE(document.image(), expected);
            document.undo();
            QVERIFY(!document.canUndo());

            const auto allowed = Document::uvRegion(Document::RightArm, Document::Overlay, model);
            document.texturePixel(pixel, 1, Document::RightArm, Document::Overlay, settings, allowed);
            expected.setPixelColor(mirror, before.pixelColor(mirror));
            QCOMPARE(document.image(), expected);
            document.undo();
            document.setSelection(QRegion(QRect(pixel, QSize(1, 1))));
            document.texturePixel(pixel, 3, Document::RightArm, Document::Overlay, settings);
            QCOMPARE(document.image(), expected);
            document.undo();
            document.clearSelection();
            document.texturePixel(pixel, 1, Document::RightArm, Document::Base, settings);
            QCOMPARE(document.image(), before);
            document.texturePixel(pixel, 1, Document::RightArm, Document::Overlay, settings,
                                  Document::uvRegion(Document::LeftArm, Document::Overlay, model));
            QCOMPARE(document.image(), before);
            QVERIFY(!document.canUndo());

            // Texture never adds paint to an empty source or mirrored texel.
            source.setPixelColor(pixel, QColor(12, 24, 48, 0));
            QVERIFY(document.load(source, model));
            const auto transparentSource = document.image();
            document.texturePixel(pixel, 1, Document::RightArm, Document::Overlay, settings);
            QCOMPARE(document.image(), transparentSource);
            QVERIFY(!document.canUndo());
            source.setPixelColor(pixel, before.pixelColor(pixel));
            source.setPixelColor(mirror, QColor(12, 24, 48, 0));
            QVERIFY(document.load(source, model));
            document.texturePixel(pixel, 1, Document::RightArm, Document::Overlay, settings);
            QCOMPARE(document.image().pixelColor(pixel), color);
            QCOMPARE(document.image().pixelColor(mirror), source.pixelColor(mirror));
        }
    }

    void textureStrengthAndInvalidPixelsStayBounded()
    {
        QImage source(64, 64, QImage::Format_ARGB32);
        source.fill(QColor(120, 160, 200, 137));
        Document document;
        QVERIFY(document.load(source, SkinModel::SLIM));
        const auto before = document.image();
        document.texturePixel({ 8, 8 }, 8, Document::All, Document::Both, { Document::Fine, 0 });
        document.texturePixel({ 8, 8 }, 8, Document::All, Document::Both, { Document::Fine, -50 });
        document.texturePixel({ 8, 8 }, 8, Document::All, Document::Both, { static_cast<Document::TextureStyle>(-1), 20 });
        document.texturePixel({ 54, 20 }, 1, Document::All, Document::Both, { Document::Fine, 20 });
        document.texturePixel({ -20, -20 }, 8, Document::All, Document::Both, { Document::Fine, 20 });
        QCOMPARE(document.image(), before);
        QVERIFY(!document.canUndo());
        document.texturePixel({ 11, 11 }, 8, Document::Head, Document::Base, { Document::Fabric, 1000 });
        const auto maximum = document.image();
        for (int y = 8; y < 16; ++y)
            for (int x = 8; x < 16; ++x) {
                QCOMPARE(maximum.pixelColor(x, y).alpha(), 255);
                QVERIFY(qAbs(maximum.pixelColor(x, y).value() - before.pixelColor(x, y).value()) <= 62);
            }
        document.undo();
        document.texturePixel({ 11, 11 }, 8, Document::Head, Document::Base, { Document::Fabric, 100 });
        QCOMPARE(document.image(), maximum);
    }

    void textureBaselineFollowsDocumentLifecycle()
    {
        for (int operation = 0; operation < 4; ++operation) {
            QImage source(64, 64, QImage::Format_ARGB32);
            source.fill(QColor(120, 80, 40, 137));
            Document document;
            QVERIFY(document.load(source));
            document.beginStroke();
            document.texturePixel({ 43, 11 }, 8, Document::Head, Document::Overlay, { Document::Fabric, 60 });
            if (operation == 0) {
                document.undo();
            } else if (operation == 1) {
                source.fill(QColor(40, 120, 180, 192));
                QVERIFY(document.load(source));
            } else if (operation == 2) {
                document.setModel(SkinModel::SLIM);
            } else {
                document.reset();
            }
            const auto freshBaseline = document.image();
            Document expected;
            QVERIFY(expected.load(freshBaseline, document.model()));
            expected.texturePixel({ 43, 11 }, 8, Document::Head, Document::Overlay, { Document::Fine, 20 });
            document.beginStroke();
            document.texturePixel({ 43, 11 }, 8, Document::Head, Document::Overlay, { Document::Fine, 20 });
            document.endStroke();
            QCOMPARE(document.image(), expected.image());
            document.undo();
            QCOMPARE(document.image(), freshBaseline);
        }
    }

    void mirrorUsesAnatomicalFaces_data()
    {
        QTest::addColumn<int>("model");
        QTest::addColumn<int>("part");
        QTest::addColumn<int>("layer");
        QTest::addColumn<QPoint>("source");
        QTest::addColumn<QPoint>("destination");
        const auto row = [](const char* name, QPoint source, QPoint destination, Document::Part part = Document::Head,
                            Document::Layer layer = Document::Base, SkinModel::Model model = SkinModel::CLASSIC) {
            QTest::newRow(name) << int(model) << int(part) << int(layer) << source << destination;
        };
        row("head front", { 8, 8 }, { 15, 8 });
        row("head back", { 24, 8 }, { 31, 8 });
        row("head right to left", { 0, 9 }, { 23, 9 });
        row("head left to right", { 16, 10 }, { 7, 10 });
        row("head top", { 8, 0 }, { 15, 0 });
        row("head bottom", { 16, 0 }, { 23, 0 });
        row("hat side", { 32, 9 }, { 55, 9 }, Document::Head, Document::Overlay);
        row("body front", { 20, 20 }, { 27, 20 }, Document::Body);
        row("body back", { 32, 20 }, { 39, 20 }, Document::Body);
        row("body side", { 16, 21 }, { 31, 21 }, Document::Body);
        row("body top", { 20, 16 }, { 27, 16 }, Document::Body);
        row("body bottom", { 28, 16 }, { 35, 16 }, Document::Body);
        row("jacket front", { 20, 36 }, { 27, 36 }, Document::Body, Document::Overlay);
        row("arm front", { 44, 20 }, { 39, 52 }, Document::RightArm);
        row("arm right to left", { 40, 20 }, { 43, 52 }, Document::RightArm);
        row("arm left to right", { 48, 20 }, { 35, 52 }, Document::RightArm);
        row("arm back", { 52, 20 }, { 47, 52 }, Document::RightArm);
        row("arm top", { 44, 16 }, { 39, 48 }, Document::RightArm);
        row("arm bottom", { 48, 16 }, { 43, 48 }, Document::RightArm);
        row("sleeve front", { 44, 36 }, { 55, 52 }, Document::RightArm, Document::Overlay);
        row("slim front", { 44, 20 }, { 38, 52 }, Document::RightArm, Document::Base, SkinModel::SLIM);
        row("slim right to left", { 40, 20 }, { 42, 52 }, Document::RightArm, Document::Base, SkinModel::SLIM);
        row("slim left to right", { 47, 20 }, { 35, 52 }, Document::RightArm, Document::Base, SkinModel::SLIM);
        row("slim back", { 51, 20 }, { 45, 52 }, Document::RightArm, Document::Base, SkinModel::SLIM);
        row("slim top", { 44, 16 }, { 38, 48 }, Document::RightArm, Document::Base, SkinModel::SLIM);
        row("slim bottom", { 47, 16 }, { 41, 48 }, Document::RightArm, Document::Base, SkinModel::SLIM);
        row("slim sleeve front", { 44, 36 }, { 54, 52 }, Document::RightArm, Document::Overlay, SkinModel::SLIM);
        row("slim sleeve side", { 40, 36 }, { 58, 52 }, Document::RightArm, Document::Overlay, SkinModel::SLIM);
        row("leg front", { 4, 20 }, { 23, 52 }, Document::RightLeg);
        row("leg right to left", { 0, 20 }, { 27, 52 }, Document::RightLeg);
        row("leg left to right", { 8, 20 }, { 19, 52 }, Document::RightLeg);
        row("leg back", { 12, 20 }, { 31, 52 }, Document::RightLeg);
        row("leg top", { 4, 16 }, { 23, 48 }, Document::RightLeg);
        row("leg bottom", { 8, 16 }, { 27, 48 }, Document::RightLeg);
        row("trouser leg front", { 4, 36 }, { 7, 52 }, Document::RightLeg, Document::Overlay);
    }

    void mirrorUsesAnatomicalFaces()
    {
        QFETCH(int, model);
        QFETCH(int, part);
        QFETCH(int, layer);
        QFETCH(QPoint, source);
        QFETCH(QPoint, destination);
        Document document;
        QVERIFY(document.load(texture(), static_cast<SkinModel::Model>(model)));
        document.setMirrorOptions({ true, true, true });
        QVERIFY(!document.isDirty());
        QVERIFY(!document.canUndo());
        const auto before = document.image();
        document.paintPixel(source, QColor(200, 40, 60, 80), 1, static_cast<Document::Part>(part), static_cast<Document::Layer>(layer));
        const QColor expectedColor(200, 40, 60, layer == Document::Base ? 255 : 80);
        auto expected = before;
        expected.setPixelColor(source, expectedColor);
        expected.setPixelColor(destination, expectedColor);
        QCOMPARE(document.image(), expected);
        document.undo();
        QCOMPARE(document.image(), before);
        QVERIFY(!document.canUndo());
        document.redo();
        QCOMPARE(document.image(), expected);
        document.undo();
        // Mirroring the opposite anatomical side must map back to the source.
        document.paintPixel(destination, expectedColor, 1, Document::All, Document::Both);
        QCOMPARE(document.image(), expected);
    }

    void mirrorGroupsAreIndependent()
    {
        const QPoint sources[] = { { 8, 8 }, { 20, 20 }, { 44, 20 }, { 4, 20 } };
        const QPoint destinations[] = { { 15, 8 }, { 27, 20 }, { 39, 52 }, { 23, 52 } };
        for (int enabled = -1; enabled < 3; ++enabled) {
            Document document;
            QVERIFY(document.load(texture()));
            document.setMirrorOptions({ enabled == 0, enabled == 1, enabled == 2 });
            const auto before = document.image();
            auto expected = before;
            document.beginStroke();
            for (int i = 0; i < 4; ++i) {
                document.paintPixel(sources[i], Qt::red, 1, Document::All, Document::Both);
                expected.setPixelColor(sources[i], Qt::red);
                if ((i < 2 && enabled == 0) || (i == 2 && enabled == 1) || (i == 3 && enabled == 2))
                    expected.setPixelColor(destinations[i], Qt::red);
            }
            document.endStroke();
            QCOMPARE(document.image(), expected);
            document.undo();
            QCOMPARE(document.image(), before);
            QVERIFY(!document.canUndo());
        }
    }

    void mirrorHonorsDestinationVisibilityAndSelection()
    {
        Document document;
        QVERIFY(document.load(texture()));
        document.setMirrorOptions({ true, true, true });
        const auto before = document.image();
        const QRegion rightArm = Document::uvRegion(Document::RightArm, Document::Both, document.model());
        const QRegion leftArm = Document::uvRegion(Document::LeftArm, Document::Both, document.model());
        document.paintPixel({ 44, 20 }, Qt::red, 1, Document::RightArm, Document::Base, false, rightArm);
        QCOMPARE(document.image().pixelColor(44, 20), QColor(Qt::red));
        QCOMPARE(document.image().pixelColor(39, 52), before.pixelColor(39, 52));
        document.undo();
        // A hidden source cannot paint its visible counterpart either.
        document.paintPixel({ 44, 20 }, Qt::red, 1, Document::RightArm, Document::Base, false, leftArm);
        QCOMPARE(document.image(), before);
        QVERIFY(!document.canUndo());
        const auto bothArms = rightArm + leftArm;
        document.setSelection(QRegion(44, 20, 1, 1));
        document.paintPixel({ 44, 20 }, Qt::red, 1, Document::RightArm, Document::Base, false, bothArms);
        QCOMPARE(document.image().pixelColor(39, 52), before.pixelColor(39, 52));
        document.undo();
        document.setSelection(QRegion(44, 20, 1, 1) + QRegion(39, 52, 1, 1));
        document.paintPixel({ 44, 20 }, Qt::blue, 3, Document::RightArm, Document::Base, false, bothArms);
        auto expected = before;
        expected.setPixelColor(44, 20, Qt::blue);
        expected.setPixelColor(39, 52, Qt::blue);
        QCOMPARE(document.image(), expected);
        document.undo();
        document.clearSelection();
        // Hidden base pixels remain protected even when the outer layer is enabled.
        document.paintPixel({ 44, 20 }, Qt::red, 1, Document::All, Document::Both, false,
                            Document::uvRegion(Document::All, Document::Overlay, document.model()));
        QCOMPARE(document.image(), before);
        QVERIFY(!document.canUndo());
    }

    void mirrorEraserAndLargeStrokesPreserveLayersAndUndoTogether()
    {
        Document document;
        QVERIFY(document.load(texture(), SkinModel::SLIM));
        document.setMirrorOptions({ false, true, false });
        const auto before = document.image();
        document.beginStroke();
        document.paintPixel({ 45, 37 }, Qt::red, 3, Document::RightArm, Document::Overlay);
        document.paintPixel({ 45, 38 }, Qt::red, 3, Document::RightArm, Document::Overlay);
        document.endStroke();
        auto expected = before;
        for (int y = 36; y <= 39; ++y)
            for (int x = 44; x <= 46; ++x) {
                expected.setPixelColor(x, y, Qt::red);
                expected.setPixelColor(54 - (x - 44), y + 16, Qt::red);
            }
        QCOMPARE(document.image(), expected);
        document.undo();
        QCOMPARE(document.image(), before);
        QVERIFY(!document.canUndo());
        document.redo();
        QCOMPARE(document.image(), expected);
        document.paintPixel({ 45, 37 }, Qt::blue, 1, Document::RightArm, Document::Overlay, true);
        QCOMPARE(document.image().pixelColor(45, 37).alpha(), 0);
        QCOMPARE(document.image().pixelColor(53, 53).alpha(), 0);
        QCOMPARE(document.image().pixelColor(44, 37), QColor(Qt::red));
        document.undo();
        QCOMPARE(document.image(), expected);
        document.paintPixel({ 45, 21 }, Qt::blue, 3, Document::RightArm, Document::Base, true);
        QCOMPARE(document.image(), expected);
    }

    void liveAdjustmentsUseStartingColorsAndOneUndo()
    {
        QImage source(64, 64, QImage::Format_ARGB32);
        source.fill(QColor(90, 160, 45, 180));
        Document document;
        QVERIFY(document.load(source));
        const auto before = document.image();
        const auto scope = Document::uvRegion(Document::Head, Document::Overlay, document.model());
        document.setSelection(QRegion(QRect(40, 8, 2, 1)));
        document.setColorAdjustments(40, 0, scope);
        QVERIFY(document.image() != before);
        document.setColorAdjustments(50, 20, scope);
        Document expected;
        QVERIFY(expected.load(before));
        expected.setSelection(document.selection());
        expected.applyEffect(Document::Hue, 50, scope);
        expected.applyEffect(Document::Brightness, 20, scope);
        QCOMPARE(document.image(), expected.image());
        QCOMPARE(document.image().pixelColor(42, 8), before.pixelColor(42, 8));
        QCOMPARE(document.image().pixelColor(40, 8).alpha(), before.pixelColor(40, 8).alpha());
        const auto adjusted = document.image();
        document.undo();
        QCOMPARE(document.image(), before);
        QVERIFY(!document.canUndo());
        QCOMPARE(document.hueAdjustment(), 0);
        document.redo();
        QCOMPARE(document.image(), adjusted);
        QCOMPARE(document.hueAdjustment(), 50);
        QCOMPARE(document.brightnessAdjustment(), 20);
        document.setColorAdjustments(0, 0, scope);
        QCOMPARE(document.image(), before);
        QVERIFY(!document.isDirty());
        QVERIFY(!document.canUndo());
        document.setColorAdjustments(70, 0, scope);
        document.setSelection(QRegion(QRect(41, 8, 1, 1)));
        QCOMPARE(document.hueAdjustment(), 0);
        const auto frozen = document.image();
        document.setColorAdjustments(10, 0, scope);
        document.undo();
        QCOMPARE(document.image(), frozen);
        document.undo();
        QCOMPARE(document.image(), before);
    }

    void editingAfterLiveAdjustmentsUsesASeparateUndo()
    {
        QImage source(64, 64, QImage::Format_ARGB32);
        source.fill(Qt::red);
        Document document;
        QVERIFY(document.load(source));
        const auto scope = Document::uvRegion(Document::All, Document::Both, document.model());
        document.setColorAdjustments(60, 0, scope);
        const auto adjusted = document.image();
        document.beginStroke();
        document.paintPixel(QPoint(8, 8), Qt::blue, 1, Document::Head, Document::Base);
        document.endStroke();
        QCOMPARE(document.hueAdjustment(), 0);
        document.undo();
        QCOMPARE(document.image(), adjusted);
        QCOMPARE(document.hueAdjustment(), 60);
        document.setColorAdjustments(40, 0, scope);
        QVERIFY(!document.canRedo());
        document.undo();
        QCOMPARE(document.image(), SkinModel::normalizeTexture(source));
        QVERIFY(!document.canUndo());
    }

    void capeOnlyRejectsMissingOfflineAndUnownedAccounts()
    {
        auto missing = SkinApplyTask::forCape(nullptr, "cape");
        missing->start();
        QVERIFY(missing->isFinished());
        QVERIFY(!missing->wasSuccessful());
        const auto account = MinecraftAccount::createOffline("CapeTest");
        auto offline = SkinApplyTask::forCape(account, "cape");
        offline->start();
        QVERIFY(!offline->wasSuccessful());
        account->accountData()->type = AccountType::MSA;
        auto unowned = SkinApplyTask::forCape(account, "unowned");
        unowned->start();
        QVERIFY(unowned->failReason().contains("owned"));
        QVERIFY(account->accountData()->minecraftProfile.currentCape.isEmpty());
    }

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

    void liveAdjustmentAtTheUndoLimitCanReturnToZero()
    {
        Document document;
        auto original = texture();
        original.setPixelColor(8, 8, Qt::red);
        QVERIFY(document.load(original));
        const auto region = Document::uvRegion(Document::All, Document::Both, document.model());
        document.setColorAdjustments(60, 0, region);
        for (int i = 0; i < 100; ++i)
            document.paintPixel({ 8, 8 }, i % 2 ? Qt::red : Qt::blue, 1, Document::Head, Document::Base);
        while (document.canUndo())
            document.undo();
        QCOMPARE(document.hueAdjustment(), 60);
        document.setColorAdjustments(0, 0, region);
        QCOMPARE(document.image(), original);
        QVERIFY(!document.canUndo());
        document.paintPixel({ 8, 8 }, Qt::green, 1, Document::Head, Document::Base);
        QVERIFY(document.canUndo());
        document.undo();
        QCOMPARE(document.image(), original);
    }

    void selectionIsNonDestructiveAndClipsBrushes()
    {
        Document document;
        QVERIFY(document.load(texture()));
        QSignalSpy changed(&document, &Document::changed), selected(&document, &Document::selectionChanged);
        document.setSelection(QRegion(-2, -2, 15, 15));
        QVERIFY(document.hasSelection());
        QVERIFY(document.selection().contains(QPoint(8, 8)));
        QVERIFY(!document.selection().contains(QPoint(0, 0)));
        QVERIFY(!document.selection().contains(QPoint(-1, 8)));
        QVERIFY(!document.isDirty());
        QVERIFY(!document.canUndo());
        QCOMPARE(changed.count(), 0);
        QCOMPARE(selected.count(), 1);
        document.setSelection(QRegion(9, 9, 1, 1));
        const auto original = document.image();
        document.paintPixel({ 9, 9 }, Qt::red, 8, Document::All, Document::Both);
        auto expected = original;
        expected.setPixelColor(9, 9, Qt::red);
        QCOMPARE(document.image(), expected);
        QCOMPARE(document.selection(), QRegion(9, 9, 1, 1));
        document.undo();
        QCOMPARE(document.image(), original);
        QVERIFY(document.hasSelection());
        document.clearSelection();
        QVERIFY(!document.hasSelection());
        QVERIFY(!document.isDirty());
        document.setSelection(QRegion(44, 20, 1, 1) + QRegion(54, 20, 1, 1));
        document.setModel(SkinModel::SLIM);
        QCOMPARE(document.selection(), QRegion(44, 20, 1, 1));
        QVERIFY(document.load(texture()));
        QVERIFY(!document.hasSelection());
    }

    void successfulImportClearsSelectionButFailedImportPreservesIt()
    {
        Document document;
        QVERIFY(document.load(texture()));
        document.setSelection(QRegion(8, 8, 3, 3));
        QTemporaryDir dir;
        QVERIFY(!document.importPng(dir.filePath("missing.png")));
        QVERIFY(document.hasSelection());
        const auto path = dir.filePath("valid.png");
        QVERIFY(texture().save(path, "PNG"));
        QVERIFY(document.importPng(path));
        QVERIFY(!document.hasSelection());
    }

    void fillUsesFourNeighborsAndOneUndo()
    {
        auto image = texture();
        for (int y = 8; y < 11; ++y)
            for (int x = 8; x < 12; ++x)
                image.setPixelColor(x, y, x == 10 ? Qt::blue : Qt::red);
        Document document;
        QVERIFY(document.load(image));
        const QRegion allowed(8, 8, 4, 3);
        document.floodFill({ 8, 8 }, QColor(0, 255, 0, 40), allowed);
        for (int y = 8; y < 11; ++y) {
            QCOMPARE(document.image().pixelColor(8, y), QColor(Qt::green));
            QCOMPARE(document.image().pixelColor(9, y), QColor(Qt::green));
            QCOMPARE(document.image().pixelColor(10, y), QColor(Qt::blue));
            QCOMPARE(document.image().pixelColor(11, y), QColor(Qt::red));
        }
        const auto filled = document.image();
        document.undo();
        QCOMPARE(document.image(), image);
        QVERIFY(!document.canUndo());
        document.redo();
        QCOMPARE(document.image(), filled);
        document.undo();
        document.floodFill({ 8, 8 }, Qt::red, allowed);
        QVERIFY(!document.canUndo());
        document.floodFill({ 0, 0 }, Qt::green, allowed);
        QCOMPARE(document.image(), image);
    }

    void fillHonorsSelectionAndTransparentConnectivity()
    {
        auto image = texture();
        image.setPixelColor(40, 8, QColor(10, 20, 30, 0));
        image.setPixelColor(41, 8, QColor(90, 80, 70, 0));
        image.setPixelColor(43, 8, QColor(1, 2, 3, 0));
        Document document;
        QVERIFY(document.load(image));
        document.setSelection(QRegion(40, 8, 2, 1) + QRegion(43, 8, 1, 1));
        document.floodFill({ 40, 8 }, QColor(1, 200, 30, 120), QRegion(40, 8, 4, 1));
        QCOMPARE(document.image().pixelColor(40, 8), QColor(1, 200, 30, 120));
        QCOMPARE(document.image().pixelColor(41, 8), QColor(1, 200, 30, 120));
        QCOMPARE(document.image().pixelColor(42, 8), image.pixelColor(42, 8));
        QCOMPARE(document.image().pixelColor(43, 8), image.pixelColor(43, 8));
        document.undo();
        QCOMPARE(document.image(), image);
        QVERIFY(!document.canUndo());
        image.setPixelColor(40, 8, QColor(200, 10, 10, 100));
        image.setPixelColor(41, 8, QColor(200, 10, 10, 100));
        image.setPixelColor(42, 8, QColor(200, 10, 10, 101));
        QVERIFY(document.load(image));
        document.floodFill({ 40, 8 }, Qt::blue, QRegion(40, 8, 3, 1));
        QCOMPARE(document.image().pixelColor(41, 8), QColor(Qt::blue));
        QCOMPARE(document.image().pixelColor(42, 8), image.pixelColor(42, 8));
        // Diagonal contact alone does not connect two pixels.
        QVERIFY(document.load(texture()));
        document.floodFill({ 40, 8 }, Qt::red, QRegion(40, 8, 1, 1) + QRegion(41, 9, 1, 1));
        QCOMPARE(document.image().pixelColor(41, 9).alpha(), 0);
    }

    void effectsRespectMasksAlphaAndHistory_data()
    {
        QTest::addColumn<int>("effect");
        QTest::addColumn<int>("amount");
        QTest::addColumn<QColor>("input");
        QTest::addColumn<QColor>("output");
        QTest::newRow("hue positive") << int(Document::Hue) << 120 << QColor(255, 0, 0, 77) << QColor(0, 255, 0, 77);
        QTest::newRow("hue negative") << int(Document::Hue) << -120 << QColor(255, 0, 0, 77) << QColor(0, 0, 255, 77);
        QTest::newRow("hue clamps") << int(Document::Hue) << 999 << QColor(255, 0, 0, 77) << QColor(0, 255, 255, 77);
        QTest::newRow("brightness positive") << int(Document::Brightness) << 50 << QColor(100, 50, 0, 77) << QColor(178, 153, 128, 77);
        QTest::newRow("brightness negative") << int(Document::Brightness) << -50 << QColor(100, 50, 0, 77) << QColor(50, 25, 0, 77);
        QTest::newRow("brightness clamps white") << int(Document::Brightness) << 999 << QColor(100, 50, 0, 77) << QColor(255, 255, 255, 77);
        QTest::newRow("brightness clamps black") << int(Document::Brightness) << -999 << QColor(100, 50, 0, 77) << QColor(0, 0, 0, 77);
        QTest::newRow("grayscale") << int(Document::Grayscale) << 0 << QColor(40, 80, 120, 77) << QColor(72, 72, 72, 77);
        QTest::newRow("invert") << int(Document::Invert) << 0 << QColor(40, 80, 120, 77) << QColor(215, 175, 135, 77);
    }

    void effectsRespectMasksAlphaAndHistory()
    {
        QFETCH(int, effect);
        QFETCH(int, amount);
        QFETCH(QColor, input);
        QFETCH(QColor, output);
        auto image = texture();
        for (int x = 40; x < 45; ++x)
            image.setPixelColor(x, 8, input);
        image.setPixelColor(43, 8, QColor(17, 19, 23, 0));
        Document document;
        QVERIFY(document.load(image));
        const QRegion selection = QRegion(40, 8, 2, 1) + QRegion(43, 8, 2, 1);
        document.setSelection(selection);
        document.applyEffect(static_cast<Document::Effect>(effect), amount, QRegion(40, 8, 4, 1));
        QCOMPARE(document.image().pixelColor(40, 8), output);
        QCOMPARE(document.image().pixelColor(41, 8), output);
        QCOMPARE(document.image().pixelColor(42, 8), input);
        QCOMPARE(document.image().pixelColor(43, 8), image.pixelColor(43, 8));
        QCOMPARE(document.image().pixelColor(44, 8), input);
        const auto edited = document.image();
        document.undo();
        QCOMPARE(document.image(), image);
        QVERIFY(!document.canUndo());
        document.redo();
        QCOMPARE(document.image(), edited);
        QCOMPARE(document.selection(), selection);
    }

    void neutralEffectsAndInvalidRegionsDoNotCreateHistory()
    {
        Document document;
        QVERIFY(document.load(texture(), SkinModel::SLIM));
        document.applyEffect(Document::Hue, 0, QRegion(0, 0, 64, 64));
        document.applyEffect(Document::Brightness, 0, QRegion(0, 0, 64, 64));
        document.applyEffect(Document::Invert, 0, QRegion(54, 20, 1, 1));
        document.applyEffect(Document::Invert, 0, QRegion(0, 0, 2, 2));
        document.floodFill({ 54, 20 }, Qt::red, QRegion(0, 0, 64, 64));
        QVERIFY(!document.canUndo());
        QVERIFY(!document.isDirty());
        const auto original = document.image();
        document.applyEffect(Document::Invert, 0, QRegion(8, 8, 4, 1));
        QCOMPARE(document.image().pixelColor(8, 8).alpha(), 255);
        document.undo();
        QCOMPARE(document.image(), original);
        QVERIFY(!document.canUndo());
    }

    void copyPreservesSelectionShapeAndPasteClipsIt()
    {
        auto image = texture();
        image.setPixelColor(8, 8, Qt::red);
        image.setPixelColor(9, 8, Qt::green);
        image.setPixelColor(10, 8, Qt::blue);
        Document source;
        QVERIFY(source.load(image));
        source.setSelection(QRegion(8, 8, 1, 1) + QRegion(10, 8, 1, 1));
        const auto patch = source.copyPixels(QRegion(8, 8, 3, 1));
        QCOMPARE(patch.origin, QPoint(8, 8));
        QCOMPARE(patch.image.size(), QSize(3, 1));
        QCOMPARE(patch.mask, QRegion(0, 0, 1, 1) + QRegion(2, 0, 1, 1));
        QCOMPARE(patch.image.pixelColor(1, 0).alpha(), 0);
        QVERIFY(!source.isDirty());
        Document target;
        QVERIFY(target.load(texture()));
        target.setSelection(QRegion(40, 8, 2, 1));
        target.pastePixels(patch, { 40, 8 }, QRegion(40, 8, 3, 1));
        QCOMPARE(target.image().pixelColor(40, 8), QColor(Qt::red));
        QCOMPARE(target.image().pixelColor(41, 8).alpha(), 0);
        QCOMPARE(target.image().pixelColor(42, 8).alpha(), 0);
        target.undo();
        QVERIFY(!target.canUndo());
        target.clearSelection();
        target.pastePixels(patch, { 40, 8 }, QRegion(40, 8, 3, 1));
        QCOMPARE(target.image().pixelColor(42, 8), QColor(Qt::blue));
        const auto pasted = target.image();
        target.undo();
        QCOMPARE(target.image(), texture());
        QVERIFY(!target.canUndo());
        target.redo();
        QCOMPARE(target.image(), pasted);
        source.clearSelection();
        const auto whole = source.copyPixels(QRegion(8, 8, 3, 1));
        QCOMPARE(whole.mask, QRegion(0, 0, 3, 1));
        QCOMPARE(whole.image.pixelColor(1, 0), QColor(Qt::green));
    }

    void copySelectionRespectsAllowedPartAndLayer()
    {
        auto image = texture();
        image.setPixelColor(8, 8, Qt::red);
        image.setPixelColor(40, 8, Qt::blue);
        Document document;
        QVERIFY(document.load(image));
        document.setSelection(QRegion(8, 8, 1, 1) + QRegion(40, 8, 1, 1));
        const auto outer = Document::uvRegion(Document::Head, Document::Overlay, SkinModel::CLASSIC);
        const auto patch = document.copyPixels(outer);
        QCOMPARE(patch.origin, QPoint(40, 8));
        QCOMPARE(patch.mask, QRegion(0, 0, 1, 1));
        QCOMPARE(patch.image.pixelColor(0, 0), QColor(Qt::blue));
        document.setSelection(QRegion(8, 8, 1, 1));
        QVERIFY(document.copyPixels(outer).image.isNull());
        QVERIFY(document.copyPixels(outer).mask.isEmpty());
        QVERIFY(document.hasSelection());
    }

    void pasteProtectsBaseOpacityAndHandlesTextureEdges()
    {
        Document::PixelPatch patch{ QImage(2, 1, QImage::Format_ARGB32), QRegion(0, 0, 2, 1), {} };
        patch.image.setPixelColor(0, 0, QColor(255, 0, 0, 0));
        patch.image.setPixelColor(1, 0, QColor(0, 0, 255, 64));
        auto image = texture();
        image.setPixelColor(8, 8, Qt::green);
        image.setPixelColor(40, 8, Qt::green);
        Document document;
        QVERIFY(document.load(image));
        const QRegion all(0, 0, 64, 64);
        document.pastePixels(patch, { 8, 8 }, all);
        QCOMPARE(document.image().pixelColor(8, 8), QColor(Qt::green));
        QCOMPARE(document.image().pixelColor(9, 8), QColor(Qt::blue));
        document.undo();
        QCOMPARE(document.image(), image);
        document.pastePixels(patch, { 40, 8 }, all);
        QCOMPARE(document.image().pixelColor(40, 8).alpha(), 0);
        QCOMPARE(document.image().pixelColor(41, 8), QColor(0, 0, 255, 64));
        document.undo();
        document.pastePixels(patch, { -1, 8 }, QRegion(0, 8, 1, 1));
        QCOMPARE(document.image().pixelColor(0, 8), QColor(Qt::blue));
        document.undo();
        QCOMPARE(document.image(), image);
        document.pastePixels(patch, { std::numeric_limits<int>::max(), std::numeric_limits<int>::min() }, all);
        QVERIFY(!document.canUndo());
        QVERIFY(document.load(texture(), SkinModel::SLIM));
        document.pastePixels(patch, { 54, 20 }, all);
        QVERIFY(!document.canUndo());
    }

    void patchCodecPreservesOnlyMaskedPixels()
    {
        Document::PixelPatch patch{ QImage(4, 2, QImage::Format_RGBA8888), QRegion(0, 0, 1, 1) + QRegion(3, 1, 1, 1), { 60, 62 } };
        patch.image.fill(Qt::magenta);
        patch.image.setPixelColor(0, 0, QColor(1, 2, 3, 0));
        patch.image.setPixelColor(3, 1, QColor(40, 50, 60, 77));
        const auto encoded = Document::encodePatch(patch);
        QVERIFY(!encoded.isEmpty());
        const auto decoded = Document::decodePatch(encoded);
        QCOMPARE(decoded.origin, patch.origin);
        QCOMPARE(decoded.mask, patch.mask);
        QCOMPARE(decoded.image.size(), patch.image.size());
        QCOMPARE(decoded.image.pixelColor(0, 0), patch.image.pixelColor(0, 0));
        QCOMPARE(decoded.image.pixelColor(3, 1), patch.image.pixelColor(3, 1));
        QCOMPARE(decoded.image.pixelColor(1, 0), QColor(Qt::transparent));
        QCOMPARE(Document::encodePatch(decoded), encoded);
        Document::PixelPatch maximum{ texture(), QRegion(0, 0, 64, 64), {} };
        QCOMPARE(Document::encodePatch(maximum).size(), 16912);
        QCOMPARE(Document::decodePatch(Document::encodePatch(maximum)).image, maximum.image);
    }

    void patchCodecRejectsInvalidBoundsAndMalformedPayloads()
    {
        Document::PixelPatch patch{ QImage(3, 1, QImage::Format_ARGB32), QRegion(0, 0, 3, 1), {} };
        patch.image.fill(Qt::red);
        const auto valid = Document::encodePatch(patch);
        for (int length = 0; length < valid.size(); ++length)
            QVERIFY(Document::decodePatch(valid.first(length)).image.isNull());
        auto bad = valid;
        bad[8] = char(65);
        QVERIFY(Document::decodePatch(bad).image.isNull());
        bad = valid;
        bad[9] = 0;
        QVERIFY(Document::decodePatch(bad).image.isNull());
        bad = valid;
        bad[10] = char(64);
        QVERIFY(Document::decodePatch(bad).image.isNull());
        bad = valid;
        bad[12] = 1;
        QVERIFY(Document::decodePatch(bad).image.isNull());
        bad = valid;
        bad[16] = 0;
        QVERIFY(Document::decodePatch(bad).image.isNull());
        bad = valid;
        bad[16] = char(0x87);  // Unused mask bits must be zero.
        QVERIFY(Document::decodePatch(bad).image.isNull());
        bad = valid;
        bad[0] = 'x';
        QVERIFY(Document::decodePatch(bad).image.isNull());
        QVERIFY(Document::decodePatch(valid + "trailing").image.isNull());
        QVERIFY(Document::decodePatch(QByteArray(16913, '\0')).image.isNull());
        patch.mask = QRegion(-1, 0, 2, 1);
        QVERIFY(Document::encodePatch(patch).isEmpty());
        patch.mask = QRegion(0, 0, 3, 1);
        patch.origin = { -1, 0 };
        QVERIFY(Document::encodePatch(patch).isEmpty());
        patch.origin = { 63, 0 };
        QVERIFY(Document::encodePatch(patch).isEmpty());
        patch.origin = {};
        patch.image = QImage(65, 1, QImage::Format_ARGB32);
        QVERIFY(Document::encodePatch(patch).isEmpty());
    }

    void clipboardSupportsMaskedPatchesAndBoundedPngFallback()
    {
        if (QGuiApplication::platformName() != "offscreen" && QGuiApplication::platformName() != "minimal")
            QSKIP("Clipboard fixtures require an isolated offscreen clipboard.");
        Document::PixelPatch patch{ QImage(3, 1, QImage::Format_ARGB32), QRegion(0, 0, 1, 1) + QRegion(2, 0, 1, 1), { 8, 8 } };
        patch.image.fill(QColor(10, 20, 30, 77));
        QVERIFY(Document::writeClipboard(patch));
        auto copied = Document::readClipboard();
        QCOMPARE(copied.mask, patch.mask);
        QCOMPARE(copied.origin, patch.origin);
        QCOMPARE(copied.image.pixelColor(0, 0), patch.image.pixelColor(0, 0));
        QCOMPARE(copied.image.pixelColor(1, 0).alpha(), 0);
        QVERIFY(!Document::writeClipboard({}));
        QCOMPARE(Document::readClipboard().mask, patch.mask);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(patch.image.save(&buffer, "PNG"));
        auto* mime = new QMimeData;
        mime->setData("image/png", png);
        QGuiApplication::clipboard()->setMimeData(mime);
        copied = Document::readClipboard();
        QCOMPARE(copied.image, patch.image);
        QCOMPARE(copied.mask, QRegion(patch.image.rect()));
        QCOMPARE(copied.origin, QPoint());
        mime = new QMimeData;
        mime->setData("application/x-chroma-skin-pixels-v1", "invalid");
        mime->setData("image/png", png);
        QGuiApplication::clipboard()->setMimeData(mime);
        QVERIFY(Document::readClipboard().image.isNull());
        QImage oversized(65, 1, QImage::Format_ARGB32);
        oversized.fill(Qt::red);
        png.clear();
        buffer.seek(0);
        QVERIFY(oversized.save(&buffer, "PNG"));
        mime = new QMimeData;
        mime->setData("image/png", png);
        QGuiApplication::clipboard()->setMimeData(mime);
        QVERIFY(Document::readClipboard().image.isNull());
        mime = new QMimeData;
        mime->setData("image/png", QByteArray(1024 * 1024 + 1, 'x'));
        QGuiApplication::clipboard()->setMimeData(mime);
        QVERIFY(Document::readClipboard().image.isNull());
        QGuiApplication::clipboard()->clear();
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

QTEST_MAIN(SkinTextureDocumentTest)
#include "SkinTextureDocument_test.moc"
