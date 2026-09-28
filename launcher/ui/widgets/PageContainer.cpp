// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
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

#include "PageContainer.h"
#include "BuildConfig.h"
#include "PageContainer_p.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QStackedLayout>
#include <QStyledItemDelegate>
#include <QUrl>
#include <QVBoxLayout>

#include "settings/SettingsObject.h"

#include "ui/themes/ClayStyle.h"
#include "ui/widgets/ClayWidgets.h"
#include "ui/widgets/IconLabel.h"
#include "ui/widgets/SmoothScroll.h"

#include "Application.h"
#include "DesktopServices.h"

class PageEntryFilterModel : public QSortFilterProxyModel {
   public:
    explicit PageEntryFilterModel(QObject* parent = 0) : QSortFilterProxyModel(parent) {}

   protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
    {
        const QString pattern = filterRegularExpression().pattern();
        const auto model = static_cast<PageModel*>(sourceModel());
        const auto page = model->pages().at(sourceRow);
        if (!page->shouldDisplay())
            return false;
        // Regular contents check, then check page-filter.
        return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
    }
};

PageContainer::PageContainer(BasePageProvider* pageProvider, QString defaultId, QWidget* parent) : QWidget(parent)
{
    createUI();
    useSidebarStyle(true);

    m_model = new PageModel(this);
    m_proxyModel = new PageEntryFilterModel(this);
    int counter = 0;
    auto pages = pageProvider->getPages();
    for (auto page : pages) {
        auto widget = dynamic_cast<QWidget*>(page);
        // Let pages with their own scrollers shrink below their preferred size.
        auto policy = widget->sizePolicy();
        if (policy.horizontalPolicy() == QSizePolicy::MinimumExpanding)
            policy.setHorizontalPolicy(QSizePolicy::Expanding);
        if (policy.verticalPolicy() == QSizePolicy::MinimumExpanding)
            policy.setVerticalPolicy(QSizePolicy::Expanding);
        widget->setSizePolicy(policy);
        if (widget->property("chromaCatalog").toBool()) {
            // Catalogs own their grid/detail scrolling and use all available space.
            page->stackIndex = m_pageStack->addWidget(widget);
        } else {
            auto scroll = new QScrollArea(this);
            SmoothScroll::install(scroll);
            scroll->setObjectName("pageScroll_" + page->id());
            scroll->setProperty("role", "pageViewport");
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidgetResizable(true);
            // The body takes the remaining height. Its preferred page height must
            // not push the fixed navigation and buttons outside an inline host.
            scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
            scroll->setMinimumSize(0, 0);
            scroll->setWidget(widget);
            page->stackIndex = m_pageStack->addWidget(scroll);
        }
        page->listIndex = counter;
        page->setParentContainer(this);
        counter++;
        page->updateExtraInfo = [this](QString id, QString info) {
            if (m_currentPage && id == m_currentPage->id())
                m_header->setText(m_currentPage->displayName() + info);
        };
    }
    m_model->setPages(pages);

    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_pageList->setIconSize(QSize(pageIconSize, pageIconSize));
    m_pageList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pageList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_pageList->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    m_pageList->setModel(m_proxyModel);
    m_pageSelector->setModel(m_proxyModel);
    connect(m_pageList->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &PageContainer::currentChanged);
    connect(m_pageSelector, qOverload<int>(&QComboBox::activated), this,
            [this](int row) { m_pageList->setCurrentIndex(m_proxyModel->index(row, 0)); });
    m_pageStack->setStackingMode(QStackedLayout::StackOne);
    m_pageList->setFocus();
    selectPage(defaultId);
    updateNavigation();
}

bool PageContainer::selectPage(QString pageId)
{
    // now find what we want to have selected...
    auto page = m_model->findPageEntryById(pageId);
    QModelIndex index;
    if (page) {
        index = m_proxyModel->mapFromSource(m_model->index(page->listIndex));
    }
    if (!index.isValid()) {
        index = m_proxyModel->index(0, 0);
    }
    if (index.isValid()) {
        m_pageList->setCurrentIndex(index);
        return true;
    }
    return false;
}

BasePage* PageContainer::getPage(QString pageId)
{
    return m_model->findPageEntryById(pageId);
}

BasePage* PageContainer::selectedPage() const
{
    return m_currentPage;
}

const QList<BasePage*>& PageContainer::getPages() const
{
    return m_model->pages();
}

void PageContainer::refreshContainer()
{
    m_proxyModel->invalidate();
    if (!m_currentPage || !m_currentPage->shouldDisplay()) {
        auto index = m_proxyModel->index(0, 0);
        if (index.isValid()) {
            m_pageList->setCurrentIndex(index);
        } else {
            // FIXME: unhandled corner case: what to do when there's no page to select?
        }
    }
}

