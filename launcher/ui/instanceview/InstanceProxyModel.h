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

#pragma once

#include <QCollator>
#include <QDateTime>
#include <QHash>
#include <QSortFilterProxyModel>

#include "modplatform/ModIndex.h"

class QTimer;

class InstanceProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

   public:
    enum { InstanceSummaryRole = Qt::UserRole + 3 };

    InstanceProxyModel(QObject* parent = 0);
    void setSourceModel(QAbstractItemModel* sourceModel) override;
    static QString formatSummary(const QString& version, ModPlatform::ModLoaderTypes loaders);

   protected:
    QVariant data(const QModelIndex& index, int role) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
    bool subSortLessThan(const QModelIndex& left, const QModelIndex& right) const;

   private:
    struct SummaryCacheEntry {
        QString path;
        QDateTime modified;
        qint64 size = -1;
        bool customMinecraft = false;
        QString version;
        ModPlatform::ModLoaderTypes loaders;
    };

    void refreshSummaries();

    QCollator m_naturalSort;
    QHash<QString, SummaryCacheEntry> m_summaries;
    QList<QMetaObject::Connection> m_sourceConnections;
    QTimer* m_summaryTimer;
};
