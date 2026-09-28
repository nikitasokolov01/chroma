// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
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

#include "NewInstanceDialog.h"
#include "Application.h"
#include "ui/pages/modplatform/ModpackProviderBasePage.h"
#include "ui/pages/modplatform/import_ftb/ImportFTBPage.h"
#include "ui_NewInstanceDialog.h"

#include <BaseVersion.h>
#include <InstanceList.h>
#include <icons/IconList.h>
#include <tasks/Task.h>

#include "IconPickerDialog.h"
#include "ProgressDialog.h"
#include "VersionSelectDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QValidator>
#include <functional>
#include <utility>

#include "ui/pages/modplatform/CustomPage.h"
#include "ui/pages/modplatform/ImportPage.h"
#include "ui/pages/modplatform/atlauncher/AtlPage.h"
#include "ui/pages/modplatform/flame/FlamePage.h"
#include "ui/pages/modplatform/legacy_ftb/Page.h"
#include "ui/pages/modplatform/modrinth/ModrinthPage.h"
#include "ui/pages/modplatform/technic/TechnicPage.h"
#include "ui/widgets/PageContainer.h"

namespace {

class ProviderHubPage : public QWidget, public BasePage {
   public:
    explicit ProviderHubPage(std::function<void(const QString&)> select, bool curseForge, QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName("providerHub");
        setProperty("chromaCatalog", true);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* scroll = new QScrollArea(this);
        scroll->setObjectName("providerHubScroll");
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        auto* content = new QWidget;
        content->setObjectName("providerHubContent");
        auto* body = new QVBoxLayout(content);
        body->setContentsMargins(2, 2, 12, 12);
        body->setSpacing(16);
        auto* heading = new QLabel(tr("Find your next world"), content);
        heading->setObjectName("providerHubTitle");
        body->addWidget(heading);
        auto* intro = new QLabel(tr("Choose a modpack service to explore its collection."), content);
        intro->setWordWrap(true);
        body->addWidget(intro);
        m_grid = new QGridLayout;
        m_grid->setSpacing(14);
        body->addLayout(m_grid);

        struct Service {
            QString id;
            QString name;
            QString icon;
            QString description;
            bool enabled = true;
        };
        const QList<Service> services{
            { "modrinth", tr("Modrinth"), "modrinth", tr("Discover community-made worlds and new ways to play.") },
            { "flame", tr("CurseForge"), "flame", tr("Explore an extensive collection of Minecraft modpacks."), curseForge },
            { "atl", tr("ATLauncher"), "atlauncher", tr("Find adventures, technical packs, and familiar favorites.") },
            { "technic", tr("Technic"), "technic", tr("Browse the Technic community's modpack collection.") },
            { "legacy_ftb", tr("FTB Legacy"), "ftb_logo", tr("Revisit classic Feed The Beast packs and private packs.") },
        };
        for (const auto& service : services) {
            auto* card = new QPushButton(content);
            card->setObjectName("providerCard_" + service.id);
            card->setProperty("role", "providerCard");
            card->setAccessibleName(service.name);
            card->setAccessibleDescription(service.enabled ? service.description : tr("Unavailable in this build"));
            card->setCursor(Qt::PointingHandCursor);
            card->setMinimumHeight(202);
            card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            card->setEnabled(service.enabled);
            card->setAutoDefault(true);
            auto* cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(18, 18, 18, 18);
            cardLayout->setSpacing(8);
            auto* artwork = new QLabel(card);
            artwork->setPixmap(QIcon::fromTheme(service.icon).pixmap(44, 44));
            artwork->setFixedHeight(44);
            auto* name = new QLabel(service.name, card);
            name->setObjectName("providerName");
            auto* description = new QLabel(service.description, card);
            description->setWordWrap(true);
            description->setMinimumHeight(36);
            auto* action = new QLabel(service.enabled ? tr("Browse packs  →") : tr("Unavailable in this build"), card);
            action->setObjectName("providerAction");
            for (auto* label : { artwork, name, description, action }) {
                label->setAttribute(Qt::WA_TransparentForMouseEvents);
                cardLayout->addWidget(label);
            }
            m_cards.append(card);
            connect(card, &QPushButton::clicked, this, [select, id = service.id] { select(id); });
        }
        auto* ownHeading = new QLabel(tr("Start with your own setup"), content);
        ownHeading->setObjectName("providerSectionTitle");
        body->addWidget(ownHeading);
        m_localGrid = new QGridLayout;
        m_localGrid->setSpacing(8);
        struct LocalOption {
            QString objectName;
            QString page;
            QString label;
            QString icon;
        };
        for (const auto& option : QList<LocalOption>{
                 { "createCustomInstance", "vanilla", tr("Create a custom instance"), "minecraft" },
                 { "importModpackFile", "import", tr("Import a modpack file or link"), "folder" },
                 { "importFtbInstance", "import_ftb", tr("Import from the FTB App"), "ftb_logo" },
             }) {
            auto* button = new QPushButton(QIcon::fromTheme(option.icon), option.label, content);
            button->setObjectName(option.objectName);
            button->setAutoDefault(true);
            button->setCursor(Qt::PointingHandCursor);
            m_localButtons.append(button);
            connect(button, &QPushButton::clicked, this, [select, page = option.page] { select(page); });
        }
        body->addLayout(m_localGrid);
        body->addStretch();
        scroll->setWidget(content);
        layout->addWidget(scroll);
        setStyleSheet(QStringLiteral(R"(
            QWidget#providerHub, QWidget#providerHubContent, QScrollArea#providerHubScroll { background: palette(base); border: 0; }
            QWidget#providerHub QLabel { background: transparent; }
            QLabel#providerHubTitle { font-size: 28px; font-weight: 700; }
            QLabel#providerSectionTitle { font-size: 18px; font-weight: 600; margin-top: 8px; }
            QPushButton[role="providerCard"] { background: palette(alternate-base); border: 1px solid palette(mid); border-radius: 14px; padding: 0; text-align: left; }
            QPushButton[role="providerCard"]:hover, QPushButton[role="providerCard"]:focus { border: 2px solid palette(highlight); }
            QPushButton[role="providerCard"]:disabled { background: palette(window); }
            QLabel#providerName { font-size: 18px; font-weight: 700; }
            QLabel#providerAction { color: palette(link); font-weight: 600; }
            QWidget#providerHub QLabel:disabled { color: palette(placeholder-text); }
        )"));
        updateGrid();
    }

    QString id() const override { return "providers"; }
    QString displayName() const override { return tr("Browse modpacks"); }
    QIcon icon() const override { return QIcon::fromTheme("view-grid"); }

   protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        updateGrid();
    }

