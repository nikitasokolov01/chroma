// SPDX-License-Identifier: GPL-3.0-only
#include "LauncherHome.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

#include "Application.h"
#include "BaseInstance.h"
#include "BuildConfig.h"
#include "InstanceList.h"
#include "MMCTime.h"
#include "icons/IconList.h"
#include "ui/instanceview/InstanceProxyModel.h"
#include "ui/instanceview/InstanceView.h"

namespace {
class HeaderTitleLabel : public QLabel {
   public:
    using QLabel::QLabel;

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};

QLabel* label(const QString& text, const char* role, QWidget* parent)
{
    auto* result = new QLabel(text, parent);
    result->setTextFormat(Qt::PlainText);
    result->setProperty("role", role);
    return result;
}

QToolButton* actionButton(QAction* action, QWidget* parent, const char* role = "secondary")
{
    auto* button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setProperty("role", role);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(36);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    if (action->menu())
        button->setPopupMode(QToolButton::MenuButtonPopup);
    return button;
}

QFrame* divider(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setObjectName("homeDivider");
    line->setFixedHeight(1);
    return line;
}
}  // namespace

LauncherHome::LauncherHome(InstanceView* view, InstanceProxyModel* model, const Actions& actions, QWidget* parent)
    : QWidget(parent), m_view(view), m_model(model), m_actions(actions)
{
    setObjectName("launcherHome");
    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    auto* header = new QFrame(this);
    header->setObjectName("homeHeader");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 12, 20, 12);
    headerLayout->setSpacing(12);
    auto* logo = new QLabel(header);
    logo->setPixmap(APPLICATION->logo().pixmap(30, 30));
    headerLayout->addWidget(logo);
    headerLayout->addWidget(label(BuildConfig.LAUNCHER_DISPLAYNAME, "brand", header));
    auto* separator = label(QStringLiteral("/"), "muted", header);
    headerLayout->addWidget(separator);
    m_pageTitle = new HeaderTitleLabel(tr("Home"), header);
    m_pageTitle->setObjectName("homePageTitle");
    m_pageTitle->setTextFormat(Qt::PlainText);
    m_pageTitle->setProperty("role", "body");
    m_pageTitle->setMinimumWidth(0);
    m_pageTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerLayout->addWidget(m_pageTitle, 1);
    m_running = label({}, "muted", header);
    m_running->setObjectName("runningStatus");
    headerLayout->addWidget(m_running);
    shell->addWidget(header);

    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    shell->addLayout(body, 1);

    auto* rail = new QFrame(this);
    rail->setObjectName("homeRail");
    rail->setFixedWidth(72);
    auto* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(10, 18, 10, 16);
    railLayout->setSpacing(12);
    auto navigation = [rail, railLayout](const QString& text, const QString& icon) {
        auto* button = new QToolButton(rail);
        button->setIcon(QIcon::fromTheme(icon));
        button->setText(text);
        button->setToolTip(text);
        button->setAccessibleName(text);
        button->setIconSize(QSize(24, 24));
        button->setFixedSize(50, 50);
        button->setProperty("role", "navigation");
        button->setCursor(Qt::PointingHandCursor);
        railLayout->addWidget(button);
        return button;
    };
    m_homeButton = navigation(tr("Home"), "launch");
    m_libraryButton = navigation(tr("Library"), "viewfolder");
    m_homeButton->setCheckable(true);
    m_libraryButton->setCheckable(true);
    m_homeButton->setChecked(true);
    auto requestHome = [this](bool libraryOnly) {
        // The owner must finish or cancel an active page before changing pages.
        m_homeButton->setChecked(m_pages->currentWidget() == m_homePage && !m_libraryOnly);
        m_libraryButton->setChecked(m_pages->currentWidget() == m_homePage && m_libraryOnly);
        emit homeRequested(libraryOnly);
    };
    connect(m_homeButton, &QToolButton::clicked, this, [requestHome] { requestHome(false); });
    connect(m_libraryButton, &QToolButton::clicked, this, [requestHome] { requestHome(true); });
    auto* add = navigation(tr("Add instance or modpack"), "new");
    connect(add, &QToolButton::clicked, m_actions.add, &QAction::trigger);
    railLayout->addWidget(divider(rail));
    m_pinsScroll = new QScrollArea(rail);
    m_pinsScroll->setObjectName("pinnedInstances");
    m_pinsScroll->setAccessibleName(tr("Pinned instances"));
    m_pinsScroll->setWidgetResizable(true);
    m_pinsScroll->setFrameShape(QFrame::NoFrame);
    m_pinsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pinsScroll->setMinimumHeight(0);
    auto* pins = new QWidget(m_pinsScroll);
    m_pinnedRows = new QVBoxLayout(pins);
    m_pinnedRows->setContentsMargins(0, 0, 0, 0);
    m_pinnedRows->setSpacing(8);
    m_pinnedRows->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_pinsScroll->setWidget(pins);
    railLayout->addWidget(m_pinsScroll, 1);
    auto* folders = navigation(tr("Launcher menu"), "more");
    folders->setText(QStringLiteral("\u2022\u2022\u2022"));
    folders->setToolButtonStyle(Qt::ToolButtonTextOnly);
    folders->setMenu(m_actions.launcherMenu);
    folders->setPopupMode(QToolButton::InstantPopup);
    auto* settings = navigation(tr("Settings"), "settings");
    connect(settings, &QToolButton::clicked, m_actions.settings, &QAction::trigger);
    body->addWidget(rail);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName("homePageStack");
    body->addWidget(m_pages, 1);
    m_homePage = new QWidget(m_pages);
    auto* homeLayout = new QHBoxLayout(m_homePage);
    homeLayout->setContentsMargins(0, 0, 0, 0);
    homeLayout->setSpacing(0);
    m_pages->addWidget(m_homePage);
    m_pageHost = new QWidget(m_pages);
    m_pageHost->setObjectName("inlinePageHost");
    auto* pageLayout = new QVBoxLayout(m_pageHost);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);
    m_pages->addWidget(m_pageHost);

    m_homeScroll = new QScrollArea(m_homePage);
    m_homeScroll->setObjectName("homeContentScroll");
    m_homeScroll->setWidgetResizable(true);
    m_homeScroll->setFrameShape(QFrame::NoFrame);
    m_homeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_homeScroll->setMinimumWidth(340);
    m_homeScroll->verticalScrollBar()->setSingleStep(48);
    auto* library = new QWidget(m_homeScroll);
    library->setObjectName("homeLibrary");
    library->setMinimumWidth(340);
    auto* libraryLayout = new QVBoxLayout(library);
    libraryLayout->setContentsMargins(24, 22, 24, 14);
    libraryLayout->setSpacing(14);

    m_recent = new QWidget(library);
    m_recent->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto* recentLayout = new QVBoxLayout(m_recent);
    recentLayout->setContentsMargins(0, 0, 0, 8);
    recentLayout->setSpacing(12);
    auto* recentTitle = label(tr("Jump back in"), "heading", m_recent);
    recentTitle->setObjectName("recentTitle");
    recentLayout->addWidget(recentTitle);
    m_recentRows = new QVBoxLayout();
    m_recentRows->setSpacing(8);
    recentLayout->addLayout(m_recentRows);
    libraryLayout->addWidget(m_recent);

    auto* titleRow = new QHBoxLayout();
    auto* libraryTitle = label(tr("Your library"), "heading", library);
    libraryTitle->setObjectName("libraryTitle");
    titleRow->addWidget(libraryTitle);
    m_count = label({}, "muted", library);
    titleRow->addWidget(m_count);
    titleRow->addStretch();
    libraryLayout->addLayout(titleRow);

    auto* searchRow = new QHBoxLayout();
    searchRow->setSpacing(10);
    m_search = new QLineEdit(library);
    m_search->setObjectName("librarySearch");
    m_search->setPlaceholderText(tr("Search your instances"));
    m_search->setAccessibleName(tr("Search your instances"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumHeight(38);
    m_model->setFilterCaseSensitivity(Qt::CaseInsensitive);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_model->setFilterFixedString(text);
        refresh();
    });
    searchRow->addWidget(m_search, 1);
    auto* newInstance = actionButton(m_actions.add, library, "primary");
    searchRow->addWidget(newInstance);
    libraryLayout->addLayout(searchRow);

    auto* filters = new QHBoxLayout();
    m_sort = new QComboBox(library);
    m_sort->setAccessibleName(tr("Sort instances"));
    m_sort->addItem(tr("Name"), "Name");
    m_sort->addItem(tr("Last played"), "LastLaunch");
    m_sort->setCurrentIndex(APPLICATION->settings()->get("InstSortMode").toString() == "LastLaunch" ? 1 : 0);
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this] {
        APPLICATION->settings()->set("InstSortMode", m_sort->currentData());
        m_model->invalidate();
        m_model->sort(0);
    });
    filters->addWidget(m_sort);
    filters->addWidget(actionButton(m_actions.group, library));
    filters->addStretch();
    libraryLayout->addLayout(filters);

    m_empty = label({}, "empty", library);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    libraryLayout->addWidget(m_empty);
    m_profileButton = actionButton(m_actions.usePrismFolder, library, "primary");
    m_profileButton->setObjectName("usePrismFolderButton");
    libraryLayout->addWidget(m_profileButton, 0, Qt::AlignHCenter);
    m_view->setObjectName("instanceLibrary");
    m_view->setProperty("homeEmptyState", true);
    libraryLayout->addWidget(m_view);
    libraryLayout->addStretch();
    m_homeScroll->setWidget(library);
    m_view->setOuterScrollArea(m_homeScroll);
    homeLayout->addWidget(m_homeScroll, 1);

    auto* detailsScroll = new QScrollArea(m_homePage);
    detailsScroll->setObjectName("homeDetails");
    detailsScroll->setWidgetResizable(true);
    detailsScroll->setFrameShape(QFrame::NoFrame);
    detailsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailsScroll->setFixedWidth(258);
    m_details = detailsScroll;
    auto* details = new QWidget(detailsScroll);
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(18, 22, 18, 18);
    detailsLayout->setSpacing(12);
    auto* playingLabel = label(tr("PLAYING AS"), "eyebrow", details);
    playingLabel->setObjectName("playingLabel");
    detailsLayout->addWidget(playingLabel);
    auto* accountButton = actionButton(m_actions.accounts, details);
    accountButton->setPopupMode(QToolButton::InstantPopup);
    accountButton->setIconSize(QSize(32, 32));
    accountButton->setMinimumHeight(56);
    accountButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    detailsLayout->addWidget(accountButton);
    detailsLayout->addSpacing(6);
    detailsLayout->addWidget(divider(details));
    auto* selectedLabel = label(tr("INSTANCE DETAILS"), "eyebrow", details);
    selectedLabel->setObjectName("selectedLabel");
    detailsLayout->addWidget(selectedLabel);
    m_selectedIcon = new QLabel(details);
    m_selectedIcon->setAlignment(Qt::AlignLeft);
    detailsLayout->addWidget(m_selectedIcon);
    m_selectedName = label({}, "heading", details);
    m_selectedName->setWordWrap(true);
    detailsLayout->addWidget(m_selectedName);
    m_selectedInfo = label({}, "muted", details);
    m_selectedInfo->setWordWrap(true);
    detailsLayout->addWidget(m_selectedInfo);
    m_playtime = label({}, "muted", details);
    detailsLayout->addWidget(m_playtime);
    for (auto* action : { m_actions.launch, m_actions.stop, m_actions.edit, m_actions.folder }) {
        auto* button = actionButton(action, details, action == m_actions.launch ? "primary" : "secondary");
        if (action == m_actions.launch)
            button->setPopupMode(QToolButton::MenuButtonPopup);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        detailsLayout->addWidget(button);
    }
    m_pinButton = new QToolButton(details);
    m_pinButton->setObjectName("pinSelectedInstance");
    m_pinButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_pinButton->setIcon(QIcon::fromTheme("star"));
    m_pinButton->setMinimumHeight(36);
    m_pinButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_pinButton, &QToolButton::clicked, this, &LauncherHome::toggleSelectedPin);
    detailsLayout->addWidget(m_pinButton);
    auto* more = new QToolButton(details);
    more->setText(tr("More instance actions"));
    more->setObjectName("moreInstanceActions");
    more->setMenu(m_actions.instanceMenu);
    more->setPopupMode(QToolButton::InstantPopup);
    more->setToolButtonStyle(Qt::ToolButtonTextOnly);
    more->setMinimumHeight(36);
    more->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    detailsLayout->addWidget(more);
    detailsLayout->addStretch();
    detailsLayout->addWidget(divider(details));
    detailsLayout->addWidget(actionButton(m_actions.news, details));
    auto* tip = label(tr("Make it yours\nChoose your accent in Settings → Launcher → Appearance."), "muted", details);
    tip->setObjectName("appearanceTip");
    tip->setWordWrap(true);
    detailsLayout->addWidget(tip);
    detailsScroll->setWidget(details);
    homeLayout->addWidget(detailsScroll);

    auto* searchShortcut = new QShortcut(QKeySequence::Find, this);
    connect(searchShortcut, &QShortcut::activated, this, [this] {
        if (m_pages->currentWidget() == m_homePage) {
            m_homeScroll->ensureWidgetVisible(m_search);
            m_search->setFocus();
        }
    });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), m_search);
    escape->setContext(Qt::WidgetShortcut);
    connect(escape, &QShortcut::activated, m_search, &QLineEdit::clear);
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    connect(m_refreshTimer, &QTimer::timeout, this, &LauncherHome::refresh);
    auto schedule = [this] { m_refreshTimer->start(0); };
    auto* instances = APPLICATION->instances().get();
    connect(instances, &QAbstractItemModel::dataChanged, this, schedule);
    connect(instances, &QAbstractItemModel::modelReset, this, schedule);
    connect(instances, &QAbstractItemModel::rowsInserted, this, schedule);
    connect(instances, &QAbstractItemModel::rowsRemoved, this, schedule);
    connect(APPLICATION->icons().get(), &IconList::iconUpdated, this, schedule);
    connect(APPLICATION, &Application::globalSettingsApplied, this, [this, schedule] {
        QSignalBlocker blocker(m_sort);
        m_sort->setCurrentIndex(APPLICATION->settings()->get("InstSortMode").toString() == "LastLaunch" ? 1 : 0);
        schedule();
    });
    applyStyle();
    refresh();
}

