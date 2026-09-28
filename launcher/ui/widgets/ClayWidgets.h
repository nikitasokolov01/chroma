// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QComboBox>
#include <QFrame>
#include <QLineEdit>
#include <QToolButton>
#include <QWidget>

class QEnterEvent;
class QVariantAnimation;

// Native event handling, actions, accessibility, and menu hit testing remain
// owned by Qt. These widgets only replace their surfaces in the Chroma theme.
class ClayToolButton : public QToolButton {
   public:
    explicit ClayToolButton(QWidget* parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

   private:
    void animateHover(qreal target);
    void updateAppearance();
    QString m_menuStyle;
    bool m_updatingAppearance = false;
    QVariantAnimation* m_hoverAnimation;
    qreal m_hover = 0;
};

class ClayLineEdit : public QLineEdit {
   public:
    explicit ClayLineEdit(QWidget* parent = nullptr);
    QSize sizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

   private:
    void updateAppearance();
    bool m_clayAppearance = false;
    bool m_updatingAppearance = false;
};

class ClayComboBox : public QComboBox {
   public:
    explicit ClayComboBox(QWidget* parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

   private:
    void updateAppearance();
    bool m_clayAppearance = false;
    bool m_updatingAppearance = false;
};

class ClayPanel : public QFrame {
   public:
    explicit ClayPanel(QWidget* parent = nullptr);

   protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

   private:
    void updateAppearance();
    bool m_clayAppearance = false;
    bool m_updatingAppearance = false;
};

class ClayCanvas : public QWidget {
   public:
    explicit ClayCanvas(QWidget* parent = nullptr);

   protected:
    void paintEvent(QPaintEvent* event) override;
};
