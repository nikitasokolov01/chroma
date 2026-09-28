// SPDX-License-Identifier: GPL-3.0-only
#include <QElapsedTimer>
#include <QPainter>
#include <QTest>

#include "ui/instanceview/InstanceDelegate.h"
#include "ui/instanceview/InstanceView.h"

class LibraryModel : public QAbstractListModel {
   public:
    int count = 4000;
    mutable int reads = 0;
    bool firstEnabled = true;
    QHash<int, QString> groups;

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : count; }
    QVariant data(const QModelIndex& index, int role) const override
    {
        ++reads;
        if (role == InstanceViewRoles::GroupRole)
            return groups.value(index.row(), "Library");
        if (role == Qt::DisplayRole)
            return QString("Instance %1").arg(index.row());
        return {};
    }
    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        return Qt::ItemIsSelectable | ((index.row() != 0 || firstEnabled) ? Qt::ItemIsEnabled : Qt::NoItemFlags);
    }
    void notify(int row, const QList<int>& roles) { emit dataChanged(index(row, 0), index(row, 0), roles); }
};

class CountingCardDelegate : public ListViewDelegate {
   public:
    using ListViewDelegate::ListViewDelegate;
    mutable int hints = 0;
    mutable int paints = 0;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        ++hints;
        return QSize(192, 240);
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        ++paints;
        painter->fillRect(option.rect, Qt::darkGray);
        painter->drawText(option.rect, index.data().toString());
    }
};

class InstanceViewTest : public QObject {
    Q_OBJECT
   private slots:
    void largeLibraryUsesVisibleRows()
    {
        LibraryModel model;
        InstanceView view;
        CountingCardDelegate delegate;
        view.setItemDelegate(&delegate);
        view.setModel(&model);
        view.resize(1920, 1080);
        view.show();
        QTest::qWait(25);
        view.doItemsLayout();
        model.reads = delegate.hints = delegate.paints = 0;
        QElapsedTimer timer;
        timer.start();
        for (int row = 0; row < model.count; row += 17) {
            const auto index = model.index(row, 0);
            const auto rect = view.visualRect(index);
            QVERIFY(rect.isValid());
            QCOMPARE(view.indexAt(rect.center()), index);
        }
        qInfo() << "4000-instance geometry/hit lookup (ms):" << timer.elapsed();
        QCOMPARE(delegate.hints, 0);
        QCOMPARE(model.reads, 0);
        view.viewport()->repaint();
        QVERIFY(delegate.paints > 0);
        QVERIFY(delegate.paints < 100);
        QVERIFY(model.reads < 100);

        delegate.hints = 0;
        model.notify(1500, { InstanceViewRoles::ProgressValueRole });
        QTest::qWait(10);
        QCOMPARE(delegate.hints, 0);
    }

    void groupingAndHoverRemainCorrect()
    {
        LibraryModel model;
        model.count = 12;
        InstanceView view;
        CountingCardDelegate delegate;
        view.setItemDelegate(&delegate);
        view.setModel(&model);
        view.resize(1000, 700);
        view.show();
        QTest::qWait(25);
        const auto first = model.index(0, 0);
        QTest::mouseMove(view.viewport(), view.visualRect(first).center());
        QCOMPARE(view.viewport()->cursor().shape(), Qt::PointingHandCursor);
        QTest::mouseMove(view.viewport(), QPoint(1, 1));
        QCOMPARE(view.viewport()->cursor().shape(), Qt::ArrowCursor);
        model.firstEnabled = false;
        model.notify(0, {});
        QTest::mouseMove(view.viewport(), view.visualRect(first).center());
        QCOMPARE(view.viewport()->cursor().shape(), Qt::ArrowCursor);

        const auto moved = model.index(3, 0);
        const int oldTop = view.visualRect(moved).top();
        model.groups.insert(3, "New group");
        model.notify(3, { InstanceViewRoles::GroupRole });
        QTest::qWait(10);
        const auto rect = view.visualRect(moved);
        QVERIFY(rect.top() > oldTop);
        QCOMPARE(view.indexAt(rect.center()), moved);
        QCOMPARE(view.indexAt(QPoint(rect.center().x(), rect.bottom())), moved);
    }
};

QTEST_MAIN(InstanceViewTest)
#include "InstanceView_test.moc"