void PageContainer::createUI()
{
    setObjectName("pageContainer");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_pageStack = new QStackedLayout;
    m_pageList = new PageView;
    m_pageList->setObjectName("pageNavigation");
    m_pageList->setAccessibleName(tr("Pages"));
    m_pageList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_pageList->setMinimumWidth(172);
    m_pageList->setMaximumWidth(220);
    m_pageList->setTextElideMode(Qt::ElideRight);
    m_pageList->setSpacing(3);
    m_pageSelector = new ClayComboBox(this);
    m_pageSelector->setObjectName("pageSelector");
    m_pageSelector->setAccessibleName(tr("Current page"));
    m_pageSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pageSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_pageSelector->setMinimumContentsLength(10);
    m_pageSelector->setIconSize(QSize(pageIconSize, pageIconSize));
    m_header = new QLabel();
    m_header->setObjectName("pageHeader");
    m_header->setTextFormat(Qt::PlainText);
    m_header->setWordWrap(true);

    QFont headerLabelFont = m_header->font();
    headerLabelFont.setBold(true);
    const int pointSize = headerLabelFont.pointSize();
    if (pointSize > 0)
        headerLabelFont.setPointSize(pointSize + 2);
    m_header->setFont(headerLabelFont);

    m_pageStack->setContentsMargins(0, 0, 0, 0);
    m_pageStack->addWidget(new QWidget(this));

    auto footer = new QWidget(this);
    footer->setObjectName("pageFooter");
    m_buttonLayout = new QVBoxLayout(footer);
    m_buttonLayout->setContentsMargins(0, 0, 0, 0);

    m_layout = new QGridLayout;
    m_layout->addWidget(m_pageSelector, 0, 0, 1, 2);
    m_layout->addWidget(m_header, 1, 1);
    m_layout->addWidget(m_pageList, 1, 0, 3, 1);
    m_layout->addLayout(m_pageStack, 2, 1);
    m_layout->addWidget(footer, 3, 1);
    m_layout->setColumnStretch(1, 1);
    m_layout->setRowStretch(2, 1);
    m_layout->setContentsMargins(16, 16, 16, 16);
    m_layout->setHorizontalSpacing(18);
    m_layout->setVerticalSpacing(12);
    setLayout(m_layout);

    applyStyle();
}

void PageContainer::applyStyle()
{
    m_clayStyle = Clay::enabled();
    auto sheet = QStringLiteral(R"(
        QWidget#pageContainer { background: palette(base); }
        QWidget#pageContainer QLabel { background: transparent; }
        QListView#pageNavigation { background: palette(window); color: palette(text); border: 1px solid palette(mid); border-radius: 10px; padding: 5px; outline: none; }
        QListView#pageNavigation::item { min-height: 34px; padding: 4px 8px; border-radius: 7px; }
        QListView#pageNavigation::item:hover { background: palette(alternate-base); }
        QListView#pageNavigation::item:selected { background: palette(highlight); color: palette(highlighted-text); }
        QListView#pageNavigation:focus { border-color: palette(highlight); }
        QWidget#pageContainer QScrollArea[role="pageViewport"] { background: palette(base); border: none; }
        QWidget#pageContainer QGroupBox { border: 1px solid palette(mid); border-radius: 9px; margin-top: 12px; padding: 12px 8px 8px; }
        QWidget#pageContainer QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
        QWidget#pageContainer QLineEdit, QWidget#pageContainer QAbstractSpinBox, QWidget#pageContainer QComboBox { background: palette(base); color: palette(text); border: 1px solid palette(mid); border-radius: 7px; min-height: 24px; padding: 3px 7px; }
        QWidget#pageContainer QLineEdit:focus, QWidget#pageContainer QAbstractSpinBox:focus, QWidget#pageContainer QComboBox:focus { border-color: palette(highlight); }
        QWidget#pageContainer QComboBox#pageSelector { background: palette(button); min-height: 32px; }
        QWidget#pageContainer QPushButton { background: palette(button); color: palette(button-text); border: 1px solid palette(mid); border-radius: 8px; padding: 6px 12px; }
        QWidget#pageContainer QPushButton:hover { border-color: palette(highlight); }
        QWidget#pageContainer QPushButton:focus { border: 2px solid palette(highlight); }
        QWidget#pageContainer QPushButton[role="primary"] { background: palette(highlight); color: palette(highlighted-text); border-color: palette(highlight); font-weight: 600; }
        QWidget#pageContainer QPushButton:disabled, QWidget#pageContainer QPushButton[role="primary"]:disabled { background: palette(button); color: palette(placeholder-text); border-color: palette(mid); }
        QWidget#pageContainer QTabWidget::pane { border: 1px solid palette(mid); border-radius: 8px; }
        QWidget#pageContainer QTabBar::tab { background: palette(window); color: palette(text); padding: 8px 12px; border-bottom: 2px solid transparent; }
        QWidget#pageContainer QTabBar::tab:selected { border-bottom-color: palette(highlight); background: palette(alternate-base); }
    )");
    if (m_clayStyle) {
        sheet += Clay::formStyleSheet(QStringLiteral("QWidget#pageContainer"));
        sheet += Clay::scrollBarStyleSheet(QStringLiteral("QWidget#pageContainer"));
        sheet += QStringLiteral(R"(
            QLabel#pageHeader { font-family: "Nunito"; font-weight: 800; }
            QListView#pageNavigation { border-radius: 24px; }
            QListView#pageNavigation::item { border-radius: 20px; min-height: 36px; }
            QWidget#pageContainer QComboBox#pageSelector { min-height: 28px; }
        )");
    }
    setStyleSheet(sheet);
}

