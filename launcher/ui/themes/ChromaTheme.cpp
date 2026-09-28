// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2024 Tayou <git@tayou.org>
 *  Copyright (C) 2024 TheKodeToad <TheKodeToad@proton.me>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */
#include "ChromaTheme.h"

#include <QDebug>
#include <QFontDatabase>
#include <QObject>
#include "AccentColor.h"
#include "Application.h"
#include "ClayStyle.h"

static void registerClayFonts()
{
    // Static fonts work on the Qt 6.5 Windows font backend as well as newer
    // runtimes. Register once, even when the accent or theme changes.
    static const bool registered = [] {
        Q_INIT_RESOURCE(chroma_fonts);
        Q_INIT_RESOURCE(chroma_style);
        const char* files[] = { ":/fonts/DMSans-400.ttf", ":/fonts/DMSans-500.ttf", ":/fonts/DMSans-700.ttf",
                                ":/fonts/Nunito-700.ttf", ":/fonts/Nunito-800.ttf", ":/fonts/Nunito-900.ttf" };
        for (const auto* file : files) {
            if (QFontDatabase::addApplicationFont(QString::fromLatin1(file)) < 0)
                qWarning() << "Unable to load bundled interface font:" << file;
        }
        return true;
    }();
    Q_UNUSED(registered)
}

QString ChromaTheme::id()
{
    return m_dark ? "chroma-dark" : "chroma";
}

QString ChromaTheme::name()
{
    return m_dark ? QObject::tr("Chroma Dark") : QObject::tr("Chroma");
}

QPalette ChromaTheme::colorScheme()
{
    const auto& colors = Clay::colors(m_dark);
    const QColor accent = AccentColor::fromSetting(APPLICATION->settings()->get("AccentColor").toString());
    const QColor linkSurface = m_dark ? colors.Surface : colors.Canvas;
    QPalette palette;
    palette.setColor(QPalette::Window, colors.Canvas);
    palette.setColor(QPalette::WindowText, colors.Foreground);
    palette.setColor(QPalette::Base, colors.Surface);
    palette.setColor(QPalette::AlternateBase, colors.Input);
    palette.setColor(QPalette::ToolTipBase, colors.Surface);
    palette.setColor(QPalette::ToolTipText, colors.Foreground);
    palette.setColor(QPalette::Text, colors.Foreground);
    palette.setColor(QPalette::Button, colors.Surface);
    palette.setColor(QPalette::ButtonText, colors.Foreground);
    palette.setColor(QPalette::BrightText, m_dark ? QColor("#FF80AD") : QColor("#AE1855"));
    palette.setColor(QPalette::Link, AccentColor::link(accent, linkSurface));
    palette.setColor(QPalette::LinkVisited, AccentColor::link(Clay::Pink, linkSurface));
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, AccentColor::foreground(accent));
    palette.setColor(QPalette::PlaceholderText, colors.Muted);
    palette.setColor(QPalette::Mid, m_dark ? QColor("#4A4058") : QColor("#D8D0E5"));
    palette.setColor(QPalette::Midlight, m_dark ? QColor("#3D334B") : QColor("#E6E0F0"));
    palette.setColor(QPalette::Dark, m_dark ? QColor("#14101B") : colors.Muted);
    palette.setColor(QPalette::Light, m_dark ? QColor("#4D425D") : QColor("#FFFFFF"));
    palette.setColor(QPalette::Shadow, m_dark ? QColor("#09070D") : QColor("#BFB5D2"));
    palette = fadeInactive(palette, fadeAmount(), fadeColor());
    // Disabled controls keep legible labels instead of fading below the
    // design system's minimum text contrast.
    for (const auto role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::Link })
        palette.setColor(QPalette::Disabled, role, colors.Muted);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                     AccentColor::foreground(palette.color(QPalette::Disabled, QPalette::Highlight)));
    return palette;
}

double ChromaTheme::fadeAmount()
{
    return 0.25;
}

QColor ChromaTheme::fadeColor()
{
    return Clay::colors(m_dark).Canvas;
}

bool ChromaTheme::hasStyleSheet()
{
    return true;
}

QString ChromaTheme::appStyleSheet()
{
    registerClayFonts();
    const auto& colors = Clay::colors(m_dark);
    const QColor accent = AccentColor::fromSetting(APPLICATION->settings()->get("AccentColor").toString());
    const QColor linkSurface = m_dark ? colors.Surface : colors.Canvas;
    // Keep native font sizes and control geometry in dense settings pages.
    // Text editors are deliberately absent: console and code fonts are user
    // preferences. Home controls use the painted Clay widgets for depth.
    return QStringLiteral(R"(
        QLabel, QAbstractButton, QLineEdit, QComboBox, QAbstractSpinBox,
        QMenu, QMenuBar, QTabBar, QHeaderView, QAbstractItemView, QGroupBox {
            font-family: "DM Sans";
        }
        QLabel[role="heading"], QLabel[role="brand"], QLabel[role="strong"], QLabel[role="eyebrow"],
        QLabel[clayRole="heading"], QLabel[clayRole="number"], QLabel[clayRole="label"] {
            font-family: "Nunito"; font-weight: 800;
        }
        QToolTip {
            color: %1; background-color: %2; border: 1px solid %8;
            border-radius: 20px; padding: 8px 12px;
        }
        QPushButton {
            color: %1; border: 2px solid %9; border-radius: 20px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 %2, stop:1 %3);
            padding: 8px 16px; min-height: 24px; font-weight: 700;
        }
        QPushButton:hover { border-color: %4; }
        QPushButton:focus { border-color: %4; }
        QPushButton:pressed, QPushButton:checked { background: %3; border-color: %4; }
        QPushButton:disabled { color: %5; background: %3; }
        QLineEdit {
            color: %1; background: %3; border: 2px solid %9;
            placeholder-text-color: %5;
            border-radius: 20px; padding: 5px 12px;
            selection-background-color: %6; selection-color: %7;
        }
        QLineEdit:focus { background: %2; border-color: %4; }
        QLineEdit:disabled { color: %5; }
        QAbstractButton:focus, QComboBox:focus, QAbstractSpinBox:focus { border-color: %4; }
        QLabel[tone="muted"] { color: %5; }
        QLabel[tone="error"] { color: palette(bright-text); }
        QLabel[tone="success"] { color: %4; }
        QTabBar::tab { padding: 8px 14px; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { border-bottom-color: %4; }
        QTabBar::tab:hover { background: %3; }
        QTabBar::tab:focus { border-bottom: 2px solid %4; }
        QComboBox::drop-down {
            subcontrol-origin: padding; subcontrol-position: top right;
            width: 28px; border: none;
        }
        QComboBox::down-arrow { image: url(%10); width: 12px; height: 12px; }
    )")
               .arg(colors.Foreground.name(), colors.Surface.name(), colors.Input.name(), AccentColor::link(accent, linkSurface).name(),
                    colors.Muted.name(), accent.name(), AccentColor::foreground(accent).name(),
                    m_dark ? QStringLiteral("#4A4058") : colors.Input.name(),
                    m_dark ? QStringLiteral("#4A4058") : QStringLiteral("transparent"))
               .arg(m_dark ? QStringLiteral(":/chroma/style/chevron-down-dark.svg")
                           : QStringLiteral(":/chroma/style/chevron-down-light.svg")) +
           Clay::menuStyleSheet() + Clay::scrollBarStyleSheet(QString());
}

QString ChromaTheme::tooltip()
{
    return QObject::tr("Soft clay surfaces, rounded typography, and your chosen accent color.");
}
