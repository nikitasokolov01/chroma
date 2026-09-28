/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "InstanceProxyModel.h"

#include <BaseInstance.h>
#include <icons/IconList.h>
#include "Application.h"
#include "InstanceList.h"
#include "InstanceView.h"
#include "minecraft/Component.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <algorithm>

InstanceProxyModel::InstanceProxyModel(QObject* parent) : QSortFilterProxyModel(parent)
{
    m_naturalSort.setNumericMode(true);
    m_naturalSort.setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);
    // FIXME: use loaded translation as source of locale instead, hook this up to translation changes
    m_naturalSort.setLocale(QLocale::system());
    m_summaryTimer = new QTimer(this);
    m_summaryTimer->setSingleShot(true);
    connect(m_summaryTimer, &QTimer::timeout, this, &InstanceProxyModel::refreshSummaries);
}

void InstanceProxyModel::setSourceModel(QAbstractItemModel* model)
{
    for (const auto& connection : m_sourceConnections)
        disconnect(connection);
    m_sourceConnections.clear();
    m_summaryTimer->stop();
    m_summaries.clear();
    m_summaryRows.clear();
    m_allSummariesDirty = true;
    QSortFilterProxyModel::setSourceModel(model);
    if (!model)
        return;
    if (auto* instances = qobject_cast<InstanceList*>(model))
        m_sourceConnections.append(connect(instances, &InstanceList::manualOrderChanged, this, [this] { invalidate(); }));

    const auto schedule = [this] {
        m_allSummariesDirty = true;
        m_summaryTimer->start(0);
    };
    m_sourceConnections.append(connect(model, &QAbstractItemModel::dataChanged, this,
                                       [this](const QModelIndex& first, const QModelIndex& last, const QList<int>& roles) {
                                           if (!roles.isEmpty() && !roles.contains(Qt::DisplayRole) && !roles.contains(InstanceSummaryRole))
                                               return;
                                           for (int row = first.row(); row <= last.row(); ++row)
                                               m_summaryRows.insert(row);
                                           m_summaryTimer->start(0);
                                       }));
    m_sourceConnections.append(connect(model, &QAbstractItemModel::rowsInserted, this, schedule));
    m_sourceConnections.append(connect(model, &QAbstractItemModel::rowsRemoved, this, schedule));
    m_sourceConnections.append(connect(model, &QAbstractItemModel::modelReset, this, [this, schedule] {
        m_summaries.clear();
        schedule();
    }));
    refreshSummaries();
}

QString InstanceProxyModel::formatSummary(const QString& version, ModPlatform::ModLoaderTypes loaders)
{
    QStringList names;
    for (auto type : ModPlatform::modLoaderTypesToList(loaders)) {
        QString name = ModPlatform::getModLoaderAsString(type);
        switch (type) {
            case ModPlatform::NeoForge:
                name = "NeoForge";
                break;
            case ModPlatform::LiteLoader:
                name = "LiteLoader";
                break;
            case ModPlatform::LegacyFabric:
                name = "Legacy Fabric";
                break;
            case ModPlatform::BTA:
                name = "BTA";
                break;
            default:
                if (!name.isEmpty())
                    name[0] = name[0].toUpper();
                break;
        }
        if (!name.isEmpty())
            names.append(name);
    }
    return tr("%1 %2").arg(names.isEmpty() ? tr("Vanilla") : names.join(" + "), version);
}