void PageContainer::retranslate()
{
    m_pageList->setAccessibleName(tr("Pages"));
    m_pageSelector->setAccessibleName(tr("Current page"));
    if (m_currentPage)
        m_header->setText(m_currentPage->displayName());

    for (auto page : m_model->pages())
        page->retranslate();
}

void PageContainer::addButtons(QWidget* buttons)
{
    m_buttonLayout->addWidget(buttons);
}

void PageContainer::addButtons(QLayout* buttons)
{
    m_buttonLayout->addLayout(buttons);
}

void PageContainer::useSidebarStyle(bool sidebar)
{
    m_pageList->setProperty("_kde_side_panel_view", sidebar);
}

void PageContainer::showPage(int row)
{
    if (m_currentPage) {
        m_currentPage->closed();
    }
    if (row != -1) {
        m_currentPage = m_model->pages().at(row);
    } else {
        m_currentPage = nullptr;
    }
    if (m_currentPage) {
        m_pageStack->setCurrentIndex(m_currentPage->stackIndex);
        m_header->setText(m_currentPage->displayName());
        m_currentPage->opened();
    } else {
        m_pageStack->setCurrentIndex(0);
        m_header->setText(QString());
    }
}

void PageContainer::help()
{
    if (m_currentPage) {
        QString pageId = m_currentPage->helpPage();
        if (pageId.isEmpty())
            return;
        DesktopServices::openUrl(QUrl(BuildConfig.HELP_URL.arg(pageId)));
    }
}

void PageContainer::currentChanged(const QModelIndex& current)
{
    int selected_index = current.isValid() ? m_proxyModel->mapToSource(current).row() : -1;

    auto* selected = selected_index >= 0 ? m_model->pages().at(selected_index) : nullptr;
    auto* previous = m_currentPage;

    const QSignalBlocker blocker(m_pageSelector);
    m_pageSelector->setCurrentIndex(current.row());

    emit selectedPageChanged(previous, selected);

    showPage(selected_index);
}

void PageContainer::hidePageList()
{
    m_navigationHidden = true;
    m_layout->setHorizontalSpacing(0);
    updateNavigation();
}

void PageContainer::setHeaderVisible(bool visible)
{
    m_header->setVisible(visible);
    m_layout->setVerticalSpacing(visible ? 12 : 0);
    m_buttonLayout->setContentsMargins(0, visible ? 0 : 12, 0, 0);
}

void PageContainer::updateNavigation()
{
    const bool compact = width() < 760;
    const bool navigationFocused = m_pageList->hasFocus() || m_pageSelector->hasFocus();
    m_pageList->setVisible(!m_navigationHidden && !compact);
    m_pageSelector->setVisible(!m_navigationHidden && compact);
    if (navigationFocused && !m_navigationHidden) {
        auto navigation = compact ? static_cast<QWidget*>(m_pageSelector) : static_cast<QWidget*>(m_pageList);
        navigation->setFocus(Qt::OtherFocusReason);
    }
}

void PageContainer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateNavigation();
}

bool PageContainer::prepareToClose()
{
    if (!saveAll()) {
        return false;
    }
    if (m_currentPage) {
        m_currentPage->closed();
    }
    return true;
}

bool PageContainer::saveAll()
{
    for (auto page : m_model->pages()) {
        if (!page->apply())
            return false;
    }
    return true;
}

void PageContainer::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && m_clayStyle != Clay::enabled())
        applyStyle();
}
