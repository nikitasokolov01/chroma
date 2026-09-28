// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "FusionTheme.h"

class ChromaTheme : public FusionTheme {
   public:
    explicit ChromaTheme(bool dark = false) : m_dark(dark) {}
    QString id() override;
    QString name() override;
    QString tooltip() override;
    bool hasStyleSheet() override;
    QString appStyleSheet() override;
    QPalette colorScheme() override;
    double fadeAmount() override;
    QColor fadeColor() override;

   private:
    bool m_dark;
};
