// SPDX-License-Identifier: GPL-3.0-only
#include "LauncherHome.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
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
#include "ui/themes/ClayStyle.h"
#include "ui/widgets/ClayWidgets.h"
#include "ui/widgets/SmoothScroll.h"

namespace {
class SidebarPanel : public ClayPanel {
   public:
    using ClayPanel::ClayPanel;
    void setDropActive(bool active)
    {
        if (m_dropActive == active)
            return;
        m_dropActive = active;
        update();
    }

   protected:
    void paintEvent(QPaintEvent* event) override
    {
        ClayPanel::paintEvent(event);
        if (m_dropActive) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(rect().adjusted(3, 3, -3, -3), 24, 24);
        }
    }

   private:
    bool m_dropActive = false;
};

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
    auto* button = new ClayToolButton(parent);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setProperty("role", role);
    button->setProperty("claySymbolic", true);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(48);
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

    auto* header = new ClayPanel(this);
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

    auto* rail = new SidebarPanel(this);
    rail->setObjectName("homeRail");
    rail->setAcceptDrops(true);
    rail->installEventFilter(this);
    rail->setFixedWidth(80);
    auto* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(12, 18, 12, 16);
    railLayout->setSpacing(12);
    auto navigation = [rail, railLayout](const QString& text, const QString& icon) {
        auto* button = new ClayToolButton(rail);
        button->setIcon(QIcon::fromTheme(icon));
        button->setText(text);
        button->setToolTip(text);
        button->setAccessibleName(text);
        button->setIconSize(QSize(24, 24));
        button->setFixedSize(56, 56);
        button->setProperty("role", "navigation");
        button->setProperty("claySymbolic", true);
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
    m_skinsButton = navigation(tr("Skins"), "accounts");
    m_skinsButton->setObjectName("skinsNavigation");
    m_skinsButton->setCheckable(true);
    connect(m_skinsButton, &QToolButton::clicked, this, [this] {
        m_skinsButton->setChecked(false);
        emit skinsRequested();
    });
    railLayout->addWidget(divider(rail));
    m_pinsScroll = new QScrollArea(rail);
    SmoothScroll::install(m_pinsScroll);
    m_pinsScroll->setObjectName("pinnedInstances");
    m_pinsScroll->setAccessibleName(tr("Pinned instances"));
    m_pinsScroll->setWidgetResizable(true);
    m_pinsScroll->setFrameShape(QFrame::NoFrame);
    m_pinsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pinsScroll->setMinimumHeight(0);
    m_pinsScroll->viewport()->setAcceptDrops(true);
    m_pinsScroll->viewport()->installEventFilter(this);
    auto* pins = new QWidget(m_pinsScroll);
    m_pinnedRows = new QVBoxLayout(pins);
    m_pinnedRows->setContentsMargins(0, 0, 0, 0);
    m_pinnedRows->setSpacing(8);
    m_pinnedRows->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_pinsScroll->setWidget(pins);
    m_pinsScroll->viewport()->setAutoFillBackground(false);
    pins->setAutoFillBackground(false);
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
    SmoothScroll::install(m_homeScroll);
    m_homeScroll->setObjectName("homeContentScroll");
    m_homeScroll->setWidgetResizable(true);
    m_homeScroll->setFrameShape(QFrame::NoFrame);
    m_homeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_homeScroll->setMinimumWidth(340);
    m_homeScroll->verticalScrollBar()->setSingleStep(48);
    auto* library = new ClayCanvas(m_homeScroll);
    library->setObjectName("homeLibrary");
    library->setMinimumWidth(340);
    auto* libraryLayout = new QVBoxLayout(library);
    libraryLayout->setContentsMargins(24, 20, 24, 24);
    libraryLayout->setSpacing(12);

    m_recent = new QWidget(library);
    m_recent->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto* recentLayout = new QVBoxLayout(m_recent);
    recentLayout->setContentsMargins(0, 0, 0, 8);
    recentLayout->setSpacing(6);
    auto* recentTitle = label(tr("Recent instances"), "heading", m_recent);
    recentTitle->setObjectName("recentTitle");
    recentLayout->addWidget(recentTitle);
    m_recentRows = new QGridLayout();
    m_recentRows->setSpacing(10);
    recentLayout->addLayout(m_recentRows);
    libraryLayout->addWidget(m_recent);

    m_libraryControls = new QGridLayout();
    m_libraryControls->setHorizontalSpacing(12);
    m_libraryControls->setVerticalSpacing(10);
    m_libraryControls->setColumnStretch(0, 1);
    libraryLayout->addLayout(m_libraryControls);
    auto* titleRow = new QHBoxLayout();
    m_libraryTitleRow = titleRow;
    auto* libraryTitle = label(tr("Your library"), "heading", library);
    libraryTitle->setObjectName("libraryTitle");
    titleRow->addWidget(libraryTitle);
    m_count = label({}, "muted", library);
    m_count->setObjectName("libraryCount");
    titleRow->addWidget(m_count);
    titleRow->addStretch();
    m_libraryControls->addLayout(titleRow, 0, 0);

    auto* searchRow = new QHBoxLayout();
    searchRow->setSpacing(10);
    m_search = new ClayLineEdit(library);
    m_search->setObjectName("librarySearch");
    m_search->setPlaceholderText(tr("Search your instances"));
    m_search->setAccessibleName(tr("Search your instances"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumHeight(56);
    m_model->setFilterCaseSensitivity(Qt::CaseInsensitive);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_model->setFilterFixedString(text);
        refresh();
    });
    searchRow->addWidget(m_search, 1);
    auto* newInstance = actionButton(m_actions.add, library, "primary");
    newInstance->setMinimumHeight(56);
    searchRow->addWidget(newInstance);
    m_libraryControls->addLayout(searchRow, 1, 0, 1, 2);

    auto* filters = new QHBoxLayout();
    m_libraryFilters = filters;
    m_sort = new ClayComboBox(library);
    m_sort->setMinimumHeight(44);
    m_sort->setAccessibleName(tr("Sort instances"));
    m_sort->addItem(tr("Name"), "Name");
    m_sort->addItem(tr("Last played"), "LastLaunch");
    m_sort->addItem(tr("Manual"), "Manual");
    m_sort->setCurrentIndex(qMax(0, m_sort->findData(APPLICATION->settings()->get("InstSortMode"))));
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this] {
        APPLICATION->settings()->set("InstSortMode", m_sort->currentData());
        m_model->invalidate();
        m_model->sort(0);
    });
    filters->addWidget(m_sort);
    filters->addWidget(actionButton(m_actions.group, library));
    filters->addStretch();
    m_libraryControls->addLayout(filters, 2, 0, 1, 2);

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
    m_homeScroll->viewport()->setAutoFillBackground(false);
    m_view->setOuterScrollArea(m_homeScroll);
    homeLayout->addWidget(m_homeScroll, 1);

    auto* detailsScroll = new QScrollArea(m_homePage);
    SmoothScroll::install(detailsScroll);
    detailsScroll->setObjectName("homeDetails");
    detailsScroll->setWidgetResizable(true);
    detailsScroll->setFrameShape(QFrame::NoFrame);
    detailsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailsScroll->setFixedWidth(278);
    m_details = detailsScroll;
    auto* details = new ClayPanel(detailsScroll);
    details->setObjectName("homeDetailsPanel");
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(22, 24, 22, 24);
    detailsLayout->setSpacing(12);
    auto* playingLabel = label(tr("PLAYING AS"), "eyebrow", details);
    playingLabel->setObjectName("playingLabel");
    detailsLayout->addWidget(playingLabel);
    auto* accountButton = actionButton(m_actions.accounts, details);
    accountButton->setProperty("claySymbolic", false);
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
    m_pinButton = new ClayToolButton(details);
    m_pinButton->setObjectName("pinSelectedInstance");
    m_pinButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_pinButton->setIcon(QIcon::fromTheme("star"));
    m_pinButton->setMinimumHeight(48);
    m_pinButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_pinButton, &QToolButton::clicked, this, &LauncherHome::toggleSelectedPin);
    detailsLayout->addWidget(m_pinButton);
    auto* more = new ClayToolButton(details);
    more->setText(tr("More instance actions"));
    more->setObjectName("moreInstanceActions");
    more->setMenu(m_actions.instanceMenu);
    more->setPopupMode(QToolButton::InstantPopup);
    more->setToolButtonStyle(Qt::ToolButtonTextOnly);
    more->setMinimumHeight(48);
    more->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    detailsLayout->addWidget(more);
    detailsLayout->addStretch();
    detailsLayout->addWidget(divider(details));
    detailsLayout->addWidget(actionButton(m_actions.news, details));
    detailsScroll->setWidget(details);
    detailsScroll->viewport()->setAutoFillBackground(false);
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
    connect(instances, &QAbstractItemModel::dataChanged, this, [schedule](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
        if (!roles.isEmpty() && std::all_of(roles.cbegin(), roles.cend(), [](int role) {
                return role == InstanceViewRoles::ProgressValueRole || role == InstanceViewRoles::ProgressMaximumRole;
            }))
            return;
        schedule();
    });
    connect(instances, &QAbstractItemModel::modelReset, this, schedule);
    connect(instances, &QAbstractItemModel::rowsInserted, this, schedule);
    connect(instances, &QAbstractItemModel::rowsRemoved, this, schedule);
    connect(instances, &InstanceList::manualOrderChanged, this, schedule);
    connect(APPLICATION->icons().get(), &IconList::iconUpdated, this, schedule);
    connect(APPLICATION, &Application::globalSettingsApplied, this, [this, schedule] {
        QSignalBlocker blocker(m_sort);
        m_sort->setCurrentIndex(qMax(0, m_sort->findData(APPLICATION->settings()->get("InstSortMode"))));
        applyStyle();
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
    if (m_pages->currentWidget() == m_homePage)
        m_scrollPositions[m_libraryOnly] = m_homeScroll->verticalScrollBar()->value();
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
    m_skinsButton->setChecked(page->property("chromaSkinPage").toBool());
}

void LauncherHome::showHomePage(bool libraryOnly)
{
    if (m_pages->currentWidget() == m_homePage)
        m_scrollPositions[m_libraryOnly] = m_homeScroll->verticalScrollBar()->value();
    m_pages->setCurrentWidget(m_homePage);
    setLibraryOnly(libraryOnly);
    const int position = m_scrollPositions[libraryOnly];
    m_homeScroll->verticalScrollBar()->setValue(position);
    const int restored = m_homeScroll->verticalScrollBar()->value();
    QTimer::singleShot(0, this, [this, libraryOnly, position, restored] {
        // A user may scroll before the next layout pass. Do not undo that input.
        if (m_pages->currentWidget() == m_homePage && m_libraryOnly == libraryOnly &&
            m_homeScroll->verticalScrollBar()->value() == restored)
            m_homeScroll->verticalScrollBar()->setValue(position);
    });
}

void LauncherHome::setLibraryOnly(bool enabled)
{
    m_libraryOnly = enabled;
    const bool homeVisible = m_pages->currentWidget() == m_homePage;
    m_homeButton->setChecked(homeVisible && !enabled);
    m_libraryButton->setChecked(homeVisible && enabled);
    if (homeVisible)
        m_skinsButton->setChecked(false);
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

bool LauncherHome::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::DragEnter && event->type() != QEvent::DragMove && event->type() != QEvent::DragLeave &&
        event->type() != QEvent::Drop)
        return QWidget::eventFilter(watched, event);

    auto* rail = static_cast<SidebarPanel*>(m_pinsScroll->parentWidget());
    if (!rail->isEnabled())
        return false;
    if (event->type() == QEvent::DragLeave) {
        rail->setDropActive(false);
        event->accept();
        return true;
    }
    auto* drop = static_cast<QDropEvent*>(event);
    const QString id = QString::fromUtf8(drop->mimeData()->data("application/x-instanceid"));
    const bool valid = !id.isEmpty() && APPLICATION->instances()->getInstanceById(id) && (drop->possibleActions() & Qt::MoveAction);
    rail->setDropActive(valid && event->type() != QEvent::Drop);
    if (!valid) {
        drop->ignore();
        return true;
    }
    if (event->type() == QEvent::Drop && !APPLICATION->settings()->get("ChromaPinnedInstances").toStringList().contains(id))
        togglePin(id);
    drop->setDropAction(Qt::MoveAction);
    drop->accept();
    return true;
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
            button = new ClayToolButton(m_pinsScroll->widget());
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
    {
        QSignalBlocker blocker(m_sort);
        m_sort->setCurrentIndex(qMax(0, m_sort->findData(APPLICATION->settings()->get("InstSortMode"))));
    }
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
    QStringList signature;
    for (int i = 0; i < qMin(m_recentLimit, int(recent.size())); ++i) {
        const auto& instance = recent.at(i);
        signature << instance->id() << instance->name() << QString::number(instance->lastLaunch()) << QString::number(instance->isRunning())
                  << QString::number(instance->canLaunch())
                  << QString::number(APPLICATION->icons()->getIcon(instance->iconKey()).cacheKey());
    }
    // Filtering, selection and unrelated downloads often emit model updates.
    // Keep existing cards (and their keyboard focus) when their contents match.
    if (signature != m_recentSignature || m_recentRows->count() == 0) {
        m_recentSignature = signature;
        QString focusedId;
        if (auto* focused = QApplication::focusWidget(); focused && m_recent->isAncestorOf(focused))
            focusedId = focused->property("recentInstanceId").toString();
        while (auto* item = m_recentRows->takeAt(0)) {
            if (item->widget()) {
                item->widget()->hide();
                item->widget()->deleteLater();
            }
            delete item;
        }
        if (recent.isEmpty()) {
            auto* welcome = label(tr("No recently played instances."), "welcome", m_recent);
            welcome->setWordWrap(true);
            m_recentRows->addWidget(welcome);
        }
        for (int i = 0; i < qMin(m_recentLimit, int(recent.size())); ++i) {
            auto instance = recent.at(i);
            auto* row = new ClayPanel(m_recent);
            row->setObjectName("recentInstance");
            row->setProperty("featured", i == 0);
            row->setProperty("clayTint", QColor(i == 0 ? "#E8DDFC" : (i == 1 ? "#E2F2FB" : "#FBE4EF")));
            row->setProperty("clayDarkTint", QColor(i == 0 ? "#392A51" : (i == 1 ? "#203B48" : "#482B3F")));
            row->setMinimumHeight(i == 0 ? 164 : 82);
            row->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
            auto* cardLayout = new QVBoxLayout(row);
            cardLayout->setContentsMargins(20, 12, 20, 12);
            cardLayout->setSpacing(12);
            auto* rowLayout = new QHBoxLayout();
            rowLayout->setSpacing(10);
            cardLayout->addLayout(rowLayout, 1);
            auto* icon = new QLabel(row);
            const int iconSize = i == 0 ? 64 : 40;
            icon->setPixmap(APPLICATION->icons()->getIcon(instance->iconKey()).pixmap(iconSize, iconSize));
            rowLayout->addWidget(icon);
            auto* description = new QVBoxLayout();
            description->setContentsMargins(0, 0, 0, 0);
            description->setSpacing(4);
            auto* name = new HeaderTitleLabel(instance->name(), row);
            name->setTextFormat(Qt::PlainText);
            name->setProperty("role", i == 0 ? "featureTitle" : "strong");
            name->setMinimumWidth(0);
            name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
            name->ensurePolished();
            name->setMinimumHeight(name->fontMetrics().height() + 2);
            name->setToolTip(instance->name());
            description->addWidget(name);
            auto lastPlayed = QDateTime::fromMSecsSinceEpoch(instance->lastLaunch()).toString(tr("MMM d, yyyy"));
            auto* subtitle = new HeaderTitleLabel(instance->isRunning() ? tr("Running") : tr("Last played %1").arg(lastPlayed), row);
            subtitle->setTextFormat(Qt::PlainText);
            subtitle->setProperty("role", "muted");
            subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
            subtitle->setToolTip(subtitle->text());
            subtitle->ensurePolished();
            subtitle->setMinimumHeight(subtitle->fontMetrics().height() + 2);
            description->addWidget(subtitle);
            rowLayout->addLayout(description, 1);
            auto* play = new ClayToolButton(row);
            play->setText(instance->isRunning() ? tr("Running") : tr("Play"));
            play->setIcon(QIcon::fromTheme("launch"));
            play->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            play->setProperty("role", "primary");
            play->setProperty("claySymbolic", true);
            play->setMinimumHeight(48);
            play->setAccessibleName(tr("Play %1").arg(instance->name()));
            play->setEnabled(instance->canLaunch() && !instance->isRunning());
            const auto id = instance->id();
            play->setProperty("recentInstanceId", id);
            connect(play, &QToolButton::clicked, this, [this, id] { emit launchRequested(id); });
            if (i == 0) {
                auto* footer = new QHBoxLayout();
                auto* invitation = label(tr("Your world is waiting."), "muted", row);
                invitation->setWordWrap(true);
                footer->addWidget(invitation, 1);
                footer->addWidget(play);
                cardLayout->addLayout(footer);
            } else {
                rowLayout->addWidget(play);
            }
            m_recentRows->addWidget(row, i, 0);
            if (!focusedId.isEmpty() && focusedId == id && play->isEnabled())
                play->setFocus(Qt::OtherFocusReason);
        }
        layoutRecentCards();
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
    layoutRecentCards();
}

void LauncherHome::layoutRecentCards()
{
    // The same three native cards form a bento at wide sizes and a single
    // column on compact displays. Model order and keyboard order stay intact.
    const bool wide = width() >= 1180 && m_recentRows->count() > 1;
    if (m_libraryControls) {
        m_libraryControls->removeItem(m_libraryTitleRow);
        m_libraryControls->removeItem(m_libraryFilters);
        m_libraryControls->addLayout(m_libraryTitleRow, 0, 0, 1, width() >= 1180 ? 1 : 2);
        m_libraryControls->addLayout(m_libraryFilters, width() >= 1180 ? 0 : 2, width() >= 1180 ? 1 : 0, 1, width() >= 1180 ? 1 : 2);
    }
    QList<QWidget*> cards;
    while (auto* item = m_recentRows->takeAt(0)) {
        if (item->widget())
            cards.append(item->widget());
        delete item;
    }
    m_recentRows->setColumnStretch(0, wide ? 6 : 1);
    m_recentRows->setColumnStretch(1, wide ? 5 : 0);
    for (int i = 0; i < cards.size(); ++i) {
        if (wide && i == 0)
            m_recentRows->addWidget(cards[i], 0, 0, cards.size() - 1, 1);
        else
            m_recentRows->addWidget(cards[i], wide ? i - 1 : i, wide ? 1 : 0);
    }
}

void LauncherHome::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    if (event->type() == QEvent::PaletteChange && m_sort && m_clayStyle != Clay::enabled())
        applyStyle();
}