void LauncherHome::setSelectedInstance(const QString& id)
{
    m_selectedId = id;
    refreshSelection();
}

void LauncherHome::clearSearch()
{
    m_search->clear();
}

void LauncherHome::showPage(QWidget* page, const QString& title)
{
    if (!page)
        return;
    auto* layout = m_pageHost->layout();
    while (auto* item = layout->takeAt(0)) {
        if (item->widget() && item->widget() != page)
            item->widget()->hide();
        delete item;
    }
    layout->addWidget(page);
    page->show();
    m_pages->setCurrentWidget(m_pageHost);
    m_pageTitle->setText(title);
    m_pageTitle->setToolTip(title);
    m_homeButton->setChecked(false);
    m_libraryButton->setChecked(false);
}

void LauncherHome::showHomePage(bool libraryOnly)
{
    m_pages->setCurrentWidget(m_homePage);
    setLibraryOnly(libraryOnly);
    m_homeScroll->verticalScrollBar()->setValue(0);
}

void LauncherHome::setLibraryOnly(bool enabled)
{
    m_libraryOnly = enabled;
    const bool homeVisible = m_pages->currentWidget() == m_homePage;
    m_homeButton->setChecked(homeVisible && !enabled);
    m_libraryButton->setChecked(homeVisible && enabled);
    m_recent->setVisible(!enabled && APPLICATION->instances()->count() > 0);
    if (homeVisible) {
        m_pageTitle->setText(enabled ? tr("Library") : tr("Home"));
        m_pageTitle->setToolTip(m_pageTitle->text());
    }
    if (homeVisible && enabled)
        m_search->setFocus();
}

