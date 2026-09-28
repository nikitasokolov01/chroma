// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
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

#include "InstanceDelegate.h"

#include <QFontMetrics>
#include <QIcon>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTextEdit>

#include <array>

#include "BaseInstance.h"
#include "InstanceList.h"
#include "InstanceProxyModel.h"
#include "InstanceView.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "ui/themes/ClayStyle.h"

namespace {
constexpr int CardWidth = 164;
constexpr int CardPadding = 12;
constexpr int ArtworkHeight = 124;
constexpr int IconSize = 104;
constexpr int TitleGap = 10;
constexpr int DetailGap = 4;
constexpr int BottomPadding = 16;
constexpr int ClayCardWidth = 192;
constexpr int ClayShadowInset = 10;
constexpr int ClayTopInset = 8;
constexpr int ClayBottomInset = 14;

QFont titleFont(const QFont& font)
{
    QFont result(font);
    result.setBold(true);
    return result;
}

QFont clayTitleFont(const QFont& font)
{
    QFont result = Clay::headingFont();
    if (font.pixelSize() > 0)
        result.setPixelSize(font.pixelSize());
    else
        result.setPointSizeF(font.pointSizeF());
    return result;
}

QRect clayCardRect(const QRect& bounds)
{
    return bounds.adjusted(ClayShadowInset, ClayTopInset, -ClayShadowInset, -ClayBottomInset);
}

QRect clayTitleRect(const QRect& bounds, const QFont& font)
{
    const auto card = clayCardRect(bounds);
    return QRect(card.left() + CardPadding, card.top() + CardPadding + ArtworkHeight + TitleGap,
                 card.width() - CardPadding * 2, QFontMetrics(clayTitleFont(font)).height());
}

QColor blendColors(const QColor& background, const QColor& foreground, qreal amount)
{
    return QColor::fromRgbF(background.redF() * (1 - amount) + foreground.redF() * amount,
                            background.greenF() * (1 - amount) + foreground.greenF() * amount,
                            background.blueF() * (1 - amount) + foreground.blueF() * amount);
}

QRect artworkRect(const QRect& card)
{
    return QRect(card.left() + CardPadding, card.top() + CardPadding, card.width() - CardPadding * 2, ArtworkHeight);
}

QRect titleRect(const QRect& card, const QFont& font)
{
    return QRect(card.left() + CardPadding, card.top() + CardPadding + ArtworkHeight + TitleGap, card.width() - CardPadding * 2,
                 QFontMetrics(titleFont(font)).height());
}

QString instanceSummary(BaseInstance* instance, const QModelIndex& index)
{
    if (!instance)
        return {};

    auto minecraft = qobject_cast<MinecraftInstance*>(instance);
    if (!minecraft)
        return instance->typeName();

    auto profile = minecraft->getPackProfile();
    // Read cached component metadata only. Painting must never reload a pack or start network work.
    if (profile) {
        const QString version = profile->getComponentVersion("net.minecraft");
        if (!version.isEmpty())
            return InstanceProxyModel::formatSummary(version, profile->getModLoaders().value_or(ModPlatform::ModLoaderTypes{}));
    }
    const auto summary = index.data(InstanceProxyModel::InstanceSummaryRole).toString();
    return summary.isEmpty() ? instance->typeName() : summary;
}

void drawBadges(QPainter* painter, const QStyleOptionViewItem& option, const QRect& artwork, BaseInstance* instance, QIcon::Mode mode)
{
    QStringList badges;
    if (instance->isRunning())
        badges.append("status-running");
    else if (instance->hasCrashed() || instance->hasVersionBroken())
        badges.append("status-bad");
    if (instance->hasUpdateAvailable())
        badges.append("checkupdate");

    painter->save();
    constexpr int badgeSize = 28;
    for (int i = 0; i < badges.size(); ++i) {
        const QRect badge(artwork.right() - badgeSize - 4 - i * (badgeSize + 4), artwork.top() + 5, badgeSize, badgeSize);
        if (Clay::enabled()) {
            Clay::drawSurface(painter, badge, Clay::colors().Surface, badgeSize / 2);
        } else {
            painter->setPen(QPen(option.palette.color(QPalette::Mid), 1));
            painter->setBrush(option.palette.color(QPalette::Window));
            painter->drawEllipse(badge);
        }
        QIcon::fromTheme(badges.at(i)).paint(painter, badge.adjusted(3, 3, -3, -3), Qt::AlignCenter, mode);
    }
    painter->restore();
}

void drawProgress(QPainter* painter, const QStyleOptionViewItem& option, int value, int maximum)
{
    if (maximum <= 0 || value >= maximum)
        return;

    painter->save();
    const QRectF track(option.rect.left() + CardPadding, option.rect.bottom() - 8, option.rect.width() - CardPadding * 2, 4);
    const qreal progress = qBound(qreal(0), qreal(value) / qreal(maximum), qreal(1));
    painter->setPen(Qt::NoPen);
    painter->setBrush(option.palette.color(QPalette::Mid));
    painter->drawRoundedRect(track, 2, 2);
    if (progress > 0) {
        QRectF fill = track;
        fill.setWidth(track.width() * progress);
        painter->setBrush(option.palette.color(QPalette::Highlight));
        painter->drawRoundedRect(fill, 2, 2);
    }
    painter->restore();
}

void drawClayCard(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool enabled = option.state & QStyle::State_Enabled;
    const auto group = enabled ? QPalette::Active : QPalette::Disabled;
    const auto accent = option.palette.color(group, QPalette::Highlight);
    const auto outlineColor = option.palette.color(group, QPalette::Link);
    const auto foreground = option.palette.color(group, QPalette::Text);
    // The view uses a transparent QSS background so the ambient canvas shows
    // between cards. Its Base palette role is therefore not a surface color.
    const auto surface = Clay::colors().Surface;
    const bool dark = Clay::dark();
    const qreal lift = hovered && enabled && !(option.state & QStyle::State_Editing) && Clay::motionAllowed() ? 3 : 0;
    const QRect card = clayCardRect(option.rect);
    const QRect artwork = artworkRect(card);
    auto* instance = static_cast<BaseInstance*>(index.data(InstanceList::InstancePointerRole).value<void*>());

    // Stable instance colors survive filtering, reordering and renaming.
    static const std::array<QColor, 5> artworkColors{ Clay::Violet, Clay::Pink, Clay::Blue, Clay::Green, Clay::Amber };
    const QString colorKey = instance ? instance->id() : index.data(Qt::DisplayRole).toString();
    const auto& artworkAccent = artworkColors[qHash(colorKey, 0) % artworkColors.size()];

    painter->save();
    painter->setClipRect(option.rect, Qt::IntersectClip);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(0, -lift);
    Clay::drawSurface(painter, card, selected ? blendColors(surface, accent, dark ? 0.10 : 0.045) : surface,
                      Clay::Radius::Card, false, lift);

    QPainterPath artworkClip;
    artworkClip.addRoundedRect(QRectF(artwork), 24, 24);
    QLinearGradient artworkGradient(artwork.topLeft(), artwork.bottomRight());
    artworkGradient.setColorAt(0, blendColors(surface, artworkAccent, dark ? 0.18 : 0.10));
    artworkGradient.setColorAt(1, blendColors(surface, artworkAccent, dark ? 0.34 : 0.30));
    painter->fillPath(artworkClip, artworkGradient);
    painter->save();
    painter->setClipPath(artworkClip, Qt::IntersectClip);

    // A molded inset panel and soft icon orb retain the user's actual instance artwork.
    painter->setPen(QPen(QColor(255, 255, 255, dark ? 35 : 150), 2));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(QRectF(artwork).adjusted(1, 1, -1, -1), 23, 23);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(255, 255, 255, dark ? 16 : 60));
    painter->drawEllipse(QRectF(artwork.right() - 33, artwork.top() - 26, 76, 76));
    painter->drawEllipse(QRectF(artwork.left() - 26, artwork.bottom() - 20, 68, 68));
    constexpr int orbSize = 106;
    const QRectF orb(artwork.center().x() - orbSize / 2.0, artwork.center().y() - orbSize / 2.0, orbSize, orbSize);
    Clay::drawSurface(painter, orb, blendColors(surface, artworkAccent, 0.06), orbSize / 2.0);
    constexpr int clayIconSize = 88;
    const QRect icon(artwork.center().x() - clayIconSize / 2, artwork.center().y() - clayIconSize / 2, clayIconSize, clayIconSize);
    const auto iconMode = enabled ? QIcon::Normal : QIcon::Disabled;
    const auto iconState = option.state & QStyle::State_Open ? QIcon::On : QIcon::Off;
    option.icon.paint(painter, icon, Qt::AlignCenter, iconMode, iconState);
    painter->restore();