void LauncherHome::retranslate()
{
    m_recentSignature.clear();
    m_search->setPlaceholderText(tr("Search your instances"));
    m_search->setAccessibleName(tr("Search your instances"));
    m_sort->setItemText(0, tr("Name"));
    m_sort->setItemText(1, tr("Last played"));
    m_sort->setItemText(2, tr("Manual"));
    m_skinsButton->setText(tr("Skins"));
    m_skinsButton->setToolTip(tr("Skins"));
    m_skinsButton->setAccessibleName(tr("Skins"));
    findChild<QLabel*>("recentTitle")->setText(tr("Recent instances"));
    findChild<QLabel*>("libraryTitle")->setText(tr("Your library"));
    findChild<QLabel*>("playingLabel")->setText(tr("PLAYING AS"));
    findChild<QLabel*>("selectedLabel")->setText(tr("INSTANCE DETAILS"));
    findChild<QToolButton*>("moreInstanceActions")->setText(tr("More instance actions"));
    setLibraryOnly(m_libraryOnly);
    refresh();
}

void LauncherHome::applyStyle()
{
    m_clayStyle = Clay::enabled();
    layout()->setContentsMargins(m_clayStyle ? 12 : 0, m_clayStyle ? 10 : 0, m_clayStyle ? 12 : 0, m_clayStyle ? 10 : 0);
    layout()->setSpacing(m_clayStyle ? 8 : 0);
    m_view->viewport()->setAutoFillBackground(!m_clayStyle);
    if (m_clayStyle) {
        setStyleSheet(QStringLiteral(R"(
            QWidget#launcherHome { background: palette(window); }
            QWidget#launcherHome QLabel { background: transparent; }
            QFrame#homeHeader, QFrame#homeRail, QFrame#recentInstance,
            QFrame#homeDetailsPanel { background: transparent; border: none; }
            QWidget#homeLibrary, QWidget#instanceLibrary { background: transparent; border: none; }
            QScrollArea#homeContentScroll, QScrollArea#pinnedInstances,
            QScrollArea#homeDetails { background: transparent; border: none; }
            QFrame#homeDivider { background: palette(mid); border: none; }
            QLabel[role="brand"] { font-family: "Nunito"; font-size: 25px; font-weight: 900; }
            QLabel[role="heading"] { font-family: "Nunito"; font-size: 30px; font-weight: 900; }
            QLabel#libraryTitle { font-size: 27px; }
            QLabel[role="featureTitle"] { font-family: "Nunito"; font-size: 25px; font-weight: 900; }
            QLabel[role="strong"] { font-family: "Nunito"; font-size: 16px; font-weight: 800; }
            QLabel[role="body"] { font-size: 14px; font-weight: 500; }
            QLabel[role="muted"], QLabel[role="eyebrow"] { color: palette(placeholder-text); font-size: 12px; }
            QLabel[role="eyebrow"] { font-family: "Nunito"; font-weight: 800; }
            QLabel[role="welcome"], QLabel[role="empty"] {
                background: palette(alternate-base); border: 1px solid palette(light);
                border-radius: 24px; padding: 26px; color: palette(placeholder-text); font-size: 15px;
            }
            QLabel#runningStatus, QLabel#libraryCount {
                background: palette(alternate-base); border: 1px solid palette(light);
                border-radius: 20px; min-height: 20px; padding: 10px 14px; font-family: "Nunito"; font-weight: 800;
            }
            QWidget#launcherHome QToolButton {
                background: transparent; border: none; padding: 5px 14px;
                font-family: "Nunito"; font-size: 13px; font-weight: 800; color: palette(button-text);
            }
            QWidget#launcherHome QToolButton[role="navigation"] { padding: 0; }
            QWidget#launcherHome QToolButton:disabled { color: palette(placeholder-text); }
            QWidget#launcherHome QLineEdit#librarySearch {
                background: transparent; border: none; padding: 0 20px; font-size: 14px;
                selection-background-color: palette(highlight); selection-color: palette(highlighted-text);
            }
            QWidget#launcherHome QComboBox {
                background: palette(button); color: palette(text); border: 2px solid palette(light);
                border-radius: 20px; padding: 0 16px;
            }
            QWidget#launcherHome QComboBox:focus { border-color: palette(link); }
            QWidget#launcherHome QComboBox::drop-down {
                border: none; background: transparent; width: 26px; subcontrol-origin: padding;
                subcontrol-position: center right;
            }
        )") + Clay::scrollBarStyleSheet(QStringLiteral("QWidget#launcherHome")));
        m_view->doItemsLayout();
        return;
    }
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
    m_view->doItemsLayout();
}