bool LauncherHome::selectedInstancePinned() const
{
    return !m_selectedId.isEmpty() && APPLICATION->settings()->get("ChromaPinnedInstances").toStringList().contains(m_selectedId);
}

void LauncherHome::toggleSelectedPin()
{
    togglePin(m_selectedId);
}

void LauncherHome::togglePin(const QString& id)
{
    if (!APPLICATION->instances()->getInstanceById(id))
        return;
    auto pinned = APPLICATION->settings()->get("ChromaPinnedInstances").toStringList();
    if (pinned.contains(id))
        pinned.removeAll(id);
    else
        pinned.append(id);
    APPLICATION->settings()->set("ChromaPinnedInstances", pinned);
    refreshPins();
    refreshSelection();
    emit pinsChanged();
}

void LauncherHome::refreshPins()
{
    const auto stored = APPLICATION->settings()->get("ChromaPinnedInstances").toStringList();
    QStringList pinned;
    for (const auto& id : stored) {
        if (APPLICATION->instances()->getInstanceById(id) && !pinned.contains(id))
            pinned.append(id);
    }
    if (pinned != stored) {
        APPLICATION->settings()->set("ChromaPinnedInstances", pinned);
        emit pinsChanged();
    }
    for (auto it = m_pinButtons.begin(); it != m_pinButtons.end();) {
        if (!pinned.contains(it.key())) {
            m_pinnedRows->removeWidget(it.value());
            it.value()->hide();
            it.value()->deleteLater();
            it = m_pinButtons.erase(it);
        } else {
            ++it;
        }
    }
    for (int i = 0; i < pinned.size(); ++i) {
        const auto id = pinned.at(i);
        const auto instance = APPLICATION->instances()->getInstanceById(id);
        auto* button = m_pinButtons.value(id);
        if (!button) {
            button = new QToolButton(m_pinsScroll->widget());
            button->setObjectName("pinnedInstance");
            button->setProperty("instanceId", id);
            button->setProperty("role", "navigation");
            button->setFixedSize(44, 44);
            button->setIconSize(QSize(32, 32));
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(button, &QToolButton::clicked, this, [this, id] {
                // Selection changes only after the owner accepts navigation.
                if (auto* pin = m_pinButtons.value(id))
                    pin->setChecked(id == m_selectedId);
                emit instanceOpenRequested(id);
            });
            connect(button, &QToolButton::customContextMenuRequested, this, [this, id, button](const QPoint& point) {
                QMenu menu(this);
                auto* unpin = menu.addAction(tr("Unpin from sidebar"));
                connect(unpin, &QAction::triggered, this, [this, id] { togglePin(id); });
                menu.exec(button->mapToGlobal(point));
            });
            m_pinButtons.insert(id, button);
        }
        button->setIcon(APPLICATION->icons()->getIcon(instance->iconKey()));
        button->setText(instance->name());
        button->setToolTip(instance->name());
        button->setAccessibleName(tr("Open %1").arg(instance->name()));
        button->setChecked(id == m_selectedId);
        m_pinnedRows->insertWidget(i, button, 0, Qt::AlignHCenter);
        button->show();
    }
}

