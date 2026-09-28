// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QBuffer>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QToolButton>

#include "Application.h"
#include "minecraft/auth/AccountList.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/skins/SkinCanvas.h"
#include "ui/dialogs/skins/SkinEditorDialog.h"
#include "ui/dialogs/skins/SkinManageDialog.h"
#include "ui/widgets/InlineWorkspace.h"

namespace SkinLibraryUiTests {

inline void accountSwitching(MainWindow* window, const QString& root)
{
    QVERIFY(window->inlineWorkspace()->closeAllPages());
    const auto accounts = APPLICATION->accounts();
    QCOMPARE(accounts->count(), 0);
    const auto oldDirectory = APPLICATION->settings()->get("SkinsDir");
    const auto library = QDir(root).filePath("skin-account-switching");
    QVERIFY(QDir().mkpath(library));
    APPLICATION->settings()->set("SkinsDir", library);
    auto cleanup = qScopeGuard([&] {
        window->inlineWorkspace()->closeAllPages();
        while (accounts->count())
            accounts->removeAccount(accounts->index(0, 0));
        APPLICATION->settings()->set("SkinsDir", oldDirectory);
    });
    QImage texture(64, 64, QImage::Format_ARGB32);
    texture.fill(QColor("#86bdb5"));
    QVERIFY(texture.save(QDir(library).filePath("Local.png")));
    SkinManageDialog manager(window, MinecraftAccountPtr());
    window->resize(1280, 820);
    window->openInlinePage(&manager, "Skin Library");
    auto* combo = manager.findChild<QComboBox*>("skinAccountCombo");
    auto* capes = manager.findChild<QComboBox*>("capeCombo");
    auto* actions = manager.findChild<QDialogButtonBox*>("buttonBox");
    auto* edit = manager.findChild<QPushButton*>("editSkinButton");
    auto* reset = manager.findChild<QPushButton*>("resetBtn");
    auto* manage = manager.findChild<QPushButton*>("skinManageAccounts");
    QVERIFY(combo && capes && actions && edit && reset && manage);
    QVERIFY(!combo->isEnabled());
    QCOMPARE(combo->currentText(), QString("No accounts added"));
    QVERIFY(manager.getSelectedSkin());
    QVERIFY(edit->isEnabled());
    QVERIFY(!actions->button(QDialogButtonBox::Ok)->isEnabled());
    QVERIFY(!reset->isEnabled());
    QSignalSpy openAccounts(&manager, &SkinManageDialog::manageAccountsRequested);
    manage->click();
    QCOMPARE(openAccounts.count(), 1);

    const auto first = MinecraftAccount::createOffline("PainterOne");
    const auto second = MinecraftAccount::createOffline("PainterTwo");
    // Synthetic profile metadata only. This test never starts an auth or upload task.
    second->accountData()->type = AccountType::MSA;
    int number = 0;
    for (const auto& account : { first, second }) {
        texture.fill(number ? QColor("#dc7781") : QColor("#86bdb5"));
        auto& skin = account->accountData()->minecraftProfile.skin;
        QBuffer bytes(&skin.data);
        QVERIFY(bytes.open(QIODevice::WriteOnly));
        QVERIFY(texture.save(&bytes, "PNG"));
        skin.url = QString("https://example.invalid/skin-%1").arg(number);
        skin.variant = number ? "SLIM" : "CLASSIC";
        ++number;
    }
    Cape cape;
    cape.id = "synthetic-cape";
    cape.alias = "Second account cape";
    QBuffer capeBytes(&cape.data);
    QVERIFY(capeBytes.open(QIODevice::WriteOnly));
    QVERIFY(texture.save(&capeBytes, "PNG"));
    second->accountData()->minecraftProfile.capes.insert(cape.id, cape);
    second->accountData()->minecraftProfile.currentCape = cape.id;
    accounts->addAccount(first);
    accounts->addAccount(second);
    accounts->setDefaultAccount(first);
    QCOMPARE(combo->count(), 2);
    QCOMPARE(combo->currentData().value<MinecraftAccountPtr>(), first);
    QVERIFY(!actions->button(QDialogButtonBox::Ok)->isEnabled());
    combo->setCurrentIndex(1);
    QCOMPARE(combo->currentData().value<MinecraftAccountPtr>(), second);
    QCOMPARE(accounts->defaultAccount(), first);
    QVERIFY(manager.getSelectedSkin());
    QCOMPARE(manager.getSelectedSkin()->getModel(), SkinModel::SLIM);
    QCOMPARE(manager.getSelectedSkin()->getTexture().pixelColor(8, 8), QColor("#dc7781"));
    QVERIFY(actions->button(QDialogButtonBox::Ok)->isEnabled());
    QVERIFY(reset->isEnabled());
    QCOMPARE(capes->count(), 2);
    QCOMPARE(capes->currentData().toString(), cape.id);
    QVERIFY(manager.capes().contains(cape.id));
    for (const QSize size : { QSize(1280, 820), QSize(680, 640) }) {
        window->resize(size);
        QTest::qWait(60);
        QVERIFY(window->rect().contains(QRect(combo->mapTo(window, QPoint()), combo->size())));
        QVERIFY(window->rect().contains(QRect(manage->mapTo(window, QPoint()), manage->size())));
        for (auto* scroll : window->inlineWorkspace()->findChildren<QScrollArea*>())
            QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(window->grab().save(QDir(root).filePath(QString("skin-accounts-%1.png").arg(size.width()))));
    }
    combo->setCurrentIndex(0);
    QCOMPARE(manager.getSelectedSkin()->getModel(), SkinModel::CLASSIC);
    QCOMPARE(capes->count(), 1);
    QVERIFY(manager.capes().isEmpty());
    QVERIFY(!reset->isEnabled());
    accounts->removeAccount(accounts->index(0, 0));
    QCOMPARE(combo->currentData().value<MinecraftAccountPtr>(), second);
    accounts->removeAccount(accounts->index(0, 0));
    QVERIFY(!combo->isEnabled());
    QVERIFY(!actions->button(QDialogButtonBox::Ok)->isEnabled());
    QVERIFY(edit->isEnabled());
    manager.reject();
}

inline void compactToolbox(MainWindow* window, const QString& root)
{
    QVERIFY(window->inlineWorkspace()->closeAllPages());
    QImage texture(64, 64, QImage::Format_ARGB32);
    texture.fill(QColor("#86bdb5"));
    SkinModel skin(texture, SkinModel::CLASSIC);
    SkinEditorDialog editor(window, MinecraftAccountPtr(), skin);
    window->resize(1280, 820);
    window->openInlinePage(&editor, "Skin Studio");
    auto* document = editor.findChild<SkinTextureDocument*>();
    auto* canvas = editor.findChild<SkinCanvas*>("skinCanvas");
    auto* brush = editor.findChild<QToolButton*>("skinToolBrush");
    auto* eraser = editor.findChild<QToolButton*>("skinToolEraser");
    auto* picker = editor.findChild<QToolButton*>("skinToolPicker");
    auto* pan = editor.findChild<QToolButton*>("skinToolPan");
    QVERIFY(document && canvas && brush && eraser && picker && pan);
    auto discardEdits = qScopeGuard([&] { document->markSaved(); });
    QVERIFY(brush->isChecked());
    for (auto* button : { brush, eraser, picker, pan }) {
        QVERIFY(!button->icon().isNull());
        QVERIFY(!button->accessibleName().isEmpty());
        QVERIFY(!button->toolTip().isEmpty());
        QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QVERIFY(button->width() <= 36 && button->height() <= 36);
    }
    canvas->setFocus();
    QTest::keyClick(canvas, Qt::Key_Space);
    QCOMPARE(document->image().pixelColor(8, 8), QColor(Qt::white));
    QTest::keyClick(canvas, Qt::Key_I);
    QVERIFY(picker->isChecked());
    QVERIFY(!brush->isChecked());
    QTest::keyClick(canvas, Qt::Key_Right);
    QTest::keyClick(canvas, Qt::Key_Space);
    QTest::keyClick(canvas, Qt::Key_B);
    QVERIFY(brush->isChecked());
    QTest::keyClick(canvas, Qt::Key_Left);
    QTest::keyClick(canvas, Qt::Key_Space);
    QCOMPARE(document->image().pixelColor(8, 8), QColor("#86bdb5"));
    QTest::keyClick(canvas, Qt::Key_E);
    QVERIFY(eraser->isChecked());
    QTest::keyClick(canvas, Qt::Key_Space);
    QCOMPARE(document->image().pixelColor(8, 8).alpha(), 255);
    for (int i = 0; i < 32; ++i)
        QTest::keyClick(canvas, Qt::Key_Right);
    QTest::keyClick(canvas, Qt::Key_Space);
    QCOMPARE(document->image().pixelColor(40, 8).alpha(), 0);
    QTest::keyClick(canvas, Qt::Key_H);
    QVERIFY(pan->isChecked());
    QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
    const auto beforePan = document->image();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    QCOMPARE(document->image(), beforePan);
    auto* brushSize = editor.findChild<QSpinBox*>();
    QVERIFY(brushSize);
    brushSize->setFocus();
    QTest::keyClick(brushSize, Qt::Key_B);
    QVERIFY(pan->isChecked());
    brush->click();
    QVERIFY(brush->isChecked());
    QVERIFY(!pan->isChecked());
    QCOMPARE(canvas->cursor().shape(), Qt::CrossCursor);
    QTest::keyClick(canvas, Qt::Key_H, Qt::ControlModifier);
    QVERIFY(brush->isChecked());
    for (const QSize size : { QSize(1280, 820), QSize(680, 640) }) {
        window->resize(size);
        QTest::qWait(80);
        for (auto* button : { brush, eraser, picker, pan }) {
            QVERIFY(button->width() <= 36 && button->height() <= 36);
            QVERIFY(button->parentWidget()->rect().contains(button->geometry()));
        }
        for (auto* scroll : editor.findChildren<QScrollArea*>())
            QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(window->grab().save(QDir(root).filePath(QString("skin-tools-%1.png").arg(size.width()))));
    }
    document->markSaved();
    editor.reject();
}
}  // namespace SkinLibraryUiTests
