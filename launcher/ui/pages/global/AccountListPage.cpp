// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "AccountListPage.h"
#include "ui/dialogs/skins/SkinManageDialog.h"
#include "ui_AccountListPage.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <QDebug>

#include "ui/dialogs/ChooseOfflineNameDialog.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/MSALoginDialog.h"
#include "ui/pages/BasePageContainer.h"
#include "ui/pages/global/APIPage.h"
#include "ui/widgets/ClayWidgets.h"

#include "Application.h"

AccountListPage::AccountListPage(QWidget* parent) : QMainWindow(parent), ui(new Ui::AccountListPage)
{
    ui->setupUi(this);
    auto* profile = new ClayPanel(ui->centralwidget);
    profile->setObjectName("accountProfile");
    auto* profileLayout = new QVBoxLayout(profile);
    profileLayout->setContentsMargins(18, 18, 18, 18);
    profileLayout->setSpacing(18);
    m_profileFace = new QLabel(profile);
    m_profileFace->setFixedSize(80, 80);
    m_profileFace->setAlignment(Qt::AlignCenter);
    m_profileFace->setAccessibleName(tr("Minecraft account avatar"));
    auto* identity = new QHBoxLayout;
    identity->setSpacing(18);
    identity->addWidget(m_profileFace, 0, Qt::AlignTop);
    auto* details = new QVBoxLayout;
    details->setSpacing(7);
    m_profileName = new QLabel(profile);
    m_profileName->setObjectName("accountProfileName");
    m_profileName->setTextFormat(Qt::PlainText);
    m_profileName->setWordWrap(true);
    auto nameFont = m_profileName->font();
    nameFont.setPointSize(nameFont.pointSize() + 6);
    nameFont.setBold(true);
    m_profileName->setFont(nameFont);
    m_profileStatus = new QLabel(profile);
    m_profileStatus->setObjectName("accountProfileStatus");
    m_profileStatus->setTextFormat(Qt::PlainText);
    m_profileStatus->setWordWrap(true);
    m_profileUuid = new QLabel(profile);
    m_profileUuid->setObjectName("accountProfileUuid");
    m_profileUuid->setTextFormat(Qt::PlainText);
    m_profileUuid->setWordWrap(true);
    m_profileUuid->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    details->addWidget(m_profileName);
    details->addWidget(m_profileStatus);
    details->addWidget(m_profileUuid);
    auto* actions = new QHBoxLayout;
    m_customize = new QPushButton(tr("Customize Skin…"), profile);
    m_customize->setObjectName("accountCustomizeSkin");
    m_useAccount = new QPushButton(tr("Use Account"), profile);
    m_copyUuid = new QPushButton(tr("Copy UUID"), profile);
    for (auto* button : { m_customize, m_useAccount, m_copyUuid }) {
        button->setFixedHeight(32);
        button->setAutoDefault(false);
        actions->addWidget(button);
    }
    actions->addStretch();
    identity->addLayout(details, 1);
    profileLayout->addLayout(identity);
    profileLayout->addLayout(actions);
    ui->verticalLayout->insertWidget(0, profile);
    m_compactActions = new QWidget(ui->centralwidget);
    m_compactActions->setObjectName("compactAccountActions");
    auto* compactLayout = new QHBoxLayout(m_compactActions);
    compactLayout->setContentsMargins(0, 4, 0, 4);
    auto* add = new QPushButton(tr("Add Account"), m_compactActions);
    add->setFixedHeight(32);
    auto* addMenu = new QMenu(add);
    addMenu->addAction(ui->actionAddMicrosoft);
    addMenu->addAction(ui->actionAddOffline);
    add->setMenu(addMenu);
    compactLayout->addWidget(add);
    auto* refresh = new QToolButton(m_compactActions);
    refresh->setFixedHeight(32);
    refresh->setToolButtonStyle(Qt::ToolButtonTextOnly);
    refresh->setDefaultAction(ui->actionRefresh);
    compactLayout->addWidget(refresh);
    auto* more = new QPushButton(tr("Account Actions"), m_compactActions);
    more->setFixedHeight(32);
    auto* moreMenu = new QMenu(more);
    for (auto* action : { ui->actionSetDefault, ui->actionNoDefault, ui->actionManageSkins, ui->actionRemove })
        moreMenu->addAction(action);
    more->setMenu(moreMenu);
    compactLayout->addWidget(more);
    compactLayout->addStretch();
    ui->verticalLayout->insertWidget(1, m_compactActions);
    connect(m_customize, &QPushButton::clicked, this, &AccountListPage::on_actionManageSkins_triggered);
    connect(m_useAccount, &QPushButton::clicked, this, &AccountListPage::on_actionSetDefault_triggered);
    connect(m_copyUuid, &QPushButton::clicked, this, [this] {
        const auto selection = ui->listView->selectionModel()->selectedIndexes();
        if (!selection.isEmpty()) {
            const auto account = selection.first().data(AccountList::PointerRole).value<MinecraftAccountPtr>();
            QGuiApplication::clipboard()->setText(account->profileId());
        }
    });
    ui->listView->setEmptyMode(VersionListView::String);
    ui->listView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_accounts = APPLICATION->accounts();

    ui->listView->setModel(m_accounts.get());
    ui->listView->header()->setSectionResizeMode(AccountList::VListColumns::ProfileNameColumn, QHeaderView::Stretch);
    ui->listView->header()->setSectionResizeMode(AccountList::VListColumns::NameColumn, QHeaderView::Stretch);
    ui->listView->header()->setSectionResizeMode(AccountList::VListColumns::TypeColumn, QHeaderView::ResizeToContents);
    ui->listView->header()->setSectionResizeMode(AccountList::VListColumns::StatusColumn, QHeaderView::ResizeToContents);
    ui->listView->setSelectionMode(QAbstractItemView::SingleSelection);

    // Expand the account column

    QItemSelectionModel* selectionModel = ui->listView->selectionModel();

    connect(selectionModel, &QItemSelectionModel::selectionChanged,
            [this]([[maybe_unused]] const QItemSelection& sel, [[maybe_unused]] const QItemSelection& dsel) { updateButtonStates(); });
    connect(ui->listView, &VersionListView::customContextMenuRequested, this, &AccountListPage::ShowContextMenu);
    connect(ui->listView, &VersionListView::activated, this,
            [this](const QModelIndex& index) { m_accounts->setDefaultAccount(m_accounts->at(index.row())); });

    connect(m_accounts.get(), &AccountList::listChanged, this, &AccountListPage::listChanged);
    connect(m_accounts.get(), &AccountList::listActivityChanged, this, &AccountListPage::listChanged);
    connect(m_accounts.get(), &AccountList::defaultAccountChanged, this, &AccountListPage::listChanged);
    connect(ui->configureMicrosoftSignInButton, &QPushButton::clicked, this, &AccountListPage::configureMicrosoftSignIn);

    ui->microsoftSignInNotice->setStyleSheet(
        "QFrame#microsoftSignInNotice { background: palette(alternate-base); border: 1px solid palette(highlight); border-radius: 8px; }"
        "QLabel#microsoftSignInTitle { font-weight: 600; }");

    if (m_accounts->count()) {
        int row = 0;
        for (int i = 0; i < m_accounts->count(); ++i)
            if (m_accounts->at(i) == m_accounts->defaultAccount())
                row = i;
        ui->listView->setCurrentIndex(m_accounts->index(row, 0));
    }
    updateButtonStates();
    updateToolbarLayout();
}

