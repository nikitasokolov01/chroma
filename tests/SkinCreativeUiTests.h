// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAction>
#include <QFileInfo>
#include <QListView>
#include <QMenu>
#include <QWheelEvent>

#include "SkinLibraryUiTests.h"
#include "minecraft/skins/SkinOutfitLibrary.h"
#include "ui/dialogs/skins/SkinPalettePanel.h"
#include "SkinMirrorViewTests.h"

namespace SkinCreativeUiTests {

inline void outfitsPersistAndCheckCapeOwnership(MainWindow* window, const QString& root)
{
    QVERIFY(window->inlineWorkspace()->closeAllPages());
    const auto accounts = APPLICATION->accounts();
    QCOMPARE(accounts->count(), 0);
    const auto oldDirectory = APPLICATION->settings()->get("SkinsDir");
    const auto directory = QDir(root).filePath("outfit-source-skins");
    QVERIFY(QDir().mkpath(directory));
    APPLICATION->settings()->set("SkinsDir", directory);
    SkinOutfitLibrary library(QDir(APPLICATION->dataRoot()).filePath("skin-outfits"));
    QStringList added;
    auto cleanup = qScopeGuard([&] {
        window->inlineWorkspace()->closeAllPages();
        while (accounts->count())
            accounts->removeAccount(accounts->index(0, 0));
        APPLICATION->settings()->set("SkinsDir", oldDirectory);
        for (const auto& id : added)
            library.remove(id);
    });
    const auto source = SkinLibraryUiTests::patternedSkin();
    QVERIFY(source.save(QDir(directory).filePath("Local.png")));
    QImage capeImage(64, 32, QImage::Format_ARGB32);
    capeImage.fill(QColor("#704bc1"));
    SkinOutfitLibrary::Entry entry{ {}, "Winter outfit", source, SkinModel::SLIM, QString("outfit-cape"), capeImage };
    const auto id = library.save(entry);
    QVERIFY(!id.isEmpty());
    added.append(id);
    QString copiedId;
    {
        SkinManageDialog manager(window, MinecraftAccountPtr());
        window->resize(1280, 820);
        window->openInlinePage(&manager, "Skin Library");
        auto* outfits = manager.findChild<QComboBox*>("skinOutfitCombo");
        auto* save = manager.findChild<QPushButton*>("skinOutfitSave");
        auto* apply = manager.findChild<QPushButton*>("skinOutfitApply");
        QVERIFY(outfits && save && apply);
        QVERIFY(outfits->findData(id) > 0);
        outfits->setCurrentIndex(outfits->findData(id));
        QVERIFY(manager.getSelectedSkin());
        QCOMPARE(manager.getSelectedSkin()->getModel(), SkinModel::SLIM);
        QCOMPARE(manager.getSelectedSkin()->getTexture(), source);
        QCOMPARE(manager.getSelectedSkin()->getCapeId(), QString("outfit-cape"));
        QVERIFY(!apply->isEnabled());
        QVERIFY(save->isEnabled());
        auto* deleteSkin = manager.findChild<QAction*>("action_Delete_Skin");
        auto* renameSkin = manager.findChild<QAction*>("action_Rename_Skin");
        QVERIFY(deleteSkin && renameSkin);
        QVERIFY(!deleteSkin->isEnabled() && !renameSkin->isEnabled());
        manager.on_action_Delete_Skin_triggered(false);
        manager.on_action_Rename_Skin_triggered(false);
        QVERIFY(QFileInfo::exists(QDir(directory).filePath("Local.png")));
        bool answered = false;
        QTimer::singleShot(40, window, [&] {
            auto* dialog = qobject_cast<QDialog*>(window->inlineWorkspace()->currentPage());
            if (!dialog)
                return;
            auto* name = dialog->findChild<QLineEdit*>("skinOutfitName");
            auto* include = dialog->findChild<QCheckBox*>("skinOutfitIncludeCape");
            if (!name || !include) {
                dialog->reject();
                return;
            }
            name->setText("Night outfit");
            include->setChecked(true);
            answered = true;
            dialog->accept();
        });
        save->click();
        QVERIFY(answered);
        copiedId = outfits->currentData().toString();
        QVERIFY(!copiedId.isEmpty() && copiedId != id);
        added.append(copiedId);
        const auto saved = library.load(copiedId);
        QVERIFY(saved);
        QCOMPARE(saved->name, QString("Night outfit"));
        QCOMPARE(saved->image, source);
        QCOMPARE(saved->model, SkinModel::SLIM);
        QCOMPARE(saved->capeId, entry.capeId);
        QCOMPARE(saved->capeImage, capeImage);
        QCOMPARE(QImage(QDir(directory).filePath("Local.png")), source);
        // A preset removed while the list is open must restore the actual
        // library preview and selection before re-enabling library actions.
        outfits->setCurrentIndex(0);
        QVERIFY(library.remove(id));
        outfits->setCurrentIndex(outfits->findData(id));
        QCOMPARE(outfits->currentIndex(), 0);
        QVERIFY(manager.getSelectedSkin());
        QCOMPARE(manager.getSelectedSkin()->getModel(), SkinModel::CLASSIC);
        auto* skinList = manager.findChild<QListView*>("listView");
        QVERIFY(skinList && skinList->currentIndex().isValid());
        manager.reject();
    }
    {
        SkinManageDialog manager(window, MinecraftAccountPtr());
        window->openInlinePage(&manager, "Skin Library");
        auto* outfits = manager.findChild<QComboBox*>("skinOutfitCombo");
        auto* apply = manager.findChild<QPushButton*>("skinOutfitApply");
        auto* status = manager.findChild<QLabel*>("skinOutfitStatus");
        QVERIFY(outfits && apply && status);
        QVERIFY(outfits->findData(copiedId) > 0);
        outfits->setCurrentIndex(outfits->findData(copiedId));
        QCOMPARE(manager.getSelectedSkin()->getTexture(), source);
        const auto account = MinecraftAccount::createOffline("OutfitFixture");
        account->accountData()->type = AccountType::MSA;
        accounts->addAccount(account);
        outfits->setCurrentIndex(outfits->findData(copiedId));
        QVERIFY(!apply->isEnabled());
        QVERIFY(status->text().contains("does not own"));
        Cape cape;
        cape.id = "outfit-cape";
        cape.alias = "Outfit cape";
        QBuffer buffer(&cape.data);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(capeImage.save(&buffer, "PNG"));
        account->accountData()->minecraftProfile.capes.insert(cape.id, cape);
        emit account->changed();
        outfits->setCurrentIndex(outfits->findData(copiedId));
        QTRY_VERIFY(apply->isEnabled());
        // Eligibility only: never start a real account upload from the test.
        for (const QSize size : { QSize(1280, 820), QSize(680, 640) }) {
            window->resize(size);
            QTest::qWait(80);
            QCOMPARE(window->size(), size);
            for (auto* scroll : window->inlineWorkspace()->findChildren<QScrollArea*>())
                if (scroll->isVisible())
                    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            QVERIFY(window->grab().save(QDir(root).filePath(QString("skin-outfits-%1.png").arg(size.width()))));
        }
        manager.reject();
    }
}

inline void mirrorBrushUsesEditorControls(MainWindow* window, const QString& root)
{
    using D = SkinTextureDocument;
    QVERIFY(window->inlineWorkspace()->closeAllPages());
    window->resize(1280, 820);
    SkinEditorDialog editor(window, MinecraftAccountPtr(), SkinModel(SkinLibraryUiTests::patternedSkin()));
    window->openInlinePage(&editor, "Skin Studio");
    auto* document = editor.findChild<D*>("skinEditingDocument");
    auto* canvas = editor.findChild<SkinCanvas*>("skinCanvas");
    auto* mode = editor.findChild<QComboBox*>("skinEditMode");
    auto* outer = editor.findChild<QToolButton*>("skinShowOuter");
    auto* mirror = editor.findChild<QCheckBox*>("skinMirrorHeadBody");
    auto* arms = editor.findChild<QCheckBox*>("skinMirrorArms");
    auto* legs = editor.findChild<QCheckBox*>("skinMirrorLegs");
    QVERIFY(document && canvas && mode && outer && mirror && arms && legs);
    auto clean = qScopeGuard([&] { document->markSaved(); });
    QVERIFY(!mirror->isChecked() && !arms->isChecked() && !legs->isChecked());
    mode->setCurrentIndex(1);
    outer->setChecked(false);
    mirror->setChecked(true);
    QVERIFY(document->mirrorOptions().headAndBody);
    QVERIFY(!document->mirrorOptions().arms && !document->mirrorOptions().legs);
    const auto before = document->image();
    canvas->setFocus();
    QTest::keyClick(canvas, Qt::Key_Space);
    QCOMPARE(document->image().pixelColor(8, 8), QColor(Qt::white));
    QCOMPARE(document->image().pixelColor(15, 8), QColor(Qt::white));
    document->undo();
    QCOMPARE(document->image(), before);
    QVERIFY(!document->canUndo());
    document->setSelection(QRect(8, 8, 1, 1));
    QTest::keyClick(canvas, Qt::Key_Space);
    QCOMPARE(document->image().pixelColor(8, 8), QColor(Qt::white));
    QCOMPARE(document->image().pixelColor(15, 8), before.pixelColor(15, 8));
    document->undo();
    document->clearSelection();
    // Capture the complete mirror controls and a real mirrored stroke for the
    // release guide before checking the compact layout below.
    QTest::keyClick(canvas, Qt::Key_Space);
    auto* layers = editor.findChild<QScrollArea*>("skinVisibilityScroll");
    if (layers)
        layers->ensureWidgetVisible(mirror);
    QTest::qWait(80);
    QVERIFY(window->grab().save(QDir(root).filePath("skin-mirror-1280.png")));
    document->undo();
    window->resize(680, 640);
    QTest::qWait(80);
    QCOMPARE(window->size(), QSize(680, 640));
    if (layers)
        layers->ensureWidgetVisible(mirror);
    for (auto* scroll : editor.findChildren<QScrollArea*>())
        QVERIFY2(!scroll->isVisible() || scroll->horizontalScrollBar()->maximum() == 0,
                 qPrintable(SkinLibraryUiTests::horizontalScrollDiagnostic(scroll, &editor, window)));
    QVERIFY(window->grab().save(QDir(root).filePath("skin-mirror-compact.png")));
    editor.reject();
}

inline void paletteSwapUsesVisibleLayers(MainWindow* window, const QString& root)
{
    using D = SkinTextureDocument;
    QVERIFY(window->inlineWorkspace()->closeAllPages());
    window->resize(1280, 820);
    QImage source(64, 64, QImage::Format_ARGB32);
    source.fill(QColor("#2255aa"));
    SkinEditorDialog editor(window, MinecraftAccountPtr(), SkinModel(source));
    window->openInlinePage(&editor, "Skin Studio");
    auto* document = editor.findChild<D*>("skinEditingDocument");
    auto* tabs = editor.findChild<QTabWidget*>("skinInspectorTabs");
    auto* outer = editor.findChild<QToolButton*>("skinShowOuter");
    auto* family = editor.findChild<QComboBox*>("skinPaletteSource");
    auto* target = editor.findChild<QLineEdit*>("skinPaletteTarget");
    auto* apply = editor.findChild<QPushButton*>("skinPaletteApply");
    auto* scroll = editor.findChild<QScrollArea*>("skinPaletteScroll");
    QVERIFY(document && tabs && outer && family && target && apply && scroll);
    auto clean = qScopeGuard([&] { document->markSaved(); });
    tabs->setCurrentWidget(scroll);
    QVERIFY(!editor.findChild<SkinColorWheel*>()->isVisible());
    outer->setChecked(false);
    for (int i = 1; i < 6; ++i)
        editor.findChild<QToolButton*>(QString("skinPart%1").arg(i))->setChecked(false);
    QVERIFY(family->count() > 0);
    family->setCurrentIndex(0);
    const auto before = document->image();
    target->setText("#aa3322");
    QTest::qWait(40);
    QCOMPARE(document->image(), before);
    QVERIFY(apply->isEnabled());
    scroll->ensureWidgetVisible(apply);
    apply->click();
    const auto after = document->image();
    QVERIFY(after.pixelColor(8, 8) != before.pixelColor(8, 8));
    const auto changedRegion = D::uvRegion(D::Head, D::Base, document->model());
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (!changedRegion.contains(QPoint(x, y)))
                QCOMPARE(after.pixel(x, y), before.pixel(x, y));
    document->undo();
    QCOMPARE(document->image(), before);
    QVERIFY(!document->canUndo());
    // Show a complete synthetic skin for the layout captures.
    for (int i = 1; i < 6; ++i)
        editor.findChild<QToolButton*>(QString("skinPart%1").arg(i))->setChecked(true);
    outer->setChecked(true);
    document->load(SkinLibraryUiTests::patternedSkin());
    for (int index = 0; index < family->count(); ++index)
        if (family->itemData(index).value<QColor>() == QColor("#86bdb5"))
            family->setCurrentIndex(index);
    target->setText("#33bb77");
    window->resize(680, 640);
    QTest::qWait(80);
    QCOMPARE(window->size(), QSize(680, 640));
    scroll->ensureWidgetVisible(apply);
    QVERIFY2(scroll->horizontalScrollBar()->maximum() == 0,
             qPrintable(SkinLibraryUiTests::horizontalScrollDiagnostic(scroll, &editor, window)));
    QVERIFY(window->grab().save(QDir(root).filePath("skin-palette-compact.png")));
    window->resize(1280, 820);
    QTest::qWait(80);
    scroll->verticalScrollBar()->setValue(0);
    QTest::qWait(40);
    QVERIFY(scroll->viewport()->rect().contains(QRect(apply->mapTo(scroll->viewport(), QPoint()), apply->size())));
    QVERIFY(window->grab().save(QDir(root).filePath("skin-palette-preview.png")));
    editor.reject();
}

inline void paletteStudioScopesAndPopover(MainWindow* window, const QString& root)
{
    using D = SkinTextureDocument;
    QVERIFY(window->inlineWorkspace()->closeAllPages());
    window->resize(1280, 820);
    SkinEditorDialog editor(window, MinecraftAccountPtr(), SkinModel(SkinLibraryUiTests::patternedSkin()));
    window->openInlinePage(&editor, "Skin Studio");
    auto* document = editor.findChild<D*>("skinEditingDocument");
    auto* tabs = editor.findChild<QTabWidget*>("skinInspectorTabs");
    auto* mode = editor.findChild<QComboBox*>("skinEditMode");
    auto* panel = editor.findChild<SkinPalettePanel*>();
    auto* part = editor.findChild<QComboBox*>("skinPalettePart");
    auto* layer = editor.findChild<QComboBox*>("skinPaletteLayer");
    auto* target = editor.findChild<QLineEdit*>("skinPaletteTarget");
    auto* apply = editor.findChild<QPushButton*>("skinPaletteApply");
    auto* choose = editor.findChild<QPushButton*>("skinPaletteChooseColor");
    auto* comparison = editor.findChild<QWidget*>("skinPaletteComparison");
    QVERIFY(document && tabs && mode && panel && part && layer && target && apply && choose && comparison);
    auto clean = qScopeGuard([&] { document->markSaved(); });
    mode->setCurrentIndex(1);
    tabs->setCurrentWidget(editor.findChild<QScrollArea*>("skinPaletteScroll"));
    QVERIFY(comparison->isVisible());
    QVERIFY(!editor.findChild<QWidget*>("skinToolbox")->isVisible());
    QVERIFY(!editor.findChild<SkinCanvas*>("skinCanvas")->isVisible());
    part->setCurrentIndex(part->findData(D::Body));
    layer->setCurrentIndex(layer->findData(D::Base));
    const auto before = document->image();
    target->setText("#ce7040");
    QVERIFY(panel->previewImage() != before);
    QCOMPARE(document->image(), before);
    const auto region = D::uvRegion(D::Body, D::Base, document->model());
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (!region.contains(QPoint(x, y)))
                QCOMPARE(panel->previewImage().pixel(x, y), before.pixel(x, y));

    const auto pages = window->inlineWorkspace()->pageCount();
    choose->click();
    auto* popup = editor.findChild<QMenu*>("skinPaletteColorPopup");
    QVERIFY(popup);
    QTRY_VERIFY(popup->isVisible());
    QCOMPARE(window->inlineWorkspace()->pageCount(), pages);
    QCOMPARE(window->inlineWorkspace()->currentPage(), &editor);
    auto* popupHex = popup->findChild<QLineEdit*>("skinPalettePopupHex");
    QVERIFY(popupHex);
    popupHex->setFocus();
    popupHex->selectAll();
    QTest::keyClicks(popupHex, "#3388cc");
    QTest::keyClick(popupHex, Qt::Key_Tab);
    QTRY_COMPARE(target->text(), QString("#3388cc"));
    QCOMPARE(document->image(), before);
    QTest::qWait(100);
    QVERIFY(popup->grab().save(QDir(root).filePath("skin-palette-color-popover.png")));
    QTest::keyClick(popup, Qt::Key_Escape);
    QTRY_VERIFY(!popup->isVisible());
    QCOMPARE(window->inlineWorkspace()->pageCount(), pages);

    auto* current = editor.findChild<SkinOpenGLWindow*>("paletteCurrentView");
    auto* proposed = editor.findChild<SkinOpenGLWindow*>("palettePreviewView");
    if (current && proposed) {
        QTRY_VERIFY(current->isValid() && proposed->isValid());
        QVERIFY(current->isVisible() && proposed->isVisible());
        QVERIFY(current->height() > 260 && proposed->height() > 260);
        const auto oldCamera = current->cameraState();
        const auto center = current->rect().center();
        QTest::mousePress(current, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(current, center + QPoint(30, 12), 20);
        QTest::mouseRelease(current, Qt::LeftButton, Qt::NoModifier, center + QPoint(30, 12));
        QVERIFY(current->cameraState().yaw != oldCamera.yaw);
        QCOMPARE(proposed->cameraState().yaw, current->cameraState().yaw);
        QCOMPARE(proposed->cameraState().pitch, current->cameraState().pitch);
        QWheelEvent wheel(QPointF(center), QPointF(proposed->mapToGlobal(center)), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(proposed, &wheel);
        QVERIFY(proposed->cameraState().distance != oldCamera.distance);
        QCOMPARE(current->cameraState().distance, proposed->cameraState().distance);
        QCOMPARE(document->image(), before);
        editor.findChild<QPushButton*>("paletteCompareReset")->click();
        QCOMPARE(current->cameraState().yaw, proposed->cameraState().yaw);
        QCOMPARE(current->cameraState().distance, proposed->cameraState().distance);
    } else {
        QVERIFY(editor.findChild<QWidget*>("paletteCurrentFallback")->isVisible());
        QVERIFY(editor.findChild<QWidget*>("palettePreviewFallback")->isVisible());
    }
    const auto expected = panel->previewImage();
    apply->click();
    QCOMPARE(document->image(), expected);
    document->undo();
    QCOMPARE(document->image(), before);
    QVERIFY(!document->canUndo());
    tabs->setCurrentIndex(0);
    QCOMPARE(mode->currentIndex(), 1);
    QVERIFY(editor.findChild<SkinCanvas*>("skinCanvas")->isVisible());
    QVERIFY(editor.findChild<QWidget*>("skinToolbox")->isVisible());
    QVERIFY(!comparison->isVisible());
    editor.reject();
}

inline void textureBrushUsesExistingColors(MainWindow* window, const QString& root)
{
    using D = SkinTextureDocument;
    QVERIFY(window->inlineWorkspace()->closeAllPages());
    window->resize(1280, 820);
    SkinEditorDialog editor(window, MinecraftAccountPtr(), SkinModel(SkinLibraryUiTests::patternedSkin()));
    window->openInlinePage(&editor, "Skin Studio");
    auto* document = editor.findChild<D*>("skinEditingDocument");
    auto* canvas = editor.findChild<SkinCanvas*>("skinCanvas");
    auto* mode = editor.findChild<QComboBox*>("skinEditMode");
    auto* outer = editor.findChild<QToolButton*>("skinShowOuter");
    auto* texture = editor.findChild<QToolButton*>("skinToolTexture");
    auto* style = editor.findChild<QComboBox*>("skinTextureStyle");
    auto* strength = editor.findChild<QSpinBox*>("skinTextureStrength");
    auto* size = editor.findChild<QSpinBox*>("skinBrushSize");
    QVERIFY(document && canvas && mode && outer && texture && style && strength && size);
    auto clean = qScopeGuard([&] { document->markSaved(); });
    mode->setCurrentIndex(1);
    outer->setChecked(false);
    texture->click();
    QVERIFY(style->isVisible() && strength->isVisible());
    size->setValue(4);
    strength->setValue(35);
    const auto before = document->image();
    QList<QImage> results;
    for (int pattern = 0; pattern < 3; ++pattern) {
        style->setCurrentIndex(pattern);
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Space);
        QVERIFY(document->image() != before);
        results.append(document->image());
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                QCOMPARE(document->image().pixelColor(x, y).alpha(), before.pixelColor(x, y).alpha());
        document->undo();
        QCOMPARE(document->image(), before);
        QVERIFY(!document->canUndo());
    }
    QVERIFY(results[0] != results[1] && results[1] != results[2] && results[0] != results[2]);
    style->setCurrentIndex(D::Fabric);
    auto* preview = editor.findChild<SkinOpenGLWindow*>("skin3DCanvas");
    if (preview && mode->isEnabled()) {
        mode->setCurrentIndex(0);
        QTRY_VERIFY(preview->isValid());
        const auto point = SkinMirrorViewTests::frontPixelPoint(*preview, D::Body, QPoint(23, 25));
        QVERIFY(point.x() >= 0);
        QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(document->image() != before);
        const auto body = D::uvRegion(D::Body, D::Base, document->model());
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (!body.contains(QPoint(x, y)))
                    QCOMPARE(document->image().pixel(x, y), before.pixel(x, y));
        document->undo();
        QCOMPARE(document->image(), before);
        QVERIFY(!document->canUndo());
    }
    for (const QSize dimensions : { QSize(1280, 820), QSize(680, 640) }) {
        window->resize(dimensions);
        QTest::qWait(80);
        QCOMPARE(window->size(), dimensions);
        for (const auto* name : { "skinImport", "skinExport", "skinUndo", "skinRedo" }) {
            auto* button = editor.findChild<QPushButton*>(name);
            QVERIFY(button && button->isVisible());
            QVERIFY2(button->height() >= 28, qPrintable(QString("%1 collapsed to %2px").arg(name).arg(button->height())));
        }
        QVERIFY(window->rect().contains(QRect(style->mapTo(window, QPoint()), style->size())));
        QVERIFY(window->rect().contains(QRect(strength->mapTo(window, QPoint()), strength->size())));
        for (auto* scroll : editor.findChildren<QScrollArea*>())
            QVERIFY(!scroll->isVisible() || scroll->horizontalScrollBar()->maximum() == 0);
        QVERIFY(window->grab().save(QDir(root).filePath(QString("skin-texture-%1.png").arg(dimensions.width()))));
    }
    editor.findChild<QToolButton*>("skinToolBrush")->click();
    QVERIFY(!style->isVisible());
    editor.reject();
}

}  // namespace SkinCreativeUiTests
