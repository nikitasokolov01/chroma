// SPDX-License-Identifier: GPL-3.0-only
#include "ModpackCardDelegate.h"

#include <QApplication>
#include <QEvent>
#include <QIcon>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QStyleOption>
#include <QTextDocumentFragment>
#include <QTextLayout>
#include <QTimer>

namespace {
constexpr int gap = 12;
constexpr int minimumCardWidth = 164;

QColor blend(const QColor& first, const QColor& second, qreal amount)
{
    return QColor::fromRgbF(first.redF() * (1 - amount) + second.redF() * amount, first.greenF() * (1 - amount) + second.greenF() * amount,
                            first.blueF() * (1 - amount) + second.blueF() * amount);
}

QString plainText(const QString& text)
{
    // Provider summaries can contain simple HTML; render it as text only.
    return (Qt::mightBeRichText(text) ? QTextDocumentFragment::fromHtml(text).toPlainText() : text).simplified();
}

void drawTitle(QPainter* painter, const QRect& rect, const QString& text)
{
    QTextLayout layout(text, painter->font());
    QTextOption options;
    options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(options);
    layout.beginLayout();
    const QFontMetrics metrics(painter->font());
    for (int row = 0; row < 2; ++row) {
        auto line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(rect.width());
        QString content = text.mid(line.textStart(), line.textLength());
        if (row == 1)
            content = metrics.elidedText(text.mid(line.textStart()), Qt::ElideRight, rect.width());
        painter->drawText(QRect(rect.left(), rect.top() + row * metrics.height(), rect.width(), metrics.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, content);
    }
    layout.endLayout();
}
}  // namespace

ModpackCardDelegate::ModpackCardDelegate(QListView* view) : QStyledItemDelegate(view), m_view(view)
{
    view->installEventFilter(this);
    view->viewport()->installEventFilter(this);
}

void ModpackCardDelegate::configureView(QListView* view)
{
    if (!view)
        return;
    auto* delegate = qobject_cast<ModpackCardDelegate*>(view->itemDelegate());
    if (!delegate) {
        delegate = new ModpackCardDelegate(view);
        view->setItemDelegate(delegate);
    }
    view->setViewMode(QListView::IconMode);
    view->setFlow(QListView::LeftToRight);
    view->setMovement(QListView::Static);
    view->setResizeMode(QListView::Adjust);
    view->setWrapping(true);
    view->setUniformItemSizes(true);
    view->setAlternatingRowColors(false);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setSpacing(0);
    view->setMouseTracking(true);
    view->viewport()->setAttribute(Qt::WA_Hover);
    view->setProperty("modpackCardGrid", true);
    delegate->updateGrid();
    QTimer::singleShot(0, delegate, [delegate] { delegate->updateGrid(); });
}

bool ModpackCardDelegate::eventFilter(QObject* watched, QEvent* event)
{
    if (m_view && (watched == m_view || watched == m_view->viewport())) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::FontChange ||
            event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::ScreenChangeInternal ||
            event->type() == QEvent::StyleChange || event->type() == QEvent::LayoutRequest) {
            updateGrid();
            // setGridSize may change scrollbar visibility during a resize. Recheck
            // once the viewport has settled instead of keeping the earlier width.
            if (!m_gridUpdatePending) {
                m_gridUpdatePending = true;
                QTimer::singleShot(0, this, [this] {
                    m_gridUpdatePending = false;
                    updateGrid();
                });
            }
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::PaletteChange) {
            m_view->viewport()->update();
        }
        // The view and its viewport are not delegate editors.
        return false;
    }
    return QStyledItemDelegate::eventFilter(watched, event);
}

void ModpackCardDelegate::updateGrid()
{
    if (!m_view || m_updating)
        return;
    QScopedValueRollback<bool> updating(m_updating, true);
    // QListView reserves the style's scrollbar extent while wrapping icons,
    // even when a stylesheet makes the visible scrollbar narrower. Fit that
    // layout area as well as the actual viewport so the last column stays put.
    int layoutWidth = m_view->maximumViewportSize().width();
    const auto* style = m_view->style();
    auto* scrollBar = m_view->verticalScrollBar();
    if (m_view->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded &&
        !style->pixelMetric(QStyle::PM_ScrollView_ScrollBarOverlap, nullptr, scrollBar)) {
        layoutWidth -= style->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, scrollBar);
        if (style->styleHint(QStyle::SH_ScrollView_FrameOnlyAroundContents)) {
            QStyleOption option;
            option.initFrom(m_view);
            layoutWidth -= 2 * style->pixelMetric(QStyle::PM_DefaultFrameWidth, &option);
        }
    }
    const int width = qMax(1, qMin(layoutWidth, m_view->viewport()->contentsRect().width()) - 2);
    const int columns = qBound(1, width / (minimumCardWidth + gap), 5);
    const int cellWidth = width / columns;
    const int cardWidth = qMax(60, cellWidth - gap);
    QFont titleFont = m_view->font();
    titleFont.setBold(true);
    const int titleHeight = QFontMetrics(titleFont).height();
    const int textHeight = QFontMetrics(m_view->font()).height();
    const int artworkHeight = qMin(152, qMax(36, cardWidth - 24));
    m_cardSize = QSize(cardWidth, artworkHeight + 44 + 2 * titleHeight + 2 * textHeight);
    const QSize grid(cellWidth, m_cardSize.height() + gap);
    if (m_view->gridSize() != grid)
        m_view->setGridSize(grid);
    m_view->viewport()->update();
}