    const auto nameFont = clayTitleFont(option.font);
    const auto nameRect = clayTitleRect(option.rect, option.font);
    const auto alignment = QStyle::visualAlignment(option.direction, Qt::AlignLeft | Qt::AlignVCenter);
    painter->setFont(nameFont);
    painter->setPen(foreground);
    painter->drawText(nameRect, alignment, QFontMetrics(nameFont).elidedText(option.text, Qt::ElideRight, nameRect.width()));
    const QRect detailRect(nameRect.left(), nameRect.bottom() + DetailGap + 1, nameRect.width(), option.fontMetrics.height());
    painter->setFont(option.font);
    painter->setPen(option.palette.color(group, QPalette::PlaceholderText));
    painter->drawText(detailRect, alignment,
                      option.fontMetrics.elidedText(instanceSummary(instance, index), Qt::ElideRight, detailRect.width()));

    if (instance)
        drawBadges(painter, option, artwork, instance, iconMode);
    QStyleOptionViewItem progressOption(option);
    progressOption.rect = card.adjusted(0, 0, 0, -3);
    drawProgress(painter, progressOption, index.data(InstanceViewRoles::ProgressValueRole).toInt(),
                 index.data(InstanceViewRoles::ProgressMaximumRole).toInt());

    if (selected || hovered || (option.state & QStyle::State_HasFocus)) {
        painter->setBrush(Qt::NoBrush);
        QColor outline(outlineColor);
        if (!selected && !(option.state & QStyle::State_HasFocus))
            outline.setAlpha(95);
        painter->setPen(QPen(outline, selected ? 2.0 : 1.5));
        painter->drawRoundedRect(QRectF(card).adjusted(1, 1, -1, -1), Clay::Radius::Card - 1, Clay::Radius::Card - 1);
    }
    if (option.state & QStyle::State_HasFocus) {
        painter->setPen(QPen(outlineColor, 1.5, Qt::DashLine));
        painter->drawRoundedRect(QRectF(card).adjusted(5, 5, -5, -5), Clay::Radius::Card - 5, Clay::Radius::Card - 5);
    }
    painter->restore();
}
}  // namespace

