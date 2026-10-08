// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2023-2024 Trial97 <alexandru.tripon97@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QDialog>
#include <QItemSelection>
#include <QLabel>
#include <QPixmap>
#include <memory>

#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/skins/SkinApplyTask.h"
#include "minecraft/skins/SkinList.h"
#include "minecraft/skins/SkinModel.h"
#include "minecraft/skins/SkinOutfitLibrary.h"
#include "ui/dialogs/skins/draw/SkinOpenGLWindow.h"

namespace Ui {
class SkinManageDialog;
}
class QPushButton;
class QComboBox;
class QTemporaryDir;
class SkinManageDialog : public QDialog, public SkinProvider {
    Q_OBJECT
   public:
    explicit SkinManageDialog(QWidget* parent, MinecraftAccountPtr acct);
    virtual ~SkinManageDialog();
    void resizeEvent(QResizeEvent* event) override;

    virtual SkinModel* getSelectedSkin() override;
    virtual QHash<QString, QImage> capes() override;

   signals:
    void manageAccountsRequested();

   public slots:
    void selectionChanged(QItemSelection, QItemSelection);
    void activated(QModelIndex);
    void delayed_scroll(QModelIndex);
    void on_openDirBtn_clicked();
    void on_fileBtn_clicked();
    void on_urlBtn_clicked();
    void on_userBtn_clicked();
    void accept() override;
    void reject() override;
    void on_capeCombo_currentIndexChanged(int index);
    void on_steveBtn_toggled(bool checked);
    void on_resetBtn_clicked();
    void show_context_menu(const QPoint& pos);
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void on_action_Rename_Skin_triggered(bool checked);
    void on_action_Delete_Skin_triggered(bool checked);
    void editSelectedSkin();

   protected:
    void changeEvent(QEvent* event) override;

   private:
    void setupCapes();
    void refreshAccounts();
    void selectAccount(MinecraftAccountPtr account);
    void updateAccountActions();
    bool canManageLibrarySelection() const;
    void applySelectedCape();
    void setupOutfits();
    void refreshOutfits(const QString& selectedId = {});
    void selectOutfit();
    void restoreLibrarySelection();
    void saveOutfit();
    void renameOutfit();
    void deleteOutfit();
    void applyOutfit();
    void updateOutfitActions();
    void updatePreview();
    QImage selectedCapeImage() const;

   private:
    MinecraftAccountPtr m_acct;
    Ui::SkinManageDialog* m_ui;
    SkinList m_list;
    QString m_selectedSkinKey;
    QHash<QString, QImage> m_capes;
    QHash<QString, int> m_capesIdx;
    SkinOpenGLWindow* m_skinPreview = nullptr;
    QLabel* m_skinPreviewLabel = nullptr;
    QWidget* m_skinPreviewContainer = nullptr;
    QPushButton* m_editButton = nullptr;
    QComboBox* m_accountCombo = nullptr;
    QLabel* m_accountStatus = nullptr;
    bool m_previewFailed = false;
    QPushButton* m_applyCape = nullptr;
    shared_qobject_ptr<SkinApplyTask> m_capeTask;
    SkinOutfitLibrary m_outfits;
    std::optional<SkinOutfitLibrary::Entry> m_selectedOutfit;
    std::unique_ptr<SkinModel> m_outfitSkin;
    std::unique_ptr<QTemporaryDir> m_outfitUpload;
    QComboBox* m_outfitCombo = nullptr;
    QPushButton* m_saveOutfit = nullptr;
    QPushButton* m_renameOutfit = nullptr;
    QPushButton* m_deleteOutfit = nullptr;
    QPushButton* m_applyOutfit = nullptr;
    QLabel* m_outfitStatus = nullptr;
};
