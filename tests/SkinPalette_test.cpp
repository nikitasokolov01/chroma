// SPDX-License-Identifier: GPL-3.0-only
#include <QApplication>
#include <QFontDatabase>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalSpy>
#include <QDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>

#include "minecraft/skins/SkinPalette.h"
#include "ui/dialogs/skins/SkinPalettePanel.h"
#include "ui/dialogs/skins/SkinColorWheel.h"

class SkinPaletteTest : public QObject {
    Q_OBJECT
   private:
    QImage texture(QColor color = Qt::blue) const
    {
        QImage image(64, 64, QImage::Format_ARGB32);
        image.fill(color);
        return image;
    }
   private slots:
    void initTestCase()
    {
        // The offscreen Qt platform does not discover Windows system fonts.
        // Use the app's bundled body font for deterministic compact sizing.
        const auto fontPath = QFINDTESTDATA("../launcher/resources/fonts/DMSans-400.ttf");
        const int fontId = QFontDatabase::addApplicationFont(fontPath);
        QVERIFY(fontId >= 0);
        const auto families = QFontDatabase::applicationFontFamilies(fontId);
        QVERIFY(!families.isEmpty());
        QFont font(families.first());
        font.setPixelSize(14);
        QApplication::setFont(font);
    }

    void extractsRelatedShadesAndSeparatesNeutrals()
    {
        QImage image(9, 1, QImage::Format_ARGB32);
        const QList<QColor> colors{ QColor(0, 0, 180), QColor(0, 0, 180, 80), QColor(0, 0, 80), QColor(0, 0, 240),
                                   QColor(Qt::red), QColor(Qt::red), QColor(70, 70, 70), QColor(200, 200, 200), QColor(0, 255, 0, 0) };
        for (int x = 0; x < colors.size(); ++x)
            image.setPixelColor(x, 0, colors[x]);
        const auto families = SkinPalette::extractFamilies(image, QRegion(image.rect()));
        QCOMPARE(families.size(), 3);
        QCOMPARE(families[0].color, QColor(0, 0, 180));
        QCOMPARE(families[0].pixels, 4);
        QCOMPARE(families[1].pixels + families[2].pixels, 4);
        const auto subset = SkinPalette::extractFamilies(image, QRegion(QRect(4, 0, 2, 1)));
        QCOMPARE(subset.size(), 1);
        QCOMPARE(subset[0].color, QColor(Qt::red));
        QCOMPARE(subset[0].pixels, 2);
    }

    void matchesHueAcrossWrapAndRespectsTolerance()
    {
        const auto red = QColor::fromHsv(355, 200, 200);
        QVERIFY(SkinPalette::matchesFamily(QColor::fromHsv(5, 200, 80), red, 11));
        QVERIFY(!SkinPalette::matchesFamily(QColor::fromHsv(5, 200, 80), red, 8));
        QVERIFY(!SkinPalette::matchesFamily(QColor(Qt::gray), red, 180));
        QVERIFY(!SkinPalette::matchesFamily(red, QColor(Qt::gray), 180));
        QVERIFY(SkinPalette::matchesFamily(QColor(240, 240, 240), QColor(20, 20, 20), 0));
        QVERIFY(!SkinPalette::matchesFamily(QColor(255, 0, 0, 0), red, 180));
    }

    void swappingPreservesShadingAlphaAndOtherFamilies()
    {
        QImage image(6, 1, QImage::Format_ARGB32);
        const QColor source(0, 0, 160), target(180, 0, 0);
        const QList<QColor> colors{ QColor(0, 0, 80, 100), source, QColor(0, 0, 240, 170), QColor(Qt::green),
                                   QColor(0, 0, 160, 0), QColor(Qt::gray) };
        for (int x = 0; x < colors.size(); ++x)
            image.setPixelColor(x, 0, colors[x]);
        int changed = -1;
        const auto swapped = SkinPalette::swapFamily(image, QRegion(image.rect()), source, target, 24, &changed);
        QCOMPARE(changed, 3);
        QCOMPARE(swapped.pixelColor(1, 0), target);
        QVERIFY(swapped.pixelColor(0, 0).lightness() < swapped.pixelColor(1, 0).lightness());
        QVERIFY(swapped.pixelColor(2, 0).lightness() > swapped.pixelColor(1, 0).lightness());
        QVERIFY(swapped.pixelColor(2, 0).lightness() < 255);
        for (int x = 0; x < 3; ++x) {
            QCOMPARE(swapped.pixelColor(x, 0).hsvHue(), 0);
            QCOMPARE(swapped.pixelColor(x, 0).alpha(), image.pixelColor(x, 0).alpha());
        }
        for (int x = 3; x < 6; ++x)
            QCOMPARE(swapped.pixelColor(x, 0), image.pixelColor(x, 0));
        QCOMPARE(image.pixelColor(1, 0), source);
    }

