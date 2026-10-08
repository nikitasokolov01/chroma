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

#include "SkinManageDialog.h"
#include "ui/dialogs/skins/draw/SkinOpenGLWindow.h"
#include "ui_SkinManageDialog.h"

#include <FileSystem.h>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QListView>
#include <QLineEdit>
#include <QMenu>
#include <QMimeDatabase>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QUrl>

#include "Application.h"
#include "DesktopServices.h"
#include "Json.h"
#include "QObjectPtr.h"

#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/Parsers.h"
#include "minecraft/skins/CapeChange.h"
#include "minecraft/skins/SkinApplyTask.h"
#include "minecraft/skins/SkinDelete.h"
#include "minecraft/skins/SkinList.h"
#include "minecraft/skins/SkinModel.h"
#include "minecraft/skins/SkinTextureDocument.h"
#include "minecraft/skins/SkinUpload.h"
#include "ui/dialogs/skins/SkinEditorDialog.h"

#include "net/Download.h"
#include "net/NetJob.h"
#include "tasks/Task.h"

#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/instanceview/InstanceDelegate.h"
#include "ui/widgets/ClayWidgets.h"

SkinManageDialog::SkinManageDialog(QWidget* parent, MinecraftAccountPtr acct)
    : QDialog(parent),
      m_acct(acct),
      m_ui(new Ui::SkinManageDialog),
      m_list(this, APPLICATION->settings()->get("SkinsDir").toString(), acct),
      m_outfits(QDir(APPLICATION->dataRoot()).filePath("skin-outfits"))
{
    m_ui->setupUi(this);
    m_applyCape = new QPushButton(tr("Apply cape"), m_ui->capeBox);
    m_applyCape->setObjectName("skinApplyCape");
    m_applyCape->setAutoDefault(false);
    m_applyCape->setCursor(Qt::PointingHandCursor);
    m_applyCape->setToolTip(tr("Equip the selected owned cape without changing the skin."));
    m_ui->verticalLayout_4->insertWidget(2, m_applyCape);
    connect(m_applyCape, &QPushButton::clicked, this, &SkinManageDialog::applySelectedCape);
    auto* importControls = new QGridLayout;
    int controlIndex = 0;
    while (auto* item = m_ui->buttonsHLayout->takeAt(0)) {
        if (auto* widget = item->widget()) {
            if (widget != m_ui->buttonBox) {
                importControls->addWidget(widget, controlIndex / 3, controlIndex % 3);
                ++controlIndex;
            }
        }
        delete item;
    }
    m_ui->verticalLayout->removeItem(m_ui->buttonsHLayout);
    delete m_ui->buttonsHLayout;
    m_ui->buttonsHLayout = nullptr;
    m_ui->verticalLayout->addLayout(importControls);
    m_ui->verticalLayout->addWidget(m_ui->buttonBox);
    setWindowTitle(tr("Skin Library"));
    auto* header = new QHBoxLayout;
    auto* accountLabel = new QLabel(tr("Account"), this);
    m_accountCombo = new ClayComboBox(this);
    m_accountCombo->setObjectName("skinAccountCombo");
    m_accountCombo->setAccessibleName(tr("Account for skin management"));
    m_accountCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_accountCombo->setMinimumContentsLength(12);
    accountLabel->setBuddy(m_accountCombo);
    header->addWidget(accountLabel);
    header->addWidget(m_accountCombo, 1);
    auto* manageAccounts = new QPushButton(tr("Manage Accounts"), this);
    manageAccounts->setObjectName("skinManageAccounts");
    manageAccounts->setAutoDefault(false);
    header->addWidget(manageAccounts);
    connect(manageAccounts, &QPushButton::clicked, this, &SkinManageDialog::manageAccountsRequested);
    m_ui->verticalLayout->insertLayout(0, header);
    auto* actions = new QHBoxLayout;
    m_accountStatus = new QLabel(this);
    m_accountStatus->setObjectName("skinAccountStatus");
    m_accountStatus->setTextFormat(Qt::PlainText);
    m_accountStatus->setWordWrap(true);
    actions->addWidget(m_accountStatus, 1);
    m_editButton = new QPushButton(tr("Edit Skin…"), this);
    m_editButton->setObjectName("editSkinButton");
    m_editButton->setAutoDefault(false);
    actions->addWidget(m_editButton);
    connect(m_editButton, &QPushButton::clicked, this, &SkinManageDialog::editSelectedSkin);
    auto* resetView = new QPushButton(tr("Reset View"), this);
    resetView->setAutoDefault(false);
    actions->addWidget(resetView);
    m_ui->verticalLayout->insertLayout(1, actions);
    m_ui->urlLine->setPlaceholderText(tr("Skin URL or Minecraft username"));

    m_skinPreviewLabel = new QLabel(this);
    m_skinPreviewLabel->setAlignment(Qt::AlignCenter);
    m_skinPreviewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_skinPreviewLabel->setMinimumSize(180, 200);
    m_skinPreviewLabel->setText(tr("Import a skin PNG to start your collection."));
    m_skinPreviewLabel->setWordWrap(true);
    if (SkinOpenGLWindow::hasOpenGL()) {
        m_skinPreview = new SkinOpenGLWindow(this, palette().color(QPalette::Normal, QPalette::Base), this);
        connect(resetView, &QPushButton::clicked, m_skinPreview, &SkinOpenGLWindow::resetView);
    }
    resetView->setEnabled(m_skinPreview != nullptr);

    setWindowModality(Qt::WindowModal);

    auto contentsWidget = m_ui->listView;
    contentsWidget->setViewMode(QListView::IconMode);
    contentsWidget->setFlow(QListView::LeftToRight);
    contentsWidget->setIconSize(QSize(64, 64));
    contentsWidget->setMovement(QListView::Static);
    contentsWidget->setResizeMode(QListView::Adjust);
    contentsWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    contentsWidget->setSpacing(12);
    contentsWidget->setWordWrap(false);
    contentsWidget->setWrapping(true);
    contentsWidget->setUniformItemSizes(true);
    contentsWidget->setTextElideMode(Qt::ElideRight);
    contentsWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    contentsWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentsWidget->installEventFilter(this);
    contentsWidget->setItemDelegate(new ListViewDelegate(this));

    contentsWidget->setAcceptDrops(true);
    contentsWidget->setDropIndicatorShown(true);
    contentsWidget->viewport()->setAcceptDrops(true);
    contentsWidget->setDragDropMode(QAbstractItemView::DropOnly);
    contentsWidget->setDefaultDropAction(Qt::CopyAction);

    contentsWidget->installEventFilter(this);
    contentsWidget->setModel(&m_list);
    connect(&m_list, &QAbstractItemModel::modelReset, this, [this] {
        if (m_selectedOutfit)
            return;
        int row = m_list.getSkinIndex(m_selectedSkinKey);
        if (row < 0)
            row = m_list.getSelectedAccountSkin();
        if (row < 0 && m_list.rowCount())
            row = 0;
        m_ui->listView->setCurrentIndex(m_list.index(row));
    });

    connect(contentsWidget, &QAbstractItemView::doubleClicked, this, &SkinManageDialog::activated);

    connect(contentsWidget->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SkinManageDialog::selectionChanged);
    connect(m_ui->listView, &QListView::customContextMenuRequested, this, &SkinManageDialog::show_context_menu);
    connect(m_ui->elytraCB, &QCheckBox::stateChanged, this, [this]() {
        if (m_skinPreview) {
            m_skinPreview->setElytraVisible(m_ui->elytraCB->isChecked());
        }
        on_capeCombo_currentIndexChanged(0);
    });

    setupOutfits();
    setupCapes();

    auto selectedIndex = m_list.getSelectedAccountSkin();
    m_ui->listView->setCurrentIndex(m_list.index(selectedIndex < 0 && m_list.rowCount() ? 0 : selectedIndex));

    m_ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Close"));
    m_ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Apply Skin"));
    updateAccountActions();

    if (m_skinPreview) {
        m_skinPreviewContainer = m_skinPreview;
        m_skinPreviewContainer->setMinimumSize(180, 200);
        m_skinPreviewContainer->setFocusPolicy(Qt::StrongFocus);
        m_skinPreviewContainer->setAccessibleName(tr("Interactive 3D skin preview"));
        m_ui->skinLayout->insertWidget(0, m_skinPreviewContainer);
        connect(
            m_skinPreview, &SkinOpenGLWindow::renderingFailed, this,
            [this] {
                m_previewFailed = true;
                m_skinPreviewContainer->hide();
                m_skinPreviewLabel->show();
            },
            Qt::QueuedConnection);
    }
    m_ui->skinLayout->addWidget(m_skinPreviewLabel);
    m_skinPreviewLabel->setVisible(m_skinPreview == nullptr);
    connect(m_accountCombo, &QComboBox::currentIndexChanged, this,
            [this] { selectAccount(m_accountCombo->currentData().value<MinecraftAccountPtr>()); });
    connect(APPLICATION->accounts().get(), &AccountList::listChanged, this, &SkinManageDialog::refreshAccounts);
    connect(APPLICATION->accounts().get(), &AccountList::listActivityChanged, this, &SkinManageDialog::updateAccountActions);
    refreshAccounts();
    updatePreview();
}