AccountListPage::~AccountListPage()
{
    delete ui;
}

void AccountListPage::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateToolbarLayout();
}

void AccountListPage::updateToolbarLayout()
{
    if (!m_compactActions)
        return;
    const bool compact = width() < 760;
    ui->toolBar->setVisible(!compact);
    m_compactActions->setVisible(compact);
}

void AccountListPage::retranslate()
{
    ui->retranslateUi(this);
    updateMicrosoftSignInState();
    m_customize->setText(tr("Customize Skin…"));
    m_useAccount->setText(tr("Use Account"));
    m_copyUuid->setText(tr("Copy UUID"));
    updateProfile();
}

void AccountListPage::openedImpl()
{
    updateMicrosoftSignInState();
}

void AccountListPage::updateMicrosoftSignInState()
{
    const bool available = !APPLICATION->getMSAClientID().trimmed().isEmpty();
    ui->actionAddMicrosoft->setVisible(true);
    ui->actionAddMicrosoft->setEnabled(available);
    ui->actionAddMicrosoft->setToolTip(available ? tr("Add a Microsoft account")
                                                 : tr("Configure a Microsoft Authentication client ID in Services to enable sign-in."));
    ui->microsoftSignInNotice->setVisible(!available);
    ui->listView->setEmptyString(available ? tr("Welcome!\nSelect \"Add Microsoft\" to link your Microsoft account.")
                                           : tr("No accounts yet.\nConfigure Microsoft sign-in to add your account."));
}