    void brightTargetRetainsHighlights()
    {
        QImage image(3, 1, QImage::Format_ARGB32);
        image.setPixelColor(0, 0, QColor(0, 0, 80));
        image.setPixelColor(1, 0, QColor(0, 0, 160));
        image.setPixelColor(2, 0, QColor(0, 0, 240));
        const auto swapped = SkinPalette::swapFamily(image, QRegion(image.rect()), QColor(0, 0, 160), QColor(Qt::red));
        QCOMPARE(swapped.pixelColor(1, 0), QColor(Qt::red));
        QVERIFY(swapped.pixelColor(0, 0).lightness() < swapped.pixelColor(1, 0).lightness());
        QVERIFY(swapped.pixelColor(2, 0).lightness() > swapped.pixelColor(1, 0).lightness());
        QVERIFY(swapped.pixelColor(2, 0) != QColor(Qt::red));
    }

    void clipsDisjointRegionsAndLeavesOutsideUntouched()
    {
        const auto image = texture();
        const auto region = QRegion(QRect(-10, -10, 11, 11)) + QRegion(QRect(4, 5, 1, 1)) + QRegion(QRect(70, 70, 5, 5));
        int changed = 0;
        const auto swapped = SkinPalette::swapFamily(image, region, Qt::blue, Qt::red, 24, &changed);
        QCOMPARE(changed, 2);
        QCOMPARE(swapped.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(swapped.pixelColor(4, 5), QColor(Qt::red));
        QCOMPARE(swapped.pixelColor(1, 0), QColor(Qt::blue));
        QCOMPARE(swapped.pixelColor(4, 6), QColor(Qt::blue));
    }

    void neutralTargetsAndSourcesKeepShadeOrdering()
    {
        QImage grays(3, 1, QImage::Format_ARGB32);
        grays.setPixelColor(0, 0, QColor(30, 30, 30, 50));
        grays.setPixelColor(1, 0, QColor(120, 120, 120, 100));
        grays.setPixelColor(2, 0, QColor(230, 230, 230, 150));
        const auto region = QRegion(grays.rect());
        const auto colored = SkinPalette::swapFamily(grays, region, QColor(120, 120, 120), QColor(0, 170, 0));
        const auto grayAgain = SkinPalette::swapFamily(colored, region, QColor(0, 170, 0), QColor(120, 120, 120));
        for (const auto& image : { colored, grayAgain }) {
            QVERIFY(image.pixelColor(0, 0).lightness() < image.pixelColor(1, 0).lightness());
            QVERIFY(image.pixelColor(1, 0).lightness() < image.pixelColor(2, 0).lightness());
            for (int x = 0; x < 3; ++x)
                QCOMPARE(image.pixelColor(x, 0).alpha(), grays.pixelColor(x, 0).alpha());
        }
        for (int x = 0; x < 3; ++x) {
            QCOMPARE(colored.pixelColor(x, 0).hsvHue(), 120);
            QCOMPARE(grayAgain.pixelColor(x, 0).hsvSaturation(), 0);
        }
    }

    void emptyInvalidAndUnchangedInputsAreNoOps()
    {
        QVERIFY(SkinPalette::extractFamilies({}, QRegion(QRect(0, 0, 64, 64))).isEmpty());
        QVERIFY(SkinPalette::extractFamilies(texture(Qt::transparent), QRegion(QRect(0, 0, 64, 64))).isEmpty());
        QVERIFY(SkinPalette::extractFamilies(texture(), {}).isEmpty());
        const auto image = texture();
        int changed = -1;
        QCOMPARE(SkinPalette::swapFamily(image, {}, Qt::blue, Qt::red, 24, &changed), image);
        QCOMPARE(changed, 0);
        QCOMPARE(SkinPalette::swapFamily(image, QRegion(image.rect()), {}, Qt::red), image);
        QCOMPARE(SkinPalette::swapFamily(image, QRegion(image.rect()), Qt::blue, {}), image);
        QCOMPARE(SkinPalette::swapFamily(image, QRegion(image.rect()), Qt::blue, Qt::blue), image);
        QVERIFY(SkinPalette::swapFamily({}, {}, Qt::blue, Qt::red).isNull());
    }

    void panelPreviewsWithoutMutationAndAppliesOneUndoableEdit()
    {
        SkinTextureDocument document;
        auto image = texture(QColor(0, 0, 180));
        image.setPixelColor(40, 8, QColor(0, 0, 180, 110));
        QVERIFY(document.load(image));
        const auto before = document.image();
        SkinPalettePanel panel(&document);
        panel.setAllowedRegion(QRegion(QRect(40, 8, 3, 1)));
        document.setSelection(QRegion(QRect(40, 8, 2, 2)));
        auto* target = panel.findChild<QLineEdit*>("skinPaletteTarget");
        auto* apply = panel.findChild<QPushButton*>("skinPaletteApply");
        auto* reset = panel.findChild<QPushButton*>("skinPaletteReset");
        QVERIFY(target && apply && reset);
        QVERIFY(!apply->isEnabled());
        target->setText("#CC0000");
        QVERIFY(apply->isEnabled());
        QCOMPARE(document.image(), before);
        QVERIFY(panel.previewImage() != before);
        QVERIFY(!document.isDirty());
        QVERIFY(!document.canUndo());
        reset->click();
        QVERIFY(!apply->isEnabled());
        QCOMPARE(document.image(), before);
        target->setText("#CC0000");
        target->setText("#00AA00");
        QCOMPARE(document.image(), before);
        apply->click();
        QCOMPARE(document.image().pixelColor(40, 8), QColor(0, 170, 0, 110));
        QCOMPARE(document.image().pixelColor(41, 8), QColor(0, 170, 0));
        QCOMPARE(document.image().pixelColor(42, 8), before.pixelColor(42, 8));
        QCOMPARE(document.image().pixelColor(40, 9), before.pixelColor(40, 9));
        QCOMPARE(document.image().pixelColor(8, 8), before.pixelColor(8, 8));
        QVERIFY(!apply->isEnabled());
        const auto after = document.image();
        document.undo();
        QCOMPARE(document.image(), before);
        QVERIFY(!document.canUndo());
        document.redo();
        QCOMPARE(document.image(), after);
    }

    void scopedPaletteSeparatesBodyPartsAndLayers()
    {
        using D = SkinTextureDocument;
        auto image = texture();
        const auto headBase = D::uvRegion(D::Head, D::Base, SkinModel::CLASSIC);
        const auto headOuter = D::uvRegion(D::Head, D::Overlay, SkinModel::CLASSIC);
        for (const auto& rect : headBase)
            for (int y = rect.top(); y <= rect.bottom(); ++y)
                for (int x = rect.left(); x <= rect.right(); ++x)
                    image.setPixelColor(x, y, QColor(180, 0, 0));
        for (const auto& rect : headOuter)
            for (int y = rect.top(); y <= rect.bottom(); ++y)
                for (int x = rect.left(); x <= rect.right(); ++x)
                    image.setPixelColor(x, y, QColor(0, 180, 0, 90));
        D document;
        QVERIFY(document.load(image));
        const auto before = document.image();
        SkinPalettePanel panel(&document);
        auto* part = panel.findChild<QComboBox*>("skinPalettePart");
        auto* layer = panel.findChild<QComboBox*>("skinPaletteLayer");
        auto* source = panel.findChild<QComboBox*>("skinPaletteSource");
        auto* target = panel.findChild<QLineEdit*>("skinPaletteTarget");
        auto* apply = panel.findChild<QPushButton*>("skinPaletteApply");
        QSignalSpy previews(&panel, &SkinPalettePanel::previewChanged);
        part->setCurrentIndex(part->findData(int(D::Head)));
        layer->setCurrentIndex(layer->findData(int(D::Base)));
        QCOMPARE(source->count(), 1);
        QCOMPARE(source->currentData().value<QColor>(), QColor(180, 0, 0));
        target->setText("#00B400");
        QVERIFY(!previews.isEmpty());
        QCOMPARE(qvariant_cast<QImage>(previews.last().at(0)), panel.previewImage());
        QCOMPARE(panel.previewImage().pixelColor(8, 8), QColor(0, 180, 0));
        QCOMPARE(panel.previewImage().pixelColor(40, 8), before.pixelColor(40, 8));
        QCOMPARE(panel.previewImage().pixelColor(20, 20), before.pixelColor(20, 20));
        QCOMPARE(document.image(), before);
        QVERIFY(!document.canUndo());
        layer->setCurrentIndex(layer->findData(int(D::Overlay)));
        QCOMPARE(panel.previewImage(), before);
        QCOMPARE(source->count(), 1);
        QCOMPARE(source->currentData().value<QColor>(), QColor(0, 180, 0));
        target->setText("#B40000");
        QCOMPARE(panel.previewImage().pixelColor(40, 8), QColor(180, 0, 0, 90));
        QCOMPARE(panel.previewImage().pixelColor(8, 8), before.pixelColor(8, 8));
        apply->click();
        QCOMPARE(document.image().pixelColor(40, 8), QColor(180, 0, 0, 90));
        QCOMPARE(document.image().pixelColor(8, 8), before.pixelColor(8, 8));
        QCOMPARE(document.image().pixelColor(20, 36), before.pixelColor(20, 36));
        document.undo();
        QCOMPARE(document.image(), before);
        QCOMPARE(panel.previewImage(), before);
        QVERIFY(!document.canUndo());
    }

    void groupedLimbsIntersectVisibilitySelectionAndModel()
    {
        using D = SkinTextureDocument;
        D document;
        QVERIFY(document.load(texture()));
        SkinPalettePanel panel(&document);
        auto* part = panel.findChild<QComboBox*>("skinPalettePart");
        auto* layer = panel.findChild<QComboBox*>("skinPaletteLayer");
        auto* source = panel.findChild<QComboBox*>("skinPaletteSource");
        auto* target = panel.findChild<QLineEdit*>("skinPaletteTarget");
        part->setCurrentIndex(part->findData(-2));  // Both arms.
        layer->setCurrentIndex(layer->findData(int(D::Overlay)));
        target->setText("#FF0000");
        QCOMPARE(panel.previewImage().pixelColor(44, 36), QColor(Qt::red));
        QCOMPARE(panel.previewImage().pixelColor(52, 52), QColor(Qt::red));
        QCOMPARE(panel.previewImage().pixelColor(4, 36), QColor(Qt::blue));
        QCOMPARE(panel.previewImage().pixelColor(20, 36), QColor(Qt::blue));
        panel.setAllowedRegion(D::uvRegion(D::RightArm, D::Both, document.model()));
        target->setText("#FF0000");
        QCOMPARE(panel.previewImage().pixelColor(44, 36), QColor(Qt::red));
        QCOMPARE(panel.previewImage().pixelColor(52, 52), QColor(Qt::blue));
        document.setSelection(D::uvRegion(D::LeftArm, D::Overlay, document.model()));
        QCOMPARE(source->count(), 0);
        QVERIFY(!panel.findChild<QPushButton*>("skinPaletteApply")->isEnabled());
        QCOMPARE(panel.previewImage(), document.image());
        document.clearSelection();
        panel.setAllowedRegion(QRegion(QRect(0, 0, 64, 64)));
        part->setCurrentIndex(part->findData(-3));  // Both legs.
        target->setText("#FF0000");
        QCOMPARE(panel.previewImage().pixelColor(4, 36), QColor(Qt::red));
        QCOMPARE(panel.previewImage().pixelColor(4, 52), QColor(Qt::red));
        QCOMPARE(panel.previewImage().pixelColor(44, 36), QColor(Qt::blue));
        QVERIFY(!document.isDirty());
        document.setModel(SkinModel::SLIM);
        part->setCurrentIndex(part->findData(int(D::RightArm)));
        target->setText("#FF0000");
        QCOMPARE(panel.previewImage().pixelColor(53, 36), QColor(Qt::red));
        QCOMPARE(panel.previewImage().pixelColor(55, 36), QColor(Qt::blue));
        QCOMPARE(document.image(), texture());
    }

    void floatingPickerUpdatesPreviewAndDismissesWithoutDialog()
    {
        SkinTextureDocument document;
        QVERIFY(document.load(texture()));
        const auto before = document.image();
        SkinPalettePanel panel(&document);
        panel.resize(188, 480);
        panel.show();
        QVERIFY(panel.minimumSizeHint().width() <= 188);
        auto* choose = panel.findChild<QPushButton*>("skinPaletteChooseColor");
        auto* popup = panel.findChild<QMenu*>("skinPaletteColorPopup");
        auto* wheel = panel.findChild<SkinColorWheel*>("skinPaletteColorWheel");
        auto* popupHex = panel.findChild<QLineEdit*>("skinPalettePopupHex");
        QVERIFY(choose && popup && wheel && popupHex);
        QVERIFY(panel.findChildren<QDialog*>().isEmpty());
        choose->click();
        QTRY_VERIFY(popup->isVisible());
        QCOMPARE(popup->windowType(), Qt::Popup);
        popupHex->setFocus();
        popupHex->selectAll();
        QTest::keyClicks(popupHex, "#AA0000");
        QCOMPARE(panel.findChild<QLineEdit*>("skinPaletteTarget")->text(), QString("#AA0000"));
        QCOMPARE(panel.previewImage().pixelColor(40, 8), QColor(170, 0, 0));
        QCOMPARE(document.image(), before);
        QVERIFY(!document.isDirty());
        QVERIFY(popup->isVisible());
        QTest::keyClick(popupHex, Qt::Key_Escape);
        QTRY_VERIFY(!popup->isVisible());
        QCOMPARE(document.image(), before);
        choose->click();
        QTRY_VERIFY(popup->isVisible());
        wheel->setFocus();
        QTest::keyClick(wheel, Qt::Key_Escape);
        QTRY_VERIFY(!popup->isVisible());
        panel.findChild<QPushButton*>("skinPaletteReset")->click();
        QCOMPARE(panel.previewImage(), before);
        QVERIFY(!document.canUndo());
    }
    void panelHandlesLoadSelectionAndInvalidTargets()
    {
        SkinTextureDocument document;
        SkinPalettePanel panel(&document);
        auto* source = panel.findChild<QComboBox*>("skinPaletteSource");
        auto* target = panel.findChild<QLineEdit*>("skinPaletteTarget");
        auto* apply = panel.findChild<QPushButton*>("skinPaletteApply");
        QVERIFY(!source->isEnabled());
        QVERIFY(document.load(texture()));
        QVERIFY(source->isEnabled());
        target->setText("broken");
        QVERIFY(!apply->isEnabled());
        target->setText("#FF0000");
        QVERIFY(apply->isEnabled());
        panel.setAllowedRegion({});
        QVERIFY(!apply->isEnabled());
        QVERIFY(!source->isEnabled());
        QVERIFY(!document.isDirty());
        panel.setAllowedRegion(QRegion(QRect(40, 8, 1, 1)));
        QVERIFY(source->isEnabled());
        document.setSelection(QRegion(QRect(8, 8, 1, 1)));
        QVERIFY(!source->isEnabled());
        QVERIFY(!apply->isEnabled());
    }
};

QTEST_MAIN(SkinPaletteTest)
#include "SkinPalette_test.moc"
