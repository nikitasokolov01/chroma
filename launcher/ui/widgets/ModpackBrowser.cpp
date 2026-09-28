// SPDX-License-Identifier: GPL-3.0-only
#include "ModpackBrowser.h"

#include <QComboBox>
#include <QGridLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "ModpackCardDelegate.h"

namespace {
class CatalogEmptyState : public QLabel {
   public:
    explicit CatalogEmptyState(QListView* view) : QLabel(view->viewport()), m_view(view)
    {
        setObjectName("catalogEmptyState");
        setText(tr("No modpacks to show yet.\nBrowse this service or try a different search."));
        setTextFormat(Qt::PlainText);
        setAlignment(Qt::AlignCenter);
        setWordWrap(true);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setStyleSheet("color: palette(placeholder-text); background: transparent; padding: 24px;");
        view->viewport()->installEventFilter(this);
        auto update = [this] { refresh(); };
        connect(view->model(), &QAbstractItemModel::modelReset, this, update);
        connect(view->model(), &QAbstractItemModel::rowsInserted, this, update);
        connect(view->model(), &QAbstractItemModel::rowsRemoved, this, update);
        refresh();
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
            refresh();
        return QLabel::eventFilter(watched, event);
    }

   private:
    void refresh()
    {
        setGeometry(m_view->viewport()->rect());
        setVisible(m_view->model()->rowCount(m_view->rootIndex()) == 0);
    }
    QListView* m_view;
};
}  // namespace