void AccountListPage::configureMicrosoftSignIn()
{
    if (!m_container)
        return;
    auto* page = dynamic_cast<APIPage*>(m_container->getPage("apis"));
    if (page && m_container->selectPage("apis"))
        page->focusMicrosoftClientId();
}

void AccountListPage::ShowContextMenu(const QPoint& pos)
{
    auto menu = ui->toolBar->createContextMenu(this, tr("Context menu"));
    menu->exec(ui->listView->mapToGlobal(pos));
    delete menu;
}

void AccountListPage::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QMainWindow::changeEvent(event);
}

QMenu* AccountListPage::createPopupMenu()
{
    QMenu* filteredMenu = QMainWindow::createPopupMenu();
    filteredMenu->removeAction(ui->toolBar->toggleViewAction());
    return filteredMenu;
}

void AccountListPage::listChanged()
{
    if (ui->listView->selectionModel()->selectedIndexes().isEmpty() && m_accounts->count())
        ui->listView->setCurrentIndex(m_accounts->index(0, 0));
    updateButtonStates();
}

void AccountListPage::on_actionAddMicrosoft_triggered()
{
    if (APPLICATION->getMSAClientID().trimmed().isEmpty()) {
        updateMicrosoftSignInState();
        return;
    }
    auto account = MSALoginDialog::newAccount(this);
    if (account) {
        m_accounts->addAccount(account);
        if (m_accounts->count() == 1) {
            m_accounts->setDefaultAccount(account);
        }
    }
}

void AccountListPage::on_actionAddOffline_triggered()
{
    if (!m_accounts->anyAccountIsValid()) {
        QMessageBox::warning(this, tr("Error"),
                             tr("You must add a Microsoft account that owns Minecraft before you can add an offline account."
                                "<br><br>"
                                "If you have lost your account you can contact Microsoft for support."));
        return;
    }

    ChooseOfflineNameDialog dialog(tr("Please enter your desired username to add your offline account."), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (const MinecraftAccountPtr account = MinecraftAccount::createOffline(dialog.getUsername())) {
        account->login()->start();  // The task will complete here.
        m_accounts->addAccount(account);
        if (m_accounts->count() == 1) {
            m_accounts->setDefaultAccount(account);
        }
    }
}

void AccountListPage::on_actionRemove_triggered()
{
    auto response = CustomMessageBox::selectable(this, tr("Remove account?"), tr("Do you really want to delete this account?"),
                                                 QMessageBox::Question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                        ->exec();
    if (response != QMessageBox::Yes) {
        return;
    }
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.size() > 0) {
        QModelIndex selected = selection.first();
        m_accounts->removeAccount(selected);
    }
}

void AccountListPage::on_actionRefresh_triggered()
{
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.size() > 0) {
        QModelIndex selected = selection.first();
        MinecraftAccountPtr account = selected.data(AccountList::PointerRole).value<MinecraftAccountPtr>();
        m_accounts->requestRefresh(account->internalId());
    }
}

void AccountListPage::on_actionSetDefault_triggered()
{
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.size() > 0) {
        QModelIndex selected = selection.first();
        MinecraftAccountPtr account = selected.data(AccountList::PointerRole).value<MinecraftAccountPtr>();
        m_accounts->setDefaultAccount(account);
    }
}

void AccountListPage::on_actionNoDefault_triggered()
{
    m_accounts->setDefaultAccount(nullptr);
}