void InstanceProxyModel::refreshSummaries()
{
    QHash<QString, SummaryCacheEntry> summaries = m_allSummariesDirty ? QHash<QString, SummaryCacheEntry>() : m_summaries;
    bool changed = false;
    QList<QModelIndex> changedIndices;
    if (auto* model = sourceModel()) {
        QList<int> rows;
        if (m_allSummariesDirty) {
            rows.reserve(model->rowCount());
            for (int row = 0; row < model->rowCount(); ++row)
                rows.append(row);
        } else
            rows = m_summaryRows.values();
        for (int row : rows) {
            if (row < 0 || row >= model->rowCount())
                continue;
            const auto sourceIndex = model->index(row, 0);
            const auto id = sourceIndex.data(InstanceList::InstanceIDRole).toString();
            auto* instance = static_cast<BaseInstance*>(sourceIndex.data(InstanceList::InstancePointerRole).value<void*>());
            if (!instance)
                continue;

            SummaryCacheEntry entry;
            entry.path = QDir(instance->instanceRoot()).filePath("mmc-pack.json");
            const QFileInfo fileInfo(entry.path);
            entry.modified = fileInfo.lastModified();
            entry.size = fileInfo.exists() ? fileInfo.size() : -1;
            entry.customMinecraft = QFileInfo::exists(QDir(instance->instanceRoot()).filePath("patches/net.minecraft.json"));
            const auto previous = m_summaries.constFind(id);
            if (previous != m_summaries.cend() && previous->path == entry.path && previous->modified == entry.modified &&
                previous->size == entry.size && previous->customMinecraft == entry.customMinecraft) {
                summaries.insert(id, *previous);
                continue;
            }

            // Only this refresh path reads files. data() and the card painter remain memory-only.
            QFile file(entry.path);
            if (file.open(QIODevice::ReadOnly)) {
                const auto document = QJsonDocument::fromJson(file.readAll());
                const auto root = document.object();
                if (root.value("formatVersion").toInt() == 1 && root.value("components").isArray()) {
                    QSet<QString> componentIds;
                    for (const auto& value : root.value("components").toArray()) {
                        const auto component = value.toObject();
                        if (!value.isObject() || !component.value("uid").isString()) {
                            entry.version.clear();
                            entry.loaders = {};
                            break;
                        }
                        const auto uid = component.value("uid").toString();
                        if (componentIds.contains(uid))
                            continue;
                        componentIds.insert(uid);
                        // Important and dependency-only components cannot be disabled in Component::isEnabled().
                        if (component.value("disabled").toBool() && !component.value("important").toBool() &&
                            !component.value("dependencyOnly").toBool())
                            continue;
                        if (uid == "net.minecraft") {
                            // A custom patch's display version differs from the version it would revert to.
                            // Otherwise the requested version wins over cache data left behind by an unloaded edit.
                            entry.version = component.value(entry.customMinecraft ? "cachedVersion" : "version").toString();
                            if (entry.version.isEmpty())
                                entry.version = component.value(entry.customMinecraft ? "version" : "cachedVersion").toString();
                        }
                        const auto loader = Component::KNOWN_MODLOADERS.constFind(uid);
                        if (loader != Component::KNOWN_MODLOADERS.cend())
                            entry.loaders |= loader->type;
                    }
                }
            }
            const bool entryChanged =
                previous == m_summaries.cend() || previous->version != entry.version || previous->loaders != entry.loaders;
            changed |= entryChanged;
            if (entryChanged)
                changedIndices.append(sourceIndex);
            summaries.insert(id, entry);
        }
    }
    m_summaries = std::move(summaries);
    m_allSummariesDirty = false;
    m_summaryRows.clear();
    if (changed) {
        for (const auto& sourceIndex : changedIndices) {
            const auto index = mapFromSource(sourceIndex);
            if (index.isValid())
                emit dataChanged(index, index, { InstanceSummaryRole });
        }
    }
}

QVariant InstanceProxyModel::data(const QModelIndex& index, int role) const
{
    if (role == InstanceSummaryRole) {
        const auto id = QSortFilterProxyModel::data(index, InstanceList::InstanceIDRole).toString();
        const auto summary = m_summaries.constFind(id);
        if (summary == m_summaries.cend() || summary->version.isEmpty())
            return {};
        return formatSummary(summary->version, summary->loaders);
    }
    QVariant data = QSortFilterProxyModel::data(index, role);
    if (role == Qt::DecorationRole) {
        return QVariant(APPLICATION->icons()->getIcon(data.toString()));
    }
    return data;
}

bool InstanceProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const QString leftCategory = left.data(InstanceViewRoles::GroupRole).toString();
    const QString rightCategory = right.data(InstanceViewRoles::GroupRole).toString();
    if (leftCategory == rightCategory) {
        return subSortLessThan(left, right);
    } else {
        // FIXME: real group sorting happens in InstanceView::updateGeometries(), see LocaleString
        auto result = leftCategory.localeAwareCompare(rightCategory);
        if (result == 0) {
            return subSortLessThan(left, right);
        }
        return result < 0;
    }
}

bool InstanceProxyModel::subSortLessThan(const QModelIndex& left, const QModelIndex& right) const
{
    BaseInstance* pdataLeft = static_cast<BaseInstance*>(left.internalPointer());
    BaseInstance* pdataRight = static_cast<BaseInstance*>(right.internalPointer());
    QString sortMode = APPLICATION->settings()->get("InstSortMode").toString();
    if (sortMode == "Manual") {
        const int leftRank = left.data(InstanceList::ManualOrderRole).toInt();
        const int rightRank = right.data(InstanceList::ManualOrderRole).toInt();
        if (leftRank != rightRank)
            return leftRank < rightRank;
    }
    if (sortMode == "LastLaunch") {
        return pdataLeft->lastLaunch() > pdataRight->lastLaunch();
    } else {
        return m_naturalSort.compare(pdataLeft->name(), pdataRight->name()) < 0;
    }
}

QStringList InstanceProxyModel::orderedInstanceIds() const
{
    QList<QModelIndex> indices;
    if (!sourceModel())
        return {};
    for (int row = 0; row < sourceModel()->rowCount(); ++row)
        indices.append(sourceModel()->index(row, 0));
    std::stable_sort(indices.begin(), indices.end(),
                     [this](const QModelIndex& left, const QModelIndex& right) { return lessThan(left, right); });
    QStringList ids;
    for (const auto& index : indices)
        ids.append(index.data(InstanceList::InstanceIDRole).toString());
    return ids;
}
