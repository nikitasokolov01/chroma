// SPDX-License-Identifier: GPL-3.0-only
#include "InlineWorkspace.h"
#include "ui/dialogs/ProgressDialog.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

InlineWorkspace::InlineWorkspace(QMainWindow* window, QWidget* parent) : QWidget(parent), m_window(window)
{
    setObjectName("inlineWorkspace");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(14);
    auto* heading = new QHBoxLayout;
    auto* back = new QToolButton(this);
    back->setObjectName("inlineBackButton");
    back->setText(tr("Back"));
    back->setIcon(QIcon::fromTheme("go-previous"));
    back->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    back->setCursor(Qt::PointingHandCursor);
    connect(back, &QToolButton::clicked, this, [this] {
        if (auto* page = currentPage())
            page->close();
    });
    heading->addWidget(back);
    m_title = new QLabel(this);
    m_title->setObjectName("inlinePageTitle");
    m_title->setTextFormat(Qt::PlainText);
    m_title->setWordWrap(true);
    heading->addWidget(m_title, 1);
    layout->addLayout(heading);
    m_canvas = new QWidget(this);
    m_canvas->setObjectName("inlineCanvas");
    m_stack = new QStackedLayout(m_canvas);
    // Hiding a QDialog ends exec(). Keep covered pages visible until they close
    // themselves, so nested prompts cannot prematurely finish their parent.
    m_stack->setStackingMode(QStackedLayout::StackAll);
    m_stack->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_canvas, 1);
    applyStyle();
    qApp->installEventFilter(this);
}

InlineWorkspace::~InlineWorkspace()
{
    qApp->removeEventFilter(this);
    // QWidget destroys its children after our members; their destruction must
    // not call lambdas that access the already-destroyed navigation state.
    for (const auto& entry : std::as_const(m_pages)) {
        if (entry.widget)
            disconnect(entry.widget, nullptr, this, nullptr);
    }
}

QWidget* InlineWorkspace::currentPage() const
{
    return m_active.isEmpty() ? nullptr : m_active.last();
}

QWidget* InlineWorkspace::activePage(const QString& objectName) const
{
    for (auto* page : m_active) {
        if (page->objectName() == objectName)
            return page;
    }
    return nullptr;
}

bool InlineWorkspace::navigationBlocked() const
{
    for (auto* page : m_active) {
        if (qobject_cast<ProgressDialog*>(page))
            return true;
    }
    return false;
}

void InlineWorkspace::present(QWidget* page, const QString& title)
{
    presentPage(page, title, false);
}

void InlineWorkspace::presentPage(QWidget* page, const QString& title, bool deferShow)
{
    if (!page || m_adopting)
        return;
    QScopedValueRollback<bool> adopting(m_adopting, true);
    if (!m_pages.contains(page)) {
        const bool ownsScrolling = page->property("chromaOwnsScrolling").toBool();
        auto* scroll = ownsScrolling ? nullptr : new QScrollArea(m_canvas);
        QWidget* wrapper = scroll ? static_cast<QWidget*>(scroll) : new QWidget(m_canvas);
        wrapper->setObjectName(ownsScrolling ? "inlineResponsivePage" : "inlinePageScroll");
        wrapper->hide();
        if (scroll) {
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidgetResizable(true);
            scroll->viewport()->setAutoFillBackground(true);
        }
        wrapper->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        wrapper->setMinimumSize(0, 0);
        wrapper->setAutoFillBackground(true);
        page->setParent(wrapper, Qt::Widget);
        page->setWindowModality(Qt::NonModal);
        page->setAttribute(Qt::WA_ShowModal, false);
        page->setAttribute(Qt::WA_QuitOnClose, false);
        page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        // Older screens use an outer scroller. Responsive pages already scroll
        // their bodies, so their preferred height must not displace fixed actions.
        if (page->layout())
            page->layout()->setSizeConstraint(QLayout::SetDefaultConstraint);
        page->setMinimumSize(0, 0);
        page->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        page->setProperty("chromaInline", true);
        if (scroll) {
            scroll->setWidget(page);
        } else {
            auto* layout = new QVBoxLayout(wrapper);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSizeConstraint(QLayout::SetNoConstraint);
            layout->addWidget(page);
        }
        m_pages.insert(page, { page, wrapper, title.isEmpty() ? page->windowTitle() : title });
        m_stack->addWidget(wrapper);
        connect(page, &QWidget::windowTitleChanged, this, [this, page](const QString& text) {
            if (m_pages.contains(page)) {
                m_pages[page].title = text;
                if (currentPage() == page)
                    updateCurrent();
            }
        });
        connect(page, &QObject::destroyed, this, [this, page] {
            leave(page);
            const auto entry = m_pages.take(page);
            if (entry.wrapper)
                entry.wrapper->deleteLater();
        });
        if (auto* dialog = qobject_cast<QDialog*>(page))
            connect(dialog, &QDialog::finished, this, [this, page] { leave(page); });
    } else if (!title.isEmpty()) {
        m_pages[page].title = title;
    }
    page->setWindowModality(Qt::NonModal);
    page->setAttribute(Qt::WA_ShowModal, false);
    m_active.removeAll(page);
    m_active.append(page);
    updateCurrent();
    m_pages[page].wrapper->show();
    if (!deferShow && !page->isVisible())
        page->show();
    page->setFocus(Qt::OtherFocusReason);
}

