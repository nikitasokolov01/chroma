// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;

class PrismProfileDialog : public QDialog {
    Q_OBJECT

   public:
    explicit PrismProfileDialog(QWidget* parent = nullptr, const QString& initialSource = {});

    QString selectedProfilePath() const { return m_selectedProfilePath; }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void inspectProfile();
    void useProfile();
    void clearInspection();

    QString m_inspectedPath;
    QString m_selectedProfilePath;
    QComboBox* m_profilePath = nullptr;
    QPushButton* m_inspect = nullptr;
    QPushButton* m_use = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_status = nullptr;
};