ListViewDelegate::ListViewDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void ListViewDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    if (Clay::enabled()) {
        drawClayCard(painter, opt, index);
        return;
    }
    painter->save();
    painter->setClipRect(opt.rect);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool selected = opt.state & QStyle::State_Selected;
    const bool hovered = opt.state & QStyle::State_MouseOver;
    const bool enabled = opt.state & QStyle::State_Enabled;
    const auto group = !enabled ? QPalette::Disabled : (opt.state & QStyle::State_Active ? QPalette::Active : QPalette::Inactive);
    const QColor accent = opt.palette.color(group, QPalette::Highlight);
    const QColor foreground = opt.palette.color(group, QPalette::Text);
    const QColor surface = opt.palette.color(group, QPalette::Window);
    const QColor base = opt.palette.color(group, QPalette::Base);
    const QColor cardColor = blendColors(surface, selected ? accent : foreground, selected ? 0.12 : (hovered ? 0.08 : 0.035));
    const QColor borderColor = selected ? accent : blendColors(cardColor, hovered ? accent : foreground, hovered ? 0.6 : 0.1);

    const QRectF card = QRectF(opt.rect).adjusted(1, 1, -1, -1);
    painter->setPen(QPen(borderColor, selected ? 2 : 1));
    painter->setBrush(cardColor);
    painter->drawRoundedRect(card, 14, 14);

    const QRect artwork = artworkRect(opt.rect);
    QPainterPath artworkClip;
    artworkClip.addRoundedRect(QRectF(artwork), 10, 10);
    painter->fillPath(artworkClip, blendColors(base, accent, selected ? 0.12 : 0.045));
    painter->save();
    painter->setClipPath(artworkClip, Qt::IntersectClip);
    const QRect icon(artwork.center().x() - IconSize / 2, artwork.center().y() - IconSize / 2, IconSize, IconSize);
    const QIcon::Mode iconMode = enabled ? QIcon::Normal : QIcon::Disabled;
    const QIcon::State iconState = opt.state & QStyle::State_Open ? QIcon::On : QIcon::Off;
    opt.icon.paint(painter, icon, Qt::AlignCenter, iconMode, iconState);
    painter->restore();

    const QRect nameRect = titleRect(opt.rect, opt.font);
    const QFont nameFont = titleFont(opt.font);
    painter->setFont(nameFont);
    painter->setPen(foreground);
    const auto alignment = QStyle::visualAlignment(opt.direction, Qt::AlignLeft | Qt::AlignVCenter);
    painter->drawText(nameRect, alignment, QFontMetrics(nameFont).elidedText(opt.text, Qt::ElideRight, nameRect.width()));

    auto instance = static_cast<BaseInstance*>(index.data(InstanceList::InstancePointerRole).value<void*>());
    const QRect detailRect(nameRect.left(), nameRect.bottom() + DetailGap + 1, nameRect.width(), opt.fontMetrics.height());
    painter->setFont(opt.font);
    painter->setPen(blendColors(cardColor, foreground, 0.72));
    painter->drawText(detailRect, alignment,
                      opt.fontMetrics.elidedText(instanceSummary(instance, index), Qt::ElideRight, detailRect.width()));

    if (instance)
        drawBadges(painter, opt, artwork, instance, iconMode);
    drawProgress(painter, opt, index.data(InstanceViewRoles::ProgressValueRole).toInt(),
                 index.data(InstanceViewRoles::ProgressMaximumRole).toInt());

    if (opt.state & QStyle::State_HasFocus) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(accent, 1.5, Qt::DashLine));
        painter->drawRoundedRect(card.adjusted(3, 3, -3, -3), 11, 11);
    }
    painter->restore();
}