void AccountListPage::updateButtonStates()
{
    updateMicrosoftSignInState();
    // If there is no selection, disable buttons that require something selected.
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    bool hasSelection = !selection.empty();
    bool accountIsReady = false;
    bool accountIsOnline = false;
    if (hasSelection) {
        QModelIndex selected = selection.first();
        MinecraftAccountPtr account = selected.data(AccountList::PointerRole).value<MinecraftAccountPtr>();
        accountIsReady = !account->isActive();
        accountIsOnline = account->accountType() != AccountType::Offline;
    }
    ui->actionRemove->setEnabled(accountIsReady);
    ui->actionSetDefault->setEnabled(accountIsReady);
    ui->actionManageSkins->setEnabled(accountIsReady);
    ui->actionRefresh->setEnabled(accountIsReady && accountIsOnline);

    if (m_accounts->defaultAccount().get() == nullptr) {
        ui->actionNoDefault->setEnabled(false);
        ui->actionNoDefault->setChecked(true);
    } else {
        ui->actionNoDefault->setEnabled(true);
        ui->actionNoDefault->setChecked(false);
    }
    ui->listView->resizeColumnToContents(3);
    updateProfile();
}

void AccountListPage::updateProfile()
{
    const auto selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.isEmpty()) {
        m_profileFace->setPixmap(QIcon::fromTheme("noaccount").pixmap(64, 64));
        m_profileName->setText(tr("Your Minecraft accounts"));
        m_profileStatus->setText(tr("Connect an account to see your Minecraft profile and customize its skin."));
        m_profileUuid->clear();
        m_customize->setEnabled(false);
        m_useAccount->setEnabled(false);
        m_copyUuid->setEnabled(false);
        return;
    }
    const auto index = selection.first();
    const auto account = index.data(AccountList::PointerRole).value<MinecraftAccountPtr>();
    auto face = account->getFace();
    if (face.isNull())
        face = QIcon::fromTheme("noaccount").pixmap(64, 64);
    m_profileFace->setPixmap(face.scaled(80, 80, Qt::KeepAspectRatio, Qt::FastTransformation));
    m_profileName->setText(account->profileName().isEmpty() ? account->accountDisplayString() : account->profileName());
    const auto state = m_accounts->index(index.row(), AccountList::StatusColumn).data().toString();
    const bool offline = account->accountType() == AccountType::Offline;
    const bool isDefault = account == m_accounts->defaultAccount();
    QStringList status{ offline ? tr("Offline account") : tr("Microsoft account"), state };
    if (isDefault)
        status.append(tr("Active account"));
    if (!offline) {
        const auto& entitlement = account->accountData()->minecraftEntitlement;
        status.append(account->ownsMinecraft()       ? tr("Minecraft Java owned")
                      : entitlement.canPlayMinecraft ? tr("Minecraft Java access")
                                                     : tr("Minecraft Java access unavailable"));
    }
    if (account->isInUse())
        status.append(tr("Minecraft is running"));
    const auto variant = account->accountData()->minecraftProfile.skin.variant;
    if (variant.compare("slim", Qt::CaseInsensitive) == 0)
        status.append(tr("Slim skin"));
    else if (variant.compare("classic", Qt::CaseInsensitive) == 0)
        status.append(tr("Classic skin"));
    m_profileStatus->setText(status.join(" · "));
    m_profileUuid->setText(account->hasProfile() ? tr("UUID: %1").arg(account->profileId()) : tr("No Minecraft Java profile"));
    m_copyUuid->setEnabled(account->hasProfile());
    m_useAccount->setEnabled(!account->isActive() && !isDefault);
    m_useAccount->setText(isDefault ? tr("Active Account") : tr("Use Account"));
    m_customize->setEnabled(!account->isActive());
    m_customize->setToolTip(offline ? tr("Edit and export local skins. Applying a skin requires a Microsoft account.")
                                    : tr("View, edit and apply Minecraft skins."));
}

void AccountListPage::on_actionManageSkins_triggered()
{
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.size() > 0) {
        QModelIndex selected = selection.first();
        MinecraftAccountPtr account = selected.data(AccountList::PointerRole).value<MinecraftAccountPtr>();
        SkinManageDialog dialog(this, account);
        dialog.exec();
    }
}