QSize ModpackCardDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return m_cardSize;
}

void ModpackCardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    // Catalog status rows have no pack payload. Keep their ordinary text presentation.
    if (!index.data(Qt::UserRole).isValid() || !(index.flags() & Qt::ItemIsSelectable)) {
        auto statusOption = option;
        statusOption.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        QStyledItemDelegate::paint(painter, statusOption, index);
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    const QRect card = option.rect.adjusted(1, 1, -1, -1);
    const bool enabled = option.state & QStyle::State_Enabled;
    const bool selected = enabled && (option.state & QStyle::State_Selected);
    const bool hovered = enabled && (option.state & QStyle::State_MouseOver);
    const bool focused = option.state & QStyle::State_HasFocus;
    auto palette = option.palette;
    palette.setCurrentColorGroup(!enabled                                ? QPalette::Disabled
                                 : (option.state & QStyle::State_Active) ? QPalette::Active
                                                                         : QPalette::Inactive);
    const auto accent = palette.color(QPalette::Highlight);
    auto surface = palette.color(QPalette::AlternateBase);
    if (selected || hovered)
        surface = blend(surface, accent, selected ? 0.16 : 0.07);
    painter->setBrush(surface);
    painter->setPen(QPen(selected || hovered ? accent : palette.color(QPalette::Mid), selected ? 2 : 1));
    painter->drawRoundedRect(card, 13, 13);
    if (focused) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(accent, 1, Qt::DashLine));
        painter->drawRoundedRect(card.adjusted(4, 4, -4, -4), 10, 10);
    }

    const int artworkHeight = qMin(152, qMax(36, m_cardSize.width() - 24));
    const QRect artwork(card.left() + 11, card.top() + 11, card.width() - 22, artworkHeight);
    QPainterPath artworkClip;
    artworkClip.addRoundedRect(artwork, 9, 9);
    painter->fillPath(artworkClip, palette.color(QPalette::Base));
    painter->save();
    painter->setClipPath(artworkClip);
    const auto decoration = index.data(Qt::DecorationRole);
    QPixmap pixmap;
    const qreal ratio = painter->device()->devicePixelRatioF();
    if (decoration.canConvert<QIcon>())
        pixmap = qvariant_cast<QIcon>(decoration).pixmap(artwork.size(), ratio, enabled ? QIcon::Normal : QIcon::Disabled);
    else if (decoration.canConvert<QPixmap>())
        pixmap = qvariant_cast<QPixmap>(decoration);
    if (!pixmap.isNull()) {
        const auto size = pixmap.size().scaled(artwork.size(), Qt::KeepAspectRatio);
        QRect target(QPoint(), size);
        target.moveCenter(artwork.center());
        painter->drawPixmap(target, pixmap);
    } else {
        QFont fallback = option.font;
        fallback.setPixelSize(32);
        fallback.setBold(true);
        painter->setFont(fallback);
        painter->setPen(accent);
        painter->drawText(artwork, Qt::AlignCenter, index.data(Qt::DisplayRole).toString().left(1).toUpper());
    }
    painter->restore();

    QFont titleFont = option.font;
    titleFont.setBold(true);
    painter->setFont(titleFont);
    const QFontMetrics titleMetrics(titleFont);
    const QRect title(artwork.left(), artwork.bottom() + 11, artwork.width(), titleMetrics.height() * 2);
    auto titleColor = palette.color(QPalette::Text);
    const auto foreground = index.data(Qt::ForegroundRole);
    if (enabled && foreground.canConvert<QColor>() && foreground.value<QColor>().isValid())
        titleColor = foreground.value<QColor>();
    painter->setPen(titleColor);
    drawTitle(painter, title, index.data(Qt::DisplayRole).toString().simplified());

    painter->setFont(option.font);
    painter->setPen(palette.color(QPalette::PlaceholderText));
    const QFontMetrics textMetrics(option.font);
    const QRect author(title.left(), title.bottom() + 5, title.width(), textMetrics.height());
    const QString authorText = plainText(index.data(ModpackCardRoles::AuthorRole).toString());
    const QString summary = plainText(index.data(ModpackCardRoles::SummaryRole).toString());
    if (!authorText.isEmpty())
        painter->drawText(author, Qt::AlignLeft | Qt::AlignVCenter,
                          textMetrics.elidedText(tr("by %1").arg(authorText), Qt::ElideRight, author.width()));
    const QRect summaryRect(author.left(), authorText.isEmpty() ? author.top() : author.bottom() + 5, author.width(), textMetrics.height());
    painter->drawText(summaryRect, Qt::AlignLeft | Qt::AlignVCenter, textMetrics.elidedText(summary, Qt::ElideRight, summaryRect.width()));
    painter->restore();
}
