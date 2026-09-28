// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

class QAbstractScrollArea;
class QVariantAnimation;

// Smooth discrete mouse wheels only; high-resolution trackpad gestures and
// native scrollbar/keyboard input keep their original behavior.
class SmoothScroll final : public QObject {
   public:
    static void install(QAbstractScrollArea* area);

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    explicit SmoothScroll(QAbstractScrollArea* area);
    QAbstractScrollArea* m_area;
    QVariantAnimation* m_animation;
    int m_target = 0;
    bool m_settingValue = false;
};