SkinManageDialog::~SkinManageDialog()
{
    // The preview's provider refers to members destroyed before QWidget children.
    delete m_skinPreviewContainer;
    delete m_ui;
}

void SkinManageDialog::refreshAccounts()
{
    if (m_capeTask && m_capeTask->isRunning())
        return;
    const QSignalBlocker blocker(m_accountCombo);
    m_accountCombo->clear();
    const auto accounts = APPLICATION->accounts();
    int selected = -1;
    for (int i = 0; i < accounts->count(); ++i) {
        const auto account = accounts->at(i);
        m_accountCombo->addItem(
            tr("%1 · %2").arg(account->profileName(), account->accountType() == AccountType::MSA ? tr("Microsoft") : tr("Offline")),
            QVariant::fromValue(account));
        if (account == m_acct)
            selected = i;
    }
    if (!accounts->count()) {
        m_accountCombo->addItem(tr("No accounts added"));
        m_accountCombo->setEnabled(false);
        selectAccount(MinecraftAccountPtr());
    } else {
        m_accountCombo->setEnabled(true);
        if (selected < 0)
            selected =
                qMax(0, accounts->findAccountByProfileId(accounts->defaultAccount() ? accounts->defaultAccount()->profileId() : QString()));
        m_accountCombo->setCurrentIndex(selected);
        selectAccount(accounts->at(selected));
    }
    updateAccountActions();
}

void SkinManageDialog::selectAccount(MinecraftAccountPtr account)
{
    if (m_acct == account)
        return;
    m_acct = account;
    m_selectedSkinKey.clear();
    setupCapes();
    m_list.setAccount(account);
    if (m_selectedOutfit)
        selectOutfit();
    updateAccountActions();
}

void SkinManageDialog::updateAccountActions()
{
    const bool online = m_acct && m_acct->accountType() == AccountType::MSA && m_acct->hasProfile();
    const bool changingCape = m_capeTask && m_capeTask->isRunning();
    const bool ready = online && !m_acct->isActive() && !m_acct->isInUse() && !changingCape;
    const auto* skin = getSelectedSkin();
    m_editButton->setEnabled(skin != nullptr && !changingCape);
    findChild<QPushButton*>("skinManageAccounts")->setEnabled(!changingCape);
    m_ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(skin && ready && !m_selectedOutfit);
    m_ui->resetBtn->setEnabled(ready);
    m_applyCape->setEnabled(ready && !m_selectedOutfit &&
                            m_ui->capeCombo->currentData().toString() != m_acct->accountData()->minecraftProfile.currentCape);
    m_ui->capeCombo->setEnabled(!changingCape && !m_selectedOutfit);
    m_ui->modelBox->setEnabled(!changingCape && !m_selectedOutfit);
    m_accountCombo->setEnabled(!changingCape && APPLICATION->accounts()->count() > 0);
    m_ui->buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(!changingCape);
    m_ui->listView->setEnabled(!changingCape);
    m_ui->action_Rename_Skin->setEnabled(canManageLibrarySelection());
    m_ui->action_Delete_Skin->setEnabled(canManageLibrarySelection());
    for (auto* button : { m_ui->fileBtn, m_ui->urlBtn, m_ui->userBtn })
        button->setEnabled(!changingCape);
    updateOutfitActions();
    if (changingCape)
        return;
    m_accountStatus->setText(!m_acct   ? tr("Local library · Add an account to apply skins.")
                             : !online ? tr("Local editing · Microsoft sign-in is required to apply skins.")
                             : !ready  ? tr("Close Minecraft and finish sign-in before applying a skin.")
                                       : tr("Applying a skin updates %1’s Minecraft profile.").arg(m_acct->profileName()));
}

