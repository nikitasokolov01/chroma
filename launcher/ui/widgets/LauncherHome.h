// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QStringList>
#include <QWidget>

class QAction;
class QComboBox;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QResizeEvent;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;
class InstanceView;
class InstanceProxyModel;
class ChromaUpdater;
class UpdateNotice;
class WindowControls;

// The home screen shares Prism's actions and instance view. Launching, account
// handling, imports, editing, and drag/drop remain owned by their existing code.
class LauncherHome : public QWidget {
    Q_OBJECT
   public:
    struct Actions {
        QAction* add;
        QAction* usePrismFolder;
        QAction* settings;
        QAction* accounts;
        QAction* launch;
        QAction* stop;
        QAction* edit;
        QAction* folder;
        QAction* group;
        QAction* news;
        QMenu* instanceMenu;
        QMenu* launcherMenu;
    };

    LauncherHome(InstanceView* view, InstanceProxyModel* model, const Actions& actions, QWidget* parent = nullptr);
    void setSelectedInstance(const QString& id);
    void clearSearch();
    QWidget* pageHost() const { return m_pageHost; }
    void showPage(QWidget* page, const QString& title);
    void showHomePage(bool libraryOnly = false);
    bool libraryOnly() const { return m_libraryOnly; }
    void toggleSelectedPin();
    bool selectedInstancePinned() const;
    void setUpdater(ChromaUpdater* updater);

   signals:
    void launchRequested(const QString& id);
    void homeRequested(bool libraryOnly);
    void skinsRequested();
    void instanceOpenRequested(const QString& id);
    void pinsChanged();

   protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void refresh();
    void refreshSelection();
    void applyStyle();
    void retranslate();
    void setLibraryOnly(bool enabled);
    void refreshPins();
    void layoutRecentCards();
    void togglePin(const QString& id);

    InstanceView* m_view;
    InstanceProxyModel* m_model;
    Actions m_actions;
    QString m_selectedId;
    QTimer* m_refreshTimer = nullptr;
    int m_recentLimit = 3;
    bool m_libraryOnly = false;
    int m_scrollPositions[2] = { 0, 0 };
    QStringList m_recentSignature;
    bool m_clayStyle = false;
    QStackedWidget* m_pages;
    QWidget* m_homePage;
    QWidget* m_pageHost;
    QScrollArea* m_homeScroll;
    QScrollArea* m_pinsScroll;
    QVBoxLayout* m_pinnedRows;
    QVBoxLayout* m_contentLayout;
    QToolButton* m_pinButton;
    QHash<QString, QToolButton*> m_pinButtons;
    QLineEdit* m_search;
    QComboBox* m_sort = nullptr;
    QWidget* m_recent;
    QGridLayout* m_recentRows;
    QGridLayout* m_libraryControls = nullptr;
    QHBoxLayout* m_libraryTitleRow = nullptr;
    QHBoxLayout* m_libraryFilters = nullptr;
    QWidget* m_details;
    QLabel* m_running;
    QLabel* m_count;
    QLabel* m_empty;
    QLabel* m_selectedIcon;
    QLabel* m_selectedName;
    QLabel* m_selectedInfo;
    QLabel* m_playtime;
    QToolButton* m_homeButton;
    QToolButton* m_libraryButton;
    QToolButton* m_skinsButton;
    QToolButton* m_profileButton;
    QLabel* m_pageTitle;
    UpdateNotice* m_updateNotice = nullptr;
    WindowControls* m_windowControls = nullptr;
};