ModpackBrowser::ModpackBrowser(QLineEdit* search, QComboBox* sort, QWidget* catalog, QComboBox* versions, QWidget* parent)
    : QWidget(parent), m_search(search), m_sort(sort), m_catalog(catalog)
{
    setObjectName("modpackBrowser");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_root = new QVBoxLayout(this);
    m_root->setContentsMargins(0, 0, 0, 0);
    m_root->setSpacing(14);
    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName("catalogSearchToolbar");
    m_toolbarLayout = new QGridLayout(m_toolbar);
    m_toolbarLayout->setContentsMargins(0, 0, 0, 0);
    m_toolbarLayout->setSpacing(10);
    m_search->setParent(m_toolbar);
    m_search->setPlaceholderText(tr("Search modpacks"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Search modpacks"));
    m_search->setMinimumWidth(0);
    m_search->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (m_sort) {
        m_sort->setParent(m_toolbar);
        m_sort->setMinimumWidth(0);
        m_sort->setMaximumWidth(260);
        m_sort->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_sort->setMinimumContentsLength(10);
        m_sort->setAccessibleName(tr("Sort modpacks"));
    }
    m_root->addWidget(m_toolbar);
    m_body = new QHBoxLayout;
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(18);
    m_catalog->setParent(this);
    m_catalog->setProperty("catalogResults", true);
    m_catalog->setMinimumSize(0, 0);
    m_catalog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_body->addWidget(m_catalog, 1);

    auto* detailScroll = new QScrollArea(this);
    m_details = detailScroll;
    m_details->setObjectName("catalogDetails");
    detailScroll->setFrameShape(QFrame::NoFrame);
    detailScroll->setWidgetResizable(true);
    detailScroll->setMinimumSize(0, 0);
    m_details->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    auto* detailContent = new QWidget;
    detailContent->setObjectName("catalogDetailContent");
    detailContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    detailScroll->setWidget(detailContent);
    auto* detailLayout = new QVBoxLayout(detailContent);
    detailLayout->setContentsMargins(16, 14, 16, 14);
    detailLayout->setSpacing(12);
    auto* back = new QPushButton(tr("Back to results"), detailContent);
    back->setObjectName("catalogBackToResults");
    back->setIcon(QIcon::fromTheme("go-previous"));
    detailLayout->addWidget(back, 0, Qt::AlignLeft);
    connect(back, &QPushButton::clicked, this, &ModpackBrowser::showResults);
    auto* summary = new QHBoxLayout;
    m_artwork = new QLabel(detailContent);
    m_artwork->setObjectName("catalogArtwork");
    m_artwork->setFixedSize(64, 64);
    m_artwork->setAlignment(Qt::AlignCenter);
    m_title = new QLabel(detailContent);
    m_title->setObjectName("catalogTitle");
    m_title->setTextFormat(Qt::PlainText);
    m_title->setWordWrap(true);
    summary->addWidget(m_artwork);
    summary->addWidget(m_title, 1);
    detailLayout->addLayout(summary);
    auto* versionLabel = new QLabel(tr("Version"), detailContent);
    versionLabel->setBuddy(versions);
    detailLayout->addWidget(versionLabel);
    versions->setMinimumWidth(0);
    versions->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    versions->setMinimumContentsLength(8);
    versions->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    versions->setAccessibleName(tr("Modpack version"));
    detailLayout->addWidget(versions);
    m_notice = new QLabel(detailContent);
    m_notice->setObjectName("catalogNotice");
    m_notice->setTextFormat(Qt::PlainText);
    m_notice->setWordWrap(true);
    m_notice->hide();
    detailLayout->addWidget(m_notice);
    m_descriptions = new QStackedWidget(detailContent);
    m_descriptions->setObjectName("catalogDescriptionStack");
    m_descriptions->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_descriptions->setMinimumHeight(80);
    detailLayout->addWidget(m_descriptions, 1);
    m_body->addWidget(m_details, 0);
    m_root->addLayout(m_body, 1);
    m_details->hide();

    setStyleSheet(QStringLiteral(R"(
        QWidget#modpackBrowser { background: palette(base); }
        QWidget#modpackBrowser QListView { background: palette(base); border: none; outline: none; }
        QWidget#catalogDetails { background: palette(window); border: 1px solid palette(mid); border-radius: 12px; }
        QWidget#catalogDetailContent { background: palette(window); border: none; }
        QWidget#catalogDetails QLabel { background: transparent; border: none; }
        QLabel#catalogTitle { font-size: 18px; font-weight: 600; }
        QLabel#catalogNotice { color: palette(placeholder-text); }
        QWidget#catalogDetails QTextBrowser { background: transparent; border: none; padding: 0; }
        QWidget#catalogSearchToolbar QLineEdit, QWidget#catalogSearchToolbar QComboBox { min-height: 30px; }
        QWidget#catalogSearchToolbar QPushButton:checked { background: palette(highlight); color: palette(highlighted-text); }
    )"));
    updateToolbar();
    updateLayout();
}

void ModpackBrowser::addView(QListView* view, QTextBrowser* description)
{
    ModpackCardDelegate::configureView(view);
    new CatalogEmptyState(view);
    description->setMinimumSize(0, 0);
    description->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_descriptions->addWidget(description);
    connect(view, &QListView::clicked, this,
            [this, view, description](const QModelIndex& index) { openDetails(view, description, index); });
    connect(view, &QListView::activated, this,
            [this, view, description](const QModelIndex& index) { openDetails(view, description, index); });
    connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this, view, description](const QModelIndex& index) {
        if (!index.isValid() && m_activeView == view) {
            showResults();
        } else if (index.isValid() && m_detailOpen && m_activeView == view) {
            openDetails(view, description, index);
        }
    });
    connect(view->model(), &QAbstractItemModel::modelReset, this, [this, view] {
        if (m_activeView == view)
            showResults();
    });
    connect(view->model(), &QAbstractItemModel::dataChanged, this,
            [this, view, description](const QModelIndex& first, const QModelIndex& last) {
                const auto current = view->currentIndex();
                if (m_detailOpen && m_activeView == view && current.isValid() && current.parent() == first.parent() &&
                    current.row() >= first.row() && current.row() <= last.row())
                    openDetails(view, description, current);
            });
}

void ModpackBrowser::openDetails(QListView* view, QTextBrowser* description, const QModelIndex& index)
{
    if (!index.isValid())
        return;
    m_activeView = view;
    m_title->setText(index.data(Qt::DisplayRole).toString());
    const auto artwork = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    m_artwork->setPixmap(artwork.pixmap(64, 64));
    m_descriptions->setCurrentWidget(description);
    m_detailOpen = true;
    if (m_filterButton)
        m_filterButton->setChecked(false);
    updateLayout();
}

void ModpackBrowser::showResults()
{
    m_detailOpen = false;
    updateLayout();
    if (m_activeView)
        m_activeView->setFocus(Qt::OtherFocusReason);
}

void ModpackBrowser::setFilterWidget(QWidget* filters, QPushButton* button)
{
    m_filters = filters;
    m_filterButton = button;
    filters->setParent(this);
    filters->setMinimumSize(0, 0);
    filters->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    filters->hide();
    m_body->insertWidget(0, filters);
    button->setParent(m_toolbar);
    button->setText(tr("Filters"));
    button->setCheckable(true);
    button->setChecked(false);
    connect(button, &QPushButton::toggled, this, [this](bool checked) {
        if (checked)
            m_detailOpen = false;
        updateLayout();
    });
    updateToolbar();
}

void ModpackBrowser::addStatusWidget(QWidget* widget)
{
    m_root->insertWidget(m_root->count() - 1, widget);
    // Provider progress widgets show themselves when their first task starts.
    widget->hide();
}

void ModpackBrowser::setNotice(const QString& text)
{
    m_notice->setText(text);
    m_notice->setVisible(!text.isEmpty());
}

void ModpackBrowser::updateLayout()
{
    const bool narrow = width() < 900;
    const bool filtersOpen = m_filterButton && m_filterButton->isChecked();
    // Apply constraints before revealing siblings so a resize never lays out
    // the results using the detail pane's old (compact) zero minimum width.
    m_details->setMaximumWidth(narrow ? QWIDGETSIZE_MAX : 390);
    m_body->setStretchFactor(m_details, narrow ? 1 : 0);
    m_details->setMinimumWidth(m_detailOpen && !narrow ? 330 : 0);
    m_details->updateGeometry();
    if (m_filters) {
        m_filters->setMaximumWidth(narrow ? QWIDGETSIZE_MAX : 300);
        m_filters->setMinimumWidth(filtersOpen && !narrow ? 280 : 0);
        m_filters->updateGeometry();
        m_body->setStretchFactor(m_filters, narrow ? 1 : 0);
        m_filters->setVisible(filtersOpen);
    }
    m_catalog->setVisible(!(narrow && (m_detailOpen || filtersOpen)));
    m_details->setVisible(m_detailOpen);
    m_toolbar->setVisible(!narrow || !m_detailOpen);
    m_body->invalidate();
    m_root->invalidate();
}

void ModpackBrowser::updateToolbar()
{
    m_toolbarLayout->removeWidget(m_search);
    if (m_sort)
        m_toolbarLayout->removeWidget(m_sort);
    if (m_filterButton)
        m_toolbarLayout->removeWidget(m_filterButton);
    m_compactToolbar = width() < 680;
    m_toolbarLayout->addWidget(m_search, 0, 0, 1, m_compactToolbar ? 2 : 1);
    if (m_sort)
        m_toolbarLayout->addWidget(m_sort, m_compactToolbar ? 1 : 0, m_compactToolbar ? 0 : 1);
    if (m_filterButton)
        m_toolbarLayout->addWidget(m_filterButton, m_compactToolbar ? 1 : 0, m_compactToolbar ? 1 : 2);
    m_toolbarLayout->setColumnStretch(0, 1);
}

void ModpackBrowser::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_compactToolbar != (width() < 680))
        updateToolbar();
    updateLayout();
}

void ModpackBrowser::install(QWidget* page, ModpackBrowser* browser)
{
    delete page->layout();
    for (auto* child : page->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (child != browser)
            child->hide();
    }
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(browser);
    page->setProperty("chromaCatalog", true);
    page->setMinimumSize(0, 0);
    page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
}