void InlineWorkspace::leave(QWidget* page)
{
    if (m_adopting || !m_active.removeOne(page))
        return;
    const auto entry = m_pages.value(page);
    if (entry.wrapper)
        entry.wrapper->hide();
    updateCurrent();
}

void InlineWorkspace::updateCurrent()
{
    emit navigationLockChanged(navigationBlocked());
    if (m_active.isEmpty()) {
        m_title->clear();
        if (!m_closing)
            emit emptied();
        return;
    }
    auto* current = currentPage();
    for (auto* page : m_active) {
        const auto entry = m_pages.value(page);
        if (entry.wrapper)
            entry.wrapper->setEnabled(page == current);
    }
    const auto entry = m_pages.value(current);
    if (entry.wrapper)
        m_stack->setCurrentWidget(entry.wrapper);
    m_title->setText(entry.title);
    emit pagePresented(entry.title);
}

bool InlineWorkspace::closeAllPages()
{
    QScopedValueRollback<bool> closing(m_closing, true);
    while (auto* page = currentPage()) {
        QPointer<QWidget> guard(page);
        if (!page->close())
            return false;
        if (guard && m_active.contains(page))
            leave(page);
    }
    return true;
}

bool InlineWorkspace::eventFilter(QObject* watched, QEvent* event)
{
    if (m_adopting)
        return false;
    if (event->type() == QEvent::Shortcut && navigationBlocked()) {
        auto* owner = watched;
        while (owner && owner != currentPage())
            owner = owner->parent();
        if (!owner)
            return true;
    }
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget)
        return false;
    if (event->type() == QEvent::Show && m_window && m_window->isVisible()) {
        if (m_pages.contains(widget)) {
            widget->setWindowModality(Qt::NonModal);
            widget->setAttribute(Qt::WA_ShowModal, false);
            if (!m_active.contains(widget))
                present(widget);
        } else if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->isWindow()) {
            // Feature dialogs, file/color pickers, and confirmation prompts all
            // use the same inline navigation. Menus and tooltips remain popups.
            presentPage(dialog, {}, true);
            // Reparenting interrupts the first show; resume once exec() has
            // entered its event loop, with the dialog already a child widget.
            QPointer<QDialog> guard(dialog);
            QTimer::singleShot(0, this, [this, guard] {
                // A fast completion may precede this event. Re-showing then
                // would reset QColorDialog's selected color or reopen a prompt.
                if (guard && m_active.contains(guard) && !guard->isVisible())
                    guard->show();
            });
        }
    } else if (event->type() == QEvent::Hide && m_pages.contains(widget)) {
        QPointer<QWidget> guard(widget);
        QTimer::singleShot(0, this, [this, guard] {
            if (guard && guard->isHidden())
                leave(guard);
        });
    }
    return false;
}

