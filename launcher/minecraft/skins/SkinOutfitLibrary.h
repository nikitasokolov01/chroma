// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QString>
#include <optional>

#include "SkinModel.h"

// A preset is a snapshot of the composed skin, independent of its source skin
// and Extras. No account credentials or account identity are persisted.
class SkinOutfitLibrary {
   public:
    struct Entry {
        QString id;
        QString name;
        QImage image;
        SkinModel::Model model = SkinModel::CLASSIC;
        // Absent keeps the account's cape; an empty ID explicitly removes it.
        std::optional<QString> capeId;
        QImage capeImage;
    };
    explicit SkinOutfitLibrary(QString directory);
    QList<Entry> entries(QString* warning = nullptr) const;
    std::optional<Entry> load(const QString& id, QString* error = nullptr) const;
    QString save(const Entry& entry, QString* error = nullptr);
    bool rename(const QString& id, const QString& name, QString* error = nullptr);
    bool remove(const QString& id, QString* error = nullptr);

   private:
    QString path(const QString& id) const;
    bool write(const Entry& entry, QString* error);
    QString m_directory;
};