QSize ListViewDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    if (Clay::enabled()) {
        return QSize(ClayCardWidth, ClayTopInset + CardPadding + ArtworkHeight + TitleGap +
                                       QFontMetrics(clayTitleFont(opt.font)).height() + DetailGap + opt.fontMetrics.height() +
                                       BottomPadding + ClayBottomInset);
    }
    return QSize(CardWidth, CardPadding + ArtworkHeight + TitleGap + QFontMetrics(titleFont(opt.font)).height() + DetailGap +
                                opt.fontMetrics.height() + BottomPadding);
}

class NoReturnTextEdit : public QTextEdit {
    Q_OBJECT
   public:
    explicit NoReturnTextEdit(QWidget* parent) : QTextEdit(parent)
    {
        setTextInteractionFlags(Qt::TextEditorInteraction);
        setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
    }
    bool event(QEvent* event) override
    {
        auto eventType = event->type();
        if (eventType == QEvent::KeyPress || eventType == QEvent::KeyRelease) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            auto key = keyEvent->key();
            if ((key == Qt::Key_Return || key == Qt::Key_Enter) && eventType == QEvent::KeyPress) {
                emit editingDone();
                return true;
            }
            if (key == Qt::Key_Tab) {
                return true;
            }
        }
        return QTextEdit::event(event);
    }
   signals:
    void editingDone();
};

void ListViewDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    if (Clay::enabled()) {
        editor->setFont(clayTitleFont(opt.font));
        editor->setGeometry(clayTitleRect(opt.rect, opt.font).adjusted(-4, -4, 4, 4));
    } else {
        editor->setFont(titleFont(opt.font));
        editor->setGeometry(titleRect(opt.rect, opt.font).adjusted(-4, -4, 4, 4));
    }
}

void ListViewDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto text = index.data(Qt::EditRole).toString();
    QTextEdit* realEditor = qobject_cast<NoReturnTextEdit*>(editor);
    realEditor->setPlainText(text);
    realEditor->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    realEditor->selectAll();
    realEditor->document()->clearUndoRedoStacks();
}

void ListViewDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    QTextEdit* realEditor = qobject_cast<NoReturnTextEdit*>(editor);
    QString text = realEditor->toPlainText();
    text.replace(QChar('\n'), QChar(' '));
    text = text.trimmed();
    // Prevent instance names longer than 128 chars
    text.truncate(128);
    if (text.size() != 0) {
        emit textChanged(model->data(index).toString(), text);
        model->setData(index, text);
    }
}

QWidget* ListViewDelegate::createEditor(QWidget* parent,
                                        [[maybe_unused]] const QStyleOptionViewItem& option,
                                        [[maybe_unused]] const QModelIndex& index) const
{
    auto editor = new NoReturnTextEdit(parent);
    editor->setFrameShape(QFrame::NoFrame);
    connect(editor, &NoReturnTextEdit::editingDone, this, &ListViewDelegate::editingDone);
    return editor;
}

void ListViewDelegate::editingDone()
{
    NoReturnTextEdit* editor = qobject_cast<NoReturnTextEdit*>(sender());
    emit commitData(editor);
    emit closeEditor(editor);
}

#include "InstanceDelegate.moc"
