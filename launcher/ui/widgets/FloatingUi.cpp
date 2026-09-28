// SPDX-License-Identifier: GPL-3.0-only
#include "FloatingUi.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QMenu>
#include <QPropertyAnimation>
#include <QScreen>
#include <QWidget>

#include "ui/themes/ClayStyle.h"

FloatingUi::FloatingUi(QObject* parent) : QObject(parent)
{
    setObjectName("chromaFloatingUi");
    qApp->installEventFilter(this);
}

void FloatingUi::install()
{
    if (!qApp->findChild<QObject*>("chromaFloatingUi", Qt::FindDirectChildrenOnly))
        new FloatingUi(qApp);
}

void FloatingUi::anchor(QMenu* menu, QWidget* trigger)
{
    if (!menu || !trigger)
        return;
    // Shared QAction menus can be opened by more than one button. Resolve the
    // current trigger just before popup, without changing its ownership.
    menu->setProperty("chromaPopupAnchor", QVariant::fromValue(QPointer<QWidget>(trigger)));
}

bool FloatingUi::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Polish || event->type() == QEvent::EnabledChange || event->type() == QEvent::Enter) {
        if (auto* widget = qobject_cast<QWidget*>(watched);
            widget && (qobject_cast<QAbstractButton*>(widget) || qobject_cast<QComboBox*>(widget))) {
            widget->setCursor(widget->isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
    }
    auto* menu = qobject_cast<QMenu*>(watched);
    if (!menu)
        return false;
    if (event->type() == QEvent::Show && Clay::enabled()) {
        const auto trigger = menu->property("chromaPopupAnchor").value<QPointer<QWidget>>();
        if (trigger && trigger->isVisible()) {
            const QRect screen = trigger->screen()->availableGeometry();
            const QRect anchor(trigger->mapToGlobal(QPoint()), trigger->size());
            QPoint position = menu->pos();
            const int below = anchor.bottom() + 1 + Clay::Space::Small;
            const int above = anchor.top() - Clay::Space::Small - menu->height();
            // Only add a gap when the whole popup fits. Qt still handles menus
            // taller than the display and layouts requiring horizontal fallback.
            if (below + menu->height() <= screen.bottom() + 1)
                position.setY(below);
            else if (above >= screen.top())
                position.setY(above);
            position.setX(qBound(screen.left(), position.x(), qMax(screen.left(), screen.right() + 1 - menu->width())));
            menu->move(position);
        }
        // A fast reveal never delays input or closing. Native popup windows
        // provide the compositor shadow; avoid expensive effects on child content.
        const auto platform = QGuiApplication::platformName();
        if (Clay::motionAllowed() && (platform == "windows" || platform == "xcb" || platform == "cocoa")) {
            auto* animation = menu->findChild<QPropertyAnimation*>("chromaPopupReveal", Qt::FindDirectChildrenOnly);
            if (!animation) {
                animation = new QPropertyAnimation(menu, "windowOpacity", menu);
                animation->setObjectName("chromaPopupReveal");
                animation->setDuration(Clay::Motion::Popup);
                animation->setEasingCurve(QEasingCurve::OutCubic);
            }
            animation->stop();
            animation->setStartValue(0.85);
            animation->setEndValue(1.0);
            animation->start();
        }
    } else if (event->type() == QEvent::Hide) {
        if (auto* animation = menu->findChild<QPropertyAnimation*>("chromaPopupReveal", Qt::FindDirectChildrenOnly)) {
            animation->stop();
            menu->setWindowOpacity(1.0);
        }
        menu->setProperty("chromaPopupAnchor", QVariant());
    }
    return false;
}
