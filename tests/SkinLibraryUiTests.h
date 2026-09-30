// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QTest>
#include <QToolButton>

#include "Application.h"
#include "minecraft/auth/AccountList.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/skins/SkinCanvas.h"
#include "ui/dialogs/skins/SkinColorWheel.h"
#include "ui/dialogs/skins/SkinEditorDialog.h"
#include "ui/dialogs/skins/SkinManageDialog.h"
#include "ui/dialogs/skins/draw/SkinOpenGLWindow.h"
#include "ui/widgets/InlineWorkspace.h"

namespace SkinLibraryUiTests {

inline QString horizontalScrollDiagnostic(QScrollArea* scroll, QWidget* editor, QWidget* window)
{
    const auto describe = [](QWidget* widget) {
        if (!widget)
            return QString("none");
        const auto size = widget->size();
        const auto minimum = widget->minimumSize();
        const auto hint = widget->minimumSizeHint();
        return QString("%1(%2) size=%3x%4 minimum=%5x%6 minimumHint=%7x%8")
            .arg(widget->metaObject()->className(), widget->objectName())
            .arg(size.width())
            .arg(size.height())
            .arg(minimum.width())
            .arg(minimum.height())
            .arg(hint.width())
            .arg(hint.height());
    };
    QStringList details{ QString("Unexpected horizontal range %1; editor width=%2; window width=%3")
                             .arg(scroll->horizontalScrollBar()->maximum())
                             .arg(editor->width())
                             .arg(window->width()),
                         describe(scroll), describe(scroll->viewport()), describe(scroll->widget()) };
    if (scroll->widget())
        for (auto* child : scroll->widget()->findChildren<QWidget*>())
            details.append(describe(child));
    return details.join('\n');
}

// Original synthetic artwork: a teal jacket, dark trousers, and a gold scarf.
// Distinct faces, cuffs, and a transparent outer layer make renderer mistakes
// visible in screenshots without using a real player's skin or account.
inline QImage patternedSkin(QColor jacket = QColor("#86bdb5"))
{
    QImage texture(64, 64, QImage::Format_ARGB32);
    texture.fill(Qt::transparent);
    QPainter paint(&texture);
    const QColor hair("#383344");
    const QColor skin("#d49b7f");
    const QColor trousers("#444b68");
    const QColor trim("#e8b65b");
    const QColor boots("#303348");
    for (int part = SkinTextureDocument::Head; part <= SkinTextureDocument::LeftLeg; ++part) {
        const auto region =
            SkinTextureDocument::uvRegion(static_cast<SkinTextureDocument::Part>(part), SkinTextureDocument::Base, SkinModel::CLASSIC);
        const QColor color = part == SkinTextureDocument::Head ? hair : part >= SkinTextureDocument::RightLeg ? trousers : jacket;
        for (const auto& rect : region)
            paint.fillRect(rect, color);
    }
    paint.fillRect(QRect(8, 10, 8, 6), skin);
    paint.fillRect(QRect(10, 11, 2, 1), Qt::white);
    paint.fillRect(QRect(13, 11, 2, 1), Qt::white);
    paint.fillRect(QRect(11, 11, 1, 1), boots);
    paint.fillRect(QRect(13, 11, 1, 1), boots);
    paint.fillRect(QRect(11, 14, 3, 1), QColor("#ac665e"));
    paint.fillRect(QRect(20, 20, 8, 2), trim);
    paint.fillRect(QRect(23, 22, 2, 10), jacket.darker(135));
    paint.fillRect(QRect(20, 27, 2, 2), jacket.darker(120));
    paint.fillRect(QRect(26, 27, 2, 2), jacket.darker(120));
    for (const QPoint arm : { QPoint(40, 16), QPoint(32, 48) }) {
        paint.fillRect(QRect(arm + QPoint(0, 11), QSize(16, 2)), trim);
        paint.fillRect(QRect(arm + QPoint(0, 13), QSize(16, 3)), skin);
    }
    for (const QPoint leg : { QPoint(0, 16), QPoint(16, 48) }) {
        paint.fillRect(QRect(leg + QPoint(0, 12), QSize(16, 4)), boots);
        paint.fillRect(QRect(leg + QPoint(4, 4), QSize(1, 8)), trousers.lighter(115));
    }
    paint.fillRect(QRect(20, 36, 8, 1), trim);
    paint.fillRect(QRect(25, 37, 2, 4), trim);
    paint.fillRect(QRect(41, 8, 6, 1), trim);
    paint.end();
    // These cursor positions are used by the keyboard editing regressions.
    texture.setPixelColor(8, 8, jacket);
    texture.setPixelColor(9, 8, jacket);
    return SkinModel::normalizeTexture(texture);
}

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
    QImage texture = patternedSkin();
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
        texture = patternedSkin(number ? QColor("#dc7781") : QColor("#86bdb5"));
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
    QImage texture = patternedSkin();
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
    auto* mode = editor.findChild<QComboBox*>("skinEditMode");
    auto* outer = editor.findChild<QToolButton*>("skinShowOuter");
    QVERIFY(document && canvas && brush && eraser && picker && pan && mode && outer);
    auto discardEdits = qScopeGuard([&] { document->markSaved(); });
    mode->setCurrentIndex(1);
    outer->setChecked(false);
    QVERIFY(canvas->isVisible());
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
    outer->setChecked(true);
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
            QVERIFY2(scroll->horizontalScrollBar()->maximum() == 0, qPrintable(horizontalScrollDiagnostic(scroll, &editor, window)));
        QVERIFY(window->grab().save(QDir(root).filePath(QString("skin-tools-%1.png").arg(size.width()))));
    }
    document->markSaved();
    editor.reject();
}

