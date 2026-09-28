// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QPointer>
#include <QWidget>

class QLabel;
class QMainWindow;
class QScrollArea;
class QStackedLayout;

// Adapts existing Qt screens to a single launcher window while preserving their
// accept/reject, validation, and task lifetimes. Nested dialogs remain nested.
class InlineWorkspace : public QWidget {
    Q_OBJECT
   public:
    explicit InlineWorkspace(QMainWindow* window, QWidget* parent = nullptr);
    ~InlineWorkspace() override;
    void present(QWidget* page, const QString& title = {});
    bool closeAllPages();
    int pageCount() const { return m_active.size(); }
    QWidget* currentPage() const;
    QWidget* activePage(const QString& objectName) const;
    bool navigationBlocked() const;

   signals:
    void pagePresented(const QString& title);
    void emptied();
    void navigationLockChanged(bool locked);

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

   private:
    struct Page {
        QPointer<QWidget> widget;
        QPointer<QWidget> wrapper;
        QString title;
    };
    void leave(QWidget* page);
    void presentPage(QWidget* page, const QString& title, bool deferShow);
    void updateCurrent();
    void applyStyle();

    QPointer<QMainWindow> m_window;
    QWidget* m_canvas;
    QStackedLayout* m_stack;
    QLabel* m_title;
    QHash<QWidget*, Page> m_pages;
    QList<QWidget*> m_active;
    bool m_adopting = false;
    bool m_closing = false;
    bool m_clayStyle = false;
};