void InlineWorkspace::applyStyle()
{
    setStyleSheet(QStringLiteral(R"(
        QWidget#inlineWorkspace, QWidget#inlineCanvas, QWidget#inlineResponsivePage { background: palette(base); }
        QScrollArea#inlinePageScroll, QScrollArea#inlinePageScroll > QWidget > QWidget { background: palette(base); border: 0; }
        QLabel#inlinePageTitle { font-size: 24px; font-weight: 700; background: transparent; }
        QWidget#inlineWorkspace QLabel { background: transparent; }
        QWidget#inlineWorkspace QPushButton, QWidget#inlineWorkspace QToolButton {
            background: palette(button); color: palette(button-text); border: 1px solid palette(mid);
            border-radius: 8px; padding: 7px 12px; min-height: 20px;
        }
        QWidget#inlineWorkspace QPushButton:hover, QWidget#inlineWorkspace QToolButton:hover { border-color: palette(highlight); }
        QWidget#inlineWorkspace QPushButton:focus, QWidget#inlineWorkspace QToolButton:focus { border: 2px solid palette(highlight); }
        QWidget#inlineWorkspace QPushButton:disabled, QWidget#inlineWorkspace QToolButton:disabled { color: palette(placeholder-text); }
        QWidget#inlineWorkspace QPushButton[role="primary"], QWidget#inlineWorkspace QPushButton:default {
            background: palette(highlight); color: palette(highlighted-text); border-color: palette(highlight); font-weight: 600;
        }
        QWidget#inlineWorkspace QPushButton[role="primary"]:disabled, QWidget#inlineWorkspace QPushButton:default:disabled {
            background: palette(button); color: palette(placeholder-text); border-color: palette(mid);
        }
        QWidget#inlineWorkspace QLineEdit, QWidget#inlineWorkspace QSpinBox, QWidget#inlineWorkspace QDoubleSpinBox,
        QWidget#inlineWorkspace QComboBox { background: palette(button); color: palette(text); border: 1px solid palette(mid); border-radius: 7px; padding: 5px 8px; min-height: 20px; }
        QWidget#inlineWorkspace QLineEdit:focus, QWidget#inlineWorkspace QComboBox:focus { border-color: palette(highlight); }
        QWidget#inlineWorkspace QTreeView, QWidget#inlineWorkspace QTableView {
            background: palette(base); alternate-background-color: palette(alternate-base); color: palette(text);
            border: 1px solid palette(mid); selection-background-color: palette(highlight); selection-color: palette(highlighted-text);
        }
        QWidget#inlineWorkspace QHeaderView::section {
            background: palette(alternate-base); color: palette(text); border: 0;
            border-bottom: 1px solid palette(mid); padding: 6px 8px; font-weight: 600;
        }
        QWidget#inlineWorkspace QTableCornerButton::section { background: palette(alternate-base); border: 0; }
        QWidget#inlineWorkspace QGroupBox { border: 1px solid palette(mid); border-radius: 10px; margin-top: 18px; padding: 12px; }
        QWidget#inlineWorkspace QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; font-weight: 600; }
        QWidget#inlineWorkspace QTabWidget::pane { border: 1px solid palette(mid); border-radius: 8px; }
        QWidget#inlineWorkspace QTabBar::tab { background: palette(window); padding: 9px 14px; margin-right: 3px; border-bottom: 2px solid transparent; }
        QWidget#inlineWorkspace QTabBar::tab:selected { border-bottom-color: palette(highlight); background: palette(alternate-base); }
        QWidget#inlineWorkspace QProgressBar { border: 1px solid palette(mid); border-radius: 6px; background: palette(button); text-align: center; min-height: 18px; }
        QWidget#inlineWorkspace QProgressBar::chunk { background: palette(highlight); border-radius: 5px; }
        QWidget#inlineWorkspace QScrollBar:vertical { width: 8px; background: transparent; }
        QWidget#inlineWorkspace QScrollBar::handle:vertical { background: palette(mid); border-radius: 4px; min-height: 30px; }
        QWidget#inlineWorkspace QScrollBar::add-line:vertical, QWidget#inlineWorkspace QScrollBar::sub-line:vertical { height: 0; }
    )"));
}