inline void editorModesAndVisibility(MainWindow* window, const QString& root)
{
    QVERIFY(window->inlineWorkspace()->closeAllPages());
    window->resize(1280, 820);
    const auto texture = patternedSkin();
    SkinModel skin(texture, SkinModel::CLASSIC);
    SkinEditorDialog editor(window, MinecraftAccountPtr(), skin);
    window->openInlinePage(&editor, "Skin Studio");
    auto* document = editor.findChild<SkinTextureDocument*>();
    auto* canvas = editor.findChild<SkinCanvas*>("skinCanvas");
    auto* preview = editor.findChild<SkinOpenGLWindow*>("skin3DCanvas");
    auto* mode = editor.findChild<QComboBox*>("skinEditMode");
    auto* model = editor.findChild<QComboBox*>("skinModel");
    auto* wheel = editor.findChild<SkinColorWheel*>("skinColorWheel");
    auto* hex = editor.findChild<QLineEdit*>("skinColorHex");
    auto* opacity = editor.findChild<QSlider*>("skinOpacity");
    auto* brushSize = editor.findChild<QSpinBox*>("skinBrushSize");
    auto* showBase = editor.findChild<QToolButton*>("skinShowBase");
    auto* showOuter = editor.findChild<QToolButton*>("skinShowOuter");
    auto* diagram = editor.findChild<QWidget*>("skinBodySelector");
    auto* grid = editor.findChild<QCheckBox*>("skinGrid");
    QVERIFY(document && canvas && mode && model && wheel && hex && opacity && brushSize && showBase && showOuter && diagram && grid);
    QVERIFY(!editor.findChild<QComboBox*>("skinLayer"));
    QVERIFY(showBase->isChecked() && showOuter->isChecked());
    QCOMPARE(showBase->text(), QString("Body"));
    QCOMPARE(showOuter->text(), QString("Outer layer"));
    auto discardEdits = qScopeGuard([&] { document->markSaved(); });
    const bool native = QGuiApplication::platformName() == "windows" && SkinOpenGLWindow::hasOpenGL();
    if (native) {
        QVERIFY(preview);
        QTRY_VERIFY(preview->isValid());
        QCOMPARE(mode->currentIndex(), 0);
        QVERIFY(preview->isVisible());
        QVERIFY(!canvas->isVisible());
    } else {
        QCOMPARE(mode->currentIndex(), 1);
        QVERIFY(canvas->isVisible());
    }
    QVERIFY(wheel->isVisible());
    QVERIFY(!wheel->accessibleName().isEmpty());
    QCOMPARE(opacity->minimum(), 0);
    QCOMPARE(opacity->maximum(), 255);
    QCOMPARE(opacity->value(), 255);

    for (int part = 0; part < 6; ++part) {
        auto* button = editor.findChild<QToolButton*>(QString("skinPart%1").arg(part));
        QVERIFY(button && button->isChecked());
        QVERIFY(!button->accessibleName().isEmpty());
        QVERIFY(!button->accessibleDescription().isEmpty());
        QVERIFY(button->focusPolicy() == Qt::StrongFocus);
        button->setFocus();
        QTest::keyClick(button, Qt::Key_Space);
        QVERIFY(!button->isChecked());
        if (native) {
            QVERIFY(!preview->partLayerVisible(part, SkinTextureDocument::Base));
            QVERIFY(!preview->partLayerVisible(part, SkinTextureDocument::Overlay));
        }
        QTest::keyClick(button, Qt::Key_Space);
        QVERIFY(button->isChecked());
    }
    if (native) {
        showBase->click();
        for (int part = 0; part < 6; ++part) {
            QVERIFY(!preview->partLayerVisible(part, SkinTextureDocument::Base));
            QVERIFY(preview->partLayerVisible(part, SkinTextureDocument::Overlay));
        }
        showBase->click();
        showOuter->click();
        QVERIFY(preview->gridVisible());
        grid->click();
        QVERIFY(!preview->gridVisible());
        grid->click();
        for (int part = 0; part < 6; ++part) {
            QVERIFY(preview->partLayerVisible(part, SkinTextureDocument::Base));
            QVERIFY(!preview->partLayerVisible(part, SkinTextureDocument::Overlay));
        }
        showOuter->click();
    }
    // Visibility and brush choices must not alter the texture or its history.
    QCOMPARE(document->image(), texture);
    QVERIFY(!document->canUndo());

    // The outer layer owns painting while visible, even when its texel is
    // transparent. Hidden parts and layers cannot be changed in the 2D view.
    mode->setCurrentIndex(1);
    canvas->setFocus();
    QTest::keyClick(canvas, Qt::Key_Space);
    QCOMPARE(document->image(), texture);
    QVERIFY(!document->canUndo());
    for (int i = 0; i < 32; ++i)
        QTest::keyClick(canvas, Qt::Key_Right);
    QTest::keyClick(canvas, Qt::Key_Space);
    QCOMPARE(document->image().pixelColor(40, 8), QColor(Qt::white));
    QCOMPARE(document->image().pixelColor(8, 8), texture.pixelColor(8, 8));
    showOuter->setChecked(false);
    canvas->setColor(Qt::red);
    QTest::keyClick(canvas, Qt::Key_Space);
    QCOMPARE(document->image().pixelColor(40, 8), QColor(Qt::white));
    for (int i = 0; i < 32; ++i)
        QTest::keyClick(canvas, Qt::Key_Left);
    QTest::keyClick(canvas, Qt::Key_Space);
    QCOMPARE(document->image().pixelColor(8, 8), QColor(Qt::red));
    auto* head = editor.findChild<QToolButton*>("skinPart0");
    QVERIFY(head);
    head->setChecked(false);
    canvas->setColor(Qt::black);
    const auto beforeHidden = document->image();
    QTest::keyClick(canvas, Qt::Key_Space);
    QCOMPARE(document->image(), beforeHidden);
    showBase->setChecked(false);
    head->setChecked(true);
    QTest::keyClick(canvas, Qt::Key_Space);
    QCOMPARE(document->image(), beforeHidden);
    showBase->setChecked(true);
    showOuter->setChecked(true);
    canvas->setColor(Qt::white);
    document->undo();
    document->undo();
    QCOMPARE(document->image(), texture);
    QVERIFY(!document->canUndo());

    hex->setFocus();
    hex->setText("#b75cde");
    QTest::keyClick(hex, Qt::Key_Return);
    QCOMPARE(wheel->color().rgba(), QColor("#b75cde").rgba());
    opacity->setValue(73);
    QCOMPARE(wheel->color().alpha(), 73);
    QSignalSpy wheelChanges(wheel, &SkinColorWheel::colorChanged);
    QTest::mouseClick(wheel, Qt::LeftButton, Qt::NoModifier, wheel->rect().center());
    QCOMPARE(wheelChanges.count(), 1);
    QCOMPARE(QColor(hex->text()).rgb(), wheel->color().rgb());
    QCOMPARE(wheel->color().alpha(), 73);
    QTest::keyClick(wheel, Qt::Key_Up);
    QCOMPARE(wheelChanges.count(), 2);
    QCOMPARE(QColor(hex->text()).rgb(), wheel->color().rgb());
    QCOMPARE(wheel->color().alpha(), 73);

    hex->setText("#40dc7781");
    QTest::keyClick(hex, Qt::Key_Return);
    QCOMPARE(wheel->color().rgba(), QColor("#40dc7781").rgba());
    QCOMPARE(opacity->value(), 64);
    opacity->setValue(255);
    brushSize->setValue(2);
    mode->setCurrentIndex(1);
    showOuter->setChecked(false);
    QVERIFY(canvas->isVisible());
    if (preview)
        QVERIFY(preview->isVisible());
    canvas->setFocus();
    QTest::keyClick(canvas, Qt::Key_Space);
    QCOMPARE(document->image().pixelColor(8, 8), QColor("#dc7781"));
    const auto edited = document->image();
    QVERIFY(document->canUndo());
    model->setCurrentIndex(1);
    QCOMPARE(document->model(), SkinModel::SLIM);
    const auto selectedColor = wheel->color();
    if (native) {
        showOuter->setChecked(true);
        head->click();
        for (int i = 0; i < 3; ++i) {
            mode->setCurrentIndex(0);
            QVERIFY(preview->isVisible());
            QVERIFY(!canvas->isVisible());
            QVERIFY(!preview->partLayerVisible(0, SkinTextureDocument::Base));
            QVERIFY(!preview->partLayerVisible(0, SkinTextureDocument::Overlay));
            QCOMPARE(document->image(), edited);
            QCOMPARE(document->model(), SkinModel::SLIM);
            QCOMPARE(wheel->color().rgba(), selectedColor.rgba());
            QCOMPARE(brushSize->value(), 2);
            mode->setCurrentIndex(1);
        }
        head->click();
    }
    document->undo();
    QCOMPARE(document->model(), SkinModel::CLASSIC);
    document->undo();
    QCOMPARE(document->image(), texture);
    document->redo();
    QCOMPARE(document->image(), edited);
    document->undo();
    if (native)
        mode->setCurrentIndex(0);
    for (const QSize size : { QSize(1280, 820), QSize(680, 640) }) {
        window->resize(size);
        QTest::qWait(100);
        QVERIFY(wheel->isVisible());
        const QRect wheelBounds(wheel->mapTo(window, QPoint()), wheel->size());
        QVERIFY(window->rect().contains(wheelBounds));
        auto* activeCanvas = native ? static_cast<QWidget*>(preview) : static_cast<QWidget*>(canvas);
        const QRect canvasBounds(activeCanvas->mapTo(window, QPoint()), activeCanvas->size());
        QVERIFY(!wheelBounds.intersects(canvasBounds));
        for (auto* scroll : editor.findChildren<QScrollArea*>())
            QVERIFY2(scroll->horizontalScrollBar()->maximum() == 0, qPrintable(horizontalScrollDiagnostic(scroll, &editor, window)));
        auto* visibility = editor.findChild<QScrollArea*>("skinVisibilityScroll");
        QVERIFY(visibility);
        visibility->verticalScrollBar()->setValue(visibility->verticalScrollBar()->maximum());
        QCOMPARE(QRect(wheel->mapTo(window, QPoint()), wheel->size()), wheelBounds);
        visibility->verticalScrollBar()->setValue(0);
        QCOMPARE(diagram->size(), QSize(104, 148));
        for (int part = 0; part < 6; ++part) {
            auto* button = editor.findChild<QToolButton*>(QString("skinPart%1").arg(part));
            QVERIFY(button);
            QCOMPARE(button->size(), QSize(part < 2 ? 44 : 22, part == 0 ? 44 : 52));
            QVERIFY(diagram->rect().contains(button->geometry()));
        }
        auto* torso = editor.findChild<QToolButton*>("skinPart1");
        auto* rightArm = editor.findChild<QToolButton*>("skinPart2");
        auto* leftArm = editor.findChild<QToolButton*>("skinPart3");
        QVERIFY(torso && rightArm && leftArm);
        QVERIFY(head->geometry().bottom() <= torso->geometry().top());
        QVERIFY(rightArm->geometry().right() <= torso->geometry().left());
        QVERIFY(leftArm->geometry().left() >= torso->geometry().right());
        QVERIFY(
            window->grab().save(QDir(root).filePath(QString("skin-studio-%1-%2.png").arg(native ? "3d" : "fallback").arg(size.width()))));
    }
    window->resize(1280, 820);
    mode->setCurrentIndex(1);
    QTest::qWait(80);
    QVERIFY(window->grab().save(QDir(root).filePath("skin-studio-2d.png")));
    QVERIFY(texture.save(QDir(root).filePath("skin-studio-synthetic.png")));
    document->markSaved();
    editor.reject();
}
}  // namespace SkinLibraryUiTests