void LauncherHome::refresh()
{
    auto instances = APPLICATION->instances();
    QList<InstancePtr> recent;
    int running = 0;
    for (int i = 0; i < instances->count(); ++i) {
        auto instance = instances->at(i);
        running += instance->isRunning() ? 1 : 0;
        if (instance->lastLaunch() > 0 || instance->isRunning())
            recent.append(instance);
    }
    std::stable_sort(recent.begin(), recent.end(), [](const InstancePtr& a, const InstancePtr& b) {
        if (a->isRunning() != b->isRunning())
            return a->isRunning();
        return a->lastLaunch() > b->lastLaunch();
    });
    while (auto* item = m_recentRows->takeAt(0)) {
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
    if (recent.isEmpty()) {
        auto* welcome =
            label(tr("Your next adventure starts here.\nAdd an instance or import a modpack to get started."), "welcome", m_recent);
        welcome->setWordWrap(true);
        m_recentRows->addWidget(welcome);
    }
    for (int i = 0; i < qMin(m_recentLimit, int(recent.size())); ++i) {
        auto instance = recent.at(i);
        auto* row = new QFrame(m_recent);
        row->setObjectName("recentInstance");
        row->setMinimumHeight(68);
        row->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(14, 10, 14, 10);
        rowLayout->setSpacing(12);
        auto* icon = new QLabel(row);
        icon->setPixmap(APPLICATION->icons()->getIcon(instance->iconKey()).pixmap(44, 44));
        rowLayout->addWidget(icon);
        auto* description = new QVBoxLayout();
        description->setContentsMargins(0, 0, 0, 0);
        description->setSpacing(4);
        auto* name = label(instance->name(), "strong", row);
        name->setMinimumWidth(0);
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
        name->ensurePolished();
        name->setMinimumHeight(name->fontMetrics().height() + 2);
        name->setToolTip(instance->name());
        description->addWidget(name);
        auto lastPlayed = QDateTime::fromMSecsSinceEpoch(instance->lastLaunch()).toString(tr("MMM d, yyyy"));
        auto* subtitle = label(instance->isRunning() ? tr("Running") : tr("Last played %1").arg(lastPlayed), "muted", row);
        subtitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        subtitle->ensurePolished();
        subtitle->setMinimumHeight(subtitle->fontMetrics().height() + 2);
        description->addWidget(subtitle);
        rowLayout->addLayout(description, 1);
        auto* play = new QToolButton(row);
        play->setText(instance->isRunning() ? tr("Running") : tr("Play"));
        play->setIcon(QIcon::fromTheme("launch"));
        play->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        play->setProperty("role", "primary");
        play->setMinimumHeight(36);
        play->setAccessibleName(tr("Play %1").arg(instance->name()));
        play->setEnabled(instance->canLaunch() && !instance->isRunning());
        const auto id = instance->id();
        connect(play, &QToolButton::clicked, this, [this, id] { emit launchRequested(id); });
        rowLayout->addWidget(play);
        m_recentRows->addWidget(row);
    }
    m_running->setText(running == 0 ? tr("No instances running") : tr("%n instance(s) running", nullptr, running));
    m_count->setText(tr("%1 / %2").arg(m_model->rowCount()).arg(instances->count()));
    m_recent->setVisible(!m_libraryOnly && instances->count() > 0);
    m_profileButton->setVisible(instances->count() == 0);
    m_empty->setVisible(m_model->rowCount() == 0);
    m_empty->setText(instances->count() == 0
                         ? tr("Your Minecraft library, ready to play.\nUse your Prism folder or create a new instance above.")
                         : tr("No instances match your search.\nTry a different name or clear the search."));
    refreshPins();
    refreshSelection();
}

void LauncherHome::refreshSelection()
{
    auto instance = APPLICATION->instances()->getInstanceById(m_selectedId);
    m_pinButton->setEnabled(bool(instance));
    m_pinButton->setText(selectedInstancePinned() ? tr("Unpin from sidebar") : tr("Pin to sidebar"));
    for (auto it = m_pinButtons.cbegin(); it != m_pinButtons.cend(); ++it)
        it.value()->setChecked(it.key() == m_selectedId);
    if (!instance) {
        m_selectedIcon->clear();
        m_selectedName->setText(tr("Ready when you are"));
        m_selectedInfo->setText(tr("Select an instance to see its details and launch your game."));
        m_playtime->clear();
        return;
    }
    m_selectedIcon->setPixmap(APPLICATION->icons()->getIcon(instance->iconKey()).pixmap(64, 64));
    m_selectedName->setText(instance->name());
    const auto sourceIndex = APPLICATION->instances()->getInstanceIndexById(m_selectedId);
    const auto summary = m_model->mapFromSource(sourceIndex).data(InstanceProxyModel::InstanceSummaryRole).toString();
    const bool showStatus = summary.isEmpty() || instance->hasCrashed() || instance->hasVersionBroken();
    m_selectedInfo->setText(showStatus ? instance->getStatusbarDescription() : summary);
    m_playtime->setVisible(!showStatus && instance->settings()->get("ShowGameTime").toBool());
    m_playtime->setText(
        tr("Playtime · %1")
            .arg(Time::prettifyDuration(instance->totalTimePlayed(), APPLICATION->settings()->get("ShowGameTimeWithoutDays").toBool())));
}

void LauncherHome::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // The library and its context menu remain usable on smaller displays.
    m_details->setVisible(event->size().width() >= 1000);
}

