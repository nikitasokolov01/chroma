// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTest>

#include "Application.h"
#include "InstanceList.h"
#include "ui/instanceview/InstanceDelegate.h"
#include "ui/instanceview/InstanceProxyModel.h"
#include "ui/instanceview/InstanceView.h"

namespace InstanceOrderUiTests {
inline void persistentReordering()
{
    auto instances = APPLICATION->instances();
    QVERIFY(instances->count() >= 3);
    const auto oldMode = APPLICATION->settings()->get("InstSortMode");
    const auto oldOrder = instances->manualOrder();
    QMap<QString, QString> oldGroups;
    for (int row = 0; row < instances->count(); ++row) {
        const auto id = instances->at(row)->id();
        oldGroups.insert(id, instances->getInstanceGroup(id));
    }
    auto restore = qScopeGuard([&] {
        for (auto it = oldGroups.cbegin(); it != oldGroups.cend(); ++it)
            instances->setInstanceGroup(it.key(), it.value());
        instances->setManualOrder(oldOrder);
        APPLICATION->settings()->set("InstSortMode", oldMode);
        instances->manualOrderChanged();
    });
    for (auto it = oldGroups.cbegin(); it != oldGroups.cend(); ++it)
        instances->setInstanceGroup(it.key(), QString());
    APPLICATION->settings()->set("InstSortMode", "Name");

    InstanceView view;
    InstanceProxyModel proxy;
    ListViewDelegate delegate;
    proxy.setSourceModel(instances.get());
    proxy.sort(0);
    view.setItemDelegate(&delegate);
    view.setModel(&proxy);
    view.resize(1280, 760);
    view.show();
    QTest::qWait(30);

    const auto indexForId = [&](const QString& id) {
        for (int row = 0; row < proxy.rowCount(); ++row) {
            const auto index = proxy.index(row, 0);
            if (index.data(InstanceList::InstanceIDRole).toString() == id)
                return index;
        }
        return QModelIndex();
    };
    const auto dropBefore = [&](const QString& moving, const QString& target) {
        const auto index = indexForId(target);
        if (!index.isValid())
            return false;
        view.scrollTo(index);
        QCoreApplication::processEvents();
        const auto rect = view.visualRect(index);
        const QPoint point(rect.left() + 8, rect.center().y());
        QMimeData data;
        data.setData("application/x-instanceid", moving.toUtf8());
        QDragEnterEvent enter(point, Qt::MoveAction, &data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &enter);
        if (!enter.isAccepted())
            return false;
        QDragMoveEvent move(point, Qt::MoveAction, &data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &move);
        QDropEvent drop(point, Qt::MoveAction, &data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &drop);
        QCoreApplication::processEvents();
        return drop.isAccepted();
    };

    auto expected = proxy.orderedInstanceIds();
    const auto moving = expected.takeLast();
    const auto target = expected.first();
    expected.prepend(moving);
    QVERIFY(dropBefore(moving, target));
    QCOMPARE(APPLICATION->settings()->get("InstSortMode").toString(), QString("Manual"));
    QCOMPARE(instances->manualOrder(), expected);
    QCOMPARE(proxy.orderedInstanceIds(), expected);
    const auto groupsPath = QFileInfo(instances->at(0)->instanceRoot()).dir().filePath("instgroups.json");
    QFile groupsFile(groupsPath);
    QVERIFY(groupsFile.open(QIODevice::ReadOnly));
    const auto saved = QJsonDocument::fromJson(groupsFile.readAll()).object()["instanceOrder"].toArray();
    QStringList savedIds;
    for (const auto& item : saved)
        savedIds.append(item.toString());
    QCOMPARE(savedIds, expected);
    groupsFile.close();

    const auto filteredMoving = expected.last();
    proxy.setFilterRole(InstanceList::InstanceIDRole);
    proxy.setFilterRegularExpression(
        QRegularExpression("^(" + QRegularExpression::escape(moving) + "|" + QRegularExpression::escape(filteredMoving) + ")$"));
    QCOMPARE(proxy.rowCount(), 2);
    expected.removeAll(filteredMoving);
    expected.prepend(filteredMoving);
    QVERIFY(dropBefore(filteredMoving, moving));
    QCOMPARE(instances->manualOrder(), expected);
    QCOMPARE(proxy.orderedInstanceIds(), expected);
    proxy.setFilterRegularExpression(QRegularExpression());
    QCOMPARE(proxy.rowCount(), instances->count());

    instances->setInstanceGroup(filteredMoving, "Drop destination");
    QCoreApplication::processEvents();
    QVERIFY(dropBefore(moving, filteredMoving));
    QCOMPARE(instances->getInstanceGroup(moving), QString("Drop destination"));
    QVERIFY(instances->manualOrder().indexOf(moving) < instances->manualOrder().indexOf(filteredMoving));
    const auto beforeUnknown = instances->manualOrder();
    QVERIFY(!dropBefore("missing-test-instance", moving));
    QCOMPARE(instances->manualOrder(), beforeUnknown);
    QCOMPARE(instances->count(), oldGroups.size());
}
}  // namespace InstanceOrderUiTests
