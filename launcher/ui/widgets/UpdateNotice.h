// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QFrame>
#include <QPointer>

class ChromaUpdater;
class QLabel;
class QPushButton;
class QBoxLayout;

// A persistent launcher-wide alert. Reviewing notes is always an explicit
// action; discovering a release does not navigate away from the current page.
class UpdateNotice : public QFrame {
    Q_OBJECT
   public:
    explicit UpdateNotice(QWidget* parent = nullptr);
    void setUpdater(ChromaUpdater* updater);

   protected:
    void resizeEvent(QResizeEvent* event) override;

   private:
    void refresh();
    QPointer<ChromaUpdater> m_updater;
    QBoxLayout* m_layout;
    QLabel* m_title;
    QLabel* m_summary;
    QPushButton* m_review;
    QPushButton* m_later;
};
