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

#include <QObject>
#include "AccentColor.h"
#include "Application.h"

QString ChromaTheme::id()
{
    return "chroma";
}

QString ChromaTheme::name()
{
    return QObject::tr("Chroma");
}

QPalette ChromaTheme::colorScheme()
{
    const QColor accent = AccentColor::fromSetting(APPLICATION->settings()->get("AccentColor").toString());
    const QColor background("#17191d");
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor("#202329"));
    darkPalette.setColor(QPalette::WindowText, QColor("#f3f4f7"));
    darkPalette.setColor(QPalette::Base, background);
    darkPalette.setColor(QPalette::AlternateBase, QColor("#262a31"));
    darkPalette.setColor(QPalette::ToolTipBase, QColor("#30343d"));
    darkPalette.setColor(QPalette::ToolTipText, QColor("#f3f4f7"));
    darkPalette.setColor(QPalette::Text, QColor("#f3f4f7"));
    darkPalette.setColor(QPalette::Button, QColor("#30343d"));
    darkPalette.setColor(QPalette::ButtonText, QColor("#f3f4f7"));
    darkPalette.setColor(QPalette::BrightText, QColor("#ff7b91"));
    darkPalette.setColor(QPalette::Link, AccentColor::link(accent, darkPalette.color(QPalette::Window)));
    darkPalette.setColor(QPalette::Highlight, accent);
    darkPalette.setColor(QPalette::HighlightedText, AccentColor::foreground(accent));
    darkPalette.setColor(QPalette::PlaceholderText, QColor("#979eaa"));
    darkPalette.setColor(QPalette::Mid, QColor("#3a404b"));
    darkPalette.setColor(QPalette::Dark, QColor("#111316"));
    darkPalette.setColor(QPalette::Light, QColor("#454c59"));
    return fadeInactive(darkPalette, fadeAmount(), fadeColor());
}

double ChromaTheme::fadeAmount()
{
    return 0.5;
}

QColor ChromaTheme::fadeColor()
{
    return QColor("#202329");
}

bool ChromaTheme::hasStyleSheet()
{
    return true;
}

QString ChromaTheme::appStyleSheet()
{
    return "QToolTip { color: #f3f4f7; background-color: #30343d; border: 1px solid #454c59; padding: 6px; }";
}

QString ChromaTheme::tooltip()
{
    return "";
}
