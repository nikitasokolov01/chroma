// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QPointer>
#include <QStyledItemDelegate>

class QListView;

namespace ModpackCardRoles {
enum { AuthorRole = Qt::UserRole + 80, SummaryRole };
}

// Shared visual catalog for provider models. DisplayRole is the pack name,
// DecorationRole is its existing artwork, and UserRole stays provider-owned.
class ModpackCardDelegate final : public QStyledItemDelegate {
    Q_OBJECT

   public:
    explicit ModpackCardDelegate(QListView* view);
    static void configureView(QListView* view);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void updateGrid();
    QPointer<QListView> m_view;
    QSize m_cardSize{ 164, 244 };
    bool m_updating = false;
    bool m_gridUpdatePending = false;
};
