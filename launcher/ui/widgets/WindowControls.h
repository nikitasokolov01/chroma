// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QPointer>
#include <QWidget>

class QToolButton;

// Client-area caption controls. Closing always goes through QWidget::close(),
// allowing the launcher's close event to protect running tasks and unsaved work.
class WindowControls final : public QWidget {
    Q_OBJECT
   public:
    explicit WindowControls(QWidget* targetWindow, QWidget* parent = nullptr);

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void updateState();
    QPointer<QWidget> m_window;
    QToolButton* m_minimize;
    QToolButton* m_maximize;
    QToolButton* m_close;
};
