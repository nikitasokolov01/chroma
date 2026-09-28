// SPDX-License-Identifier: GPL-3.0-only
#include "SmoothScroll.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QVariantAnimation>
#include <QWheelEvent>

#include "ui/themes/ClayStyle.h"

void SmoothScroll::install(QAbstractScrollArea* area)
{
    if (area && !area->findChild<QObject*>("chromaSmoothScroll", Qt::FindDirectChildrenOnly))
        new SmoothScroll(area);
}

SmoothScroll::SmoothScroll(QAbstractScrollArea* area) : QObject(area), m_area(area), m_animation(new QVariantAnimation(this))
{
    setObjectName("chromaSmoothScroll");
    area->viewport()->installEventFilter(this);
    area->installEventFilter(this);
    m_animation->setDuration(Clay::Motion::Scroll);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        if (!Clay::motionAllowed() || !m_area->isVisible()) {
            m_animation->stop();
            return;
        }
        m_settingValue = true;
        m_area->verticalScrollBar()->setValue(value.toInt());
        m_settingValue = false;
    });
    connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (!m_settingValue)
            m_animation->stop();
    });
    connect(area->verticalScrollBar(), &QScrollBar::sliderPressed, m_animation, &QVariantAnimation::stop);
    connect(area->verticalScrollBar(), &QScrollBar::rangeChanged, m_animation, &QVariantAnimation::stop);
}

bool SmoothScroll::eventFilter(QObject*, QEvent* event)
{
    if (event->type() == QEvent::Hide || event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress)
        m_animation->stop();
    if (event->type() != QEvent::Wheel)
        return false;
    auto* wheel = static_cast<QWheelEvent*>(event);
    if (!Clay::enabled() || !Clay::motionAllowed() || !wheel->pixelDelta().isNull() || wheel->angleDelta().y() == 0 ||
        wheel->modifiers() != Qt::NoModifier || wheel->phase() != Qt::NoScrollPhase) {
        m_animation->stop();
        return false;
    }
    auto* bar = m_area->verticalScrollBar();
    const int delta = qRound(wheel->angleDelta().y() / 120.0 * bar->singleStep() * QApplication::wheelScrollLines());
    if (delta == 0)
        return false;
    const bool running = m_animation->state() == QAbstractAnimation::Running;
    const bool reversed = running && ((m_target > bar->value() && delta > 0) || (m_target < bar->value() && delta < 0));
    const int start = running && !reversed ? m_target : bar->value();
    m_target = qBound(bar->minimum(), start - delta, bar->maximum());
    if (m_target == bar->value()) {
        m_animation->stop();
        return false;
    }
    m_animation->stop();
    {
        const QSignalBlocker blocker(m_animation);
        m_animation->setCurrentTime(0);
        m_animation->setStartValue(bar->value());
        m_animation->setEndValue(m_target);
    }
    m_animation->start();
    wheel->accept();
    return true;
}
