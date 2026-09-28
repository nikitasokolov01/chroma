// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QPointer>
#include <QWidget>

class QComboBox;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QVBoxLayout;
class QModelIndex;

// Presentation only: providers retain their models, selection handlers and install tasks.
class ModpackBrowser : public QWidget {
    Q_OBJECT
   public:
    ModpackBrowser(QLineEdit* search, QComboBox* sort, QWidget* catalog, QComboBox* versions, QWidget* parent = nullptr);
    void addView(QListView* view, QTextBrowser* description);
    void setFilterWidget(QWidget* filters, QPushButton* button);
    void addStatusWidget(QWidget* widget);
    void setNotice(const QString& text);
    static void install(QWidget* page, ModpackBrowser* browser);

   public slots:
    void showResults();

   protected:
    void resizeEvent(QResizeEvent* event) override;

   private:
    void openDetails(QListView* view, QTextBrowser* description, const QModelIndex& index);
    void updateLayout();
    void updateToolbar();

    QWidget* m_toolbar;
    QGridLayout* m_toolbarLayout;
    QLineEdit* m_search;
    QComboBox* m_sort;
    QPushButton* m_filterButton = nullptr;
    QWidget* m_filters = nullptr;
    QWidget* m_catalog;
    QWidget* m_details;
    QHBoxLayout* m_body;
    QVBoxLayout* m_root;
    QStackedWidget* m_descriptions;
    QLabel* m_title;
    QLabel* m_artwork;
    QLabel* m_notice;
    QPointer<QListView> m_activeView;
    bool m_detailOpen = false;
    bool m_compactToolbar = false;
};