void LauncherHome::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslate();
}

void LauncherHome::retranslate()
{
    m_search->setPlaceholderText(tr("Search your instances"));
    m_search->setAccessibleName(tr("Search your instances"));
    m_sort->setItemText(0, tr("Name"));
    m_sort->setItemText(1, tr("Last played"));
    findChild<QLabel*>("recentTitle")->setText(tr("Jump back in"));
    findChild<QLabel*>("libraryTitle")->setText(tr("Your library"));
    findChild<QLabel*>("playingLabel")->setText(tr("PLAYING AS"));
    findChild<QLabel*>("selectedLabel")->setText(tr("INSTANCE DETAILS"));
    findChild<QToolButton*>("moreInstanceActions")->setText(tr("More instance actions"));
    findChild<QLabel*>("appearanceTip")->setText(tr("Make it yours\nChoose your accent in Settings → Launcher → Appearance."));
    setLibraryOnly(m_libraryOnly);
    refresh();
}

void LauncherHome::applyStyle()
{
    // Scoped to the native home widget; Qt palettes keep other color schemes usable.
    setStyleSheet(QStringLiteral(R"(
        QWidget#launcherHome { background: palette(base); }
        QWidget#launcherHome QLabel { background: transparent; }
        QFrame#homeHeader, QFrame#homeRail, QScrollArea#homeDetails { background: palette(window); }
        QFrame#homeHeader { border-bottom: 1px solid palette(mid); }
        QFrame#homeRail { border-right: 1px solid palette(mid); }
        QScrollArea#homeDetails { border-left: 1px solid palette(mid); }
        QWidget#homeLibrary, QWidget#instanceLibrary { background: palette(base); }
        QScrollArea#homeContentScroll, QScrollArea#pinnedInstances { border: none; background: transparent; }
        QFrame#homeDivider { background: palette(mid); }
        QLabel[role="brand"] { font-size: 21px; font-weight: 700; }
        QLabel[role="heading"] { font-size: 22px; font-weight: 700; }
        QLabel[role="strong"] { font-size: 14px; font-weight: 600; }
        QLabel[role="body"] { font-size: 14px; font-weight: 600; }
        QLabel[role="muted"], QLabel[role="eyebrow"] { color: palette(placeholder-text); font-size: 12px; }
        QLabel[role="eyebrow"] { font-weight: 600; }
        QLabel[role="welcome"] { background: palette(alternate-base); border: 1px solid palette(mid); border-radius: 12px; padding: 24px; color: palette(placeholder-text); font-size: 14px; }
        QLabel[role="empty"] { padding: 22px; color: palette(placeholder-text); font-size: 14px; }
        QLabel#runningStatus { border: 1px solid palette(mid); border-radius: 12px; padding: 6px 12px; }
        QFrame#recentInstance { background: palette(alternate-base); border: 1px solid palette(mid); border-radius: 12px; }
        QWidget#launcherHome QToolButton { background: palette(button); color: palette(button-text); border: 1px solid palette(mid); border-radius: 9px; padding: 3px 12px; font-size: 13px; font-weight: 600; }
        QWidget#launcherHome QToolButton:hover { border-color: palette(highlight); background: palette(alternate-base); }
        QWidget#launcherHome QToolButton:focus { border: 2px solid palette(highlight); }
        QWidget#launcherHome QToolButton:disabled { color: palette(placeholder-text); background: palette(window); }
        QWidget#launcherHome QToolButton[role="primary"] { background: palette(highlight); color: palette(highlighted-text); border-color: palette(highlight); }
        QWidget#launcherHome QToolButton[role="primary"]:disabled { background: palette(button); color: palette(placeholder-text); border-color: palette(mid); }
        QWidget#launcherHome QToolButton[role="navigation"] { background: transparent; border: 1px solid transparent; padding: 0; }
        QWidget#launcherHome QToolButton[role="navigation"]:checked { background: palette(alternate-base); border: 2px solid palette(highlight); }
        QWidget#launcherHome QLineEdit { background: palette(button); color: palette(text); border: 1px solid palette(mid); border-radius: 9px; padding: 0 12px; font-size: 14px; }
        QWidget#launcherHome QLineEdit:focus { border: 1px solid palette(highlight); }
        QWidget#launcherHome QComboBox { background: palette(button); border: 1px solid palette(mid); border-radius: 8px; min-height: 30px; padding: 2px 10px; }
        QWidget#launcherHome QScrollBar:vertical { width: 8px; background: transparent; }
        QWidget#launcherHome QScrollBar::handle:vertical { background: palette(mid); border-radius: 4px; min-height: 30px; }
        QWidget#launcherHome QScrollBar::add-line:vertical, QWidget#launcherHome QScrollBar::sub-line:vertical { height: 0; }
    )"));
}