   private:
    void updateGrid()
    {
        const int columns = width() >= 820 ? 3 : 2;
        if (columns == m_columns)
            return;
        m_columns = columns;
        for (auto* card : m_cards)
            m_grid->removeWidget(card);
        for (int i = 0; i < 3; ++i)
            m_grid->setColumnStretch(i, i < columns ? 1 : 0);
        for (int i = 0; i < m_cards.size(); ++i)
            m_grid->addWidget(m_cards[i], i / columns, i % columns);
        for (auto* button : m_localButtons)
            m_localGrid->removeWidget(button);
        for (int i = 0; i < m_localButtons.size(); ++i)
            m_localGrid->addWidget(m_localButtons[i], columns == 3 ? 0 : i, columns == 3 ? i : 0);
    }
    QGridLayout* m_grid;
    QGridLayout* m_localGrid;
    QList<QPushButton*> m_cards;
    QList<QPushButton*> m_localButtons;
    int m_columns = 0;
};

}  // namespace

NewInstanceDialog::NewInstanceDialog(const QString& initialGroup,
                                     const QString& url,
                                     const QMap<QString, QString>& extra_info,
                                     QWidget* parent)
    : QDialog(parent), ui(new Ui::NewInstanceDialog)
{
    ui->setupUi(this);
    setObjectName("newInstanceDialog");
    setProperty("chromaOwnsScrolling", true);
    setWindowTitle(tr("Browse modpacks"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->verticalLayout->setContentsMargins(20, 16, 20, 16);
    ui->verticalLayout->setSpacing(14);
    ui->gridLayout_2->setHorizontalSpacing(14);
    ui->gridLayout_2->setVerticalSpacing(6);
    ui->iconButton->setAccessibleName(tr("Choose instance icon"));
    ui->iconButton->setToolTip(tr("Choose instance icon"));
    ui->instNameTextBox->setAccessibleName(tr("Instance name"));
    ui->groupBox->setAccessibleName(tr("Instance group"));
    ui->groupBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ui->groupBox->setMinimumContentsLength(8);
    ui->line->hide();
    ui->newInstanceDescription->hide();

    // Instance naming is secondary to choosing a pack. Keep it in a collapsible
    // footer on catalogs, and visible for custom instances and local imports.
    ui->verticalLayout->removeItem(ui->gridLayout_2);
    ui->gridLayout_2->setParent(nullptr);
    m_instanceOptions = new QWidget(this);
    m_instanceOptions->setObjectName("instanceOptions");
    m_instanceOptions->setLayout(ui->gridLayout_2);
    m_instanceOptions->hide();

    m_providerBar = new QWidget(this);
    m_providerBar->setObjectName("providerNavigation");
    auto* providerLayout = new QHBoxLayout(m_providerBar);
    providerLayout->setContentsMargins(0, 0, 0, 0);
    providerLayout->setSpacing(12);
    auto* allProviders = new QPushButton(QIcon::fromTheme("go-previous"), tr("All providers"), m_providerBar);
    allProviders->setObjectName("browseProvidersButton");
    allProviders->setAutoDefault(false);
    connect(allProviders, &QPushButton::clicked, this, &NewInstanceDialog::showProviderHub);
    providerLayout->addWidget(allProviders);
    m_providerSelector = new QComboBox(m_providerBar);
    m_providerSelector->setObjectName("providerSelector");
    m_providerSelector->setAccessibleName(tr("Modpack service"));
    m_providerSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_providerSelector->setMinimumContentsLength(12);
    providerLayout->addWidget(m_providerSelector, 1);
    ui->verticalLayout->insertWidget(0, m_providerBar);

    setWindowIcon(QIcon::fromTheme("new"));

    InstIconKey = "default";
    ui->iconButton->setIcon(APPLICATION->icons()->getIcon(InstIconKey));

    QStringList groups = APPLICATION->instances()->getGroups();
    groups.prepend("");
    int index = groups.indexOf(initialGroup);
    if (index == -1) {
        index = 1;
        groups.insert(index, initialGroup);
    }
    ui->groupBox->addItems(groups);
    ui->groupBox->setCurrentIndex(index);
    ui->groupBox->lineEdit()->setPlaceholderText(tr("No group"));

    // NOTE: m_buttons must be initialized before PageContainer, because it indirectly accesses m_buttons through setSuggestedPack! Do not
    // move this below.
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Help | QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    m_container = new PageContainer(this, "providers", this);
    m_container->useSidebarStyle(false);
    m_container->hidePageList();
    m_container->setHeaderVisible(false);
    m_container->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
    m_container->layout()->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout->addWidget(m_container, 1);

    m_selectionSummary = new QWidget(this);
    m_selectionSummary->setObjectName("selectedPackSummary");
    auto* summaryLayout = new QHBoxLayout(m_selectionSummary);
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    m_selectedPackLabel = new QLabel(m_selectionSummary);
    m_selectedPackLabel->setObjectName("selectedPackName");
    m_selectedPackLabel->setTextFormat(Qt::PlainText);
    m_selectedPackLabel->setWordWrap(true);
    m_selectedPackLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    summaryLayout->addWidget(m_selectedPackLabel, 1);
    m_optionsButton = new QPushButton(tr("Instance options"), m_selectionSummary);
    m_optionsButton->setObjectName("instanceOptionsButton");
    m_optionsButton->setCheckable(true);
    m_optionsButton->setAutoDefault(false);
    summaryLayout->addWidget(m_optionsButton);
    connect(m_optionsButton, &QPushButton::toggled, this, &NewInstanceDialog::updateBrowseState);
    m_container->addButtons(m_instanceOptions);
    m_container->addButtons(m_selectionSummary);
    m_container->addButtons(m_buttons);

    for (const auto& id : { "modrinth", "flame", "atl", "technic", "legacy_ftb", "vanilla", "import", "import_ftb" }) {
        if (auto* page = m_container->getPage(id))
            m_providerSelector->addItem(page->icon(), page->displayName(), page->id());
    }
    connect(m_providerSelector, qOverload<int>(&QComboBox::activated), this,
            [this](int index) { selectProvider(m_providerSelector->itemData(index).toString()); });
    connect(m_container, &PageContainer::selectedPageChanged, this, &NewInstanceDialog::selectedPageChanged);

    // Bonk Qt over its stupid head and make sure it understands which button is the default one...
    // See: https://stackoverflow.com/questions/24556831/qbuttonbox-set-default-button
    auto OkButton = m_buttons->button(QDialogButtonBox::Ok);
    OkButton->setDefault(true);
    OkButton->setAutoDefault(true);
    OkButton->setText(tr("Create instance"));
    OkButton->setObjectName("createInstanceButton");
    OkButton->setProperty("role", "primary");
    connect(OkButton, &QPushButton::clicked, this, &NewInstanceDialog::accept);

    auto CancelButton = m_buttons->button(QDialogButtonBox::Cancel);
    CancelButton->setDefault(false);
    CancelButton->setAutoDefault(false);
    CancelButton->setText(tr("Cancel"));
    connect(CancelButton, &QPushButton::clicked, this, &NewInstanceDialog::reject);

    auto HelpButton = m_buttons->button(QDialogButtonBox::Help);
    HelpButton->setDefault(false);
    HelpButton->setAutoDefault(false);
    HelpButton->setText(tr("Help"));
    connect(HelpButton, &QPushButton::clicked, m_container, &PageContainer::help);

    setStyleSheet(QStringLiteral(R"(
        QDialog#newInstanceDialog { background: palette(base); }
        QDialog#newInstanceDialog QLabel { background: transparent; }
        QDialog#newInstanceDialog QLineEdit#instNameTextBox, QDialog#newInstanceDialog QComboBox#groupBox { background: palette(button); color: palette(text); border: 1px solid palette(mid); border-radius: 8px; padding: 7px 10px; min-height: 24px; }
        QDialog#newInstanceDialog QLineEdit#instNameTextBox:focus, QDialog#newInstanceDialog QComboBox#groupBox:focus { border-color: palette(highlight); }
        QDialog#newInstanceDialog QToolButton#iconButton { background: palette(alternate-base); border: 1px solid palette(mid); border-radius: 12px; padding: 9px; }
        QDialog#newInstanceDialog QToolButton#iconButton:hover, QDialog#newInstanceDialog QToolButton#iconButton:focus { border-color: palette(highlight); }
        QLabel#providerHubTitle { font-size: 28px; font-weight: 700; }
        QLabel#providerSectionTitle { font-size: 18px; font-weight: 600; margin-top: 8px; }
        QPushButton[role="providerCard"] { background: palette(alternate-base); border: 1px solid palette(mid); border-radius: 14px; padding: 0; text-align: left; }
        QPushButton[role="providerCard"]:hover, QPushButton[role="providerCard"]:focus { border: 2px solid palette(highlight); }
        QPushButton[role="providerCard"]:disabled { background: palette(window); color: palette(placeholder-text); }
        QLabel#providerName { font-size: 18px; font-weight: 700; }
        QLabel#providerAction { color: palette(link); font-weight: 600; }
        QLabel#selectedPackName { font-weight: 600; }
    )"));

    if (!url.isEmpty()) {
        selectProvider("import");
        importPage->setUrl(url);
        importPage->setExtraInfo(extra_info);
    }

    updateDialogState();

    if (APPLICATION->settings()->get("NewInstanceGeometry").isValid()) {
        restoreGeometry(QByteArray::fromBase64(APPLICATION->settings()->get("NewInstanceGeometry").toString().toUtf8()));
    } else {
        if (auto activeScreen = parent ? parent->screen() : QApplication::primaryScreen()) {
            const auto available = activeScreen->availableSize();
            resize(qMin(960, available.width() - 60), qMin(available.height() - 70, 740));
        }
    }

    updateHeaderLayout();

    updateBrowseState();
}

void NewInstanceDialog::reject()
{
    APPLICATION->settings()->set("NewInstanceGeometry", QString::fromUtf8(saveGeometry().toBase64()));

    // This is just so that the pages get the close() call and can react to it, if needed.
    m_container->prepareToClose();

    QDialog::reject();
}

void NewInstanceDialog::accept()
{
    if (!creationTask || instName().isEmpty())
        return;
    APPLICATION->settings()->set("NewInstanceGeometry", QString::fromUtf8(saveGeometry().toBase64()));
    importIconNow();

    // This is just so that the pages get the close() call and can react to it, if needed.
    m_container->prepareToClose();

    QDialog::accept();
}

QList<BasePage*> NewInstanceDialog::getPages()
{
    QList<BasePage*> pages;

    pages.append(new ProviderHubPage([this](const QString& id) { selectProvider(id); },
                                     APPLICATION->capabilities() & Application::SupportsFlame, this));

    importPage = new ImportPage(this);

    pages.append(new CustomPage(this));
    pages.append(importPage);
    pages.append(new AtlPage(this));
    if (APPLICATION->capabilities() & Application::SupportsFlame)
        pages.append(new FlamePage(this));
    pages.append(new LegacyFTB::Page(this));
    pages.append(new FTBImportAPP::ImportFTBPage(this));
    pages.append(new ModrinthPage(this));
    pages.append(new TechnicPage(this));

    return pages;
}

QString NewInstanceDialog::dialogTitle()
{
    return tr("Browse modpacks");
}

NewInstanceDialog::~NewInstanceDialog()
{
    delete ui;
}

void NewInstanceDialog::setSuggestedPack(const QString& name, InstanceTask* task)
{
    creationTask.reset(task);

    ui->instNameTextBox->setPlaceholderText(name);
    importVersion.clear();

    if (!task) {
        ui->iconButton->setIcon(APPLICATION->icons()->getIcon(InstIconKey));
        importIcon = false;
    }

    updateDialogState();
}

void NewInstanceDialog::setSuggestedPack(const QString& name, QString version, InstanceTask* task)
{
    creationTask.reset(task);

    ui->instNameTextBox->setPlaceholderText(name);
    importVersion = std::move(version);

    if (!task) {
        ui->iconButton->setIcon(APPLICATION->icons()->getIcon(InstIconKey));
        importIcon = false;
    }

    updateDialogState();
}

void NewInstanceDialog::setSuggestedIconFromFile(const QString& path, const QString& name)
{
    importIcon = true;
    importIconPath = path;
    importIconName = name;

    // Hmm, for some reason they can be to small
    ui->iconButton->setIcon(QIcon(path));
}

void NewInstanceDialog::setSuggestedIcon(const QString& key)
{
    if (key == "default")
        return;

    auto icon = APPLICATION->icons()->getIcon(key);
    importIcon = false;

    ui->iconButton->setIcon(icon);
}

InstanceTask* NewInstanceDialog::extractTask()
{
    InstanceTask* extracted = creationTask.release();
    if (!extracted)
        return nullptr;

    InstanceName inst_name(ui->instNameTextBox->placeholderText().trimmed(), importVersion);
    inst_name.setName(ui->instNameTextBox->text().trimmed());
    extracted->setName(inst_name);

    extracted->setGroup(instGroup());
    extracted->setIcon(iconKey());
    return extracted;
}

void NewInstanceDialog::updateDialogState()
{
    auto allowOK = creationTask && !instName().isEmpty();
    auto OkButton = m_buttons->button(QDialogButtonBox::Ok);
    if (OkButton->isEnabled() != allowOK) {
        OkButton->setEnabled(allowOK);
    }
    updateBrowseState();
}

QString NewInstanceDialog::instName() const
{
    auto result = ui->instNameTextBox->text().trimmed();
    if (result.size()) {
        return result;
    }
    result = ui->instNameTextBox->placeholderText().trimmed();
    if (result.size()) {
        return result;
    }
    return QString();
}

QString NewInstanceDialog::instGroup() const
{
    return ui->groupBox->currentText();
}
QString NewInstanceDialog::iconKey() const
{
    return InstIconKey;
}

void NewInstanceDialog::on_iconButton_clicked()
{
    importIconNow();  // so the user can switch back
    IconPickerDialog dlg(this);
    dlg.execWithSelection(InstIconKey);

    if (dlg.result() == QDialog::Accepted) {
        InstIconKey = dlg.selectedIconKey;
        ui->iconButton->setIcon(APPLICATION->icons()->getIcon(InstIconKey));
        importIcon = false;
    }
}

void NewInstanceDialog::on_instNameTextBox_textChanged([[maybe_unused]] const QString& arg1)
{
    updateDialogState();
}

void NewInstanceDialog::importIconNow()
{
    if (importIcon) {
        APPLICATION->icons()->installIcon(importIconPath, importIconName);
        InstIconKey = importIconName.mid(0, importIconName.lastIndexOf('.'));
        importIcon = false;
    }
    APPLICATION->settings()->set("NewInstanceGeometry", QString::fromUtf8(saveGeometry().toBase64()));
}

void NewInstanceDialog::selectedPageChanged(BasePage* previous, BasePage* selected)
{
    auto prevPage = dynamic_cast<ModpackProviderBasePage*>(previous);
    if (prevPage) {
        m_searchTerm = prevPage->getSerachTerm();
    }

    auto nextPage = dynamic_cast<ModpackProviderBasePage*>(selected);
    if (nextPage) {
        nextPage->setSearchTerm(m_searchTerm);
    }
    m_providerId = selected ? selected->id() : QString("providers");
    // A previous service's ready task must never survive a provider switch.
    setSuggestedPack();
    m_optionsButton->setChecked(false);
    const QSignalBlocker blocker(m_providerSelector);
    m_providerSelector->setCurrentIndex(m_providerSelector->findData(m_providerId));
    updateBrowseState();
}

void NewInstanceDialog::selectProvider(const QString& id)
{
    if (!m_container)
        return;
    auto* page = m_container->getPage(id);
    if (!page || !page->shouldDisplay())
        return;
    m_container->selectPage(id);
}

void NewInstanceDialog::showProviderHub()
{
    selectProvider("providers");
}

void NewInstanceDialog::updateBrowseState()
{
    if (!m_optionsButton)
        return;
    const bool hub = m_providerId == "providers";
    const bool local = m_providerId == "vanilla" || m_providerId == "import";
    m_providerBar->setVisible(!hub);
    m_buttons->setVisible(!hub);
    m_buttons->button(QDialogButtonBox::Ok)->setText(local ? tr("Create instance") : tr("Install modpack"));
    m_selectionSummary->setVisible(!hub && !local && bool(creationTask));
    m_selectedPackLabel->setText(instName());
    m_instanceOptions->setVisible(!hub && (local || (creationTask && m_optionsButton->isChecked())));
}

void NewInstanceDialog::updateHeaderLayout()
{
    const bool compact = width() < 480;
    if (ui->gridLayout_2->count() == 5 && compact == m_compactHeader && ui->gridLayout_2->columnStretch(1) > 0)
        return;
    m_compactHeader = compact;
    for (auto widget :
         { static_cast<QWidget*>(ui->iconButton), static_cast<QWidget*>(ui->nameLabel), static_cast<QWidget*>(ui->instNameTextBox),
           static_cast<QWidget*>(ui->groupLabel), static_cast<QWidget*>(ui->groupBox) })
        ui->gridLayout_2->removeWidget(widget);
    if (compact) {
        ui->gridLayout_2->addWidget(ui->iconButton, 0, 0, 4, 1, Qt::AlignTop);
        ui->gridLayout_2->addWidget(ui->nameLabel, 0, 1);
        ui->gridLayout_2->addWidget(ui->instNameTextBox, 1, 1);
        ui->gridLayout_2->addWidget(ui->groupLabel, 2, 1);
        ui->gridLayout_2->addWidget(ui->groupBox, 3, 1);
    } else {
        ui->gridLayout_2->addWidget(ui->iconButton, 0, 0, 2, 1, Qt::AlignTop);
        ui->gridLayout_2->addWidget(ui->nameLabel, 0, 1);
        ui->gridLayout_2->addWidget(ui->instNameTextBox, 1, 1);
        ui->gridLayout_2->addWidget(ui->groupLabel, 0, 2);
        ui->gridLayout_2->addWidget(ui->groupBox, 1, 2);
    }
    ui->gridLayout_2->setColumnStretch(1, 2);
    ui->gridLayout_2->setColumnStretch(2, compact ? 0 : 1);
}

void NewInstanceDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updateHeaderLayout();
}