void SkinManageDialog::applySelectedCape()
{
    if (!m_applyCape->isEnabled())
        return;
    const auto account = m_acct;
    m_capeTask = SkinApplyTask::forCape(account, m_ui->capeCombo->currentData().toString());
    connect(m_capeTask.get(), &Task::started, this, &SkinManageDialog::updateAccountActions);
    connect(m_capeTask.get(), &Task::status, m_accountStatus, &QLabel::setText);
    connect(m_capeTask.get(), &Task::finished, this, [this, account] {
        refreshAccounts();
        updateAccountActions();
        m_accountStatus->setText(m_capeTask->wasSuccessful() ? tr("Cape updated on %1’s Minecraft account.").arg(account->profileName())
                                                             : m_capeTask->failReason());
    });
    m_capeTask->start();
}

void SkinManageDialog::activated(QModelIndex index)
{
    if (!canManageLibrarySelection() || index != m_ui->listView->currentIndex())
        return;
    m_selectedSkinKey = index.data(Qt::UserRole).toString();
    editSelectedSkin();
}

void SkinManageDialog::selectionChanged(QItemSelection selected, [[maybe_unused]] QItemSelection deselected)
{
    if (selected.empty()) {
        if (m_selectedOutfit)
            return;
        updateAccountActions();
        updatePreview();
        return;
    }

    QString key = selected.first().indexes().first().data(Qt::UserRole).toString();
    if (key.isEmpty())
        return;
    {
        const QSignalBlocker blocker(m_outfitCombo);
        m_outfitCombo->setCurrentIndex(0);
        m_selectedOutfit.reset();
        m_outfitSkin.reset();
    }
    m_selectedSkinKey = key;
    auto skin = getSelectedSkin();
    updateAccountActions();
    if (!skin)
        return;

    if (m_skinPreview) {
        m_skinPreview->updateScene(skin);
    }
    m_skinPreviewLabel->setPixmap(
        QPixmap::fromImage(skin->getPreview()).scaled(m_skinPreviewLabel->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    m_ui->capeCombo->setCurrentIndex(m_capesIdx.value(skin->getCapeId()));
    m_ui->steveBtn->setChecked(skin->getModel() == SkinModel::CLASSIC);
    m_ui->alexBtn->setChecked(skin->getModel() == SkinModel::SLIM);
    updatePreview();
}

void SkinManageDialog::delayed_scroll(QModelIndex model_index)
{
    auto contentsWidget = m_ui->listView;
    contentsWidget->scrollTo(model_index);
}

void SkinManageDialog::on_openDirBtn_clicked()
{
    DesktopServices::openPath(m_list.getDir(), true);
}

void SkinManageDialog::on_fileBtn_clicked()
{
    auto filter = QMimeDatabase().mimeTypeForName("image/png").filterString();
    QString raw_path = QFileDialog::getOpenFileName(this, tr("Select Skin Texture"), QString(), filter);
    if (raw_path.isNull()) {
        return;
    }
    auto message = m_list.installSkin(raw_path, {});
    if (!message.isEmpty()) {
        CustomMessageBox::selectable(this, tr("Selected file is not a valid skin"), message, QMessageBox::Critical)->show();
        return;
    }
}

QPixmap previewCape(QImage capeImage, bool elytra = false)
{
    if (elytra) {
        auto wing = capeImage.copy(34, 2, 12, 20);
        QImage mirrored = wing.mirrored(true, false);

        QImage combined(wing.width() * 2 + 1, wing.height() + 14, capeImage.format());
        combined.fill(Qt::transparent);

        QPainter painter(&combined);
        painter.drawImage(0, 7, wing);
        painter.drawImage(wing.width() + 1, 7, mirrored);
        painter.end();
        return QPixmap::fromImage(combined.scaled(84, 128, Qt::KeepAspectRatio, Qt::FastTransformation));
    }
    return QPixmap::fromImage(capeImage.copy(1, 1, 10, 16).scaled(80, 128, Qt::IgnoreAspectRatio, Qt::FastTransformation));
}

void SkinManageDialog::setupOutfits()
{
    auto* box = new QGroupBox(tr("Outfits"), this);
    box->setObjectName("skinOutfitBox");
    auto* layout = new QGridLayout(box);
    m_outfitCombo = new ClayComboBox(box);
    m_outfitCombo->setObjectName("skinOutfitCombo");
    m_outfitCombo->setAccessibleName(tr("Saved outfit"));
    m_outfitCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_outfitCombo->setMinimumContentsLength(12);
    m_saveOutfit = new QPushButton(tr("Save outfit…"), box);
    m_saveOutfit->setObjectName("skinOutfitSave");
    m_saveOutfit->setToolTip(tr("Save this skin, including any Extras already applied in Skin Studio, as a reusable outfit."));
    m_renameOutfit = new QPushButton(tr("Rename…"), box);
    m_renameOutfit->setObjectName("skinOutfitRename");
    m_deleteOutfit = new QPushButton(tr("Delete…"), box);
    m_deleteOutfit->setObjectName("skinOutfitDelete");
    m_applyOutfit = new QPushButton(tr("Apply outfit"), box);
    m_applyOutfit->setObjectName("skinOutfitApply");
    for (auto* button : { m_saveOutfit, m_renameOutfit, m_deleteOutfit, m_applyOutfit })
        button->setAutoDefault(false);
    layout->addWidget(m_outfitCombo, 0, 0, 1, 3);
    layout->addWidget(m_applyOutfit, 0, 3);
    layout->addWidget(m_saveOutfit, 1, 0);
    layout->addWidget(m_renameOutfit, 1, 1);
    layout->addWidget(m_deleteOutfit, 1, 2);
    layout->setColumnStretch(0, 1);
    m_outfitStatus = new QLabel(box);
    m_outfitStatus->setObjectName("skinOutfitStatus");
    m_outfitStatus->setWordWrap(true);
    m_outfitStatus->setTextFormat(Qt::PlainText);
    layout->addWidget(m_outfitStatus, 2, 0, 1, 4);
    m_ui->verticalLayout->insertWidget(2, box);
    connect(m_outfitCombo, &QComboBox::currentIndexChanged, this, &SkinManageDialog::selectOutfit);
    connect(m_saveOutfit, &QPushButton::clicked, this, &SkinManageDialog::saveOutfit);
    connect(m_renameOutfit, &QPushButton::clicked, this, &SkinManageDialog::renameOutfit);
    connect(m_deleteOutfit, &QPushButton::clicked, this, &SkinManageDialog::deleteOutfit);
    connect(m_applyOutfit, &QPushButton::clicked, this, &SkinManageDialog::applyOutfit);
    refreshOutfits();
}

void SkinManageDialog::refreshOutfits(const QString& selectedId)
{
    QString warning;
    {
        const QSignalBlocker blocker(m_outfitCombo);
        m_outfitCombo->clear();
        m_outfitCombo->addItem(tr("Skin library selection"), QString());
        for (const auto& entry : m_outfits.entries(&warning)) {
            const SkinModel skin(entry.image, entry.model);
            m_outfitCombo->addItem(QIcon(QPixmap::fromImage(skin.getPreview())), entry.name, entry.id);
        }
        m_outfitCombo->setCurrentIndex(qMax(0, m_outfitCombo->findData(selectedId)));
    }
    // During construction the remaining preview controls are still being set up.
    if (!selectedId.isEmpty() || m_selectedOutfit)
        selectOutfit();
    if (!warning.isEmpty())
        m_outfitStatus->setText(warning);
}

void SkinManageDialog::selectOutfit()
{
    const auto id = m_outfitCombo->currentData().toString();
    m_selectedOutfit.reset();
    m_outfitSkin.reset();
    if (id.isEmpty()) {
        restoreLibrarySelection();
        return;
    }
    QString error;
    m_selectedOutfit = m_outfits.load(id, &error);
    if (!m_selectedOutfit) {
        restoreLibrarySelection();
        m_outfitStatus->setText(error);
        return;
    }
    m_outfitSkin = std::make_unique<SkinModel>(m_selectedOutfit->image, m_selectedOutfit->model);
    const auto capeId = m_selectedOutfit->capeId.value_or(m_acct ? m_acct->accountData()->minecraftProfile.currentCape : QString());
    m_outfitSkin->setCapeId(capeId);
    {
        const QSignalBlocker selectionBlocker(m_ui->listView->selectionModel());
        m_ui->listView->setCurrentIndex(QModelIndex());
        const QSignalBlocker capeBlocker(m_ui->capeCombo);
        m_ui->capeCombo->setCurrentIndex(capeId.isEmpty() ? 0 : m_ui->capeCombo->findData(capeId));
        const QSignalBlocker modelBlocker(m_ui->steveBtn);
        m_ui->steveBtn->setChecked(m_selectedOutfit->model == SkinModel::CLASSIC);
        m_ui->alexBtn->setChecked(m_selectedOutfit->model == SkinModel::SLIM);
    }
    updatePreview();
    updateAccountActions();
}

void SkinManageDialog::restoreLibrarySelection()
{
    m_selectedOutfit.reset();
    m_outfitSkin.reset();
    const QSignalBlocker outfitBlocker(m_outfitCombo);
    m_outfitCombo->setCurrentIndex(0);
    int row = m_list.getSkinIndex(m_selectedSkinKey);
    if (row < 0)
        row = m_list.getSelectedAccountSkin();
    if (row < 0 && m_list.rowCount())
        row = 0;
    const auto index = m_list.index(row);
    {
        const QSignalBlocker selectionBlocker(m_ui->listView->selectionModel());
        m_ui->listView->setCurrentIndex(index);
    }
    m_selectedSkinKey = index.isValid() ? index.data(Qt::UserRole).toString() : QString();
    const auto* skin = getSelectedSkin();
    {
        const QSignalBlocker capeBlocker(m_ui->capeCombo);
        m_ui->capeCombo->setCurrentIndex(skin ? m_capesIdx.value(skin->getCapeId()) : 0);
        const QSignalBlocker modelBlocker(m_ui->steveBtn);
        m_ui->steveBtn->setChecked(!skin || skin->getModel() == SkinModel::CLASSIC);
        m_ui->alexBtn->setChecked(skin && skin->getModel() == SkinModel::SLIM);
    }
    updatePreview();
    updateAccountActions();
}

QImage SkinManageDialog::selectedCapeImage() const
{
    if (m_selectedOutfit) {
        if (!m_selectedOutfit->capeImage.isNull())
            return m_selectedOutfit->capeImage;
        const auto id = m_selectedOutfit->capeId.value_or(m_acct ? m_acct->accountData()->minecraftProfile.currentCape : QString());
        return m_capes.value(id);
    }
    return m_capes.value(m_ui->capeCombo->currentData().toString());
}

void SkinManageDialog::updatePreview()
{
    const auto cape = selectedCapeImage();
    if (auto* skin = getSelectedSkin()) {
        if (m_skinPreview) {
            m_skinPreview->updateScene(skin);
            m_skinPreview->updateCape(cape);
        }
        m_skinPreviewLabel->setPixmap(
            QPixmap::fromImage(skin->getPreview()).scaled(m_skinPreviewLabel->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
        if (m_skinPreviewContainer)
            m_skinPreviewContainer->setVisible(isEnabled() && !m_previewFailed);
        m_skinPreviewLabel->setVisible(!m_skinPreview || m_previewFailed);
    } else {
        if (m_skinPreview) {
            QImage empty(64, 64, QImage::Format_ARGB32);
            empty.fill(Qt::transparent);
            m_skinPreview->setTexture(empty, SkinModel::CLASSIC);
            m_skinPreview->updateCape({});
        }
        if (m_skinPreviewContainer)
            m_skinPreviewContainer->hide();
        m_skinPreviewLabel->setText(tr("Import a skin PNG to start your collection."));
        m_skinPreviewLabel->show();
        m_ui->capeImage->clear();
        return;
    }
    if (cape.isNull())
        m_ui->capeImage->clear();
    else
        m_ui->capeImage->setPixmap(previewCape(cape, m_ui->elytraCB->isChecked()));
}

void SkinManageDialog::updateOutfitActions()
{
    if (!m_outfitCombo)
        return;
    const bool busy = m_capeTask && m_capeTask->isRunning();
    const bool online = m_acct && m_acct->accountType() == AccountType::MSA && m_acct->hasProfile();
    const bool ready = online && !m_acct->isActive() && !m_acct->isInUse();
    const bool ownsCape = !m_selectedOutfit || !m_selectedOutfit->capeId || m_selectedOutfit->capeId->isEmpty() ||
                          (m_acct && m_acct->accountData()->minecraftProfile.capes.contains(*m_selectedOutfit->capeId));
    m_outfitCombo->setEnabled(!busy);
    m_saveOutfit->setEnabled(!busy && getSelectedSkin());
    m_renameOutfit->setEnabled(!busy && m_selectedOutfit.has_value());
    m_deleteOutfit->setEnabled(!busy && m_selectedOutfit.has_value());
    m_applyOutfit->setEnabled(!busy && ready && ownsCape && m_selectedOutfit.has_value());
    m_ui->capeBox->setEnabled(m_acct != nullptr || m_selectedOutfit.has_value());
    if (busy)
        return;
    if (!m_selectedOutfit) {
        m_outfitStatus->setText(tr("Save a skin and optional cape together. Outfits stay in your local library."));
        return;
    }
    if (!online)
        m_outfitStatus->setText(tr("Local outfit preview · Select a Microsoft account to apply it."));
    else if (!ownsCape)
        m_outfitStatus->setText(tr("This outfit uses a cape that %1 does not own. Select an account that owns it to apply the outfit.")
                                   .arg(m_acct->profileName()));
    else if (!ready)
        m_outfitStatus->setText(tr("Close Minecraft and finish sign-in before applying this outfit."));
    else if (!m_selectedOutfit->capeId)
        m_outfitStatus->setText(tr("Apply outfit changes the skin and keeps %1’s current cape.").arg(m_acct->profileName()));
    else if (m_selectedOutfit->capeId->isEmpty())
        m_outfitStatus->setText(tr("Apply outfit changes the skin and removes %1’s cape.").arg(m_acct->profileName()));
    else
        m_outfitStatus->setText(tr("Apply outfit updates the skin and owned cape on %1’s account.").arg(m_acct->profileName()));
}

void SkinManageDialog::saveOutfit()
{
    const auto* skin = getSelectedSkin();
    if (!skin || !m_saveOutfit->isEnabled())
        return;
    SkinOutfitLibrary::Entry entry{ {}, {}, skin->getTexture(), skin->getModel(), std::nullopt, {} };
    const auto capeId = m_selectedOutfit
                            ? m_selectedOutfit->capeId.value_or(m_acct ? m_acct->accountData()->minecraftProfile.currentCape : QString())
                            : m_ui->capeCombo->currentData().toString();
    const auto capeImage = selectedCapeImage();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Save outfit"));
    dialog.resize(380, 210);
    auto* layout = new QVBoxLayout(&dialog);
    auto* label = new QLabel(tr("Outfit name"), &dialog);
    auto* name = new QLineEdit(m_selectedOutfit ? m_selectedOutfit->name : skin->name(), &dialog);
    name->setObjectName("skinOutfitName");
    name->setMaxLength(80);
    label->setBuddy(name);
    layout->addWidget(label);
    layout->addWidget(name);
    auto* includeCape = new QCheckBox(tr("Include cape selection"), &dialog);
    includeCape->setObjectName("skinOutfitIncludeCape");
    includeCape->setChecked(m_selectedOutfit ? m_selectedOutfit->capeId.has_value()
                                           : m_acct && m_acct->accountType() == AccountType::MSA && m_acct->hasProfile());
    layout->addWidget(includeCape);
    auto* help = new QLabel(tr("Includes Extras already applied in Skin Studio. A saved No Cape selection removes the equipped cape. "
                              "Leave this unchecked to keep the account’s current cape."),
                           &dialog);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(name, &QLineEdit::textChanged, buttons, [buttons](const QString& text) {
        buttons->button(QDialogButtonBox::Save)->setEnabled(!text.trimmed().isEmpty());
    });
    buttons->button(QDialogButtonBox::Save)->setEnabled(!name->text().trimmed().isEmpty());
    name->selectAll();
    if (dialog.exec() != QDialog::Accepted)
        return;
    entry.name = name->text();
    if (includeCape->isChecked()) {
        entry.capeId = capeId;
        entry.capeImage = capeId.isEmpty() ? QImage() : capeImage;
    }
    QString error;
    const auto id = m_outfits.save(entry, &error);
    if (id.isEmpty()) {
        m_outfitStatus->setText(error);
        return;
    }
    refreshOutfits(id);
    m_outfitStatus->setText(tr("Outfit saved locally. %1").arg(m_outfitStatus->text()));
}

void SkinManageDialog::renameOutfit()
{
    if (!m_selectedOutfit || !m_renameOutfit->isEnabled())
        return;
    const auto id = m_selectedOutfit->id;
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("Rename outfit"), tr("Outfit name"), QLineEdit::Normal, m_selectedOutfit->name, &ok);
    if (!ok)
        return;
    QString error;
    if (!m_outfits.rename(id, name, &error))
        m_outfitStatus->setText(error);
    else {
        refreshOutfits(id);
        m_outfitStatus->setText(tr("Outfit renamed. %1").arg(m_outfitStatus->text()));
    }
}

void SkinManageDialog::deleteOutfit()
{
    if (!m_selectedOutfit || !m_deleteOutfit->isEnabled())
        return;
    const auto id = m_selectedOutfit->id;
    if (QMessageBox::question(this, tr("Delete outfit?"),
                              tr("Delete the saved outfit “%1”? The source skin and Extras remain in your library.")
                                  .arg(m_selectedOutfit->name),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    QString error;
    if (!m_outfits.remove(id, &error))
        m_outfitStatus->setText(error);
    else {
        refreshOutfits();
        m_outfitStatus->setText(tr("Outfit deleted."));
    }
}

void SkinManageDialog::applyOutfit()
{
    // Repeat eligibility checks at the action boundary, even if the account has
    // changed since the button was enabled. SkinApplyTask also checks ownership.
    updateOutfitActions();
    if (!m_selectedOutfit || !m_applyOutfit->isEnabled())
        return;
    m_outfitUpload = std::make_unique<QTemporaryDir>();
    const auto path = m_outfitUpload->filePath("outfit.png");
    if (!m_outfitUpload->isValid() || !m_selectedOutfit->image.save(path, "PNG")) {
        m_outfitStatus->setText(tr("The outfit could not be prepared for upload. Your saved outfit is still available."));
        m_outfitUpload.reset();
        return;
    }
    const auto account = m_acct;
    m_capeTask = makeShared<SkinApplyTask>(account, path, m_selectedOutfit->model, m_selectedOutfit->capeId);
    connect(m_capeTask.get(), &Task::started, this, &SkinManageDialog::updateAccountActions);
    connect(m_capeTask.get(), &Task::status, m_outfitStatus, &QLabel::setText);
    connect(m_capeTask.get(), &Task::finished, this, [this, account] {
        m_outfitUpload.reset();
        refreshAccounts();
        if (m_selectedOutfit)
            selectOutfit();
        updateAccountActions();
        m_outfitStatus->setText(m_capeTask->wasSuccessful() ? tr("Outfit applied to %1’s Minecraft account.").arg(account->profileName())
                                                         : m_capeTask->failReason());
    });
    m_capeTask->start();
}

void SkinManageDialog::setupCapes()
{
    const QSignalBlocker blocker(m_ui->capeCombo);
    m_ui->capeCombo->clear();
    m_capes.clear();
    m_capesIdx.clear();
    m_ui->capeImage->clear();
    m_ui->capeCombo->addItem(tr("No Cape"), QVariant());
    m_ui->capeBox->setEnabled(m_acct != nullptr);
    if (m_skinPreview)
        m_skinPreview->updateCape({});
    if (!m_acct)
        return;
    // FIXME: add a model for this, download/refresh the capes on demand
    auto& accountData = *m_acct->accountData();
    int index = 0;
    auto currentCape = accountData.minecraftProfile.currentCape;
    if (currentCape.isEmpty()) {
        m_ui->capeCombo->setCurrentIndex(index);
    }

    auto capesDir = FS::PathCombine(m_list.getDir(), "capes");
    QDir().mkpath(capesDir);
    NetJob::Ptr job{ new NetJob(tr("Download capes"), APPLICATION->network()) };
    bool needsToDownload = false;
    for (auto& cape : accountData.minecraftProfile.capes) {
        auto path = FS::PathCombine(capesDir, cape.id + ".png");
        if (cape.data.size()) {
            QImage capeImage;
            if (capeImage.loadFromData(cape.data, "PNG")) {
                m_capes[cape.id] = capeImage;
                capeImage.save(path);
                continue;
            }
        }
        if (QFileInfo(path).exists()) {
            continue;
        }
        if (!cape.url.isEmpty()) {
            needsToDownload = true;
            job->addNetAction(Net::Download::makeFile(cape.url, path));
        }
    }
    if (needsToDownload) {
        ProgressDialog dlg(this);
        dlg.execWithTask(job.get());
    }
    for (auto& cape : accountData.minecraftProfile.capes) {
        index++;
        QImage capeImage = m_capes.value(cape.id);
        if (!m_capes.contains(cape.id)) {
            auto path = FS::PathCombine(capesDir, cape.id + ".png");
            if (QFileInfo(path).exists() && capeImage.load(path)) {
                m_capes[cape.id] = capeImage;
            }
        }
        if (!capeImage.isNull()) {
            m_ui->capeCombo->addItem(previewCape(capeImage, m_ui->elytraCB->isChecked()), cape.alias, cape.id);
        } else {
            m_ui->capeCombo->addItem(cape.alias, cape.id);
        }

        m_capesIdx[cape.id] = index;
    }
    m_ui->capeCombo->setCurrentIndex(m_capesIdx.value(currentCape));
}

void SkinManageDialog::on_capeCombo_currentIndexChanged(int index)
{
    Q_UNUSED(index)
    if (m_selectedOutfit) {
        updatePreview();
        return;
    }
    auto id = m_ui->capeCombo->currentData();
    auto cape = m_capes.value(id.toString(), {});
    if (!cape.isNull()) {
        m_ui->capeImage->setPixmap(
            previewCape(cape, m_ui->elytraCB->isChecked()).scaled(size() * (1. / 3), Qt::KeepAspectRatio, Qt::FastTransformation));
    } else {
        m_ui->capeImage->clear();
    }
    if (m_skinPreview) {
        m_skinPreview->updateCape(cape);
    }
    if (auto skin = getSelectedSkin(); skin) {
        skin->setCapeId(id.toString());
        if (m_skinPreview) {
            m_skinPreview->updateScene(skin);
        } else {
            m_skinPreviewLabel->setPixmap(
                QPixmap::fromImage(skin->getPreview()).scaled(m_skinPreviewLabel->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
        }
    }
    updateAccountActions();
}

void SkinManageDialog::on_steveBtn_toggled(bool checked)
{
    if (m_selectedOutfit)
        return;
    if (auto skin = getSelectedSkin(); skin) {
        skin->setModel(checked ? SkinModel::CLASSIC : SkinModel::SLIM);
        if (m_skinPreview) {
            m_skinPreview->updateScene(skin);
        } else {
            m_skinPreviewLabel->setPixmap(
                QPixmap::fromImage(skin->getPreview()).scaled(m_skinPreviewLabel->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
        }
    }
}

void SkinManageDialog::accept()
{
    if (m_capeTask && m_capeTask->isRunning())
        return;
    if (m_selectedOutfit) {
        applyOutfit();
        return;
    }
    if (!m_acct)
        return;
    auto skin = m_list.skin(m_selectedSkinKey);
    if (!skin)
        return;
    ProgressDialog prog(this);
    SkinApplyTask upload(m_acct, skin->getPath(), skin->getModel(), skin->getCapeId());
    if (prog.execWithTask(&upload) != QDialog::Accepted) {
        CustomMessageBox::selectable(this, tr("Apply Skin"), upload.failReason(), QMessageBox::Warning)->exec();
        return;
    }
    if (auto selected = m_list.skin(m_selectedSkinKey))
        selected->setURL(m_acct->accountData()->minecraftProfile.skin.url);
    QDialog::accept();
}

void SkinManageDialog::reject()
{
    if (!m_capeTask || !m_capeTask->isRunning())
        QDialog::reject();
}

void SkinManageDialog::editSelectedSkin()
{
    const auto* skin = getSelectedSkin();
    if (!skin)
        return;
    SkinEditorDialog editor(this, m_acct, *skin);
    connect(&editor, &SkinEditorDialog::skinSaved, this, [this](const QString& path, SkinModel::Model model) {
        m_selectedOutfit.reset();
        m_outfitSkin.reset();
        const QSignalBlocker blocker(m_outfitCombo);
        m_outfitCombo->setCurrentIndex(0);
        SkinModel saved(path);
        saved.setModel(model);
        m_list.updateSkin(&saved);
        m_ui->listView->setCurrentIndex(m_list.index(m_list.getSkinIndex(saved.name())));
    });
    editor.exec();
}

void SkinManageDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::EnabledChange && m_skinPreviewContainer)
        m_skinPreviewContainer->setVisible(isEnabled() && !m_previewFailed && getSelectedSkin());
}

void SkinManageDialog::on_resetBtn_clicked()
{
    if (!m_acct || m_acct->accountType() != AccountType::MSA || m_acct->isActive() || m_acct->isInUse()) {
        CustomMessageBox::selectable(this, tr("Reset skin"),
                                     tr("Close Minecraft and wait for account sign-in to finish before resetting your skin."),
                                     QMessageBox::Warning)
            ->exec();
        return;
    }
    if (QMessageBox::question(this, tr("Reset Minecraft skin?"),
                              tr("Restore the account’s default Minecraft skin? Your local skin library is kept."),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    ProgressDialog prog(this);
    if (m_acct->shouldRefresh()) {
        auto refresh = m_acct->refresh();
        if (prog.execWithTask(refresh.get()) != QDialog::Accepted)
            return;
    }
    NetJob::Ptr skinReset{ new NetJob(tr("Reset skin"), APPLICATION->network(), 1) };
    skinReset->addNetAction(SkinDelete::make(m_acct->accessToken()));
    skinReset->addTask(m_acct->refresh().staticCast<Task>());
    if (prog.execWithTask(skinReset.get()) != QDialog::Accepted) {
        CustomMessageBox::selectable(this, tr("Skin Delete"), tr("Failed to delete current skin!"), QMessageBox::Warning)->exec();
        return;
    }
    QDialog::accept();
}

void SkinManageDialog::show_context_menu(const QPoint& pos)
{
    if (!canManageLibrarySelection())
        return;
    QMenu myMenu(tr("Context menu"), this);
    myMenu.addAction(m_ui->action_Rename_Skin);
    myMenu.addAction(m_ui->action_Delete_Skin);

    myMenu.exec(m_ui->listView->mapToGlobal(pos));
}

bool SkinManageDialog::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_ui->listView) {
        if (ev->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(ev);
            switch (keyEvent->key()) {
                case Qt::Key_Delete:
                    on_action_Delete_Skin_triggered(false);
                    return true;
                case Qt::Key_F2:
                    on_action_Rename_Skin_triggered(false);
                    return true;
                default:
                    break;
            }
        }
    }
    return QDialog::eventFilter(obj, ev);
}

void SkinManageDialog::on_action_Rename_Skin_triggered(bool)
{
    if (canManageLibrarySelection()) {
        m_ui->listView->edit(m_ui->listView->currentIndex());
    }
}

void SkinManageDialog::on_action_Delete_Skin_triggered(bool)
{
    if (!canManageLibrarySelection())
        return;
    const auto selectedKey = m_selectedSkinKey;

    if (m_list.getSkinIndex(m_selectedSkinKey) == m_list.getSelectedAccountSkin()) {
        CustomMessageBox::selectable(this, tr("Delete error"), tr("Can not delete skin that is in use."), QMessageBox::Warning)->exec();
        return;
    }

    auto skin = m_list.skin(m_selectedSkinKey);
    if (!skin)
        return;

    auto response = CustomMessageBox::selectable(this, tr("Confirm Deletion"),
                                                 tr("You are about to delete \"%1\".\n"
                                                    "Are you sure?")
                                                     .arg(skin->name()),
                                                 QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                        ->exec();

    if (response == QMessageBox::Yes && canManageLibrarySelection() && m_selectedSkinKey == selectedKey &&
        m_list.getSkinIndex(selectedKey) != m_list.getSelectedAccountSkin()) {
        if (!m_list.deleteSkin(selectedKey, true)) {
            m_list.deleteSkin(selectedKey, false);
        }
    }
}

bool SkinManageDialog::canManageLibrarySelection() const
{
    const auto index = m_ui->listView->currentIndex();
    return !m_selectedOutfit && !(m_capeTask && m_capeTask->isRunning()) && index.isValid() &&
           m_ui->listView->selectionModel()->isSelected(index) && index.data(Qt::UserRole).toString() == m_selectedSkinKey &&
           m_list.skin(m_selectedSkinKey);
}

void SkinManageDialog::on_urlBtn_clicked()
{
    auto url = QUrl(m_ui->urlLine->text());
    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http") || url.host().isEmpty()) {
        CustomMessageBox::selectable(this, tr("Invalid URL"), tr("Enter a complete HTTP or HTTPS URL for a skin PNG."),
                                     QMessageBox::Critical)
            ->show();
        return;
    }

    NetJob::Ptr job{ new NetJob(tr("Download skin"), APPLICATION->network()) };
    job->setAskRetry(false);

    QTemporaryDir download;
    if (!download.isValid()) {
        CustomMessageBox::selectable(this, tr("Import skin"), tr("Could not create a temporary download folder."), QMessageBox::Critical)
            ->show();
        return;
    }
    auto path = download.filePath("skin.png");
    job->addNetAction(Net::Download::makeFile(url, path));
    ProgressDialog dlg(this);
    if (dlg.execWithTask(job.get()) != QDialog::Accepted) {
        CustomMessageBox::selectable(this, tr("Import skin"), tr("The skin could not be downloaded. Check the URL and your connection."),
                                     QMessageBox::Warning)
            ->show();
        return;
    }
    const auto name = QFileInfo(url.fileName()).completeBaseName();
    auto error = m_list.installSkin(path, name.isEmpty() ? "Imported skin.png" : name + ".png");
    if (!error.isEmpty()) {
        CustomMessageBox::selectable(this, tr("Import skin"), error, QMessageBox::Warning)->show();
        return;
    }
    m_ui->urlLine->setText("");
}

class WaitTask : public Task {
   public:
    WaitTask() : m_loop(), m_done(false) {};
    virtual ~WaitTask() = default;

   public slots:
    void quit()
    {
        m_done = true;
        m_loop.quit();
    }

   protected:
    virtual void executeTask()
    {
        if (!m_done)
            m_loop.exec();
        emitSucceeded();
    };

   private:
    QEventLoop m_loop;
    bool m_done;
};

void SkinManageDialog::on_userBtn_clicked()
{
    auto user = m_ui->urlLine->text().trimmed();
    if (user.isEmpty()) {
        return;
    }
    if (!QRegularExpression("^[A-Za-z0-9_]{1,16}$").match(user).hasMatch()) {
        CustomMessageBox::selectable(this, tr("Import user skin"),
                                     tr("Enter a Minecraft username using letters, numbers and underscores (up to 16 characters)."),
                                     QMessageBox::Warning)
            ->show();
        return;
    }
    MinecraftProfile mcProfile;
    QTemporaryDir download;
    if (!download.isValid())
        return;
    auto path = download.filePath(user + ".png");

    NetJob::Ptr job{ new NetJob(tr("Download user skin"), APPLICATION->network(), 1) };
    job->setAskRetry(false);

    auto uuidOut = std::make_shared<QByteArray>();
    auto profileOut = std::make_shared<QByteArray>();

    auto uuidLoop = makeShared<WaitTask>();
    auto profileLoop = makeShared<WaitTask>();

    auto getUUID = Net::Download::makeByteArray("https://api.minecraftservices.com/minecraft/profile/lookup/name/" + user, uuidOut);
    auto getProfile = Net::Download::makeByteArray(QUrl(), profileOut);
    auto downloadSkin = Net::Download::makeFile(QUrl(), path);

    QString failReason;

    connect(getUUID.get(), &Task::aborted, uuidLoop.get(), &WaitTask::quit);
    connect(getUUID.get(), &Task::failed, this, [&failReason](QString reason) {
        qCritical() << "Couldn't get user UUID:" << reason;
        failReason = tr("failed to get user UUID");
    });
    connect(getUUID.get(), &Task::failed, uuidLoop.get(), &WaitTask::quit);
    connect(getProfile.get(), &Task::aborted, profileLoop.get(), &WaitTask::quit);
    connect(getProfile.get(), &Task::failed, profileLoop.get(), &WaitTask::quit);
    connect(getProfile.get(), &Task::failed, this, [&failReason](QString reason) {
        qCritical() << "Couldn't get user profile:" << reason;
        failReason = tr("failed to get user profile");
    });
    connect(downloadSkin.get(), &Task::failed, this, [&failReason](QString reason) {
        qCritical() << "Couldn't download skin:" << reason;
        failReason = tr("failed to download skin");
    });

    connect(getUUID.get(), &Task::succeeded, this, [uuidLoop, uuidOut, job, getProfile, &failReason] {
        try {
            QJsonParseError parse_error{};
            QJsonDocument doc = QJsonDocument::fromJson(*uuidOut, &parse_error);
            if (parse_error.error != QJsonParseError::NoError) {
                qWarning() << "Error while parsing JSON response from Minecraft skin service at" << parse_error.offset
                           << "reason:" << parse_error.errorString();
                failReason = tr("failed to parse get user UUID response");
                uuidLoop->quit();
                return;
            }
            const auto root = doc.object();
            auto id = root["id"].toString();
            if (!id.isEmpty()) {
                getProfile->setUrl("https://sessionserver.mojang.com/session/minecraft/profile/" + id);
            } else {
                failReason = tr("user id is empty");
                job->abort();
            }
        } catch (const Exception& e) {
            qCritical() << "Couldn't load skin json:" << e.cause();
            failReason = tr("failed to parse get user UUID response");
        }
        uuidLoop->quit();
    });

    connect(getProfile.get(), &Task::succeeded, this, [profileLoop, profileOut, job, getProfile, &mcProfile, downloadSkin, &failReason] {
        if (Parsers::parseMinecraftProfileMojang(*profileOut, mcProfile)) {
            downloadSkin->setUrl(mcProfile.skin.url);
        } else {
            failReason = tr("failed to parse get user profile response");
            job->abort();
        }
        profileLoop->quit();
    });

    job->addNetAction(getUUID);
    job->addTask(uuidLoop);
    job->addNetAction(getProfile);
    job->addTask(profileLoop);
    job->addNetAction(downloadSkin);
    ProgressDialog dlg(this);
    dlg.execWithTask(job.get());

    SkinModel s(path);
    if (!s.isValid()) {
        if (failReason.isEmpty()) {
            failReason = tr("the skin is invalid");
        }
        CustomMessageBox::selectable(this, tr("Username not found"),
                                     tr("Unable to find the skin for '%1'\n because: %2.").arg(user, failReason), QMessageBox::Critical)
            ->show();
        QFile::remove(path);
        return;
    }
    m_ui->urlLine->setText("");
    QString installedPath;
    auto error = m_list.installSkin(path, user + ".png", &installedPath);
    if (!error.isEmpty()) {
        CustomMessageBox::selectable(this, tr("Import user skin"), error, QMessageBox::Warning)->show();
        return;
    }
    s = SkinModel(installedPath);
    s.setModel(mcProfile.skin.variant.toUpper() == "SLIM" ? SkinModel::SLIM : SkinModel::CLASSIC);
    s.setURL(mcProfile.skin.url);
    if (m_capes.contains(mcProfile.currentCape)) {
        s.setCapeId(mcProfile.currentCape);
    }
    m_list.updateSkin(&s);
}

void SkinManageDialog::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    QSize s = size() * (1. / 3);

    auto cape = selectedCapeImage();
    if (!cape.isNull()) {
        m_ui->capeImage->setPixmap(previewCape(cape, m_ui->elytraCB->isChecked()).scaled(s, Qt::KeepAspectRatio, Qt::FastTransformation));
    } else {
        m_ui->capeImage->clear();
    }
    if (auto skin = getSelectedSkin(); skin && (!m_skinPreview || m_previewFailed)) {
        m_skinPreviewLabel->setPixmap(
            QPixmap::fromImage(skin->getPreview()).scaled(m_skinPreviewLabel->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    }
}

SkinModel* SkinManageDialog::getSelectedSkin()
{
    if (m_outfitSkin)
        return m_outfitSkin.get();
    const auto index = m_ui->listView->currentIndex();
    if (!index.isValid() || !m_ui->listView->selectionModel()->isSelected(index) ||
        index.data(Qt::UserRole).toString() != m_selectedSkinKey)
        return nullptr;
    if (auto skin = m_list.skin(m_selectedSkinKey); skin && skin->isValid()) {
        return skin;
    }
    return nullptr;
}

QHash<QString, QImage> SkinManageDialog::capes()
{
    auto result = m_capes;
    if (m_selectedOutfit && m_selectedOutfit->capeId && !m_selectedOutfit->capeImage.isNull())
        result.insert(*m_selectedOutfit->capeId, m_selectedOutfit->capeImage);
    return result;
}
