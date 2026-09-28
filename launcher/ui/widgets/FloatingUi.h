// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QPointer>

class QMenu;
class QWidget;

// Enhances Qt's native popup behavior. QMenu retains ownership of its event
// loop, dismissal, placement, keyboard navigation and action activation.
class FloatingUi : public QObject {
   public:
    static void install();
    static void anchor(QMenu* menu, QWidget* trigger);

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    explicit FloatingUi(QObject* parent);
};
